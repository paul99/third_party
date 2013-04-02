/*
   Copyright (C) 2011 Samsung Electronics
   Copyright (C) 2012 Intel Corporation. All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef EwkView_h
#define EwkView_h

#include "EvasGLContext.h"
#include "EvasGLSurface.h"
#include "EwkViewCallbacks.h"
#include "ImmutableDictionary.h"
#include "RefPtrEfl.h"
#include "WKEinaSharedString.h"
#include "WKGeometry.h"
#include "WKRetainPtr.h"
#include "WebView.h"
#include "ewk_url_request_private.h"
#include <Evas.h>
#include <WebCore/FloatPoint.h>
#include <WebCore/IntRect.h>
#include <WebCore/RefPtrCairo.h>
#include <WebCore/TextDirection.h>
#include <WebCore/Timer.h>
#include <WebKit2/WKBase.h>
#include <wtf/HashMap.h>
#include <wtf/OwnPtr.h>
#include <wtf/RefPtr.h>
#include <wtf/text/WTFString.h>

#if ENABLE(TOUCH_EVENTS)
#include "ewk_touch.h"
#endif


#include "WebContext.h"
#include "WebPageGroup.h"
#include "WebPreferences.h"

namespace WebKit {
class ContextMenuClientEfl;
class FindClientEfl;
class FormClientEfl;
class InputMethodContextEfl;
class PageClientBase;
class PageLoadClientEfl;
class PagePolicyClientEfl;
class PageUIClientEfl;
class WebContextMenuItemData;
class WebContextMenuProxyEfl;
class WebPageGroup;
class WebPageProxy;
class WebPopupItem;
class WebPopupMenuProxyEfl;

#if ENABLE(VIBRATION)
class VibrationClientEfl;
#endif
}

namespace WebCore {
class AffineTransform;
class Color;
class Cursor;
class IntSize;
class CoordinatedGraphicsScene;
}

class EwkContext;
class EwkBackForwardList;
class EwkColorPicker;
class EwkContextMenu;
class EwkPopupMenu;
class EwkSettings;
class EwkWindowFeatures;

typedef struct _Evas_GL_Context Evas_GL_Context;
typedef struct _Evas_GL_Surface Evas_GL_Surface;

typedef struct Ewk_View_Smart_Data Ewk_View_Smart_Data;
typedef struct Ewk_View_Smart_Class Ewk_View_Smart_Class;

// EwkView object is owned by the evas object, obtained from EwkView::createEvasObject().
class EwkView {
public:

    enum ViewBehavior {
        LegacyBehavior,
        DefaultBehavior
    };

    static Evas_Object* createEvasObject(Evas* canvas, Evas_Smart* smart, PassRefPtr<EwkContext> context,  WKPageGroupRef pageGroupRef = 0, ViewBehavior behavior = EwkView::DefaultBehavior);
    static Evas_Object* createEvasObject(Evas* canvas, PassRefPtr<EwkContext> context, WKPageGroupRef pageGroupRef = 0, ViewBehavior behavior = EwkView::DefaultBehavior);

    static bool initSmartClassInterface(Ewk_View_Smart_Class&);

    static const Evas_Object* toEvasObject(WKPageRef);

    Evas_Object* evasObject() { return m_evasObject; }

    WKViewRef wkView() const { return toAPI(m_webView.get()); }
    WKPageRef wkPage() const;

    WebKit::WebPageProxy* page() { return m_webView->page(); }
    EwkContext* ewkContext() { return m_context.get(); }
    EwkSettings* settings() { return m_settings.get(); }
    EwkBackForwardList* backForwardList() { return m_backForwardList.get(); }
    EwkWindowFeatures* windowFeatures();

    WebCore::IntSize size() const;
    bool isFocused() const;
    bool isVisible() const;

    void setDeviceScaleFactor(float scale);
    float deviceScaleFactor() const;

    WebCore::AffineTransform transformToScene() const;
    WebCore::AffineTransform transformFromScene() const;
    WebCore::AffineTransform transformToScreen() const;

    const char* url() const { return m_url; }
    const char* faviconURL() const { return m_faviconURL; }
    const char* title() const;
    WebKit::InputMethodContextEfl* inputMethodContext();

    const char* themePath() const;
    void setThemePath(const char* theme);
    const char* customTextEncodingName() const;
    void setCustomTextEncodingName(const String& encoding);

    bool mouseEventsEnabled() const { return m_mouseEventsEnabled; }
    void setMouseEventsEnabled(bool enabled);
#if ENABLE(TOUCH_EVENTS)
    bool touchEventsEnabled() const { return m_touchEventsEnabled; }
    void setTouchEventsEnabled(bool enabled);
#endif

    void setCursor(const WebCore::Cursor& cursor);
    void setImageData(void* imageData, const WebCore::IntSize& size);

    void scheduleUpdateDisplay();

#if ENABLE(FULLSCREEN_API)
    void enterFullScreen();
    void exitFullScreen();
#endif

    WKRect windowGeometry() const;
    void setWindowGeometry(const WKRect&);

    bool createGLSurface(const WebCore::IntSize& viewSize);
    bool enterAcceleratedCompositingMode();
    bool exitAcceleratedCompositingMode();
    void setNeedsSurfaceResize() { m_pendingSurfaceResize = true; }

#if ENABLE(INPUT_TYPE_COLOR)
    void requestColorPicker(WKColorPickerResultListenerRef listener, const WebCore::Color&);
    void dismissColorPicker();
#endif

    WKPageRef createNewPage(PassRefPtr<EwkUrlRequest>, WKDictionaryRef windowFeatures);
    void close();

    void requestPopupMenu(WebKit::WebPopupMenuProxyEfl*, const WebCore::IntRect&, WebCore::TextDirection, double pageScaleFactor, const Vector<WebKit::WebPopupItem>& items, int32_t selectedIndex);
    void closePopupMenu();
    
    void showContextMenu(WebKit::WebContextMenuProxyEfl*, const WebCore::IntPoint& position, const Vector<WebKit::WebContextMenuItemData>& items);
    void hideContextMenu();

    void updateTextInputState();

    void requestJSAlertPopup(const WKEinaSharedString& message);
    bool requestJSConfirmPopup(const WKEinaSharedString& message);
    WKEinaSharedString requestJSPromptPopup(const WKEinaSharedString& message, const WKEinaSharedString& defaultValue);

    template<EwkViewCallbacks::CallbackType callbackType>
    EwkViewCallbacks::CallBack<callbackType> smartCallback() const
    {
        return EwkViewCallbacks::CallBack<callbackType>(m_evasObject);
    }

    unsigned long long informDatabaseQuotaReached(const String& databaseName, const String& displayName, unsigned long long currentQuota, unsigned long long currentOriginUsage, unsigned long long currentDatabaseUsage, unsigned long long expectedUsage);

    WebKit::PageClientBase* pageClient() { return m_pageClient.get(); }

    void setPageScaleFactor(float scaleFactor) { m_pageScaleFactor = scaleFactor; }
    float pageScaleFactor() const { return m_pageScaleFactor; }

    void setPagePosition(const WebCore::FloatPoint& position) { m_pagePosition = position; }
    const WebCore::FloatPoint pagePosition() const { return m_pagePosition; }

    // FIXME: needs refactoring (split callback invoke)
    void informURLChange();

    bool isHardwareAccelerated() const { return m_isHardwareAccelerated; }

    PassRefPtr<cairo_surface_t> takeSnapshot();

private:
    EwkView(Evas_Object* evasObject, PassRefPtr<EwkContext> context, WKPageGroupRef pageGroup, ViewBehavior);
    ~EwkView();

    Ewk_View_Smart_Data* smartData() const;

    void displayTimerFired(WebCore::Timer<EwkView>*);

    WebCore::CoordinatedGraphicsScene* coordinatedGraphicsScene();

    void informIconChange();

    // Evas_Smart_Class callback interface:
    static void handleEvasObjectAdd(Evas_Object*);
    static void handleEvasObjectDelete(Evas_Object*);
    static void handleEvasObjectMove(Evas_Object*, Evas_Coord x, Evas_Coord y);
    static void handleEvasObjectResize(Evas_Object*, Evas_Coord width, Evas_Coord height);
    static void handleEvasObjectShow(Evas_Object*);
    static void handleEvasObjectHide(Evas_Object*);
    static void handleEvasObjectColorSet(Evas_Object*, int red, int green, int blue, int alpha);
    static void handleEvasObjectCalculate(Evas_Object*);

    // Ewk_View_Smart_Class callback interface:
    static Eina_Bool handleEwkViewFocusIn(Ewk_View_Smart_Data* smartData);
    static Eina_Bool handleEwkViewFocusOut(Ewk_View_Smart_Data* smartData);
    static Eina_Bool handleEwkViewMouseWheel(Ewk_View_Smart_Data* smartData, const Evas_Event_Mouse_Wheel* wheelEvent);
    static Eina_Bool handleEwkViewMouseDown(Ewk_View_Smart_Data* smartData, const Evas_Event_Mouse_Down* downEvent);
    static Eina_Bool handleEwkViewMouseUp(Ewk_View_Smart_Data* smartData, const Evas_Event_Mouse_Up* upEvent);
    static Eina_Bool handleEwkViewMouseMove(Ewk_View_Smart_Data* smartData, const Evas_Event_Mouse_Move* moveEvent);
    static Eina_Bool handleEwkViewKeyDown(Ewk_View_Smart_Data* smartData, const Evas_Event_Key_Down* downEvent);
    static Eina_Bool handleEwkViewKeyUp(Ewk_View_Smart_Data* smartData, const Evas_Event_Key_Up* upEvent);

#if ENABLE(TOUCH_EVENTS)
    void feedTouchEvents(Ewk_Touch_Event_Type type);
    static void handleTouchDown(void* data, Evas*, Evas_Object*, void* eventInfo);
    static void handleTouchUp(void* data, Evas*, Evas_Object*, void* eventInfo);
    static void handleTouchMove(void* data, Evas*, Evas_Object*, void* eventInfo);
#endif
    static void handleFaviconChanged(const char* pageURL, void* eventInfo);

private:
    // Note, initialization order matters.
    Evas_Object* m_evasObject;
    RefPtr<EwkContext> m_context;
    OwnPtr<Evas_GL> m_evasGL;
    OwnPtr<WebKit::EvasGLContext> m_evasGLContext;
    OwnPtr<WebKit::EvasGLSurface> m_evasGLSurface;
    bool m_pendingSurfaceResize;
    OwnPtr<WebKit::PageClientBase> m_pageClient;
    RefPtr<WebKit::WebView> m_webView;
    OwnPtr<WebKit::PageLoadClientEfl> m_pageLoadClient;
    OwnPtr<WebKit::PagePolicyClientEfl> m_pagePolicyClient;
    OwnPtr<WebKit::PageUIClientEfl> m_pageUIClient;
    OwnPtr<WebKit::ContextMenuClientEfl> m_contextMenuClient;
    OwnPtr<WebKit::FindClientEfl> m_findClient;
    OwnPtr<WebKit::FormClientEfl> m_formClient;
#if ENABLE(VIBRATION)
    OwnPtr<WebKit::VibrationClientEfl> m_vibrationClient;
#endif
    OwnPtr<EwkBackForwardList> m_backForwardList;
    float m_pageScaleFactor;
    WebCore::FloatPoint m_pagePosition;
    OwnPtr<EwkSettings> m_settings;
    RefPtr<EwkWindowFeatures> m_windowFeatures;
    const void* m_cursorIdentifier; // This is an address, do not free it.
    WKEinaSharedString m_faviconURL;
    WKEinaSharedString m_url;
    mutable WKEinaSharedString m_title;
    WKEinaSharedString m_theme;
    mutable WKEinaSharedString m_customEncoding;
    bool m_mouseEventsEnabled;
#if ENABLE(TOUCH_EVENTS)
    bool m_touchEventsEnabled;
#endif
    WebCore::Timer<EwkView> m_displayTimer;
    OwnPtr<EwkContextMenu> m_contextMenu;
    OwnPtr<EwkPopupMenu> m_popupMenu;
    OwnPtr<WebKit::InputMethodContextEfl> m_inputMethodContext;
#if ENABLE(INPUT_TYPE_COLOR)
    OwnPtr<EwkColorPicker> m_colorPicker;
#endif
    bool m_isHardwareAccelerated;

    static Evas_Smart_Class parentSmartClass;
};

EwkView* toEwkView(const Evas_Object*);
EwkView* toEwkView(const Ewk_View_Smart_Data* smartData);

bool isViewEvasObject(const Evas_Object* evasObject);

#endif // EwkView_h
