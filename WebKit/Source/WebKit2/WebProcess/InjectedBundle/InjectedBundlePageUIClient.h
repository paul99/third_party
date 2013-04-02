/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef InjectedBundlePageUIClient_h
#define InjectedBundlePageUIClient_h

#include "APIClient.h"
#include "WKBundlePage.h"
#include "WebEvent.h"
#include "WebImage.h"
#include <WebCore/RenderSnapshottedPlugIn.h>
#include <wtf/Forward.h>

namespace WebCore {
class GraphicsContext;
class HitTestResult;
class IntRect;
}

namespace WebKit {

class APIObject;
class WebFrame;
class WebPage;
class WebSecurityOrigin;

class InjectedBundlePageUIClient : public APIClient<WKBundlePageUIClient, kWKBundlePageUIClientCurrentVersion> {
public:
    void willAddMessageToConsole(WebPage*, const String& message, int32_t lineNumber);
    void willSetStatusbarText(WebPage*, const String&);
    void willRunJavaScriptAlert(WebPage*, const String&, WebFrame*);
    void willRunJavaScriptConfirm(WebPage*, const String&, WebFrame*);
    void willRunJavaScriptPrompt(WebPage*, const String&, const String&, WebFrame*);
    void mouseDidMoveOverElement(WebPage*, const WebCore::HitTestResult&, WebEvent::Modifiers, RefPtr<APIObject>& userData);
    void pageDidScroll(WebPage*);

    bool shouldPaintCustomOverhangArea();
    void paintCustomOverhangArea(WebPage*, WebCore::GraphicsContext*, const WebCore::IntRect&, const WebCore::IntRect&, const WebCore::IntRect&);

    String shouldGenerateFileForUpload(WebPage*, const String& originalFilePath);
    String generateFileForUpload(WebPage*, const String& originalFilePath);
    
    bool shouldRubberBandInDirection(WebPage*, WKScrollDirection) const;

    WKBundlePageUIElementVisibility statusBarIsVisible(WebPage*);
    WKBundlePageUIElementVisibility menuBarIsVisible(WebPage*);
    WKBundlePageUIElementVisibility toolbarsAreVisible(WebPage*);

    void didReachApplicationCacheOriginQuota(WebPage*, WebSecurityOrigin*, int64_t totalBytesNeeded);
    uint64_t didExceedDatabaseQuota(WebPage*, WebSecurityOrigin*, const String& databaseName, const String& databaseDisplayName, uint64_t currentQuotaBytes, uint64_t currentOriginUsageBytes, uint64_t currentDatabaseUsageBytes, uint64_t expectedUsageBytes);

    PassRefPtr<WebImage> plugInStartLabelImage(WebCore::RenderSnapshottedPlugIn::LabelSize) const;
    String plugInStartLabelTitle() const;
    String plugInStartLabelSubtitle() const;
    String plugInExtraStyleSheet() const;

};

} // namespace WebKit

#endif // InjectedBundlePageUIClient_h
