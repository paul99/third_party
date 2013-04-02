/*
 * Copyright (C) 2010 Google, Inc. All Rights Reserved.
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
#include "HTMLDocumentParser.h"

#include "BackgroundHTMLParser.h"
#include "CompactHTMLToken.h"
#include "ContentSecurityPolicy.h"
#include "DocumentFragment.h"
#include "DocumentLoader.h"
#include "Element.h"
#include "Frame.h"
#include "HTMLNames.h"
#include "HTMLParserScheduler.h"
#include "HTMLParserThread.h"
#include "HTMLTokenizer.h"
#include "HTMLPreloadScanner.h"
#include "HTMLScriptRunner.h"
#include "HTMLTreeBuilder.h"
#include "HTMLDocument.h"
#include "InspectorInstrumentation.h"
#include "NestingLevelIncrementer.h"
#include "Settings.h"
#include <wtf/Functional.h>

namespace WebCore {

using namespace HTMLNames;

// This is a direct transcription of step 4 from:
// http://www.whatwg.org/specs/web-apps/current-work/multipage/the-end.html#fragment-case
static HTMLTokenizerState::State tokenizerStateForContextElement(Element* contextElement, bool reportErrors, const HTMLParserOptions& options)
{
    if (!contextElement)
        return HTMLTokenizerState::DataState;

    const QualifiedName& contextTag = contextElement->tagQName();

    if (contextTag.matches(titleTag) || contextTag.matches(textareaTag))
        return HTMLTokenizerState::RCDATAState;
    if (contextTag.matches(styleTag)
        || contextTag.matches(xmpTag)
        || contextTag.matches(iframeTag)
        || (contextTag.matches(noembedTag) && options.pluginsEnabled)
        || (contextTag.matches(noscriptTag) && options.scriptEnabled)
        || contextTag.matches(noframesTag))
        return reportErrors ? HTMLTokenizerState::RAWTEXTState : HTMLTokenizerState::PLAINTEXTState;
    if (contextTag.matches(scriptTag))
        return reportErrors ? HTMLTokenizerState::ScriptDataState : HTMLTokenizerState::PLAINTEXTState;
    if (contextTag.matches(plaintextTag))
        return HTMLTokenizerState::PLAINTEXTState;
    return HTMLTokenizerState::DataState;
}

HTMLDocumentParser::HTMLDocumentParser(HTMLDocument* document, bool reportErrors)
    : ScriptableDocumentParser(document)
    , m_options(document)
    , m_token(m_options.useThreading ? nullptr : adoptPtr(new HTMLToken))
    , m_tokenizer(m_options.useThreading ? nullptr : HTMLTokenizer::create(m_options))
    , m_scriptRunner(HTMLScriptRunner::create(document, this))
    , m_treeBuilder(HTMLTreeBuilder::create(this, document, reportErrors, m_options))
    , m_parserScheduler(HTMLParserScheduler::create(this))
    , m_xssAuditorDelegate(document)
#if ENABLE(THREADED_HTML_PARSER)
    , m_weakFactory(this)
#endif
    , m_endWasDelayed(false)
    , m_haveBackgroundParser(false)
    , m_pumpSessionNestingLevel(0)
{
    ASSERT(shouldUseThreading() || (m_token && m_tokenizer));
}

// FIXME: Member variables should be grouped into self-initializing structs to
// minimize code duplication between these constructors.
HTMLDocumentParser::HTMLDocumentParser(DocumentFragment* fragment, Element* contextElement, FragmentScriptingPermission scriptingPermission)
    : ScriptableDocumentParser(fragment->document())
    , m_options(fragment->document())
    , m_token(adoptPtr(new HTMLToken))
    , m_tokenizer(HTMLTokenizer::create(m_options))
    , m_treeBuilder(HTMLTreeBuilder::create(this, fragment, contextElement, scriptingPermission, m_options))
    , m_xssAuditorDelegate(fragment->document())
#if ENABLE(THREADED_HTML_PARSER)
    , m_weakFactory(this)
#endif
    , m_endWasDelayed(false)
    , m_haveBackgroundParser(false)
    , m_pumpSessionNestingLevel(0)
{
    ASSERT(!shouldUseThreading());
    bool reportErrors = false; // For now document fragment parsing never reports errors.
    m_tokenizer->setState(tokenizerStateForContextElement(contextElement, reportErrors, m_options));
}

HTMLDocumentParser::~HTMLDocumentParser()
{
    ASSERT(!m_parserScheduler);
    ASSERT(!m_pumpSessionNestingLevel);
    ASSERT(!m_preloadScanner);
    ASSERT(!m_insertionPreloadScanner);
    ASSERT(!m_haveBackgroundParser);
}

void HTMLDocumentParser::detach()
{
#if ENABLE(THREADED_HTML_PARSER)
    if (m_haveBackgroundParser)
        stopBackgroundParser();
#endif
    DocumentParser::detach();
    if (m_scriptRunner)
        m_scriptRunner->detach();
    m_treeBuilder->detach();
    // FIXME: It seems wrong that we would have a preload scanner here.
    // Yet during fast/dom/HTMLScriptElement/script-load-events.html we do.
    m_preloadScanner.clear();
    m_insertionPreloadScanner.clear();
    m_parserScheduler.clear(); // Deleting the scheduler will clear any timers.
}

void HTMLDocumentParser::stopParsing()
{
    DocumentParser::stopParsing();
    m_parserScheduler.clear(); // Deleting the scheduler will clear any timers.
#if ENABLE(THREADED_HTML_PARSER)
    if (m_haveBackgroundParser)
        stopBackgroundParser();
#endif
}

// This kicks off "Once the user agent stops parsing" as described by:
// http://www.whatwg.org/specs/web-apps/current-work/multipage/the-end.html#the-end
void HTMLDocumentParser::prepareToStopParsing()
{
    ASSERT(!hasInsertionPoint());

    // pumpTokenizer can cause this parser to be detached from the Document,
    // but we need to ensure it isn't deleted yet.
    RefPtr<HTMLDocumentParser> protect(this);

#if ENABLE(THREADED_HTML_PARSER)
    // NOTE: This pump should only ever emit buffered character tokens,
    // so ForceSynchronous vs. AllowYield should be meaningless.
    if (!m_haveBackgroundParser)
#endif
        pumpTokenizerIfPossible(ForceSynchronous);

    if (isStopped())
        return;

    DocumentParser::prepareToStopParsing();

    // We will not have a scriptRunner when parsing a DocumentFragment.
    if (m_scriptRunner)
        document()->setReadyState(Document::Interactive);

    // Setting the ready state above can fire mutation event and detach us
    // from underneath. In that case, just bail out.
    if (isDetached())
        return;

    attemptToRunDeferredScriptsAndEnd();
}

bool HTMLDocumentParser::isParsingFragment() const
{
    return m_treeBuilder->isParsingFragment();
}

bool HTMLDocumentParser::processingData() const
{
    return isScheduledForResume() || inPumpSession() || m_haveBackgroundParser;
}

void HTMLDocumentParser::pumpTokenizerIfPossible(SynchronousMode mode)
{
    if (isStopped() || isWaitingForScripts())
        return;

    // Once a resume is scheduled, HTMLParserScheduler controls when we next pump.
    if (isScheduledForResume()) {
        ASSERT(mode == AllowYield);
        return;
    }

    pumpTokenizer(mode);
}

bool HTMLDocumentParser::isScheduledForResume() const
{
    return m_parserScheduler && m_parserScheduler->isScheduledForResume();
}

// Used by HTMLParserScheduler
void HTMLDocumentParser::resumeParsingAfterYield()
{
    ASSERT(!m_haveBackgroundParser);

    // pumpTokenizer can cause this parser to be detached from the Document,
    // but we need to ensure it isn't deleted yet.
    RefPtr<HTMLDocumentParser> protect(this);

    // We should never be here unless we can pump immediately.  Call pumpTokenizer()
    // directly so that ASSERTS will fire if we're wrong.
    pumpTokenizer(AllowYield);
    endIfDelayed();
}

void HTMLDocumentParser::runScriptsForPausedTreeBuilder()
{
    TextPosition scriptStartPosition = TextPosition::belowRangePosition();
    RefPtr<Element> scriptElement = m_treeBuilder->takeScriptToProcess(scriptStartPosition);
    // We will not have a scriptRunner when parsing a DocumentFragment.
    if (m_scriptRunner)
        m_scriptRunner->execute(scriptElement.release(), scriptStartPosition);
}

bool HTMLDocumentParser::canTakeNextToken(SynchronousMode mode, PumpSession& session)
{
    if (isStopped())
        return false;

    ASSERT(!shouldUseThreading() || mode == ForceSynchronous);

    if (isWaitingForScripts()) {
        if (mode == AllowYield)
            m_parserScheduler->checkForYieldBeforeScript(session);

        // If we don't run the script, we cannot allow the next token to be taken.
        if (session.needsYield)
            return false;

        // If we're paused waiting for a script, we try to execute scripts before continuing.
        runScriptsForPausedTreeBuilder();
        if (isWaitingForScripts() || isStopped())
            return false;
    }

    // FIXME: It's wrong for the HTMLDocumentParser to reach back to the
    //        Frame, but this approach is how the old parser handled
    //        stopping when the page assigns window.location.  What really
    //        should happen is that assigning window.location causes the
    //        parser to stop parsing cleanly.  The problem is we're not
    //        perpared to do that at every point where we run JavaScript.
    if (!isParsingFragment()
        && document()->frame() && document()->frame()->navigationScheduler()->locationChangePending())
        return false;

    if (mode == AllowYield)
        m_parserScheduler->checkForYieldBeforeToken(session);

    return true;
}

#if ENABLE(THREADED_HTML_PARSER)

void HTMLDocumentParser::didReceiveParsedChunkFromBackgroundParser(PassOwnPtr<ParsedChunk> chunk)
{
    if (isWaitingForScripts()) {
        m_speculations.append(chunk);
        return;
    }
    ASSERT(m_speculations.isEmpty());
    processParsedChunkFromBackgroundParser(chunk);
}

void HTMLDocumentParser::checkForSpeculationFailure()
{
    if (!m_tokenizer)
        return;
    // FIXME: If the tokenizer is in the same state as when we started this function,
    // then we haven't necessarily failed our speculation.
    didFailSpeculation(m_token.release(), m_tokenizer.release());
}

void HTMLDocumentParser::didFailSpeculation(PassOwnPtr<HTMLToken> token, PassOwnPtr<HTMLTokenizer> tokenizer)
{
    m_weakFactory.revokeAll();
    m_speculations.clear();

    HTMLParserThread::shared()->postTask(bind(&BackgroundHTMLParser::resumeFrom,
        m_backgroundParser, m_weakFactory.createWeakPtr(), token, tokenizer, m_currentChunk->checkpoint));
}

void HTMLDocumentParser::processParsedChunkFromBackgroundParser(PassOwnPtr<ParsedChunk> chunk)
{
    ASSERT(shouldUseThreading());

    // This method can cause this parser to be detached from the Document,
    // but we need to ensure it isn't deleted yet.
    RefPtr<HTMLDocumentParser> protect(this);

    ActiveParserSession session(contextForParsingSession());

    m_currentChunk = chunk;
    OwnPtr<CompactHTMLTokenStream> tokens = m_currentChunk->tokens.release();

    // FIXME: Pass in current input length.
    InspectorInstrumentationCookie cookie = InspectorInstrumentation::willWriteHTML(document(), 0, lineNumber().zeroBasedInt());

    for (Vector<CompactHTMLToken>::const_iterator it = tokens->begin(); it != tokens->end(); ++it) {
        ASSERT(!isWaitingForScripts());

        m_textPosition = it->textPosition();

        if (XSSInfo* xssInfo = it->xssInfo())
            m_xssAuditorDelegate.didBlockScript(*xssInfo);
        constructTreeFromCompactHTMLToken(*it);

        if (isStopped())
            break;

        if (!isParsingFragment()
            && document()->frame() && document()->frame()->navigationScheduler()->locationChangePending())
            break;

        if (isWaitingForScripts()) {
            ASSERT(it + 1 == tokens->end()); // The </script> is assumed to be the last token of this bunch.
            runScriptsForPausedTreeBuilder();
            break;
        }

        if (it->type() == HTMLTokenTypes::EndOfFile) {
            ASSERT(it + 1 == tokens->end()); // The EOF is assumed to be the last token of this bunch.
            prepareToStopParsing();
            break;
        }
    }

    checkForSpeculationFailure();

    InspectorInstrumentation::didWriteHTML(cookie, lineNumber().zeroBasedInt());
}

#endif // ENABLE(THREADED_HTML_PARSER)

void HTMLDocumentParser::forcePlaintextForTextDocument()
{
#if ENABLE(THREADED_HTML_PARSER)
    if (shouldUseThreading()) {
        // This method is called before any data is appended, so we have to start
        // the background parser ourselves.
        if (!m_haveBackgroundParser)
            startBackgroundParser();

        HTMLParserThread::shared()->postTask(bind(&BackgroundHTMLParser::forcePlaintextForTextDocument, m_backgroundParser));
    } else
#endif
        m_tokenizer->setState(HTMLTokenizerState::PLAINTEXTState);
}

Document* HTMLDocumentParser::contextForParsingSession()
{
    // The parsing session should interact with the document only when parsing
    // non-fragments. Otherwise, we might delay the load event mistakenly.
    if (isParsingFragment())
        return 0;
    return document();
}

void HTMLDocumentParser::pumpTokenizer(SynchronousMode mode)
{
    ASSERT(!isStopped());
    ASSERT(!isScheduledForResume());
    // ASSERT that this object is both attached to the Document and protected.
    ASSERT(refCount() >= 2);

    ASSERT(!shouldUseThreading() || mode == ForceSynchronous);

    PumpSession session(m_pumpSessionNestingLevel, contextForParsingSession());

    // We tell the InspectorInstrumentation about every pump, even if we
    // end up pumping nothing.  It can filter out empty pumps itself.
    // FIXME: m_input.current().length() is only accurate if we
    // end up parsing the whole buffer in this pump.  We should pass how
    // much we parsed as part of didWriteHTML instead of willWriteHTML.
    InspectorInstrumentationCookie cookie = InspectorInstrumentation::willWriteHTML(document(), m_input.current().length(), m_input.current().currentLine().zeroBasedInt());

    m_xssAuditor.init(document());

    while (canTakeNextToken(mode, session) && !session.needsYield) {
        if (!isParsingFragment())
            m_sourceTracker.start(m_input.current(), m_tokenizer.get(), token());

        if (!m_tokenizer->nextToken(m_input.current(), token()))
            break;

        if (!isParsingFragment()) {
            m_sourceTracker.end(m_input.current(), m_tokenizer.get(), token());

            // We do not XSS filter innerHTML, which means we (intentionally) fail
            // http/tests/security/xssAuditor/dom-write-innerHTML.html
            if (OwnPtr<XSSInfo> xssInfo = m_xssAuditor.filterToken(FilterTokenRequest(token(), m_sourceTracker, m_tokenizer->shouldAllowCDATA())))
                m_xssAuditorDelegate.didBlockScript(*xssInfo);
        }

        constructTreeFromHTMLToken(token());
        ASSERT(token().isUninitialized());
    }

    // Ensure we haven't been totally deref'ed after pumping. Any caller of this
    // function should be holding a RefPtr to this to ensure we weren't deleted.
    ASSERT(refCount() >= 1);

    if (isStopped())
        return;

    if (session.needsYield)
        m_parserScheduler->scheduleForResume();

    if (isWaitingForScripts()) {
        ASSERT(m_tokenizer->state() == HTMLTokenizerState::DataState);
        if (!m_preloadScanner) {
            m_preloadScanner = adoptPtr(new HTMLPreloadScanner(document(), m_options));
            m_preloadScanner->appendToEnd(m_input.current());
        }
        m_preloadScanner->scan();
    }

    InspectorInstrumentation::didWriteHTML(cookie, m_input.current().currentLine().zeroBasedInt());
}

void HTMLDocumentParser::constructTreeFromHTMLToken(HTMLToken& rawToken)
{
    RefPtr<AtomicHTMLToken> token = AtomicHTMLToken::create(rawToken);

    // We clear the rawToken in case constructTreeFromAtomicToken
    // synchronously re-enters the parser. We don't clear the token immedately
    // for Character tokens because the AtomicHTMLToken avoids copying the
    // characters by keeping a pointer to the underlying buffer in the
    // HTMLToken. Fortunately, Character tokens can't cause us to re-enter
    // the parser.
    //
    // FIXME: Stop clearing the rawToken once we start running the parser off
    // the main thread or once we stop allowing synchronous JavaScript
    // execution from parseAttribute.
    if (rawToken.type() != HTMLTokenTypes::Character)
        rawToken.clear();

    m_treeBuilder->constructTree(token.get());

    // AtomicHTMLToken keeps a pointer to the HTMLToken's buffer instead
    // of copying the characters for performance.
    // Clear the external characters pointer before the raw token is cleared
    // to make sure that we won't have a dangling pointer.
    token->clearExternalCharacters();

    if (!rawToken.isUninitialized()) {
        ASSERT(rawToken.type() == HTMLTokenTypes::Character);
        rawToken.clear();
    }
}

#if ENABLE(THREADED_HTML_PARSER)

void HTMLDocumentParser::constructTreeFromCompactHTMLToken(const CompactHTMLToken& compactToken)
{
    RefPtr<AtomicHTMLToken> token = AtomicHTMLToken::create(compactToken);
    m_treeBuilder->constructTree(token.get());
}

#endif

bool HTMLDocumentParser::hasInsertionPoint()
{
    // FIXME: The wasCreatedByScript() branch here might not be fully correct.
    //        Our model of the EOF character differs slightly from the one in
    //        the spec because our treatment is uniform between network-sourced
    //        and script-sourced input streams whereas the spec treats them
    //        differently.
    return m_input.hasInsertionPoint() || (wasCreatedByScript() && !m_input.haveSeenEndOfFile());
}

void HTMLDocumentParser::insert(const SegmentedString& source)
{
    if (isStopped())
        return;

    // pumpTokenizer can cause this parser to be detached from the Document,
    // but we need to ensure it isn't deleted yet.
    RefPtr<HTMLDocumentParser> protect(this);

#if ENABLE(THREADED_HTML_PARSER)
    if (!m_tokenizer) {
        ASSERT(!inPumpSession());
        ASSERT(m_haveBackgroundParser || wasCreatedByScript());
        m_token = adoptPtr(new HTMLToken);
        m_tokenizer = HTMLTokenizer::create(m_options);
    }
#endif

    SegmentedString excludedLineNumberSource(source);
    excludedLineNumberSource.setExcludeLineNumbers();
    m_input.insertAtCurrentInsertionPoint(excludedLineNumberSource);
    pumpTokenizerIfPossible(ForceSynchronous);

    if (isWaitingForScripts()) {
        // Check the document.write() output with a separate preload scanner as
        // the main scanner can't deal with insertions.
        if (!m_insertionPreloadScanner)
            m_insertionPreloadScanner = adoptPtr(new HTMLPreloadScanner(document(), m_options));
        m_insertionPreloadScanner->appendToEnd(source);
        m_insertionPreloadScanner->scan();
    }

    endIfDelayed();
}

#if ENABLE(THREADED_HTML_PARSER)

void HTMLDocumentParser::startBackgroundParser()
{
    ASSERT(shouldUseThreading());
    ASSERT(!m_haveBackgroundParser);
    m_haveBackgroundParser = true;

    RefPtr<WeakReference<BackgroundHTMLParser> > reference = WeakReference<BackgroundHTMLParser>::createUnbound();
    m_backgroundParser = WeakPtr<BackgroundHTMLParser>(reference);

    WeakPtr<HTMLDocumentParser> parser = m_weakFactory.createWeakPtr();
    OwnPtr<XSSAuditor> xssAuditor = adoptPtr(new XSSAuditor);
    xssAuditor->init(document());
    ASSERT(xssAuditor->isSafeToSendToAnotherThread());
    HTMLParserThread::shared()->postTask(bind(&BackgroundHTMLParser::create, reference.release(), m_options, parser, xssAuditor.release()));
}

void HTMLDocumentParser::stopBackgroundParser()
{
    ASSERT(shouldUseThreading());
    ASSERT(m_haveBackgroundParser);
    m_haveBackgroundParser = false;

    HTMLParserThread::shared()->postTask(bind(&BackgroundHTMLParser::stop, m_backgroundParser));
    m_weakFactory.revokeAll();
}

#endif

void HTMLDocumentParser::append(const SegmentedString& source)
{
    if (isStopped())
        return;

#if ENABLE(THREADED_HTML_PARSER)
    if (shouldUseThreading()) {
        if (!m_haveBackgroundParser)
            startBackgroundParser();

        HTMLParserThread::shared()->postTask(bind(
            &BackgroundHTMLParser::append, m_backgroundParser, source.toString().isolatedCopy()));
        return;
    }
#endif

    // pumpTokenizer can cause this parser to be detached from the Document,
    // but we need to ensure it isn't deleted yet.
    RefPtr<HTMLDocumentParser> protect(this);

    if (m_preloadScanner) {
        if (m_input.current().isEmpty() && !isWaitingForScripts()) {
            // We have parsed until the end of the current input and so are now moving ahead of the preload scanner.
            // Clear the scanner so we know to scan starting from the current input point if we block again.
            m_preloadScanner.clear();
        } else {
            m_preloadScanner->appendToEnd(source);
            if (isWaitingForScripts())
                m_preloadScanner->scan();
        }
    }

    m_input.appendToEnd(source);

    if (inPumpSession()) {
        // We've gotten data off the network in a nested write.
        // We don't want to consume any more of the input stream now.  Do
        // not worry.  We'll consume this data in a less-nested write().
        return;
    }

    pumpTokenizerIfPossible(AllowYield);

    endIfDelayed();
}

void HTMLDocumentParser::end()
{
    ASSERT(!isDetached());
    ASSERT(!isScheduledForResume());

#if ENABLE(THREADED_HTML_PARSER)
    if (m_haveBackgroundParser)
        stopBackgroundParser();
#endif

    // Informs the the rest of WebCore that parsing is really finished (and deletes this).
    m_treeBuilder->finished();
}

void HTMLDocumentParser::attemptToRunDeferredScriptsAndEnd()
{
    ASSERT(isStopping());
    ASSERT(!hasInsertionPoint());
    if (m_scriptRunner && !m_scriptRunner->executeScriptsWaitingForParsing())
        return;
    end();
}

void HTMLDocumentParser::attemptToEnd()
{
    // finish() indicates we will not receive any more data. If we are waiting on
    // an external script to load, we can't finish parsing quite yet.

    if (shouldDelayEnd()) {
        m_endWasDelayed = true;
        return;
    }
    prepareToStopParsing();
}

void HTMLDocumentParser::endIfDelayed()
{
    // If we've already been detached, don't bother ending.
    if (isDetached())
        return;

    if (!m_endWasDelayed || shouldDelayEnd())
        return;

    m_endWasDelayed = false;
    prepareToStopParsing();
}

void HTMLDocumentParser::finish()
{
    // FIXME: We should ASSERT(!m_parserStopped) here, since it does not
    // makes sense to call any methods on DocumentParser once it's been stopped.
    // However, FrameLoader::stop calls DocumentParser::finish unconditionally.

    // We're not going to get any more data off the network, so we tell the
    // input stream we've reached the end of file. finish() can be called more
    // than once, if the first time does not call end().
    if (!m_input.haveSeenEndOfFile())
        m_input.markEndOfFile();

#if ENABLE(THREADED_HTML_PARSER)
    // Empty documents never got an append() call, and thus have never started
    // a background parser. In those cases, we ignore shouldUseThreading()
    // and fall through to the non-threading case.
    if (m_haveBackgroundParser) {
        HTMLParserThread::shared()->postTask(bind(&BackgroundHTMLParser::finish, m_backgroundParser));
        return;
    }
    if (shouldUseThreading() && !wasCreatedByScript()) {
        ASSERT(!m_tokenizer && !m_token);
        // We're finishing before receiving any data. Rather than booting up
        // the background parser just to spin it down, we finish parsing
        // synchronously.
        m_token = adoptPtr(new HTMLToken);
        m_tokenizer = HTMLTokenizer::create(m_options);
    }
#endif

    attemptToEnd();
}

bool HTMLDocumentParser::isExecutingScript() const
{
    if (!m_scriptRunner)
        return false;
    return m_scriptRunner->isExecutingScript();
}

OrdinalNumber HTMLDocumentParser::lineNumber() const
{
#if ENABLE(THREADED_HTML_PARSER)
    if (m_haveBackgroundParser)
        return m_textPosition.m_line;
#endif

    return m_input.current().currentLine();
}

TextPosition HTMLDocumentParser::textPosition() const
{
#if ENABLE(THREADED_HTML_PARSER)
    if (m_haveBackgroundParser)
        return m_textPosition;
#endif

    const SegmentedString& currentString = m_input.current();
    OrdinalNumber line = currentString.currentLine();
    OrdinalNumber column = currentString.currentColumn();

    return TextPosition(line, column);
}

bool HTMLDocumentParser::isWaitingForScripts() const
{
    // When the TreeBuilder encounters a </script> tag, it returns to the HTMLDocumentParser
    // where the script is transfered from the treebuilder to the script runner.
    // The script runner will hold the script until its loaded and run. During
    // any of this time, we want to count ourselves as "waiting for a script" and thus
    // run the preload scanner, as well as delay completion of parsing.
    bool treeBuilderHasBlockingScript = m_treeBuilder->hasParserBlockingScript();
    bool scriptRunnerHasBlockingScript = m_scriptRunner && m_scriptRunner->hasParserBlockingScript();
    // Since the parser is paused while a script runner has a blocking script, it should
    // never be possible to end up with both objects holding a blocking script.
    ASSERT(!(treeBuilderHasBlockingScript && scriptRunnerHasBlockingScript));
    // If either object has a blocking script, the parser should be paused.
    return treeBuilderHasBlockingScript || scriptRunnerHasBlockingScript;
}

void HTMLDocumentParser::resumeParsingAfterScriptExecution()
{
    ASSERT(!isExecutingScript());
    ASSERT(!isWaitingForScripts());

#if ENABLE(THREADED_HTML_PARSER)
    if (m_haveBackgroundParser) {
        checkForSpeculationFailure();

        while (!m_speculations.isEmpty()) {
            processParsedChunkFromBackgroundParser(m_speculations.takeFirst());
            if (isWaitingForScripts() || isStopped())
                return;
        }
        return;
    }
#endif

    m_insertionPreloadScanner.clear();
    pumpTokenizerIfPossible(AllowYield);
    endIfDelayed();
}

void HTMLDocumentParser::watchForLoad(CachedResource* cachedScript)
{
    ASSERT(!cachedScript->isLoaded());
    // addClient would call notifyFinished if the load were complete.
    // Callers do not expect to be re-entered from this call, so they should
    // not an already-loaded CachedResource.
    cachedScript->addClient(this);
}

void HTMLDocumentParser::stopWatchingForLoad(CachedResource* cachedScript)
{
    cachedScript->removeClient(this);
}
    
void HTMLDocumentParser::appendCurrentInputStreamToPreloadScannerAndScan()
{
    ASSERT(m_preloadScanner);
    m_preloadScanner->appendToEnd(m_input.current());
    m_preloadScanner->scan();
}

void HTMLDocumentParser::notifyFinished(CachedResource* cachedResource)
{
    // pumpTokenizer can cause this parser to be detached from the Document,
    // but we need to ensure it isn't deleted yet.
    RefPtr<HTMLDocumentParser> protect(this);

    ASSERT(m_scriptRunner);
    ASSERT(!isExecutingScript());
    if (isStopping()) {
        attemptToRunDeferredScriptsAndEnd();
        return;
    }

    m_scriptRunner->executeScriptsWaitingForLoad(cachedResource);
    if (!isWaitingForScripts())
        resumeParsingAfterScriptExecution();
}

void HTMLDocumentParser::executeScriptsWaitingForStylesheets()
{
    // Document only calls this when the Document owns the DocumentParser
    // so this will not be called in the DocumentFragment case.
    ASSERT(m_scriptRunner);
    // Ignore calls unless we have a script blocking the parser waiting on a
    // stylesheet load.  Otherwise we are currently parsing and this
    // is a re-entrant call from encountering a </ style> tag.
    if (!m_scriptRunner->hasScriptsWaitingForStylesheets())
        return;

    // pumpTokenizer can cause this parser to be detached from the Document,
    // but we need to ensure it isn't deleted yet.
    RefPtr<HTMLDocumentParser> protect(this);
    m_scriptRunner->executeScriptsWaitingForStylesheets();
    if (!isWaitingForScripts())
        resumeParsingAfterScriptExecution();
}

void HTMLDocumentParser::parseDocumentFragment(const String& source, DocumentFragment* fragment, Element* contextElement, FragmentScriptingPermission scriptingPermission)
{
    RefPtr<HTMLDocumentParser> parser = HTMLDocumentParser::create(fragment, contextElement, scriptingPermission);
    parser->insert(source); // Use insert() so that the parser will not yield.
    parser->finish();
    ASSERT(!parser->processingData()); // Make sure we're done. <rdar://problem/3963151>
    parser->detach(); // Allows ~DocumentParser to assert it was detached before destruction.
}
    
void HTMLDocumentParser::suspendScheduledTasks()
{
    if (m_parserScheduler)
        m_parserScheduler->suspend();
}

void HTMLDocumentParser::resumeScheduledTasks()
{
    if (m_parserScheduler)
        m_parserScheduler->resume();
}

}
