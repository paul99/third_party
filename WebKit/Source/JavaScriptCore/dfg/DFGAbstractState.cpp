/*
 * Copyright (C) 2011, 2012, 2013 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "DFGAbstractState.h"

#if ENABLE(DFG_JIT)

#include "CodeBlock.h"
#include "DFGBasicBlock.h"
#include "GetByIdStatus.h"
#include "Operations.h"
#include "PutByIdStatus.h"

namespace JSC { namespace DFG {

AbstractState::AbstractState(Graph& graph)
    : m_codeBlock(graph.m_codeBlock)
    , m_graph(graph)
    , m_variables(m_codeBlock->numParameters(), graph.m_localVars)
    , m_block(0)
{
}

AbstractState::~AbstractState() { }

void AbstractState::beginBasicBlock(BasicBlock* basicBlock)
{
    ASSERT(!m_block);
    
    ASSERT(basicBlock->variablesAtHead.numberOfLocals() == basicBlock->valuesAtHead.numberOfLocals());
    ASSERT(basicBlock->variablesAtTail.numberOfLocals() == basicBlock->valuesAtTail.numberOfLocals());
    ASSERT(basicBlock->variablesAtHead.numberOfLocals() == basicBlock->variablesAtTail.numberOfLocals());
    
    for (size_t i = 0; i < basicBlock->size(); i++)
        forNode(basicBlock->at(i)).clear();

    m_variables = basicBlock->valuesAtHead;
    m_haveStructures = false;
    for (size_t i = 0; i < m_variables.numberOfArguments(); ++i) {
        if (m_variables.argument(i).m_currentKnownStructure.isNeitherClearNorTop()) {
            m_haveStructures = true;
            break;
        }
    }
    for (size_t i = 0; i < m_variables.numberOfLocals(); ++i) {
        if (m_variables.local(i).m_currentKnownStructure.isNeitherClearNorTop()) {
            m_haveStructures = true;
            break;
        }
    }
    
    basicBlock->cfaShouldRevisit = false;
    basicBlock->cfaHasVisited = true;
    m_block = basicBlock;
    m_isValid = true;
    m_foundConstants = false;
    m_branchDirection = InvalidBranchDirection;
}

void AbstractState::initialize(Graph& graph)
{
    BasicBlock* root = graph.m_blocks[0].get();
    root->cfaShouldRevisit = true;
    root->cfaHasVisited = false;
    root->cfaFoundConstants = false;
    for (size_t i = 0; i < root->valuesAtHead.numberOfArguments(); ++i) {
        Node* node = root->variablesAtHead.argument(i);
        ASSERT(node->op() == SetArgument);
        if (!node->shouldGenerate()) {
            // The argument is dead. We don't do any checks for such arguments, and so
            // for the purpose of the analysis, they contain no value.
            root->valuesAtHead.argument(i).clear();
            continue;
        }
        
        if (node->variableAccessData()->isCaptured()) {
            root->valuesAtHead.argument(i).makeTop();
            continue;
        }
        
        SpeculatedType prediction = node->variableAccessData()->prediction();
        if (isInt32Speculation(prediction))
            root->valuesAtHead.argument(i).set(SpecInt32);
        else if (isBooleanSpeculation(prediction))
            root->valuesAtHead.argument(i).set(SpecBoolean);
        else if (isCellSpeculation(prediction))
            root->valuesAtHead.argument(i).set(SpecCell);
        else
            root->valuesAtHead.argument(i).makeTop();
        
        root->valuesAtTail.argument(i).clear();
    }
    for (size_t i = 0; i < root->valuesAtHead.numberOfLocals(); ++i) {
        Node* node = root->variablesAtHead.local(i);
        if (node && node->variableAccessData()->isCaptured())
            root->valuesAtHead.local(i).makeTop();
        else
            root->valuesAtHead.local(i).clear();
        root->valuesAtTail.local(i).clear();
    }
    for (BlockIndex blockIndex = 1 ; blockIndex < graph.m_blocks.size(); ++blockIndex) {
        BasicBlock* block = graph.m_blocks[blockIndex].get();
        if (!block)
            continue;
        if (!block->isReachable)
            continue;
        block->cfaShouldRevisit = false;
        block->cfaHasVisited = false;
        block->cfaFoundConstants = false;
        for (size_t i = 0; i < block->valuesAtHead.numberOfArguments(); ++i) {
            block->valuesAtHead.argument(i).clear();
            block->valuesAtTail.argument(i).clear();
        }
        for (size_t i = 0; i < block->valuesAtHead.numberOfLocals(); ++i) {
            block->valuesAtHead.local(i).clear();
            block->valuesAtTail.local(i).clear();
        }
        if (!block->isOSRTarget)
            continue;
        if (block->bytecodeBegin != graph.m_osrEntryBytecodeIndex)
            continue;
        for (size_t i = 0; i < graph.m_mustHandleValues.size(); ++i) {
            AbstractValue value;
            value.setMostSpecific(graph.m_mustHandleValues[i]);
            int operand = graph.m_mustHandleValues.operandForIndex(i);
            block->valuesAtHead.operand(operand).merge(value);
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
            dataLogF("    Initializing Block #%u, operand r%d, to ", blockIndex, operand);
            block->valuesAtHead.operand(operand).dump(WTF::dataFile());
            dataLogF("\n");
#endif
        }
        block->cfaShouldRevisit = true;
    }
}

bool AbstractState::endBasicBlock(MergeMode mergeMode)
{
    ASSERT(m_block);
    
    BasicBlock* block = m_block; // Save the block for successor merging.
    
    block->cfaFoundConstants = m_foundConstants;
    block->cfaDidFinish = m_isValid;
    block->cfaBranchDirection = m_branchDirection;
    
    if (!m_isValid) {
        reset();
        return false;
    }
    
    bool changed = false;
    
    if (mergeMode != DontMerge || !ASSERT_DISABLED) {
        for (size_t argument = 0; argument < block->variablesAtTail.numberOfArguments(); ++argument) {
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
            dataLogF("        Merging state for argument %zu.\n", argument);
#endif
            AbstractValue& destination = block->valuesAtTail.argument(argument);
            changed |= mergeStateAtTail(destination, m_variables.argument(argument), block->variablesAtTail.argument(argument));
        }
        
        for (size_t local = 0; local < block->variablesAtTail.numberOfLocals(); ++local) {
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
            dataLogF("        Merging state for local %zu.\n", local);
#endif
            AbstractValue& destination = block->valuesAtTail.local(local);
            changed |= mergeStateAtTail(destination, m_variables.local(local), block->variablesAtTail.local(local));
        }
    }
    
    ASSERT(mergeMode != DontMerge || !changed);
    
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
    dataLogF("        Branch direction = %s\n", branchDirectionToString(m_branchDirection));
#endif
    
    reset();
    
    if (mergeMode != MergeToSuccessors)
        return changed;
    
    return mergeToSuccessors(m_graph, block);
}

void AbstractState::reset()
{
    m_block = 0;
    m_isValid = false;
    m_branchDirection = InvalidBranchDirection;
}

AbstractState::BooleanResult AbstractState::booleanResult(Node* node, AbstractValue& value)
{
    JSValue childConst = value.value();
    if (childConst) {
        if (childConst.toBoolean(m_codeBlock->globalObjectFor(node->codeOrigin)->globalExec()))
            return DefinitelyTrue;
        return DefinitelyFalse;
    }

    // Next check if we can fold because we know that the source is an object or string and does not equal undefined.
    if (isCellSpeculation(value.m_type)
        && value.m_currentKnownStructure.hasSingleton()) {
        Structure* structure = value.m_currentKnownStructure.singleton();
        if (!structure->masqueradesAsUndefined(m_codeBlock->globalObjectFor(node->codeOrigin))
            && structure->typeInfo().type() != StringType)
            return DefinitelyTrue;
    }
    
    return UnknownBooleanResult;
}

bool AbstractState::execute(unsigned indexInBlock)
{
    ASSERT(m_block);
    ASSERT(m_isValid);
    
    m_didClobber = false;
    
    Node* node = m_block->at(indexInBlock);
        
    if (!node->shouldGenerate())
        return true;
        
    switch (node->op()) {
    case JSConstant:
    case WeakJSConstant:
    case PhantomArguments: {
        forNode(node).set(m_graph.valueOfJSConstant(node));
        node->setCanExit(false);
        break;
    }
        
    case Identity: {
        forNode(node) = forNode(node->child1());
        node->setCanExit(false);
        break;
    }
            
    case GetLocal: {
        VariableAccessData* variableAccessData = node->variableAccessData();
        if (variableAccessData->prediction() == SpecNone) {
            m_isValid = false;
            node->setCanExit(true);
            break;
        }
        bool canExit = false;
        AbstractValue value = m_variables.operand(variableAccessData->local());
        if (!variableAccessData->isCaptured()) {
            if (value.isClear())
                canExit |= true;
        }
        if (value.value())
            m_foundConstants = true;
        forNode(node) = value;
        node->setCanExit(canExit);
        break;
    }
        
    case GetLocalUnlinked: {
        AbstractValue value = m_variables.operand(node->unlinkedLocal());
        if (value.value())
            m_foundConstants = true;
        forNode(node) = value;
        node->setCanExit(false);
        break;
    }
        
    case SetLocal: {
        if (node->variableAccessData()->isCaptured()
            || m_graph.isCreatedThisArgument(node->local())) {
            m_variables.operand(node->local()) = forNode(node->child1());
            node->setCanExit(false);
            break;
        }
        
        if (node->variableAccessData()->shouldUseDoubleFormat()) {
            speculateNumberUnary(node);
            m_variables.operand(node->local()).set(SpecDouble);
            break;
        }
        
        SpeculatedType predictedType = node->variableAccessData()->argumentAwarePrediction();
        if (isInt32Speculation(predictedType))
            speculateInt32Unary(node);
        else if (isCellSpeculation(predictedType)) {
            node->setCanExit(!isCellSpeculation(forNode(node->child1()).m_type));
            forNode(node->child1()).filter(SpecCell);
        } else if (isBooleanSpeculation(predictedType))
            speculateBooleanUnary(node);
        else
            node->setCanExit(false);
        
        m_variables.operand(node->local()) = forNode(node->child1());
        break;
    }
            
    case SetArgument:
        // Assert that the state of arguments has been set.
        ASSERT(!m_block->valuesAtHead.operand(node->local()).isClear());
        node->setCanExit(false);
        break;
            
    case BitAnd:
    case BitOr:
    case BitXor:
    case BitRShift:
    case BitLShift:
    case BitURShift: {
        JSValue left = forNode(node->child1()).value();
        JSValue right = forNode(node->child2()).value();
        if (left && right && left.isInt32() && right.isInt32()) {
            int32_t a = left.asInt32();
            int32_t b = right.asInt32();
            bool constantWasSet;
            switch (node->op()) {
            case BitAnd:
                constantWasSet = trySetConstant(node, JSValue(a & b));
                break;
            case BitOr:
                constantWasSet = trySetConstant(node, JSValue(a | b));
                break;
            case BitXor:
                constantWasSet = trySetConstant(node, JSValue(a ^ b));
                break;
            case BitRShift:
                constantWasSet = trySetConstant(node, JSValue(a >> static_cast<uint32_t>(b)));
                break;
            case BitLShift:
                constantWasSet = trySetConstant(node, JSValue(a << static_cast<uint32_t>(b)));
                break;
            case BitURShift:
                constantWasSet = trySetConstant(node, JSValue(static_cast<uint32_t>(a) >> static_cast<uint32_t>(b)));
                break;
            default:
                RELEASE_ASSERT_NOT_REACHED();
                constantWasSet = false;
            }
            if (constantWasSet) {
                m_foundConstants = true;
                node->setCanExit(false);
                break;
            }
        }
        speculateInt32Binary(node);
        forNode(node).set(SpecInt32);
        break;
    }
        
    case UInt32ToNumber: {
        JSValue child = forNode(node->child1()).value();
        if (child && child.isNumber()) {
            ASSERT(child.isInt32());
            if (trySetConstant(node, JSValue(child.asUInt32()))) {
                m_foundConstants = true;
                node->setCanExit(false);
                break;
            }
        }
        if (!node->canSpeculateInteger()) {
            forNode(node).set(SpecDouble);
            node->setCanExit(false);
        } else {
            forNode(node).set(SpecInt32);
            node->setCanExit(true);
        }
        break;
    }
              
            
    case DoubleAsInt32: {
        JSValue child = forNode(node->child1()).value();
        if (child && child.isNumber()) {
            double asDouble = child.asNumber();
            int32_t asInt = JSC::toInt32(asDouble);
            if (bitwise_cast<int64_t>(static_cast<double>(asInt)) == bitwise_cast<int64_t>(asDouble)
                && trySetConstant(node, JSValue(asInt))) {
                m_foundConstants = true;
                break;
            }
        }
        node->setCanExit(true);
        forNode(node->child1()).filter(SpecNumber);
        forNode(node).set(SpecInt32);
        break;
    }
            
    case ValueToInt32: {
        JSValue child = forNode(node->child1()).value();
        if (child && child.isNumber()) {
            bool constantWasSet;
            if (child.isInt32())
                constantWasSet = trySetConstant(node, child);
            else
                constantWasSet = trySetConstant(node, JSValue(JSC::toInt32(child.asDouble())));
            if (constantWasSet) {
                m_foundConstants = true;
                node->setCanExit(false);
                break;
            }
        }
        if (node->child1()->shouldSpeculateInteger())
            speculateInt32Unary(node);
        else if (node->child1()->shouldSpeculateNumber())
            speculateNumberUnary(node);
        else if (node->child1()->shouldSpeculateBoolean())
            speculateBooleanUnary(node);
        else
            node->setCanExit(false);
        
        forNode(node).set(SpecInt32);
        break;
    }
        
    case Int32ToDouble: {
        JSValue child = forNode(node->child1()).value();
        if (child && child.isNumber()
            && trySetConstant(node, JSValue(JSValue::EncodeAsDouble, child.asNumber()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        speculateNumberUnary(node);
        if (isInt32Speculation(forNode(node->child1()).m_type))
            forNode(node).set(SpecDoubleReal);
        else
            forNode(node).set(SpecDouble);
        break;
    }
        
    case CheckNumber:
        forNode(node->child1()).filter(SpecNumber);
        break;
            
    case ValueAdd:
    case ArithAdd: {
        JSValue left = forNode(node->child1()).value();
        JSValue right = forNode(node->child2()).value();
        if (left && right && left.isNumber() && right.isNumber()
            && trySetConstant(node, JSValue(left.asNumber() + right.asNumber()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        if (m_graph.addShouldSpeculateInteger(node)) {
            speculateInt32Binary(
                node, !nodeCanTruncateInteger(node->arithNodeFlags()));
            forNode(node).set(SpecInt32);
            break;
        }
        if (Node::shouldSpeculateNumberExpectingDefined(node->child1().node(), node->child2().node())) {
            speculateNumberBinary(node);
            if (isRealNumberSpeculation(forNode(node->child1()).m_type)
                && isRealNumberSpeculation(forNode(node->child2()).m_type))
                forNode(node).set(SpecDoubleReal);
            else
                forNode(node).set(SpecDouble);
            break;
        }
        if (node->op() == ValueAdd) {
            clobberWorld(node->codeOrigin, indexInBlock);
            forNode(node).set(SpecString | SpecInt32 | SpecNumber);
            node->setCanExit(false);
            break;
        }
        // We don't handle this yet. :-(
        m_isValid = false;
        node->setCanExit(true);
        break;
    }
            
    case ArithSub: {
        JSValue left = forNode(node->child1()).value();
        JSValue right = forNode(node->child2()).value();
        if (left && right && left.isNumber() && right.isNumber()
            && trySetConstant(node, JSValue(left.asNumber() - right.asNumber()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        if (m_graph.addShouldSpeculateInteger(node)) {
            speculateInt32Binary(
                node, !nodeCanTruncateInteger(node->arithNodeFlags()));
            forNode(node).set(SpecInt32);
            break;
        }
        speculateNumberBinary(node);
        forNode(node).set(SpecDouble);
        break;
    }
        
    case ArithNegate: {
        JSValue child = forNode(node->child1()).value();
        if (child && child.isNumber()
            && trySetConstant(node, JSValue(-child.asNumber()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        if (m_graph.negateShouldSpeculateInteger(node)) {
            speculateInt32Unary(
                node, !nodeCanTruncateInteger(node->arithNodeFlags()));
            forNode(node).set(SpecInt32);
            break;
        }
        speculateNumberUnary(node);
        forNode(node).set(SpecDouble);
        break;
    }
        
    case ArithMul: {
        JSValue left = forNode(node->child1()).value();
        JSValue right = forNode(node->child2()).value();
        if (left && right && left.isNumber() && right.isNumber()
            && trySetConstant(node, JSValue(left.asNumber() * right.asNumber()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        if (m_graph.mulShouldSpeculateInteger(node)) {
            speculateInt32Binary(
                node,
                !nodeCanTruncateInteger(node->arithNodeFlags())
                || !nodeCanIgnoreNegativeZero(node->arithNodeFlags()));
            forNode(node).set(SpecInt32);
            break;
        }
        speculateNumberBinary(node);
        if (isRealNumberSpeculation(forNode(node->child1()).m_type)
            || isRealNumberSpeculation(forNode(node->child2()).m_type))
            forNode(node).set(SpecDoubleReal);
        else
            forNode(node).set(SpecDouble);
        break;
    }
        
    case ArithDiv:
    case ArithMin:
    case ArithMax:
    case ArithMod: {
        JSValue left = forNode(node->child1()).value();
        JSValue right = forNode(node->child2()).value();
        if (left && right && left.isNumber() && right.isNumber()) {
            double a = left.asNumber();
            double b = right.asNumber();
            bool constantWasSet;
            switch (node->op()) {
            case ArithDiv:
                constantWasSet = trySetConstant(node, JSValue(a / b));
                break;
            case ArithMin:
                constantWasSet = trySetConstant(node, JSValue(a < b ? a : (b <= a ? b : a + b)));
                break;
            case ArithMax:
                constantWasSet = trySetConstant(node, JSValue(a > b ? a : (b >= a ? b : a + b)));
                break;
            case ArithMod:
                constantWasSet = trySetConstant(node, JSValue(fmod(a, b)));
                break;
            default:
                RELEASE_ASSERT_NOT_REACHED();
                constantWasSet = false;
                break;
            }
            if (constantWasSet) {
                m_foundConstants = true;
                node->setCanExit(false);
                break;
            }
        }
        if (Node::shouldSpeculateIntegerForArithmetic(node->child1().node(), node->child2().node())
            && node->canSpeculateInteger()) {
            speculateInt32Binary(node, true); // forcing can-exit, which is a bit on the conservative side.
            forNode(node).set(SpecInt32);
            break;
        }
        speculateNumberBinary(node);
        forNode(node).set(SpecDouble);
        break;
    }
            
    case ArithAbs: {
        JSValue child = forNode(node->child1()).value();
        if (child && child.isNumber()
            && trySetConstant(node, JSValue(fabs(child.asNumber())))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        if (node->child1()->shouldSpeculateIntegerForArithmetic()
            && node->canSpeculateInteger()) {
            speculateInt32Unary(node, true);
            forNode(node).set(SpecInt32);
            break;
        }
        speculateNumberUnary(node);
        forNode(node).set(SpecDouble);
        break;
    }
            
    case ArithSqrt: {
        JSValue child = forNode(node->child1()).value();
        if (child && child.isNumber()
            && trySetConstant(node, JSValue(sqrt(child.asNumber())))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        speculateNumberUnary(node);
        forNode(node).set(SpecDouble);
        break;
    }
            
    case LogicalNot: {
        bool didSetConstant = false;
        switch (booleanResult(node, forNode(node->child1()))) {
        case DefinitelyTrue:
            didSetConstant = trySetConstant(node, jsBoolean(false));
            break;
        case DefinitelyFalse:
            didSetConstant = trySetConstant(node, jsBoolean(true));
            break;
        default:
            break;
        }
        if (didSetConstant) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        Node* child = node->child1().node();
        if (isBooleanSpeculation(child->prediction()))
            speculateBooleanUnary(node);
        else if (child->shouldSpeculateNonStringCellOrOther()) {
            node->setCanExit(true);
            forNode(child).filter((SpecCell & ~SpecString) | SpecOther);
        } else if (child->shouldSpeculateInteger())
            speculateInt32Unary(node);
        else if (child->shouldSpeculateNumber())
            speculateNumberUnary(node);
        else
            node->setCanExit(false);
        forNode(node).set(SpecBoolean);
        break;
    }
        
    case IsUndefined:
    case IsBoolean:
    case IsNumber:
    case IsString:
    case IsObject:
    case IsFunction: {
        node->setCanExit(node->op() == IsUndefined && m_codeBlock->globalObjectFor(node->codeOrigin)->masqueradesAsUndefinedWatchpoint()->isStillValid());
        JSValue child = forNode(node->child1()).value();
        if (child) {
            bool constantWasSet;
            switch (node->op()) {
            case IsUndefined:
                if (m_codeBlock->globalObjectFor(node->codeOrigin)->masqueradesAsUndefinedWatchpoint()->isStillValid()) {
                    constantWasSet = trySetConstant(node, jsBoolean(
                        child.isCell()
                        ? false 
                        : child.isUndefined()));
                } else {
                    constantWasSet = trySetConstant(node, jsBoolean(
                        child.isCell()
                        ? child.asCell()->structure()->masqueradesAsUndefined(m_codeBlock->globalObjectFor(node->codeOrigin))
                        : child.isUndefined()));
                }
                break;
            case IsBoolean:
                constantWasSet = trySetConstant(node, jsBoolean(child.isBoolean()));
                break;
            case IsNumber:
                constantWasSet = trySetConstant(node, jsBoolean(child.isNumber()));
                break;
            case IsString:
                constantWasSet = trySetConstant(node, jsBoolean(isJSString(child)));
                break;
            case IsObject:
                if (child.isNull() || !child.isObject()) {
                    constantWasSet = trySetConstant(node, jsBoolean(child.isNull()));
                    break;
                }
            default:
                constantWasSet = false;
                break;
            }
            if (constantWasSet) {
                m_foundConstants = true;
                break;
            }
        }

        forNode(node).set(SpecBoolean);
        break;
    }

    case TypeOf: {
        JSGlobalData* globalData = m_codeBlock->globalData();
        JSValue child = forNode(node->child1()).value();
        AbstractValue& abstractChild = forNode(node->child1());
        if (child) {
            JSValue typeString = jsTypeStringForValue(*globalData, m_codeBlock->globalObjectFor(node->codeOrigin), child);
            if (trySetConstant(node, typeString)) {
                m_foundConstants = true;
                break;
            }
        } else if (isNumberSpeculation(abstractChild.m_type)) {
            if (trySetConstant(node, globalData->smallStrings.numberString())) {
                forNode(node->child1()).filter(SpecNumber);
                m_foundConstants = true;
                break;
            }
        } else if (isStringSpeculation(abstractChild.m_type)) {
            if (trySetConstant(node, globalData->smallStrings.stringString())) {
                forNode(node->child1()).filter(SpecString);
                m_foundConstants = true;
                break;
            }
        } else if (isFinalObjectSpeculation(abstractChild.m_type) || isArraySpeculation(abstractChild.m_type) || isArgumentsSpeculation(abstractChild.m_type)) {
            if (trySetConstant(node, globalData->smallStrings.objectString())) {
                forNode(node->child1()).filter(SpecFinalObject | SpecArray | SpecArguments);
                m_foundConstants = true;
                break;
            }
        } else if (isFunctionSpeculation(abstractChild.m_type)) {
            if (trySetConstant(node, globalData->smallStrings.functionString())) {
                forNode(node->child1()).filter(SpecFunction);
                m_foundConstants = true;
                break;
            }
        } else if (isBooleanSpeculation(abstractChild.m_type)) {
            if (trySetConstant(node, globalData->smallStrings.booleanString())) {
                forNode(node->child1()).filter(SpecBoolean);
                m_foundConstants = true;
                break;
            }
        } else {
            Node* childNode = node->child1().node();
            if (isCellSpeculation(childNode->prediction())) {
                if (isStringSpeculation(childNode->prediction()))
                    forNode(childNode).filter(SpecString);
                else
                    forNode(childNode).filter(SpecCell);
                node->setCanExit(true);
            }
        }
        forNode(node).set(SpecString);
        break;
    }
            
    case CompareLess:
    case CompareLessEq:
    case CompareGreater:
    case CompareGreaterEq:
    case CompareEq: {
        bool constantWasSet = false;

        JSValue leftConst = forNode(node->child1()).value();
        JSValue rightConst = forNode(node->child2()).value();
        if (leftConst && rightConst && leftConst.isNumber() && rightConst.isNumber()) {
            double a = leftConst.asNumber();
            double b = rightConst.asNumber();
            switch (node->op()) {
            case CompareLess:
                constantWasSet = trySetConstant(node, jsBoolean(a < b));
                break;
            case CompareLessEq:
                constantWasSet = trySetConstant(node, jsBoolean(a <= b));
                break;
            case CompareGreater:
                constantWasSet = trySetConstant(node, jsBoolean(a > b));
                break;
            case CompareGreaterEq:
                constantWasSet = trySetConstant(node, jsBoolean(a >= b));
                break;
            case CompareEq:
                constantWasSet = trySetConstant(node, jsBoolean(a == b));
                break;
            default:
                RELEASE_ASSERT_NOT_REACHED();
                constantWasSet = false;
                break;
            }
        }
        
        if (!constantWasSet && node->op() == CompareEq) {
            SpeculatedType leftType = forNode(node->child1()).m_type;
            SpeculatedType rightType = forNode(node->child2()).m_type;
            if ((isInt32Speculation(leftType) && isOtherSpeculation(rightType))
                || (isOtherSpeculation(leftType) && isInt32Speculation(rightType)))
                constantWasSet = trySetConstant(node, jsBoolean(false));
        }
        
        if (constantWasSet) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        
        forNode(node).set(SpecBoolean);
        
        Node* left = node->child1().node();
        Node* right = node->child2().node();
        SpeculatedType filter;
        SpeculatedTypeChecker checker;
        if (Node::shouldSpeculateInteger(left, right)) {
            filter = SpecInt32;
            checker = isInt32Speculation;
        } else if (Node::shouldSpeculateNumber(left, right)) {
            filter = SpecNumber;
            checker = isNumberSpeculation;
        } else if (node->op() == CompareEq) {
            if ((m_graph.isConstant(left) && m_graph.valueOfJSConstant(left).isNull())
                || (m_graph.isConstant(right) && m_graph.valueOfJSConstant(right).isNull())) {
                // We can exit if we haven't fired the MasqueradesAsUndefind watchpoint yet.
                node->setCanExit(m_codeBlock->globalObjectFor(node->codeOrigin)->masqueradesAsUndefinedWatchpoint()->isStillValid());
                break;
            }
            
            if (left->shouldSpeculateString() || right->shouldSpeculateString()) {
                node->setCanExit(false);
                break;
            } 
            if (left->shouldSpeculateNonStringCell() && right->shouldSpeculateNonStringCellOrOther()) {
                node->setCanExit(true);
                forNode(left).filter(SpecCell & ~SpecString);
                forNode(right).filter((SpecCell & ~SpecString) | SpecOther);
                break;
            }
            if (left->shouldSpeculateNonStringCellOrOther() && right->shouldSpeculateNonStringCell()) {
                node->setCanExit(true);
                forNode(left).filter((SpecCell & ~SpecString) | SpecOther);
                forNode(right).filter(SpecCell & ~SpecString);
                break;
            }
            if (left->shouldSpeculateNonStringCell() && right->shouldSpeculateNonStringCell()) {
                node->setCanExit(true);
                forNode(left).filter(SpecCell & ~SpecString);
                forNode(right).filter(SpecCell & ~SpecString);
                break;
            }
 
            filter = SpecTop;
            checker = isAnySpeculation;
            clobberWorld(node->codeOrigin, indexInBlock);
        } else {
            filter = SpecTop;
            checker = isAnySpeculation;
            clobberWorld(node->codeOrigin, indexInBlock);
        }
        node->setCanExit(
            !checker(forNode(left).m_type)
            || !checker(forNode(right).m_type));
        forNode(left).filter(filter);
        forNode(right).filter(filter);
        break;
    }
            
    case CompareStrictEq: {
        Node* leftNode = node->child1().node();
        Node* rightNode = node->child2().node();
        JSValue left = forNode(leftNode).value();
        JSValue right = forNode(rightNode).value();
        if (left && right && left.isNumber() && right.isNumber()
            && trySetConstant(node, jsBoolean(left.asNumber() == right.asNumber()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        forNode(node).set(SpecBoolean);
        if (m_graph.isJSConstant(leftNode)) {
            JSValue value = m_graph.valueOfJSConstant(leftNode);
            if (!value.isNumber() && !value.isString()) {
                node->setCanExit(false);
                break;
            }
        }
        if (m_graph.isJSConstant(rightNode)) {
            JSValue value = m_graph.valueOfJSConstant(rightNode);
            if (!value.isNumber() && !value.isString()) {
                node->setCanExit(false);
                break;
            }
        }
        if (Node::shouldSpeculateInteger(leftNode, rightNode)) {
            speculateInt32Binary(node);
            break;
        }
        if (Node::shouldSpeculateNumber(leftNode, rightNode)) {
            speculateNumberBinary(node);
            break;
        }
        if (leftNode->shouldSpeculateString() || rightNode->shouldSpeculateString()) {
            node->setCanExit(false);
            break;
        }
        if (leftNode->shouldSpeculateNonStringCell() && rightNode->shouldSpeculateNonStringCell()) {
            node->setCanExit(true);
            forNode(leftNode).filter((SpecCell & ~SpecString) | SpecOther);
            forNode(rightNode).filter((SpecCell & ~SpecString) | SpecOther);
            break;
        }
        node->setCanExit(false);
        break;
    }
        
    case StringCharCodeAt:
        node->setCanExit(true);
        forNode(node->child1()).filter(SpecString);
        forNode(node->child2()).filter(SpecInt32);
        forNode(node).set(SpecInt32);
        break;
        
    case StringCharAt:
        node->setCanExit(true);
        forNode(node->child1()).filter(SpecString);
        forNode(node->child2()).filter(SpecInt32);
        forNode(node).set(SpecString);
        break;
            
    case GetByVal: {
        node->setCanExit(true);
        switch (node->arrayMode().type()) {
        case Array::SelectUsingPredictions:
        case Array::Unprofiled:
        case Array::Undecided:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        case Array::ForceExit:
            m_isValid = false;
            break;
        case Array::Generic:
            clobberWorld(node->codeOrigin, indexInBlock);
            forNode(node).makeTop();
            break;
        case Array::String:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecString);
            break;
        case Array::Arguments:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).makeTop();
            break;
        case Array::Int32:
            forNode(node->child2()).filter(SpecInt32);
            if (node->arrayMode().isOutOfBounds()) {
                clobberWorld(node->codeOrigin, indexInBlock);
                forNode(node).makeTop();
            } else
                forNode(node).set(SpecInt32);
            break;
        case Array::Double:
            forNode(node->child2()).filter(SpecInt32);
            if (node->arrayMode().isOutOfBounds()) {
                clobberWorld(node->codeOrigin, indexInBlock);
                forNode(node).makeTop();
            } else if (node->arrayMode().isSaneChain())
                forNode(node).set(SpecDouble);
            else
                forNode(node).set(SpecDoubleReal);
            break;
        case Array::Contiguous:
        case Array::ArrayStorage:
        case Array::SlowPutArrayStorage:
            forNode(node->child2()).filter(SpecInt32);
            if (node->arrayMode().isOutOfBounds())
                clobberWorld(node->codeOrigin, indexInBlock);
            forNode(node).makeTop();
            break;
        case Array::Int8Array:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecInt32);
            break;
        case Array::Int16Array:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecInt32);
            break;
        case Array::Int32Array:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecInt32);
            break;
        case Array::Uint8Array:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecInt32);
            break;
        case Array::Uint8ClampedArray:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecInt32);
            break;
        case Array::Uint16Array:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecInt32);
            break;
        case Array::Uint32Array:
            forNode(node->child2()).filter(SpecInt32);
            if (node->shouldSpeculateInteger())
                forNode(node).set(SpecInt32);
            else
                forNode(node).set(SpecDouble);
            break;
        case Array::Float32Array:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecDouble);
            break;
        case Array::Float64Array:
            forNode(node->child2()).filter(SpecInt32);
            forNode(node).set(SpecDouble);
            break;
        default:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        }
        break;
    }
            
    case PutByVal:
    case PutByValAlias: {
        node->setCanExit(true);
        Edge child1 = m_graph.varArgChild(node, 0);
        Edge child2 = m_graph.varArgChild(node, 1);
        Edge child3 = m_graph.varArgChild(node, 2);
        switch (node->arrayMode().modeForPut().type()) {
        case Array::ForceExit:
            m_isValid = false;
            break;
        case Array::Generic:
            clobberWorld(node->codeOrigin, indexInBlock);
            break;
        case Array::Int32:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            forNode(child3).filter(SpecInt32);
            if (node->arrayMode().isOutOfBounds())
                clobberWorld(node->codeOrigin, indexInBlock);
            break;
        case Array::Double:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            forNode(child3).filter(SpecRealNumber);
            if (node->arrayMode().isOutOfBounds())
                clobberWorld(node->codeOrigin, indexInBlock);
            break;
        case Array::Contiguous:
        case Array::ArrayStorage:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (node->arrayMode().isOutOfBounds())
                clobberWorld(node->codeOrigin, indexInBlock);
            break;
        case Array::SlowPutArrayStorage:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (node->arrayMode().mayStoreToHole())
                clobberWorld(node->codeOrigin, indexInBlock);
            break;
        case Array::Arguments:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            break;
        case Array::Int8Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (child3->shouldSpeculateInteger())
                forNode(child3).filter(SpecInt32);
            else
                forNode(child3).filter(SpecNumber);
            break;
        case Array::Int16Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (child3->shouldSpeculateInteger())
                forNode(child3).filter(SpecInt32);
            else
                forNode(child3).filter(SpecNumber);
            break;
        case Array::Int32Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (child3->shouldSpeculateInteger())
                forNode(child3).filter(SpecInt32);
            else
                forNode(child3).filter(SpecNumber);
            break;
        case Array::Uint8Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (child3->shouldSpeculateInteger())
                forNode(child3).filter(SpecInt32);
            else
                forNode(child3).filter(SpecNumber);
            break;
        case Array::Uint8ClampedArray:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (child3->shouldSpeculateInteger())
                forNode(child3).filter(SpecInt32);
            else
                forNode(child3).filter(SpecNumber);
            break;
        case Array::Uint16Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (child3->shouldSpeculateInteger())
                forNode(child3).filter(SpecInt32);
            else
                forNode(child3).filter(SpecNumber);
            break;
        case Array::Uint32Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            if (child3->shouldSpeculateInteger())
                forNode(child3).filter(SpecInt32);
            else
                forNode(child3).filter(SpecNumber);
            break;
        case Array::Float32Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            forNode(child3).filter(SpecNumber);
            break;
        case Array::Float64Array:
            forNode(child1).filter(SpecCell);
            forNode(child2).filter(SpecInt32);
            forNode(child3).filter(SpecNumber);
            break;
        default:
            CRASH();
            break;
        }
        break;
    }
            
    case ArrayPush:
        node->setCanExit(true);
        switch (node->arrayMode().type()) {
        case Array::Int32:
            forNode(node->child2()).filter(SpecInt32);
            break;
        case Array::Double:
            forNode(node->child2()).filter(SpecRealNumber);
            break;
        default:
            break;
        }
        clobberWorld(node->codeOrigin, indexInBlock);
        forNode(node).set(SpecNumber);
        break;
            
    case ArrayPop:
        node->setCanExit(true);
        clobberWorld(node->codeOrigin, indexInBlock);
        forNode(node).makeTop();
        break;
            
    case RegExpExec:
    case RegExpTest:
        node->setCanExit(
            !isCellSpeculation(forNode(node->child1()).m_type)
            || !isCellSpeculation(forNode(node->child2()).m_type));
        forNode(node->child1()).filter(SpecCell);
        forNode(node->child2()).filter(SpecCell);
        forNode(node).makeTop();
        break;
            
    case Jump:
        node->setCanExit(false);
        break;
            
    case Branch: {
        Node* child = node->child1().node();
        BooleanResult result = booleanResult(node, forNode(child));
        if (result == DefinitelyTrue) {
            m_branchDirection = TakeTrue;
            node->setCanExit(false);
            break;
        }
        if (result == DefinitelyFalse) {
            m_branchDirection = TakeFalse;
            node->setCanExit(false);
            break;
        }
        // FIXME: The above handles the trivial cases of sparse conditional
        // constant propagation, but we can do better:
        // We can specialize the source variable's value on each direction of
        // the branch.
        if (child->shouldSpeculateBoolean())
            speculateBooleanUnary(node);
        else if (child->shouldSpeculateNonStringCellOrOther()) {
            node->setCanExit(true);
            forNode(child).filter((SpecCell & ~SpecString) | SpecOther);
        } else if (child->shouldSpeculateInteger())
            speculateInt32Unary(node);
        else if (child->shouldSpeculateNumber())
            speculateNumberUnary(node);
        else
            node->setCanExit(false);
        m_branchDirection = TakeBoth;
        break;
    }
            
    case Return:
        m_isValid = false;
        node->setCanExit(false);
        break;
        
    case Throw:
    case ThrowReferenceError:
        m_isValid = false;
        node->setCanExit(true);
        break;
            
    case ToPrimitive: {
        Node* child = node->child1().node();
        
        JSValue childConst = forNode(child).value();
        if (childConst && childConst.isNumber() && trySetConstant(node, childConst)) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        
        if (child->shouldSpeculateInteger()) {
            speculateInt32Unary(node);
            forNode(node).set(SpecInt32);
            break;
        }

        AbstractValue& source = forNode(child);
        AbstractValue& destination = forNode(node);
        
        // NB. The more canonical way of writing this would have been:
        //
        // destination = source;
        // if (destination.m_type & !(SpecNumber | SpecString | SpecBoolean)) {
        //     destination.filter(SpecNumber | SpecString | SpecBoolean);
        //     AbstractValue string;
        //     string.set(globalData->stringStructure);
        //     destination.merge(string);
        // }
        //
        // The reason why this would, in most other cases, have been better is that
        // then destination would preserve any non-SpeculatedType knowledge of source.
        // As it stands, the code below forgets any non-SpeculatedType knowledge that
        // source would have had. Fortunately, though, for things like strings and
        // numbers and booleans, we don't care about the non-SpeculatedType knowedge:
        // the structure won't tell us anything we don't already know, and neither
        // will ArrayModes. And if the source was a meaningful constant then we
        // would have handled that above. Unfortunately, this does mean that
        // ToPrimitive will currently forget string constants. But that's not a big
        // deal since we don't do any optimization on those currently.
        
        SpeculatedType type = source.m_type;
        if (type & ~(SpecNumber | SpecString | SpecBoolean)) {
            type &= (SpecNumber | SpecString | SpecBoolean);
            type |= SpecString;
        }
        destination.set(type);
        node->setCanExit(false);
        break;
    }
            
    case StrCat:
        node->setCanExit(false);
        forNode(node).set(SpecString);
        break;
            
    case NewArray:
        node->setCanExit(true);
        forNode(node).set(m_graph.globalObjectFor(node->codeOrigin)->arrayStructureForIndexingTypeDuringAllocation(node->indexingType()));
        m_haveStructures = true;
        break;
        
    case NewArrayBuffer:
        node->setCanExit(true);
        forNode(node).set(m_graph.globalObjectFor(node->codeOrigin)->arrayStructureForIndexingTypeDuringAllocation(node->indexingType()));
        m_haveStructures = true;
        break;

    case NewArrayWithSize:
        node->setCanExit(true);
        forNode(node->child1()).filter(SpecInt32);
        forNode(node).set(SpecArray);
        m_haveStructures = true;
        break;
            
    case NewRegexp:
        node->setCanExit(false);
        forNode(node).set(m_graph.globalObjectFor(node->codeOrigin)->regExpStructure());
        m_haveStructures = true;
        break;
            
    case ConvertThis: {
        Node* child = node->child1().node();
        AbstractValue& source = forNode(node->child1());
        AbstractValue& destination = forNode(node);
            
        if (isObjectSpeculation(source.m_type)) {
            // This is the simple case. We already know that the source is an
            // object, so there's nothing to do. I don't think this case will
            // be hit, but then again, you never know.
            destination = source;
            node->setCanExit(false);
            m_foundConstants = true; // Tell the constant folder to turn this into Identity.
            break;
        }
        
        node->setCanExit(true);
        
        if (isOtherSpeculation(child->prediction())) {
            source.filter(SpecOther);
            destination.set(SpecObjectOther);
            break;
        }
        
        if (isObjectSpeculation(child->prediction())) {
            source.filter(SpecObjectMask);
            destination = source;
            break;
        }
            
        destination = source;
        destination.merge(SpecObjectOther);
        break;
    }

    case CreateThis: {
        AbstractValue& source = forNode(node->child1());
        AbstractValue& destination = forNode(node);
        
        node->setCanExit(!isCellSpeculation(source.m_type));
            
        source.filter(SpecFunction);
        destination.set(SpecFinalObject);
        break;
    }
        
    case AllocationProfileWatchpoint:
        node->setCanExit(true);
        break;

    case NewObject:
        node->setCanExit(false);
        forNode(node).set(node->structure());
        m_haveStructures = true;
        break;
        
    case CreateActivation:
        node->setCanExit(false);
        forNode(node).set(m_codeBlock->globalObjectFor(node->codeOrigin)->activationStructure());
        m_haveStructures = true;
        break;
        
    case CreateArguments:
        node->setCanExit(false);
        forNode(node).set(m_codeBlock->globalObjectFor(node->codeOrigin)->argumentsStructure());
        m_haveStructures = true;
        break;
        
    case TearOffActivation:
    case TearOffArguments:
        node->setCanExit(false);
        // Does nothing that is user-visible.
        break;

    case CheckArgumentsNotCreated:
        if (isEmptySpeculation(
                m_variables.operand(
                    m_graph.argumentsRegisterFor(node->codeOrigin)).m_type)) {
            node->setCanExit(false);
            m_foundConstants = true;
        } else
            node->setCanExit(true);
        break;
        
    case GetMyArgumentsLength:
        // We know that this executable does not escape its arguments, so we can optimize
        // the arguments a bit. Note that this is not sufficient to force constant folding
        // of GetMyArgumentsLength, because GetMyArgumentsLength is a clobbering operation.
        // We perform further optimizations on this later on.
        if (node->codeOrigin.inlineCallFrame)
            forNode(node).set(jsNumber(node->codeOrigin.inlineCallFrame->arguments.size() - 1));
        else
            forNode(node).set(SpecInt32);
        node->setCanExit(
            !isEmptySpeculation(
                m_variables.operand(
                    m_graph.argumentsRegisterFor(node->codeOrigin)).m_type));
        break;
        
    case GetMyArgumentsLengthSafe:
        node->setCanExit(false);
        // This potentially clobbers all structures if the arguments object had a getter
        // installed on the length property.
        clobberWorld(node->codeOrigin, indexInBlock);
        // We currently make no guarantee about what this returns because it does not
        // speculate that the length property is actually a length.
        forNode(node).makeTop();
        break;
        
    case GetMyArgumentByVal:
        node->setCanExit(true);
        // We know that this executable does not escape its arguments, so we can optimize
        // the arguments a bit. Note that this ends up being further optimized by the
        // ArgumentsSimplificationPhase.
        forNode(node->child1()).filter(SpecInt32);
        forNode(node).makeTop();
        break;
        
    case GetMyArgumentByValSafe:
        node->setCanExit(true);
        // This potentially clobbers all structures if the property we're accessing has
        // a getter. We don't speculate against this.
        clobberWorld(node->codeOrigin, indexInBlock);
        // But we do speculate that the index is an integer.
        forNode(node->child1()).filter(SpecInt32);
        // And the result is unknown.
        forNode(node).makeTop();
        break;
        
    case NewFunction:
    case NewFunctionExpression:
    case NewFunctionNoCheck:
        node->setCanExit(false);
        forNode(node).set(m_codeBlock->globalObjectFor(node->codeOrigin)->functionStructure());
        break;
        
    case GetCallee:
        node->setCanExit(false);
        forNode(node).set(SpecFunction);
        break;
        
    case SetCallee:
    case SetMyScope:
        node->setCanExit(false);
        break;
            
    case GetScope: // FIXME: We could get rid of these if we know that the JSFunction is a constant. https://bugs.webkit.org/show_bug.cgi?id=106202
    case GetMyScope:
    case SkipTopScope:
        node->setCanExit(false);
        forNode(node).set(SpecCellOther);
        break;

    case SkipScope: {
        node->setCanExit(false);
        JSValue child = forNode(node->child1()).value();
        if (child && trySetConstant(node, JSValue(jsCast<JSScope*>(child.asCell())->next()))) {
            m_foundConstants = true;
            break;
        }
        forNode(node).set(SpecCellOther);
        break;
    }

    case GetScopeRegisters:
        node->setCanExit(false);
        forNode(node->child1()).filter(SpecCell);
        forNode(node).clear(); // The result is not a JS value.
        break;

    case GetScopedVar:
        node->setCanExit(false);
        forNode(node).makeTop();
        break;
            
    case PutScopedVar:
        node->setCanExit(false);
        clobberCapturedVars(node->codeOrigin);
        break;
            
    case GetById:
    case GetByIdFlush:
        node->setCanExit(true);
        if (!node->prediction()) {
            m_isValid = false;
            break;
        }
        if (isCellSpeculation(node->child1()->prediction())) {
            forNode(node->child1()).filter(SpecCell);

            if (Structure* structure = forNode(node->child1()).bestProvenStructure()) {
                GetByIdStatus status = GetByIdStatus::computeFor(
                    m_graph.m_globalData, structure,
                    m_graph.m_codeBlock->identifier(node->identifierNumber()));
                if (status.isSimple()) {
                    // Assert things that we can't handle and that the computeFor() method
                    // above won't be able to return.
                    ASSERT(status.structureSet().size() == 1);
                    ASSERT(status.chain().isEmpty());
                    
                    if (status.specificValue())
                        forNode(node).set(status.specificValue());
                    else
                        forNode(node).makeTop();
                    forNode(node->child1()).filter(status.structureSet());
                    
                    m_foundConstants = true;
                    break;
                }
            }
        }
        clobberWorld(node->codeOrigin, indexInBlock);
        forNode(node).makeTop();
        break;
            
    case GetArrayLength:
        node->setCanExit(true); // Lies, but it's true for the common case of JSArray, so it's good enough.
        forNode(node).set(SpecInt32);
        break;
        
    case CheckExecutable: {
        // FIXME: We could track executables in AbstractValue, which would allow us to get rid of these checks
        // more thoroughly. https://bugs.webkit.org/show_bug.cgi?id=106200
        // FIXME: We could eliminate these entirely if we know the exact value that flows into this.
        // https://bugs.webkit.org/show_bug.cgi?id=106201
        forNode(node->child1()).filter(SpecCell);
        node->setCanExit(true);
        break;
    }

    case CheckStructure:
    case ForwardCheckStructure: {
        // FIXME: We should be able to propagate the structure sets of constants (i.e. prototypes).
        AbstractValue& value = forNode(node->child1());
        // If this structure check is attempting to prove knowledge already held in
        // the futurePossibleStructure set then the constant folding phase should
        // turn this into a watchpoint instead.
        StructureSet& set = node->structureSet();
        if (value.m_futurePossibleStructure.isSubsetOf(set)
            || value.m_currentKnownStructure.isSubsetOf(set))
            m_foundConstants = true;
        node->setCanExit(
            !value.m_currentKnownStructure.isSubsetOf(set)
            || !isCellSpeculation(value.m_type));
        value.filter(set);
        m_haveStructures = true;
        break;
    }
        
    case StructureTransitionWatchpoint:
    case ForwardStructureTransitionWatchpoint: {
        AbstractValue& value = forNode(node->child1());

        // It's only valid to issue a structure transition watchpoint if we already
        // know that the watchpoint covers a superset of the structures known to
        // belong to the set of future structures that this value may have.
        // Currently, we only issue singleton watchpoints (that check one structure)
        // and our futurePossibleStructure set can only contain zero, one, or an
        // infinity of structures.
        ASSERT(value.m_futurePossibleStructure.isSubsetOf(StructureSet(node->structure())));
        
        ASSERT(value.isClear() || isCellSpeculation(value.m_type)); // Value could be clear if we've proven must-exit due to a speculation statically known to be bad.
        value.filter(node->structure());
        m_haveStructures = true;
        node->setCanExit(true);
        break;
    }
            
    case PutStructure:
    case PhantomPutStructure:
        node->setCanExit(false);
        if (!forNode(node->child1()).m_currentKnownStructure.isClear()) {
            clobberStructures(indexInBlock);
            forNode(node->child1()).set(node->structureTransitionData().newStructure);
            m_haveStructures = true;
        }
        break;
    case GetButterfly:
    case AllocatePropertyStorage:
    case ReallocatePropertyStorage:
        node->setCanExit(!isCellSpeculation(forNode(node->child1()).m_type));
        forNode(node->child1()).filter(SpecCell);
        forNode(node).clear(); // The result is not a JS value.
        break;
    case CheckArray: {
        if (node->arrayMode().alreadyChecked(m_graph, node, forNode(node->child1()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        node->setCanExit(true); // Lies, but this is followed by operations (like GetByVal) that always exit, so there is no point in us trying to be clever here.
        switch (node->arrayMode().type()) {
        case Array::String:
            forNode(node->child1()).filter(SpecString);
            break;
        case Array::Int32:
        case Array::Double:
        case Array::Contiguous:
        case Array::ArrayStorage:
        case Array::SlowPutArrayStorage:
            forNode(node->child1()).filter(SpecCell);
            break;
        case Array::Arguments:
            forNode(node->child1()).filter(SpecArguments);
            break;
        case Array::Int8Array:
            forNode(node->child1()).filter(SpecInt8Array);
            break;
        case Array::Int16Array:
            forNode(node->child1()).filter(SpecInt16Array);
            break;
        case Array::Int32Array:
            forNode(node->child1()).filter(SpecInt32Array);
            break;
        case Array::Uint8Array:
            forNode(node->child1()).filter(SpecUint8Array);
            break;
        case Array::Uint8ClampedArray:
            forNode(node->child1()).filter(SpecUint8ClampedArray);
            break;
        case Array::Uint16Array:
            forNode(node->child1()).filter(SpecUint16Array);
            break;
        case Array::Uint32Array:
            forNode(node->child1()).filter(SpecUint32Array);
            break;
        case Array::Float32Array:
            forNode(node->child1()).filter(SpecFloat32Array);
            break;
        case Array::Float64Array:
            forNode(node->child1()).filter(SpecFloat64Array);
            break;
        default:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        }
        forNode(node->child1()).filterArrayModes(node->arrayMode().arrayModesThatPassFiltering());
        m_haveStructures = true;
        break;
    }
    case Arrayify: {
        if (node->arrayMode().alreadyChecked(m_graph, node, forNode(node->child1()))) {
            m_foundConstants = true;
            node->setCanExit(false);
            break;
        }
        ASSERT(node->arrayMode().conversion() == Array::Convert
            || node->arrayMode().conversion() == Array::RageConvert);
        node->setCanExit(true);
        forNode(node->child1()).filter(SpecCell);
        if (node->child2())
            forNode(node->child2()).filter(SpecInt32);
        clobberStructures(indexInBlock);
        forNode(node->child1()).filterArrayModes(node->arrayMode().arrayModesThatPassFiltering());
        m_haveStructures = true;
        break;
    }
    case ArrayifyToStructure: {
        AbstractValue& value = forNode(node->child1());
        StructureSet set = node->structure();
        if (value.m_futurePossibleStructure.isSubsetOf(set)
            || value.m_currentKnownStructure.isSubsetOf(set))
            m_foundConstants = true;
        node->setCanExit(true);
        if (node->child2())
            forNode(node->child2()).filter(SpecInt32);
        clobberStructures(indexInBlock);
        value.filter(set);
        m_haveStructures = true;
        break;
    }
    case GetIndexedPropertyStorage: {
        node->setCanExit(false);
        forNode(node).clear();
        break; 
    }
    case GetByOffset:
        if (!node->child1()->hasStorageResult()) {
            node->setCanExit(!isCellSpeculation(forNode(node->child1()).m_type));
            forNode(node->child1()).filter(SpecCell);
        }
        forNode(node).makeTop();
        break;
            
    case PutByOffset: {
        bool canExit = false;
        if (!node->child1()->hasStorageResult()) {
            canExit |= !isCellSpeculation(forNode(node->child1()).m_type);
            forNode(node->child1()).filter(SpecCell);
        }
        canExit |= !isCellSpeculation(forNode(node->child2()).m_type);
        forNode(node->child2()).filter(SpecCell);
        node->setCanExit(canExit);
        break;
    }
            
    case CheckFunction: {
        JSValue value = forNode(node->child1()).value();
        if (value == node->function()) {
            m_foundConstants = true;
            ASSERT(value);
            node->setCanExit(false);
            break;
        }
        
        node->setCanExit(true); // Lies! We can do better.
        if (!forNode(node->child1()).filterByValue(node->function())) {
            m_isValid = false;
            break;
        }
        break;
    }
        
    case PutById:
    case PutByIdDirect:
        node->setCanExit(true);
        if (Structure* structure = forNode(node->child1()).bestProvenStructure()) {
            PutByIdStatus status = PutByIdStatus::computeFor(
                m_graph.m_globalData,
                m_graph.globalObjectFor(node->codeOrigin),
                structure,
                m_graph.m_codeBlock->identifier(node->identifierNumber()),
                node->op() == PutByIdDirect);
            if (status.isSimpleReplace()) {
                forNode(node->child1()).filter(structure);
                m_foundConstants = true;
                break;
            }
            if (status.isSimpleTransition()) {
                clobberStructures(indexInBlock);
                forNode(node->child1()).set(status.newStructure());
                m_haveStructures = true;
                m_foundConstants = true;
                break;
            }
        }
        forNode(node->child1()).filter(SpecCell);
        clobberWorld(node->codeOrigin, indexInBlock);
        break;
            
    case GetGlobalVar:
        node->setCanExit(false);
        forNode(node).makeTop();
        break;
        
    case GlobalVarWatchpoint:
        node->setCanExit(true);
        break;
            
    case PutGlobalVar:
    case PutGlobalVarCheck:
        node->setCanExit(false);
        break;
            
    case CheckHasInstance:
        node->setCanExit(true);
        forNode(node->child1()).filter(SpecCell);
        // Sadly, we don't propagate the fact that we've done CheckHasInstance
        break;
            
    case InstanceOf:
        node->setCanExit(true);
        // Again, sadly, we don't propagate the fact that we've done InstanceOf
        // FIXME: This appears broken: CheckHasInstance already does an unconditional cell
        // check. https://bugs.webkit.org/show_bug.cgi?id=107479
        if (!(node->child1()->prediction() & ~SpecCell) && !(forNode(node->child1()).m_type & ~SpecCell))
            forNode(node->child1()).filter(SpecCell);
        forNode(node->child2()).filter(SpecCell);
        forNode(node).set(SpecBoolean);
        break;
            
    case Phi:
    case Flush:
    case PhantomLocal:
        node->setCanExit(false);
        break;
            
    case Breakpoint:
        node->setCanExit(false);
        break;
            
    case Call:
    case Construct:
    case Resolve:
    case ResolveBase:
    case ResolveBaseStrictPut:
    case ResolveGlobal:
        node->setCanExit(true);
        clobberWorld(node->codeOrigin, indexInBlock);
        forNode(node).makeTop();
        break;

    case GarbageValue:
        clobberWorld(node->codeOrigin, indexInBlock);
        forNode(node).makeTop();
        break;

    case ForceOSRExit:
        node->setCanExit(true);
        m_isValid = false;
        break;
            
    case Phantom:
    case InlineStart:
    case Nop:
    case CountExecution:
        node->setCanExit(false);
        break;
        
    case LastNodeType:
        RELEASE_ASSERT_NOT_REACHED();
        break;
    }
    
    return m_isValid;
}

inline void AbstractState::clobberWorld(const CodeOrigin& codeOrigin, unsigned indexInBlock)
{
    clobberCapturedVars(codeOrigin);
    clobberStructures(indexInBlock);
}

inline void AbstractState::clobberCapturedVars(const CodeOrigin& codeOrigin)
{
    if (codeOrigin.inlineCallFrame) {
        const BitVector& capturedVars = codeOrigin.inlineCallFrame->capturedVars;
        for (size_t i = capturedVars.size(); i--;) {
            if (!capturedVars.quickGet(i))
                continue;
            m_variables.local(i).makeTop();
        }
    } else {
        for (size_t i = m_codeBlock->m_numVars; i--;) {
            if (m_codeBlock->isCaptured(i))
                m_variables.local(i).makeTop();
        }
    }

    for (size_t i = m_variables.numberOfArguments(); i--;) {
        if (m_codeBlock->isCaptured(argumentToOperand(i)))
            m_variables.argument(i).makeTop();
    }
}

inline void AbstractState::clobberStructures(unsigned indexInBlock)
{
    if (!m_haveStructures)
        return;
    for (size_t i = indexInBlock + 1; i--;)
        forNode(m_block->at(i)).clobberStructures();
    for (size_t i = m_variables.numberOfArguments(); i--;)
        m_variables.argument(i).clobberStructures();
    for (size_t i = m_variables.numberOfLocals(); i--;)
        m_variables.local(i).clobberStructures();
    m_haveStructures = false;
    m_didClobber = true;
}

inline bool AbstractState::mergeStateAtTail(AbstractValue& destination, AbstractValue& inVariable, Node* node)
{
    if (!node)
        return false;
        
    AbstractValue source;
    
    if (node->variableAccessData()->isCaptured()) {
        // If it's captured then we know that whatever value was stored into the variable last is the
        // one we care about. This is true even if the variable at tail is dead, which might happen if
        // the last thing we did to the variable was a GetLocal and then ended up now using the
        // GetLocal's result.
        
        source = inVariable;
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
        dataLogF("          Transfering ");
        source.dump(WTF::dataFile());
        dataLogF(" from last access due to captured variable.\n");
#endif
    } else {
        if (!node->shouldGenerate()) {
            // If the node at tail is a GetLocal that is dead, then skip it to get to the Phi.
            // The Phi may be live.
            if (node->op() != GetLocal)
                return false;
            
            node = node->child1().node();
            ASSERT(node->op() == Phi);
            if (!node->shouldGenerate())
                return false;
        }
        
        ASSERT(node->shouldGenerate());

#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
        dataLogF("          It's live, node @%u.\n", node->index());
#endif
    
        switch (node->op()) {
        case Phi:
        case SetArgument:
        case PhantomLocal:
        case Flush:
            // The block transfers the value from head to tail.
            source = inVariable;
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
            dataLogF("          Transfering ");
            source.dump(WTF::dataFile());
            dataLogF(" from head to tail.\n");
#endif
            break;
            
        case GetLocal:
            // The block refines the value with additional speculations.
            source = forNode(node);
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
            dataLogF("          Refining to ");
            source.dump(WTF::dataFile());
            dataLogF("\n");
#endif
            break;
            
        case SetLocal:
            // The block sets the variable, and potentially refines it, both
            // before and after setting it.
            if (node->variableAccessData()->shouldUseDoubleFormat()) {
                // FIXME: This unnecessarily loses precision.
                source.set(SpecDouble);
            } else
                source = forNode(node->child1());
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
            dataLogF("          Setting to ");
            source.dump(WTF::dataFile());
            dataLogF("\n");
#endif
            break;
        
        default:
            RELEASE_ASSERT_NOT_REACHED();
            break;
        }
    }
    
    if (destination == source) {
        // Abstract execution did not change the output value of the variable, for this
        // basic block, on this iteration.
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
        dataLogF("          Not changed!\n");
#endif
        return false;
    }
    
    // Abstract execution reached a new conclusion about the speculations reached about
    // this variable after execution of this basic block. Update the state, and return
    // true to indicate that the fixpoint must go on!
    destination = source;
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
    dataLogF("          Changed!\n");
#endif
    return true;
}

inline bool AbstractState::merge(BasicBlock* from, BasicBlock* to)
{
    ASSERT(from->variablesAtTail.numberOfArguments() == to->variablesAtHead.numberOfArguments());
    ASSERT(from->variablesAtTail.numberOfLocals() == to->variablesAtHead.numberOfLocals());
    
    bool changed = false;
    
    for (size_t argument = 0; argument < from->variablesAtTail.numberOfArguments(); ++argument) {
        AbstractValue& destination = to->valuesAtHead.argument(argument);
        changed |= mergeVariableBetweenBlocks(destination, from->valuesAtTail.argument(argument), to->variablesAtHead.argument(argument), from->variablesAtTail.argument(argument));
    }
    
    for (size_t local = 0; local < from->variablesAtTail.numberOfLocals(); ++local) {
        AbstractValue& destination = to->valuesAtHead.local(local);
        changed |= mergeVariableBetweenBlocks(destination, from->valuesAtTail.local(local), to->variablesAtHead.local(local), from->variablesAtTail.local(local));
    }

    if (!to->cfaHasVisited)
        changed = true;
    
    to->cfaShouldRevisit |= changed;
    
    return changed;
}

inline bool AbstractState::mergeToSuccessors(Graph& graph, BasicBlock* basicBlock)
{
    Node* terminal = basicBlock->last();
    
    ASSERT(terminal->isTerminal());
    
    switch (terminal->op()) {
    case Jump: {
        ASSERT(basicBlock->cfaBranchDirection == InvalidBranchDirection);
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
        dataLogF("        Merging to block #%u.\n", terminal.takenBlockIndex());
#endif
        return merge(basicBlock, graph.m_blocks[terminal->takenBlockIndex()].get());
    }
        
    case Branch: {
        ASSERT(basicBlock->cfaBranchDirection != InvalidBranchDirection);
        bool changed = false;
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
        dataLogF("        Merging to block #%u.\n", terminal->takenBlockIndex());
#endif
        if (basicBlock->cfaBranchDirection != TakeFalse)
            changed |= merge(basicBlock, graph.m_blocks[terminal->takenBlockIndex()].get());
#if DFG_ENABLE(DEBUG_PROPAGATION_VERBOSE)
        dataLogF("        Merging to block #%u.\n", terminal.notTakenBlockIndex());
#endif
        if (basicBlock->cfaBranchDirection != TakeTrue)
            changed |= merge(basicBlock, graph.m_blocks[terminal->notTakenBlockIndex()].get());
        return changed;
    }
        
    case Return:
    case Throw:
    case ThrowReferenceError:
        ASSERT(basicBlock->cfaBranchDirection == InvalidBranchDirection);
        return false;
        
    default:
        RELEASE_ASSERT_NOT_REACHED();
        return false;
    }
}

inline bool AbstractState::mergeVariableBetweenBlocks(AbstractValue& destination, AbstractValue& source, Node* destinationNode, Node* sourceNode)
{
    if (!destinationNode)
        return false;
    
    ASSERT_UNUSED(sourceNode, sourceNode);
    
    // FIXME: We could do some sparse conditional propagation here!
    
    return destination.merge(source);
}

void AbstractState::dump(PrintStream& out)
{
    bool first = true;
    for (size_t i = 0; i < m_block->size(); ++i) {
        Node* node = m_block->at(i);
        AbstractValue& value = forNode(node);
        if (value.isClear())
            continue;
        if (first)
            first = false;
        else
            out.printf(" ");
        out.printf("@%lu:", static_cast<unsigned long>(node->index()));
        value.dump(out);
    }
}

} } // namespace JSC::DFG

#endif // ENABLE(DFG_JIT)

