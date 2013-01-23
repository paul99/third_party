/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "cc/CCScheduler.h"
#include "TraceEvent.h"

namespace WebCore {

CCScheduler::CCScheduler(CCSchedulerClient* client, PassOwnPtr<CCFrameRateController> frameRateController)
    : m_client(client)
    , m_frameRateController(frameRateController)
    , m_updateMoreResourcesPending(false)
    , m_isInDraw(false)
    , m_setNeedsCommitAfterDraw(false)
    , m_setNeedsRedrawAfterDraw(false)
{
    ASSERT(m_client);
    m_frameRateController->setClient(this);
    m_frameRateController->setActive(m_stateMachine.vsyncCallbackNeeded());
}

CCScheduler::~CCScheduler()
{
    m_frameRateController->setActive(false);
}

void CCScheduler::setVisible(bool visible)
{
    m_stateMachine.setVisible(visible);
    processScheduledActions();
}

void CCScheduler::setNeedsCommit()
{
    if (m_isInDraw) {
        m_setNeedsCommitAfterDraw = true;
        return;
    }

    m_stateMachine.setNeedsCommit();
    processScheduledActions();
}

void CCScheduler::setNeedsRedraw()
{
    if (m_isInDraw) {
        m_setNeedsRedrawAfterDraw = true;
        return;
    }

    m_stateMachine.setNeedsRedraw();
    processScheduledActions();
}

void CCScheduler::setNeedsForcedRedraw()
{
    m_stateMachine.setNeedsForcedRedraw();
    processScheduledActions();
}

void CCScheduler::beginFrameComplete()
{
    TRACE_EVENT("CCScheduler::beginFrameComplete", this, 0);
    m_stateMachine.beginFrameComplete();
    processScheduledActions();
}

void CCScheduler::beginFrame()
{
    if (m_updateMoreResourcesPending) {
        m_updateMoreResourcesPending = false;
        m_stateMachine.beginUpdateMoreResourcesComplete(m_client->hasMoreResourceUpdates());
    }
    TRACE_EVENT("CCScheduler::beginFrame", this, 0);

    m_stateMachine.didEnterVSync();
    processScheduledActions();
    m_stateMachine.didLeaveVSync();
}

CCSchedulerStateMachine::Action CCScheduler::nextAction()
{
    m_stateMachine.setCanDraw(m_client->canDraw());
    return m_stateMachine.nextAction();
}

void CCScheduler::processScheduledActions()
{
    // The pre-allocation flag affects scheduling and vsync,
    // so make sure it is up-to-date.
    m_stateMachine.setHasMorePreallocations(m_client->hasMorePreallocations());
    m_stateMachine.setHasMoreResourceUpdates(m_client->hasMoreResourceUpdates());

    // Early out so we don't spam TRACE_EVENTS with useless processScheduledActions.
    if (nextAction() == CCSchedulerStateMachine::ACTION_NONE) {
        m_frameRateController->setActive(m_stateMachine.vsyncCallbackNeeded());
        return;
    }

    // This function can re-enter itself. For example, draw may call
    // setNeedsCommit. Proceeed with caution.
    CCSchedulerStateMachine::Action action;
    do {
        action = nextAction();
        m_stateMachine.updateState(action);

        switch (action) {
        case CCSchedulerStateMachine::ACTION_NONE:
            break;
        case CCSchedulerStateMachine::ACTION_BEGIN_FRAME:
            m_client->scheduledActionBeginFrame();
            break;
        case CCSchedulerStateMachine::ACTION_BEGIN_UPDATE_MORE_RESOURCES:
            // Preallocations shouldn't occur in update frames. However,
            // a preallocation could occur just before we start updating,
            // so we need to delay the first update in this one case.
            // FIXME: This cancellation of updates along with the rest
            // of this logic should happen in the state machine.
            if (!m_stateMachine.havePreallocatedSinceVSync())
                m_client->scheduledActionUpdateMoreResources();
            if (!m_client->hasMoreResourceUpdates()) {
                // If we were just told to update resources, but there are no
                // more pending, then tell the state machine that the
                // beginUpdateMoreResources completed. If more are pending,
                // then we will ack the update at the next draw.
                m_updateMoreResourcesPending = false;
                m_stateMachine.beginUpdateMoreResourcesComplete(false);
            } else
                m_updateMoreResourcesPending = true;
            break;
        case CCSchedulerStateMachine::ACTION_PREALLOCATE_MORE_RESOURCES:
            ASSERT(m_client->hasMorePreallocations());
            m_client->scheduledActionPreallocateMoreResources();
            break;
        case CCSchedulerStateMachine::ACTION_COMMIT:
            m_client->scheduledActionCommit();
            break;
        case CCSchedulerStateMachine::ACTION_DRAW:
            m_isInDraw = true;
            m_client->scheduledActionDrawAndSwap();
            m_isInDraw = false;
            m_frameRateController->didBeginFrame();
            if (m_setNeedsCommitAfterDraw) {
                setNeedsCommit();
                m_setNeedsCommitAfterDraw = false;
            }
            if (m_setNeedsRedrawAfterDraw) {
                setNeedsRedraw();
                m_setNeedsRedrawAfterDraw = false;
            }
            break;
        }
    } while (action != CCSchedulerStateMachine::ACTION_NONE);

    // Activate or deactivate the frame rate controller.
    // Update hasMorePreallocations as client might have changed (and we need this for vsync).
    m_stateMachine.setHasMorePreallocations(m_client->hasMorePreallocations());
    m_frameRateController->setActive(m_stateMachine.vsyncCallbackNeeded());
}

}
