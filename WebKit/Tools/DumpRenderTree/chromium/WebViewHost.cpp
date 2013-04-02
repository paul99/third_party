/*
 * Copyright (C) 2010, 2011, 2012 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebViewHost.h"

#include "DRTDevToolsAgent.h"
#include "MockWebSpeechInputController.h"
#include "MockWebSpeechRecognizer.h"
#include "Task.h"
#include "TestNavigationController.h"
#include "TestShell.h"
#include "WebCachedURLRequest.h"
#include "WebConsoleMessage.h"
#include "WebContextMenuData.h"
#include "WebDOMMessageEvent.h"
#include "WebDataSource.h"
#include "WebDeviceOrientationClientMock.h"
#include "WebDocument.h"
#include "WebElement.h"
#include "WebFrame.h"
#include "WebGeolocationClientMock.h"
#include "WebHistoryItem.h"
#include "WebKit.h"
#include "WebNode.h"
#include "WebPluginParams.h"
#include "WebPopupMenu.h"
#include "WebPopupType.h"
#include "WebPrintParams.h"
#include "WebRange.h"
#include "WebScreenInfo.h"
#include "WebSerializedScriptValue.h"
#include "WebStorageNamespace.h"
#include "WebUserMediaClientMock.h"
#include "WebView.h"
#include "WebWindowFeatures.h"
#include "skia/ext/platform_canvas.h"
#include "webkit/support/test_media_stream_client.h"
#include "webkit/support/webkit_support.h"
#include <cctype>
#include <clocale>
#include <public/WebCString.h>
#include <public/WebCompositorOutputSurface.h>
#include <public/WebCompositorSupport.h>
#include <public/WebDragData.h>
#include <public/WebRect.h>
#include <public/WebSize.h>
#include <public/WebThread.h>
#include <public/WebURLRequest.h>
#include <public/WebURLResponse.h>

#include <wtf/Assertions.h>
#include <wtf/OwnArrayPtr.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/Vector.h>

using namespace WebCore;
using namespace WebKit;
using namespace WebTestRunner;
using namespace std;

static const int screenWidth = 1920;
static const int screenHeight = 1080;
static const int screenUnavailableBorder = 8;

static int nextPageID = 1;

// WebViewClient -------------------------------------------------------------

WebView* WebViewHost::createView(WebFrame* creator, const WebURLRequest&, const WebWindowFeatures&, const WebString&, WebNavigationPolicy)
{
    creator->consumeUserGesture();
    return m_shell->createNewWindow(WebURL())->webView();
}

WebWidget* WebViewHost::createPopupMenu(WebPopupType type)
{
    switch (type) {
    case WebKit::WebPopupTypeNone:
    case WebKit::WebPopupTypePage:
    case WebKit::WebPopupTypeHelperPlugin:
        break;
    case WebKit::WebPopupTypeSelect:
    case WebKit::WebPopupTypeSuggestion:
        m_popupmenus.append(WebPopupMenu::create(0));
        return m_popupmenus.last();
    }
    return 0;
}

WebWidget* WebViewHost::createPopupMenu(const WebPopupMenuInfo&)
{
    // Do not use this method. It's been replaced by createExternalPopupMenu.
    ASSERT_NOT_REACHED();
    return 0;
}

WebStorageNamespace* WebViewHost::createSessionStorageNamespace(unsigned quota)
{
    return webkit_support::CreateSessionStorageNamespace(quota);
}

WebCompositorOutputSurface* WebViewHost::createOutputSurface()
{
    if (!webView())
        return 0;

    if (m_shell->softwareCompositingEnabled())
        return WebKit::Platform::current()->compositorSupport()->createOutputSurfaceForSoftware();

    WebGraphicsContext3D* context = webkit_support::CreateGraphicsContext3D(WebGraphicsContext3D::Attributes(), webView());
    return WebKit::Platform::current()->compositorSupport()->createOutputSurfaceFor3D(context);
}

void WebViewHost::didAddMessageToConsole(const WebConsoleMessage& message, const WebString& sourceName, unsigned sourceLine)
{
}

void WebViewHost::didStartLoading()
{
}

void WebViewHost::didStopLoading()
{
}

bool WebViewHost::shouldBeginEditing(const WebRange& range)
{
    return true;
}

bool WebViewHost::shouldEndEditing(const WebRange& range)
{
    return true;
}

bool WebViewHost::shouldInsertNode(const WebNode& node, const WebRange& range, WebEditingAction action)
{
    return true;
}

bool WebViewHost::shouldInsertText(const WebString& text, const WebRange& range, WebEditingAction action)
{
    return true;
}

bool WebViewHost::shouldChangeSelectedRange(
    const WebRange& fromRange, const WebRange& toRange, WebTextAffinity affinity, bool stillSelecting)
{
    return true;
}

bool WebViewHost::shouldDeleteRange(const WebRange& range)
{
    return true;
}

bool WebViewHost::shouldApplyStyle(const WebString& style, const WebRange& range)
{
    return true;
}

bool WebViewHost::isSmartInsertDeleteEnabled()
{
    return true;
}

bool WebViewHost::isSelectTrailingWhitespaceEnabled()
{
#if OS(WINDOWS)
    return true;
#else
    return false;
#endif
}

bool WebViewHost::handleCurrentKeyboardEvent()
{
    if (m_editCommandName.empty())
        return false;
    WebFrame* frame = webView()->focusedFrame();
    if (!frame)
        return false;

    return frame->executeCommand(WebString::fromUTF8(m_editCommandName), WebString::fromUTF8(m_editCommandValue));
}

// WebKit::WebPrerendererClient

void WebViewHost::willAddPrerender(WebKit::WebPrerender*)
{
}


void WebViewHost::runModalAlertDialog(WebFrame*, const WebString& message)
{
}

bool WebViewHost::runModalConfirmDialog(WebFrame*, const WebString& message)
{
    return true;
}

bool WebViewHost::runModalPromptDialog(WebFrame* frame, const WebString& message,
                                       const WebString& defaultValue, WebString*)
{
    return true;
}

void WebViewHost::showContextMenu(WebFrame*, const WebContextMenuData& contextMenuData)
{
}

void WebViewHost::didUpdateLayout()
{
#if OS(MAC_OS_X)
    static bool queryingPreferredSize = false;
    if (queryingPreferredSize)
        return;

    queryingPreferredSize = true;
    // Query preferred width to emulate the same functionality in Chromium:
    // see RenderView::CheckPreferredSize (src/content/renderer/render_view.cc)
    // and TabContentsViewMac::RenderViewCreated (src/chrome/browser/tab_contents/tab_contents_view_mac.mm)
    webView()->mainFrame()->contentsPreferredWidth();
    webView()->mainFrame()->documentElementScrollHeight();
    queryingPreferredSize = false;
#endif
}

void WebViewHost::navigateBackForwardSoon(int offset)
{
    navigationController()->goToOffset(offset);
}

int WebViewHost::historyBackListCount()
{
    return navigationController()->lastCommittedEntryIndex();
}

int WebViewHost::historyForwardListCount()
{
    int currentIndex =navigationController()->lastCommittedEntryIndex();
    return navigationController()->entryCount() - currentIndex - 1;
}

#if ENABLE(NOTIFICATIONS)
WebNotificationPresenter* WebViewHost::notificationPresenter()
{
    return m_shell->notificationPresenter();
}
#endif

WebKit::WebGeolocationClient* WebViewHost::geolocationClient()
{
    return geolocationClientMock();
}

WebKit::WebGeolocationClientMock* WebViewHost::geolocationClientMock()
{
    if (!m_geolocationClientMock)
        m_geolocationClientMock = adoptPtr(WebGeolocationClientMock::create());
    return m_geolocationClientMock.get();
}

#if ENABLE(INPUT_SPEECH)
WebSpeechInputController* WebViewHost::speechInputController(WebKit::WebSpeechInputListener* listener)
{
    if (!m_speechInputControllerMock)
        m_speechInputControllerMock = MockWebSpeechInputController::create(listener);
    return m_speechInputControllerMock.get();
}
#endif

#if ENABLE(SCRIPTED_SPEECH)
WebSpeechRecognizer* WebViewHost::speechRecognizer()
{
    if (!m_mockSpeechRecognizer)
        m_mockSpeechRecognizer = MockWebSpeechRecognizer::create();
    return m_mockSpeechRecognizer.get();
}
#endif

WebDeviceOrientationClientMock* WebViewHost::deviceOrientationClientMock()
{
    if (!m_deviceOrientationClientMock.get())
        m_deviceOrientationClientMock = adoptPtr(WebDeviceOrientationClientMock::create());
    return m_deviceOrientationClientMock.get();
}

WebDeviceOrientationClient* WebViewHost::deviceOrientationClient()
{
    return deviceOrientationClientMock();
}

#if ENABLE(MEDIA_STREAM)
WebUserMediaClient* WebViewHost::userMediaClient()
{
    return userMediaClientMock();
}

WebUserMediaClientMock* WebViewHost::userMediaClientMock()
{
    if (!m_userMediaClientMock.get())
        m_userMediaClientMock = WebUserMediaClientMock::create();
    return m_userMediaClientMock.get();
}
#endif

// WebWidgetClient -----------------------------------------------------------

void WebViewHost::didAutoResize(const WebSize& newSize)
{
    // Purposely don't include the virtualWindowBorder in this case so that
    // window.inner[Width|Height] is the same as window.outer[Width|Height]
    setWindowRect(WebRect(0, 0, newSize.width, newSize.height));
}

void WebViewHost::initializeLayerTreeView(WebLayerTreeViewClient* client, const WebLayer& rootLayer, const WebLayerTreeView::Settings& settings)
{
    m_layerTreeView = adoptPtr(Platform::current()->compositorSupport()->createLayerTreeView(client, rootLayer, settings));
    if (m_layerTreeView)
        m_layerTreeView->setSurfaceReady();
}

WebLayerTreeView* WebViewHost::layerTreeView()
{
    return m_layerTreeView.get();
}

void WebViewHost::scheduleAnimation()
{
    if (webView()->settings()->scrollAnimatorEnabled())
        webView()->animate(0.0);
}

void WebViewHost::didFocus()
{
    m_shell->setFocus(webWidget(), true);
}

void WebViewHost::didBlur()
{
    m_shell->setFocus(webWidget(), false);
}

WebScreenInfo WebViewHost::screenInfo()
{
    // We don't need to set actual values.
    WebScreenInfo info;
    info.depth = 24;
    info.depthPerComponent = 8;
    info.isMonochrome = false;
    info.rect = WebRect(0, 0, screenWidth, screenHeight);
    // Use values different from info.rect for testing.
    info.availableRect = WebRect(screenUnavailableBorder, screenUnavailableBorder,
                                 screenWidth - screenUnavailableBorder * 2,
                                 screenHeight - screenUnavailableBorder * 2);
    return info;
}

#if ENABLE(POINTER_LOCK)
bool WebViewHost::requestPointerLock()
{
    switch (m_pointerLockPlannedResult) {
    case PointerLockWillSucceed:
        postDelayedTask(new HostMethodTask(this, &WebViewHost::didAcquirePointerLock), 0);
        return true;
    case PointerLockWillRespondAsync:
        ASSERT(!m_pointerLocked);
        return true;
    case PointerLockWillFailSync:
        ASSERT(!m_pointerLocked);
        return false;
    default:
        ASSERT_NOT_REACHED();
        return false;
    }
}

void WebViewHost::requestPointerUnlock()
{
    postDelayedTask(new HostMethodTask(this, &WebViewHost::didLosePointerLock), 0);
}

bool WebViewHost::isPointerLocked()
{
    return m_pointerLocked;
}

void WebViewHost::didAcquirePointerLock()
{
    m_pointerLocked = true;
    webWidget()->didAcquirePointerLock();

    // Reset planned result to default.
    m_pointerLockPlannedResult = PointerLockWillSucceed;
}

void WebViewHost::didNotAcquirePointerLock()
{
    ASSERT(!m_pointerLocked);
    m_pointerLocked = false;
    webWidget()->didNotAcquirePointerLock();

    // Reset planned result to default.
    m_pointerLockPlannedResult = PointerLockWillSucceed;
}

void WebViewHost::didLosePointerLock()
{
    bool wasLocked = m_pointerLocked;
    m_pointerLocked = false;
    if (wasLocked)
        webWidget()->didLosePointerLock();
}
#endif

void WebViewHost::show(WebNavigationPolicy)
{
    m_hasWindow = true;
}



void WebViewHost::closeWidget()
{
    m_hasWindow = false;
    m_shell->closeWindow(this);
    // No more code here, we should be deleted at this point.
}

void WebViewHost::closeWidgetSoon()
{
    postDelayedTask(new HostMethodTask(this, &WebViewHost::closeWidget), 0);
}

void WebViewHost::didChangeCursor(const WebCursorInfo& cursorInfo)
{
    if (!hasWindow())
        return;
    m_currentCursor = cursorInfo;
}

WebRect WebViewHost::windowRect()
{
    return m_windowRect;
}

void WebViewHost::setWindowRect(const WebRect& rect)
{
    m_windowRect = rect;
    const int border2 = TestShell::virtualWindowBorder * 2;
    if (m_windowRect.width <= border2)
        m_windowRect.width = 1 + border2;
    if (m_windowRect.height <= border2)
        m_windowRect.height = 1 + border2;
    int width = m_windowRect.width - border2;
    int height = m_windowRect.height - border2;
    discardBackingStore();
    webWidget()->resize(WebSize(width, height));
}

WebRect WebViewHost::rootWindowRect()
{
    return windowRect();
}

WebRect WebViewHost::windowResizerRect()
{
    // Not necessary.
    return WebRect();
}

void WebViewHost::runModal()
{
    if (m_shell->isDisplayingModalDialog()) {
        // DumpRenderTree doesn't support real modal dialogs, so a test shouldn't try to start two modal dialogs at the same time.
        ASSERT_NOT_REACHED();
        return;
    }
    // This WebViewHost might get deleted before RunMessageLoop() returns, so keep a copy of the m_shell member variable around.
    ASSERT(m_shell->webViewHost() != this);
    TestShell* shell = m_shell;
    shell->setIsDisplayingModalDialog(true);
    bool oldState = webkit_support::MessageLoopNestableTasksAllowed();
    webkit_support::MessageLoopSetNestableTasksAllowed(true);
    m_inModalLoop = true;
    webkit_support::RunMessageLoop();
    webkit_support::MessageLoopSetNestableTasksAllowed(oldState);
    shell->setIsDisplayingModalDialog(false);
}

bool WebViewHost::enterFullScreen()
{
    postDelayedTask(new HostMethodTask(this, &WebViewHost::enterFullScreenNow), 0);
    return true;
}

void WebViewHost::exitFullScreen()
{
    postDelayedTask(new HostMethodTask(this, &WebViewHost::exitFullScreenNow), 0);
}

// WebFrameClient ------------------------------------------------------------

WebPlugin* WebViewHost::createPlugin(WebFrame* frame, const WebPluginParams& params)
{
    return webkit_support::CreateWebPlugin(frame, params);
}

WebMediaPlayer* WebViewHost::createMediaPlayer(WebFrame* frame, const WebURL& url, WebMediaPlayerClient* client)
{
#if ENABLE(MEDIA_STREAM)
    return webkit_support::CreateMediaPlayer(frame, url, client, testMediaStreamClient());
#else
    return webkit_support::CreateMediaPlayer(frame, url, client);
#endif
}

WebApplicationCacheHost* WebViewHost::createApplicationCacheHost(WebFrame* frame, WebApplicationCacheHostClient* client)
{
    return webkit_support::CreateApplicationCacheHost(frame, client);
}

void WebViewHost::loadURLExternally(WebFrame* frame, const WebURLRequest& request, WebNavigationPolicy policy)
{
    loadURLExternally(frame, request, policy, WebString());
}

void WebViewHost::loadURLExternally(WebFrame*, const WebURLRequest& request, WebNavigationPolicy policy, const WebString& downloadName)
{
    ASSERT(policy !=  WebKit::WebNavigationPolicyCurrentTab);
    WebViewHost* another = m_shell->createNewWindow(request.url());
    if (another)
        another->show(policy);
}

WebNavigationPolicy WebViewHost::decidePolicyForNavigation(
    WebFrame*, const WebURLRequest&,
    WebNavigationType, const WebNode&,
    WebNavigationPolicy defaultPolicy, bool)
{
    return defaultPolicy;
}

bool WebViewHost::canHandleRequest(WebFrame*, const WebURLRequest& request)
{
    return true;
}

WebURLError WebViewHost::cancelledError(WebFrame*, const WebURLRequest& request)
{
    return webkit_support::CreateCancelledError(request);
}

void WebViewHost::unableToImplementPolicyWithError(WebFrame* frame, const WebURLError& error)
{
}

void WebViewHost::didCreateDataSource(WebFrame*, WebDataSource* ds)
{
    ds->setExtraData(m_pendingExtraData.leakPtr());
}

void WebViewHost::didCommitProvisionalLoad(WebFrame* frame, bool isNewNavigation)
{
    updateForCommittedLoad(frame, isNewNavigation);
}

void WebViewHost::didClearWindowObject(WebFrame* frame)
{
    m_shell->bindJSObjectsToWindow(frame);
}

void WebViewHost::didReceiveTitle(WebFrame* frame, const WebString& title, WebTextDirection direction)
{
    setPageTitle(title);
}

void WebViewHost::didNavigateWithinPage(WebFrame* frame, bool isNewNavigation)
{
    frame->dataSource()->setExtraData(m_pendingExtraData.leakPtr());

    updateForCommittedLoad(frame, isNewNavigation);
}

void WebViewHost::willSendRequest(WebFrame* frame, unsigned, WebURLRequest& request, const WebURLResponse&)
{
    if (request.url().isEmpty())
        return;

    request.setExtraData(webkit_support::CreateWebURLRequestExtraData(frame->document().referrerPolicy()));
}

void WebViewHost::openFileSystem(WebFrame* frame, WebFileSystem::Type type, long long size, bool create, WebFileSystemCallbacks* callbacks)
{
    webkit_support::OpenFileSystem(frame, type, size, create, callbacks);
}

void WebViewHost::deleteFileSystem(WebKit::WebFrame* frame, WebKit::WebFileSystem::Type type, WebKit::WebFileSystemCallbacks* callbacks)
{
    webkit_support::DeleteFileSystem(frame, type, callbacks);
}

bool WebViewHost::willCheckAndDispatchMessageEvent(WebFrame* sourceFrame, WebFrame* targetFrame, WebSecurityOrigin target, WebDOMMessageEvent event)
{
    return false;
}

// WebTestDelegate ------------------------------------------------------------

void WebViewHost::setEditCommand(const string& name, const string& value)
{
    m_editCommandName = name;
    m_editCommandValue = value;
}

void WebViewHost::clearEditCommand()
{
    m_editCommandName.clear();
    m_editCommandValue.clear();
}

void WebViewHost::setGamepadData(const WebGamepads& pads)
{
    webkit_support::SetGamepadData(pads);
}

void WebViewHost::printMessage(const std::string& message)
{
    printf("%s", message.c_str());
}

void WebViewHost::postTask(WebTask* task)
{
    ::postTask(task);
}

void WebViewHost::postDelayedTask(WebTask* task, long long ms)
{
    ::postDelayedTask(task, ms);
}

WebString WebViewHost::registerIsolatedFileSystem(const WebVector<WebString>& absoluteFilenames)
{
    return webkit_support::RegisterIsolatedFileSystem(absoluteFilenames);
}

long long WebViewHost::getCurrentTimeInMillisecond()
{
    return webkit_support::GetCurrentTimeInMillisecond();
}

WebKit::WebString WebViewHost::getAbsoluteWebStringFromUTF8Path(const std::string& path)
{
    return webkit_support::GetAbsoluteWebStringFromUTF8Path(path);
}

WebURL WebViewHost::localFileToDataURL(const WebKit::WebURL& url)
{
    return webkit_support::LocalFileToDataURL(url);
}

WebURL WebViewHost::rewriteLayoutTestsURL(const std::string& url)
{
    return webkit_support::RewriteLayoutTestsURL(url);
}

WebPreferences* WebViewHost::preferences()
{
    return m_shell->preferences();
}

void WebViewHost::applyPreferences()
{
    m_shell->applyPreferences();
}

#if ENABLE(WEB_INTENTS)
void WebViewHost::setCurrentWebIntentRequest(const WebIntentRequest& request)
{
    m_currentRequest = request;
}

WebIntentRequest* WebViewHost::currentWebIntentRequest()
{
    return &m_currentRequest;
}
#endif

std::string WebViewHost::makeURLErrorDescription(const WebKit::WebURLError& error)
{
    return webkit_support::MakeURLErrorDescription(error);
}

void WebViewHost::showDevTools()
{
    m_shell->showDevTools();
}

void WebViewHost::closeDevTools()
{
    m_shell->closeDevTools();
}

void WebViewHost::evaluateInWebInspector(long callID, const std::string& script)
{
    m_shell->drtDevToolsAgent()->evaluateInWebInspector(callID, script);
}

void WebViewHost::clearAllDatabases()
{
    webkit_support::ClearAllDatabases();
}

void WebViewHost::setDatabaseQuota(int quota)
{
    webkit_support::SetDatabaseQuota(quota);
}

void WebViewHost::setDeviceScaleFactor(float deviceScaleFactor)
{
    webView()->setDeviceScaleFactor(deviceScaleFactor);
    discardBackingStore();
}

void WebViewHost::setFocus(bool focused)
{
    m_shell->setFocus(m_shell->webView(), focused);
}

void WebViewHost::setAcceptAllCookies(bool acceptCookies)
{
    webkit_support::SetAcceptAllCookies(acceptCookies);
}

string WebViewHost::pathToLocalResource(const string& url)
{
#if OS(WINDOWS)
    if (!url.find("/tmp/")) {
        // We want a temp file.
        const unsigned tempPrefixLength = 5;
        size_t bufferSize = MAX_PATH;
        OwnArrayPtr<WCHAR> tempPath = adoptArrayPtr(new WCHAR[bufferSize]);
        DWORD tempLength = ::GetTempPathW(bufferSize, tempPath.get());
        if (tempLength + url.length() - tempPrefixLength + 1 > bufferSize) {
            bufferSize = tempLength + url.length() - tempPrefixLength + 1;
            tempPath = adoptArrayPtr(new WCHAR[bufferSize]);
            tempLength = GetTempPathW(bufferSize, tempPath.get());
            ASSERT(tempLength < bufferSize);
        }
        string resultPath(WebString(tempPath.get(), tempLength).utf8());
        resultPath.append(url.substr(tempPrefixLength));
        return resultPath;
    }
#endif

    // Some layout tests use file://// which we resolve as a UNC path. Normalize
    // them to just file:///.
    string lowerUrl = url;
    string result = url;
    transform(lowerUrl.begin(), lowerUrl.end(), lowerUrl.begin(), ::tolower);
    while (!lowerUrl.find("file:////")) {
        result = result.substr(0, 8) + result.substr(9);
        lowerUrl = lowerUrl.substr(0, 8) + lowerUrl.substr(9);
    }
    return webkit_support::RewriteLayoutTestsURL(result).spec();
}

void WebViewHost::setLocale(const std::string& locale)
{
    setlocale(LC_ALL, locale.c_str());
}

void WebViewHost::setDeviceOrientation(WebKit::WebDeviceOrientation& orientation)
{
    deviceOrientationClientMock()->setOrientation(orientation);
}

int WebViewHost::numberOfPendingGeolocationPermissionRequests()
{
    Vector<WebViewHost*> windowList = m_shell->windowList();
    int numberOfRequests = 0;
    for (size_t i = 0; i < windowList.size(); i++)
        numberOfRequests += windowList[i]->geolocationClientMock()->numberOfPendingPermissionRequests();
    return numberOfRequests;
}

void WebViewHost::setGeolocationPermission(bool allowed)
{
    Vector<WebViewHost*> windowList = m_shell->windowList();
    for (size_t i = 0; i < windowList.size(); i++)
        windowList[i]->geolocationClientMock()->setPermission(allowed);
}

void WebViewHost::setMockGeolocationPosition(double latitude, double longitude, double accuracy)
{
    Vector<WebViewHost*> windowList = m_shell->windowList();
    for (size_t i = 0; i < windowList.size(); i++)
        windowList[i]->geolocationClientMock()->setPosition(latitude, longitude, accuracy);
}

void WebViewHost::setMockGeolocationPositionUnavailableError(const std::string& message)
{
    Vector<WebViewHost*> windowList = m_shell->windowList();
    // FIXME: Benjamin
    for (size_t i = 0; i < windowList.size(); i++)
        windowList[i]->geolocationClientMock()->setPositionUnavailableError(WebString::fromUTF8(message));
}

#if ENABLE(NOTIFICATIONS)
void WebViewHost::grantWebNotificationPermission(const std::string& origin)
{
    m_shell->notificationPresenter()->grantPermission(WebString::fromUTF8(origin));
}

bool WebViewHost::simulateLegacyWebNotificationClick(const std::string& notificationIdentifier)
{
    return m_shell->notificationPresenter()->simulateClick(WebString::fromUTF8(notificationIdentifier));
}
#endif

#if ENABLE(INPUT_SPEECH)
void WebViewHost::addMockSpeechInputResult(const std::string& result, double confidence, const std::string& language)
{
    m_speechInputControllerMock->addMockRecognitionResult(WebString::fromUTF8(result), confidence, WebString::fromUTF8(language));
}

void WebViewHost::setMockSpeechInputDumpRect(bool dumpRect)
{
    m_speechInputControllerMock->setDumpRect(dumpRect);
}
#endif

#if ENABLE(SCRIPTED_SPEECH)
void WebViewHost::addMockSpeechRecognitionResult(const std::string& transcript, double confidence)
{
    m_mockSpeechRecognizer->addMockResult(WebString::fromUTF8(transcript), confidence);
}

void WebViewHost::setMockSpeechRecognitionError(const std::string& error, const std::string& message)
{
    m_mockSpeechRecognizer->setError(WebString::fromUTF8(error), WebString::fromUTF8(message));
}

bool WebViewHost::wasMockSpeechRecognitionAborted()
{
    return m_mockSpeechRecognizer->wasAborted();
}
#endif

void WebViewHost::display()
{
    const WebKit::WebSize& size = webView()->size();
    WebRect rect(0, 0, size.width, size.height);
    proxy()->setPaintRect(rect);
    paintInvalidatedRegion();
    displayRepaintMask();
}

void WebViewHost::displayInvalidatedRegion()
{
    paintInvalidatedRegion();
    displayRepaintMask();
}

void WebViewHost::testFinished()
{
    m_shell->testFinished(this);
}

void WebViewHost::testTimedOut()
{
    m_shell->testTimedOut();
}

bool WebViewHost::isBeingDebugged()
{
    return webkit_support::BeingDebugged();
}

int WebViewHost::layoutTestTimeout()
{
    return m_shell->layoutTestTimeout();
}

void WebViewHost::closeRemainingWindows()
{
    m_shell->closeRemainingWindows();
}

int WebViewHost::navigationEntryCount()
{
    return m_shell->navigationEntryCount();
}

int WebViewHost::windowCount()
{
    return m_shell->windowCount();
}

void WebViewHost::goToOffset(int offset)
{
    m_shell->goToOffset(offset);
}

void WebViewHost::reload()
{
    m_shell->reload();
}

void WebViewHost::loadURLForFrame(const WebURL& url, const string& frameName)
{
    if (!url.isValid())
        return;
    TestShell::resizeWindowForTest(this, url);
    navigationController()->loadEntry(TestNavigationEntry::create(-1, url, WebString(), WebString::fromUTF8(frameName)).get());
}

bool WebViewHost::allowExternalPages()
{
    return m_shell->allowExternalPages();
}

// Public functions -----------------------------------------------------------

WebViewHost::WebViewHost(TestShell* shell)
    : m_shell(shell)
    , m_proxy(0)
    , m_webWidget(0)
    , m_shutdownWasInvoked(false)
{
    reset();
}

WebViewHost::~WebViewHost()
{
    ASSERT(m_shutdownWasInvoked);
    if (m_inModalLoop)
        webkit_support::QuitMessageLoop();
}

void WebViewHost::shutdown()
{
    ASSERT(!m_shutdownWasInvoked);

    // DevTools frontend page is supposed to be navigated only once and
    // loading another URL in that Page is an error.
    if (m_shell->devToolsWebView() != this) {
        // Navigate to an empty page to fire all the destruction logic for the
        // current page.
        loadURLForFrame(GURL("about:blank"), string());
    }

    for (Vector<WebKit::WebWidget*>::iterator it = m_popupmenus.begin();
         it < m_popupmenus.end(); ++it)
        (*it)->close();

    webWidget()->willCloseLayerTreeView();
    m_layerTreeView.clear();
    webWidget()->close();
    m_webWidget = 0;
    m_shutdownWasInvoked = true;
}

void WebViewHost::setWebWidget(WebKit::WebWidget* widget)
{
    m_webWidget = widget;
    webView()->setSpellCheckClient(proxy()->spellCheckClient());
    webView()->setPrerendererClient(this);
}

WebView* WebViewHost::webView() const
{
    ASSERT(m_webWidget);
    // DRT does not support popup widgets. So m_webWidget is always a WebView.
    return static_cast<WebView*>(m_webWidget);
}

WebWidget* WebViewHost::webWidget() const
{
    ASSERT(m_webWidget);
    return m_webWidget;
}

WebTestProxyBase* WebViewHost::proxy() const
{
    ASSERT(m_proxy);
    return m_proxy;
}

void WebViewHost::setProxy(WebTestProxyBase* proxy)
{
    ASSERT(!m_proxy);
    ASSERT(proxy);
    m_proxy = proxy;
}

void WebViewHost::reset()
{
    m_pageId = -1;
    m_lastPageIdUpdated = -1;
    m_hasWindow = false;
    m_inModalLoop = false;
    m_isPainting = false;
    m_canvas.clear();
#if ENABLE(POINTER_LOCK)
    m_pointerLocked = false;
    m_pointerLockPlannedResult = PointerLockWillSucceed;
#endif

    m_navigationController = adoptPtr(new TestNavigationController(this));

    m_pendingExtraData.clear();
    m_editCommandName.clear();
    m_editCommandValue.clear();

    if (m_geolocationClientMock.get())
        m_geolocationClientMock->resetMock();

#if ENABLE(INPUT_SPEECH)
    if (m_speechInputControllerMock.get())
        m_speechInputControllerMock->clearResults();
#endif

    m_currentCursor = WebCursorInfo();
    m_windowRect = WebRect();
    // m_proxy is not set when reset() is invoked from the constructor.
    if (m_proxy)
        proxy()->reset();

    if (m_webWidget) {
        webView()->mainFrame()->setName(WebString());
        webView()->settings()->setMinimumTimerInterval(webkit_support::GetForegroundTabTimerInterval());
    }
}

void WebViewHost::setClientWindowRect(const WebKit::WebRect& rect)
{
    setWindowRect(rect);
}

bool WebViewHost::navigate(const TestNavigationEntry& entry, bool reload)
{
    // Get the right target frame for the entry.
    WebFrame* frame = webView()->mainFrame();
    if (!entry.targetFrame().isEmpty())
        frame = webView()->findFrameByName(entry.targetFrame());

    // TODO(mpcomplete): should we clear the target frame, or should
    // back/forward navigations maintain the target frame?

    // A navigation resulting from loading a javascript URL should not be
    // treated as a browser initiated event. Instead, we want it to look as if
    // the page initiated any load resulting from JS execution.
    if (!GURL(entry.URL()).SchemeIs("javascript"))
        setPendingExtraData(adoptPtr(new TestShellExtraData(entry.pageID())));

    // If we are reloading, then WebKit will use the state of the current page.
    // Otherwise, we give it the state to navigate to.
    if (reload) {
        frame->reload(false);
    } else if (!entry.contentState().isNull()) {
        ASSERT(entry.pageID() != -1);
        frame->loadHistoryItem(entry.contentState());
    } else {
        ASSERT(entry.pageID() == -1);
        frame->loadRequest(WebURLRequest(entry.URL()));
    }

    // In case LoadRequest failed before DidCreateDataSource was called.
    setPendingExtraData(nullptr);

    // Restore focus to the main frame prior to loading new request.
    // This makes sure that we don't have a focused iframe. Otherwise, that
    // iframe would keep focus when the SetFocus called immediately after
    // LoadRequest, thus making some tests fail (see http://b/issue?id=845337
    // for more details).
    webView()->setFocusedFrame(frame);
    m_shell->setFocus(webView(), true);

    return true;
}

// Private functions ----------------------------------------------------------

void WebViewHost::updateForCommittedLoad(WebFrame* frame, bool isNewNavigation)
{
    // Code duplicated from RenderView::DidCommitLoadForFrame.
    TestShellExtraData* extraData = static_cast<TestShellExtraData*>(frame->dataSource()->extraData());
    const WebURL& url = frame->dataSource()->request().url();
    bool nonBlankPageAfterReset = m_pageId == -1 && !url.isEmpty() && strcmp(url.spec().data(), "about:blank");

    if (isNewNavigation || nonBlankPageAfterReset) {
        // New navigation.
        updateSessionHistory(frame);
        m_pageId = nextPageID++;
    } else if (extraData && extraData->pendingPageID != -1 && !extraData->requestCommitted) {
        // This is a successful session history navigation!
        updateSessionHistory(frame);
        m_pageId = extraData->pendingPageID;
    }

    // Don't update session history multiple times.
    if (extraData)
        extraData->requestCommitted = true;

    updateURL(frame);
}

void WebViewHost::updateURL(WebFrame* frame)
{
    WebDataSource* ds = frame->dataSource();
    ASSERT(ds);
    const WebURLRequest& request = ds->request();
    RefPtr<TestNavigationEntry> entry(TestNavigationEntry::create());

    // The referrer will be empty on https->http transitions. It
    // would be nice if we could get the real referrer from somewhere.
    entry->setPageID(m_pageId);
    if (ds->hasUnreachableURL())
        entry->setURL(ds->unreachableURL());
    else
        entry->setURL(request.url());

    const WebHistoryItem& historyItem = frame->currentHistoryItem();
    if (!historyItem.isNull())
        entry->setContentState(historyItem);

    navigationController()->didNavigateToEntry(entry.get());
    m_lastPageIdUpdated = max(m_lastPageIdUpdated, m_pageId);
}

void WebViewHost::updateSessionHistory(WebFrame* frame)
{
    // If we have a valid page ID at this point, then it corresponds to the page
    // we are navigating away from. Otherwise, this is the first navigation, so
    // there is no past session history to record.
    if (m_pageId == -1)
        return;

    TestNavigationEntry* entry = navigationController()->entryWithPageID(m_pageId);
    if (!entry)
        return;

    const WebHistoryItem& historyItem = webView()->mainFrame()->previousHistoryItem();
    if (historyItem.isNull())
        return;

    entry->setContentState(historyItem);
}

void WebViewHost::printFrameDescription(WebFrame* webframe)
{
    string name8 = webframe->uniqueName().utf8();
    if (webframe == webView()->mainFrame()) {
        if (!name8.length()) {
            fputs("main frame", stdout);
            return;
        }
        printf("main frame \"%s\"", name8.c_str());
        return;
    }
    if (!name8.length()) {
        fputs("frame (anonymous)", stdout);
        return;
    }
    printf("frame \"%s\"", name8.c_str());
}

void WebViewHost::setPendingExtraData(PassOwnPtr<TestShellExtraData> extraData)
{
    m_pendingExtraData = extraData;
}

void WebViewHost::setPageTitle(const WebString&)
{
    // Nothing to do in layout test.
}

void WebViewHost::enterFullScreenNow()
{
    webView()->willEnterFullScreen();
    webView()->didEnterFullScreen();
}

void WebViewHost::exitFullScreenNow()
{
    webView()->willExitFullScreen();
    webView()->didExitFullScreen();
}

#if ENABLE(MEDIA_STREAM)
webkit_support::TestMediaStreamClient* WebViewHost::testMediaStreamClient()
{
    if (!m_testMediaStreamClient.get())
        m_testMediaStreamClient = adoptPtr(new webkit_support::TestMediaStreamClient());
    return m_testMediaStreamClient.get();
}
#endif

// Painting functions ---------------------------------------------------------

void WebViewHost::paintRect(const WebRect& rect)
{
    ASSERT(!m_isPainting);
    ASSERT(canvas());
    m_isPainting = true;
    float deviceScaleFactor = webView()->deviceScaleFactor();
    int scaledX = static_cast<int>(static_cast<float>(rect.x) * deviceScaleFactor);
    int scaledY = static_cast<int>(static_cast<float>(rect.y) * deviceScaleFactor);
    int scaledWidth = static_cast<int>(ceil(static_cast<float>(rect.width) * deviceScaleFactor));
    int scaledHeight = static_cast<int>(ceil(static_cast<float>(rect.height) * deviceScaleFactor));
    WebRect deviceRect(scaledX, scaledY, scaledWidth, scaledHeight);
    webWidget()->paint(canvas(), deviceRect);
    m_isPainting = false;
}

void WebViewHost::paintInvalidatedRegion()
{
#if ENABLE(REQUEST_ANIMATION_FRAME)
    webWidget()->animate(0.0);
#endif
    webWidget()->layout();
    WebSize widgetSize = webWidget()->size();
    WebRect clientRect(0, 0, widgetSize.width, widgetSize.height);

    // Paint the canvas if necessary. Allow painting to generate extra rects
    // for the first two calls. This is necessary because some WebCore rendering
    // objects update their layout only when painted.
    // Store the total area painted in total_paint. Then tell the gdk window
    // to update that area after we're done painting it.
    for (int i = 0; i < 3; ++i) {
        // rect = intersect(proxy()->paintRect() , clientRect)
        WebRect damageRect = proxy()->paintRect();
        int left = max(damageRect.x, clientRect.x);
        int top = max(damageRect.y, clientRect.y);
        int right = min(damageRect.x + damageRect.width, clientRect.x + clientRect.width);
        int bottom = min(damageRect.y + damageRect.height, clientRect.y + clientRect.height);
        WebRect rect;
        if (left < right && top < bottom)
            rect = WebRect(left, top, right - left, bottom - top);

        proxy()->setPaintRect(WebRect());
        if (rect.isEmpty())
            continue;
        paintRect(rect);
    }
    ASSERT(proxy()->paintRect().isEmpty());
}

void WebViewHost::paintPagesWithBoundaries()
{
    ASSERT(!m_isPainting);
    ASSERT(canvas());
    m_isPainting = true;

    WebSize pageSizeInPixels = webWidget()->size();
    WebFrame* webFrame = webView()->mainFrame();

    int pageCount = webFrame->printBegin(pageSizeInPixels);
    int totalHeight = pageCount * (pageSizeInPixels.height + 1) - 1;

    SkCanvas* testCanvas = skia::TryCreateBitmapCanvas(pageSizeInPixels.width, totalHeight, true);
    if (testCanvas) {
        discardBackingStore();
        m_canvas = adoptPtr(testCanvas);
    } else {
        webFrame->printEnd();
        return;
    }

    webFrame->printPagesWithBoundaries(canvas(), pageSizeInPixels);
    webFrame->printEnd();

    m_isPainting = false;
}

SkCanvas* WebViewHost::canvas()
{
    if (m_canvas)
        return m_canvas.get();
    WebSize widgetSize = webWidget()->size();
    float deviceScaleFactor = webView()->deviceScaleFactor();
    int scaledWidth = static_cast<int>(ceil(static_cast<float>(widgetSize.width) * deviceScaleFactor));
    int scaledHeight = static_cast<int>(ceil(static_cast<float>(widgetSize.height) * deviceScaleFactor));
    resetScrollRect();
    m_canvas = adoptPtr(skia::CreateBitmapCanvas(scaledWidth, scaledHeight, true));
    return m_canvas.get();
}

void WebViewHost::resetScrollRect()
{
}

void WebViewHost::discardBackingStore()
{
    m_canvas.clear();
}

// Paints the entire canvas a semi-transparent black (grayish). This is used
// by the layout tests in fast/repaint. The alpha value matches upstream.
void WebViewHost::displayRepaintMask()
{
    canvas()->drawARGB(167, 0, 0, 0);
}

// Simulate a print by going into print mode and then exit straight away.
void WebViewHost::printPage(WebKit::WebFrame* frame)
{
    WebSize pageSizeInPixels = webWidget()->size();
    WebPrintParams printParams(pageSizeInPixels);
    frame->printBegin(printParams);
    frame->printEnd();
}
