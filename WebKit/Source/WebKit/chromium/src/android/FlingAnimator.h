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

#ifndef FlingAnimator_h
#define FlingAnimator_h

#include "IntRect.h"
#include "WebInputEvent.h"
#include <wtf/Noncopyable.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/RefPtr.h>

#include "base/android/scoped_java_ref.h"

namespace WebKit {

class FlingAnimator;

typedef bool (*UpdateCallback)(FlingAnimator*);

// ScrollController is used by the FlingAnimator to drive the fling animation.
// WebKit thread and CC thread may have different implementation.
class ScrollController : public RefCounted<ScrollController> {
    WTF_MAKE_NONCOPYABLE(ScrollController);
    friend class FlingAnimator;
public:
    virtual ~ScrollController() { m_update = 0; }

    virtual void scrollBy(const WebCore::IntSize&) = 0;
    virtual void scrollEnd() = 0;

    // The scroll range relative to the current position.
    const WebCore::IntRect& scrollRange() const { return m_scrollRange; }
    // The overscroll range. >=0
    const WebCore::IntSize& overScroll() const { return m_overScroll; }

#if OS(ANDROID)
    // This is an M18 hack to allow FlingAnimator to stay in sync with the compositor.
    // In the Clank master branch, we use https://gerrit-int.chromium.org/15489
    virtual void animate(double monotonicTime) = 0;
#endif

protected:
    ScrollController()
        : m_update(0)
    {
        // The derived class can override m_scrollRange. The default value is to
        // ensure scroller always generate scroll events.
        m_scrollRange.inflate(10000);
    }

    void setUpdateCallback(UpdateCallback update) { m_update = update; }

    UpdateCallback m_update;

    // The derived class may override these values to provide the proper range.
    WebCore::IntRect m_scrollRange;
    WebCore::IntSize m_overScroll;
};

class FlingAnimator {
public:
    FlingAnimator();
    ~FlingAnimator();

    void triggerFling(PassRefPtr<ScrollController>, const WebGestureEvent& event);
    void stop();
    bool update();
    bool isActive() const;

#if OS(ANDROID)
    virtual void animate(double monotonicTime)
    {
        if (m_scrollController)
            m_scrollController->animate(monotonicTime);
    }
#endif

private:
    static bool fired(FlingAnimator* animator) { return animator && animator->update(); }

    // Ideally this should be OwnPtr. But CCThread doesn't have a way to cancel
    // the posted task. Use RefPtr to keep the lifetime for the posted task.
    RefPtr<ScrollController> m_scrollController;
    WebCore::IntSize m_last;

    // Java OverScroller instance and methods.
    base::android::ScopedJavaGlobalRef<jobject> m_javaScroller;
    jmethodID m_flingMethodId;
    jmethodID m_abortMethodId;
    jmethodID m_computeMethodId;
    jmethodID m_getXMethodId;
    jmethodID m_getYMethodId;
};

}

#endif // FlingAnimator_h
