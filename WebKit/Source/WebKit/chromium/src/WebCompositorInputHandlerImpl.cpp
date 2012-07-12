/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "WebCompositorInputHandlerImpl.h"

#include "WebCompositorImpl.h"
#include "WebCompositorInputHandlerClient.h"
#include "WebInputEvent.h"
#include "WebKit.h"
#include "platform/WebKitPlatformSupport.h"
#include "cc/CCProxy.h"
#include <wtf/ThreadingPrimitives.h>

#if OS(ANDROID)
#include "android/FlingAnimator.h"
#include <wtf/CurrentTime.h>
#endif

using namespace WebCore;

namespace WebCore {

PassOwnPtr<CCInputHandler> CCInputHandler::create(CCInputHandlerClient* inputHandlerClient)
{
    return WebKit::WebCompositorInputHandlerImpl::create(inputHandlerClient);
}

}

namespace WebKit {

#if OS(ANDROID)
class CompositorScrollController : public ScrollController {
public:
    static PassRefPtr<ScrollController> create(CCInputHandlerClient* ccController, FlingAnimator* flingAnimator)
    {
        RefPtr<CompositorScrollController> controller = adoptRef(new CompositorScrollController(ccController, flingAnimator));
        controller->requestAnimate();
        return controller.release();
    }

    virtual ~CompositorScrollController() { }

    virtual void scrollBy(const IntSize& offset)
    {
        m_ccController->scrollBy(offset);
    }

    virtual void scrollEnd()
    {
        m_ccController->scrollEnd();
    }

    virtual void animate(double monotonicTime)
    {
        if (!m_needAnimate)
            return;
        m_needAnimate = false;
        if (m_update && m_update(m_flingAnimator))
            requestAnimate();
    }

private:
    CompositorScrollController(CCInputHandlerClient* ccController, FlingAnimator* flingAnimator)
        : m_ccController(ccController)
        , m_flingAnimator(flingAnimator)
        , m_needAnimate(false)
    {
        m_scrollRange = m_ccController->scrollRange();
    }

    void requestAnimate()
    {
        if (m_needAnimate)
            return;
        m_needAnimate = true;
        m_ccController->scheduleAnimation();
    }

    CCInputHandlerClient* m_ccController;
    FlingAnimator* m_flingAnimator;
    bool m_needAnimate;
};
#endif

// These statics may only be accessed from the compositor thread.
int WebCompositorInputHandlerImpl::s_nextAvailableIdentifier = 1;
HashSet<WebCompositorInputHandlerImpl*>* WebCompositorInputHandlerImpl::s_compositors = 0;

WebCompositor* WebCompositorInputHandler::fromIdentifier(int identifier)
{
    return static_cast<WebCompositor*>(WebCompositorInputHandlerImpl::fromIdentifier(identifier));
}

PassOwnPtr<WebCompositorInputHandlerImpl> WebCompositorInputHandlerImpl::create(WebCore::CCInputHandlerClient* inputHandlerClient)
{
    return adoptPtr(new WebCompositorInputHandlerImpl(inputHandlerClient));
}

WebCompositorInputHandler* WebCompositorInputHandlerImpl::fromIdentifier(int identifier)
{
    ASSERT(WebCompositorImpl::initialized());
    ASSERT(CCProxy::isImplThread());

    if (!s_compositors)
        return 0;

    for (HashSet<WebCompositorInputHandlerImpl*>::iterator it = s_compositors->begin(); it != s_compositors->end(); ++it) {
        if ((*it)->identifier() == identifier)
            return *it;
    }
    return 0;
}

WebCompositorInputHandlerImpl::WebCompositorInputHandlerImpl(CCInputHandlerClient* inputHandlerClient)
    : m_client(0)
    , m_identifier(s_nextAvailableIdentifier++)
    , m_inputHandlerClient(inputHandlerClient)
#ifndef NDEBUG
    , m_expectScrollUpdateEnd(false)
    , m_expectPinchUpdateEnd(false)
#endif
    , m_scrollStarted(false)
{
    ASSERT(CCProxy::isImplThread());
#if OS(ANDROID)
    if (!WebKit::layoutTestMode())
        m_flingAnimator = adoptPtr(new FlingAnimator);
#endif

    if (!s_compositors)
        s_compositors = new HashSet<WebCompositorInputHandlerImpl*>;
    s_compositors->add(this);
}

WebCompositorInputHandlerImpl::~WebCompositorInputHandlerImpl()
{
    ASSERT(CCProxy::isImplThread());
    if (m_client)
        m_client->willShutdown();

    ASSERT(s_compositors);
    s_compositors->remove(this);
    if (!s_compositors->size()) {
        delete s_compositors;
        s_compositors = 0;
    }
}

void WebCompositorInputHandlerImpl::setClient(WebCompositorInputHandlerClient* client)
{
    ASSERT(CCProxy::isImplThread());
    // It's valid to set a new client if we've never had one or to clear the client, but it's not valid to change from having one client to a different one.
    ASSERT(!m_client || !client);
    m_client = client;
}

void WebCompositorInputHandlerImpl::handleInputEvent(const WebInputEvent& event)
{
    ASSERT(CCProxy::isImplThread());
    ASSERT(m_client);

    if (event.type == WebInputEvent::MouseWheel && !m_inputHandlerClient->haveWheelEventHandlers()) {
        const WebMouseWheelEvent& wheelEvent = *static_cast<const WebMouseWheelEvent*>(&event);
        CCInputHandlerClient::ScrollStatus scrollStatus = m_inputHandlerClient->scrollBegin(IntPoint(wheelEvent.x, wheelEvent.y));
        switch (scrollStatus) {
        case CCInputHandlerClient::ScrollStarted:
            m_inputHandlerClient->scrollBy(IntSize(-wheelEvent.deltaX, -wheelEvent.deltaY));
            m_inputHandlerClient->scrollEnd();
            m_client->didHandleInputEvent();
            return;
        case CCInputHandlerClient::ScrollIgnored:
            m_client->didNotHandleInputEvent(false /* sendToWidget */);
            return;
        case CCInputHandlerClient::ScrollFailed:
            break;
        }
    } else if (event.type == WebInputEvent::GestureScrollBegin) {
        ASSERT(!m_scrollStarted);
        ASSERT(!m_expectScrollUpdateEnd);
#ifndef NDEBUG
        m_expectScrollUpdateEnd = true;
#endif
        const WebGestureEvent& gestureEvent = *static_cast<const WebGestureEvent*>(&event);
        CCInputHandlerClient::ScrollStatus scrollStatus = m_inputHandlerClient->scrollBegin(IntPoint(gestureEvent.x, gestureEvent.y));
        switch (scrollStatus) {
        case CCInputHandlerClient::ScrollStarted:
            m_scrollStarted = true;
            m_client->didHandleInputEvent();
            return;
        case CCInputHandlerClient::ScrollIgnored:
            m_client->didNotHandleInputEvent(false /* sendToWidget */);
            return;
        case CCInputHandlerClient::ScrollFailed:
            break;
        }
    } else if (event.type == WebInputEvent::GestureScrollUpdate) {
        ASSERT(m_expectScrollUpdateEnd);
        if (m_scrollStarted) {
            const WebGestureEvent& gestureEvent = *static_cast<const WebGestureEvent*>(&event);
#if OS(ANDROID)
            // TODO(jgreenwald): Merge related.  It seems that gesture handling is making its
            // way upstream, but the axis are inverted.
            m_inputHandlerClient->scrollBy(IntSize(gestureEvent.deltaX, gestureEvent.deltaY));
#else
            m_inputHandlerClient->scrollBy(IntSize(-gestureEvent.deltaX, -gestureEvent.deltaY));
#endif
            m_client->didHandleInputEvent();
            return;
        }
    } else if (event.type == WebInputEvent::GestureScrollEnd) {
        ASSERT(m_expectScrollUpdateEnd);
#ifndef NDEBUG
        m_expectScrollUpdateEnd = false;
#endif
        if (m_scrollStarted) {
            m_inputHandlerClient->scrollEnd();
            m_client->didHandleInputEvent();
            m_scrollStarted = false;
            return;
        }
    } else if (event.type == WebInputEvent::GesturePinchBegin) {
        ASSERT(!m_expectPinchUpdateEnd);
#ifndef NDEBUG
        m_expectPinchUpdateEnd = true;
#endif
        m_inputHandlerClient->pinchGestureBegin();
        m_client->didHandleInputEvent();
        return;
    } else if (event.type == WebInputEvent::GesturePinchEnd) {
        ASSERT(m_expectPinchUpdateEnd);
#ifndef NDEBUG
        m_expectPinchUpdateEnd = false;
#endif
        m_inputHandlerClient->pinchGestureEnd();
        m_client->didHandleInputEvent();
        return;
    } else if (event.type == WebInputEvent::GesturePinchUpdate) {
        ASSERT(m_expectPinchUpdateEnd);
        const WebGestureEvent& gestureEvent = *static_cast<const WebGestureEvent*>(&event);
        m_inputHandlerClient->pinchGestureUpdate(gestureEvent.deltaX, IntPoint(gestureEvent.x, gestureEvent.y));
        m_client->didHandleInputEvent();
        return;
#if OS(ANDROID)
    } else if (event.type == WebInputEvent::GesturePageScaleAnimation) {
        const WebPageScaleAnimationGestureEvent& animationEvent = *static_cast<const WebPageScaleAnimationGestureEvent*>(&event);
        m_inputHandlerClient->startPageScaleAnimation(
            IntSize(animationEvent.globalX, animationEvent.globalY),
            animationEvent.anchorPoint,
            animationEvent.pageScale,
            monotonicallyIncreasingTime(),
            animationEvent.durationMs / 1000.0);
        m_client->didHandleInputEvent();
        return;
    } else if (event.type == WebInputEvent::GestureFlingStart) {
        // TODO: use (x, y) to find the target CCInputHandlerClient
        const WebGestureEvent& gestureEvent = *static_cast<const WebGestureEvent*>(&event);
        CCInputHandlerClient::ScrollStatus scrollStatus;
        if (m_scrollStarted)
            scrollStatus = CCInputHandlerClient::ScrollStarted;
        else
            scrollStatus = m_inputHandlerClient->scrollBegin(IntPoint(gestureEvent.x, gestureEvent.y));
        switch (scrollStatus) {
        case CCInputHandlerClient::ScrollStarted:
            m_flingAnimator->triggerFling(CompositorScrollController::create(m_inputHandlerClient, m_flingAnimator.get()), gestureEvent);
            m_client->didHandleInputEvent();
            return;
        case CCInputHandlerClient::ScrollIgnored:
            // The user isn't touching a scrollable layer, but it may still be a valid fling gesture. Don't ignore the event.
            /* fall through */
        case CCInputHandlerClient::ScrollFailed:
            break;
        }
    } else if (event.type == WebInputEvent::GestureFlingCancel) {
        if (m_flingAnimator->isActive()) {
            m_flingAnimator->stop();
            m_client->didHandleInputEvent();
            return;
        }
#endif
    }
    m_client->didNotHandleInputEvent(true /* sendToWidget */);
}

void WebCompositorInputHandlerImpl::didVSync(double frameBeginMonotonic, double currentFrameIntervalInSec)
{
    m_inputHandlerClient->didVSync(frameBeginMonotonic, currentFrameIntervalInSec);
}

int WebCompositorInputHandlerImpl::identifier() const
{
    ASSERT(CCProxy::isImplThread());
    return m_identifier;
}

void WebCompositorInputHandlerImpl::willDraw(double monotonicTime)
{
    if (m_flingAnimator)
        m_flingAnimator->animate(monotonicTime);
}

}
