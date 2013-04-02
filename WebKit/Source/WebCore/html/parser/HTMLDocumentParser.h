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

#ifndef HTMLDocumentParser_h
#define HTMLDocumentParser_h

#include "BackgroundHTMLInputStream.h"
#include "CachedResourceClient.h"
#include "FragmentScriptingPermission.h"
#include "HTMLInputStream.h"
#include "HTMLParserOptions.h"
#include "HTMLScriptRunnerHost.h"
#include "HTMLSourceTracker.h"
#include "HTMLToken.h"
#include "ScriptableDocumentParser.h"
#include "SegmentedString.h"
#include "Timer.h"
#include "XSSAuditor.h"
#include "XSSAuditorDelegate.h"
#include <wtf/Deque.h>
#include <wtf/OwnPtr.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/TextPosition.h>

namespace WebCore {

class BackgroundHTMLParser;
class CompactHTMLToken;
class Document;
class DocumentFragment;
class HTMLDocument;
class HTMLParserScheduler;
class HTMLTokenizer;
class HTMLScriptRunner;
class HTMLTreeBuilder;
class HTMLPreloadScanner;
class ScriptController;
class ScriptSourceCode;

class PumpSession;

class HTMLDocumentParser :  public ScriptableDocumentParser, HTMLScriptRunnerHost, CachedResourceClient {
    WTF_MAKE_FAST_ALLOCATED;
public:
    static PassRefPtr<HTMLDocumentParser> create(HTMLDocument* document, bool reportErrors)
    {
        return adoptRef(new HTMLDocumentParser(document, reportErrors));
    }
    virtual ~HTMLDocumentParser();

    // Exposed for HTMLParserScheduler
    void resumeParsingAfterYield();

    static void parseDocumentFragment(const String&, DocumentFragment*, Element* contextElement, FragmentScriptingPermission = AllowScriptingContent);

    HTMLTokenizer* tokenizer() const { return m_tokenizer.get(); }

    virtual TextPosition textPosition() const;
    virtual OrdinalNumber lineNumber() const;

    virtual void suspendScheduledTasks();
    virtual void resumeScheduledTasks();

#if ENABLE(THREADED_HTML_PARSER)
    struct ParsedChunk {
        OwnPtr<CompactHTMLTokenStream> tokens;
        HTMLInputCheckpoint checkpoint;
    };
    void didReceiveParsedChunkFromBackgroundParser(PassOwnPtr<ParsedChunk>);
#endif

protected:
    virtual void insert(const SegmentedString&);
    virtual void append(const SegmentedString&);
    virtual void finish();

    HTMLDocumentParser(HTMLDocument*, bool reportErrors);
    HTMLDocumentParser(DocumentFragment*, Element* contextElement, FragmentScriptingPermission);

    HTMLTreeBuilder* treeBuilder() const { return m_treeBuilder.get(); }

    void forcePlaintextForTextDocument();

private:
    static PassRefPtr<HTMLDocumentParser> create(DocumentFragment* fragment, Element* contextElement, FragmentScriptingPermission permission)
    {
        return adoptRef(new HTMLDocumentParser(fragment, contextElement, permission));
    }

    // DocumentParser
    virtual void detach();
    virtual bool hasInsertionPoint();
    virtual bool processingData() const;
    virtual void prepareToStopParsing();
    virtual void stopParsing();
    virtual bool isWaitingForScripts() const;
    virtual bool isExecutingScript() const;
    virtual void executeScriptsWaitingForStylesheets();

    // HTMLScriptRunnerHost
    virtual void watchForLoad(CachedResource*);
    virtual void stopWatchingForLoad(CachedResource*);
    virtual HTMLInputStream& inputStream() { return m_input; }
    virtual bool hasPreloadScanner() const { return m_preloadScanner.get() && !shouldUseThreading(); }
    virtual void appendCurrentInputStreamToPreloadScannerAndScan();

    // CachedResourceClient
    virtual void notifyFinished(CachedResource*);

#if ENABLE(THREADED_HTML_PARSER)
    void startBackgroundParser();
    void stopBackgroundParser();
    void checkForSpeculationFailure();
    void didFailSpeculation(PassOwnPtr<HTMLToken>, PassOwnPtr<HTMLTokenizer>);
    void processParsedChunkFromBackgroundParser(PassOwnPtr<ParsedChunk>);
#endif

    Document* contextForParsingSession();

    enum SynchronousMode {
        AllowYield,
        ForceSynchronous,
    };
    bool canTakeNextToken(SynchronousMode, PumpSession&);
    void pumpTokenizer(SynchronousMode);
    void pumpTokenizerIfPossible(SynchronousMode);
    void constructTreeFromHTMLToken(HTMLToken&);
#if ENABLE(THREADED_HTML_PARSER)
    void constructTreeFromCompactHTMLToken(const CompactHTMLToken&);
#endif

    void runScriptsForPausedTreeBuilder();
    void resumeParsingAfterScriptExecution();

    void attemptToEnd();
    void endIfDelayed();
    void attemptToRunDeferredScriptsAndEnd();
    void end();

    bool shouldUseThreading() const { return m_options.useThreading && !isParsingFragment(); }

    bool isParsingFragment() const;
    bool isScheduledForResume() const;
    bool inPumpSession() const { return m_pumpSessionNestingLevel > 0; }
    bool shouldDelayEnd() const { return inPumpSession() || isWaitingForScripts() || isScheduledForResume() || isExecutingScript(); }

    HTMLToken& token() { return *m_token.get(); }

    HTMLParserOptions m_options;
    HTMLInputStream m_input;

    OwnPtr<HTMLToken> m_token;
    OwnPtr<HTMLTokenizer> m_tokenizer;
    OwnPtr<HTMLScriptRunner> m_scriptRunner;
    OwnPtr<HTMLTreeBuilder> m_treeBuilder;
    OwnPtr<HTMLPreloadScanner> m_preloadScanner;
    OwnPtr<HTMLPreloadScanner> m_insertionPreloadScanner;
    OwnPtr<HTMLParserScheduler> m_parserScheduler;
    HTMLSourceTracker m_sourceTracker;
    TextPosition m_textPosition;
    XSSAuditor m_xssAuditor;
    XSSAuditorDelegate m_xssAuditorDelegate;

#if ENABLE(THREADED_HTML_PARSER)
    OwnPtr<ParsedChunk> m_currentChunk;
    Deque<OwnPtr<ParsedChunk> > m_speculations;
    WeakPtrFactory<HTMLDocumentParser> m_weakFactory;
    WeakPtr<BackgroundHTMLParser> m_backgroundParser;
#endif

    bool m_endWasDelayed;
    bool m_haveBackgroundParser;
    unsigned m_pumpSessionNestingLevel;
};

}

#endif
