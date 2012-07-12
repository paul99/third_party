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
#include "FlingAnimator.h"

#include "base/android/jni_android.h"
#include "base/android/scoped_java_ref.h"

using namespace base::android;

namespace WebKit {

FlingAnimator::FlingAnimator()
{
    // hold the global reference of the Java objects.
    JNIEnv* env = AttachCurrentThread();
    ASSERT(env);
    ScopedJavaLocalRef<jclass> cls(GetClass(env, "android/widget/OverScroller"));
    jmethodID constructor = GetMethodID(env, cls, "<init>", "(Landroid/content/Context;)V");
    ScopedJavaLocalRef<jobject> tmp(env, env->NewObject(cls.obj(), constructor, GetApplicationContext()));
    ASSERT(tmp.obj());
    m_javaScroller.Reset(tmp);

    m_flingMethodId = GetMethodID(env, cls, "fling", "(IIIIIIIIII)V");
    m_abortMethodId = GetMethodID(env, cls, "abortAnimation", "()V");
    m_computeMethodId = GetMethodID(env, cls, "computeScrollOffset", "()Z");
    m_getXMethodId = GetMethodID(env, cls, "getCurrX", "()I");
    m_getYMethodId = GetMethodID(env, cls, "getCurrY", "()I");
}

FlingAnimator::~FlingAnimator()
{
    stop();
}

void FlingAnimator::triggerFling(PassRefPtr<ScrollController> controller, const WebGestureEvent& event)
{
    ASSERT(event.type == WebInputEvent::GestureFlingStart);

    if (!controller.get() || ((int)event.deltaX == 0 && (int)event.deltaY == 0))
        return;

    stop();
    m_scrollController = controller;

    const WebCore::IntRect& range = m_scrollController->scrollRange();
    const WebCore::IntSize& over = m_scrollController->overScroll();
    JNIEnv* env = m_javaScroller.env();
    env->CallVoidMethod(m_javaScroller.obj(), m_flingMethodId, 0, 0, -(int)event.deltaX, -(int)event.deltaY,
                        range.x(), range.maxX(), range.y(), range.maxY(), over.width(), over.height());
    CheckException(env);

    m_scrollController->setUpdateCallback(FlingAnimator::fired);
}

void FlingAnimator::stop()
{
    if (!m_scrollController.get())
        return;

    m_scrollController->scrollEnd();
    m_scrollController->setUpdateCallback(0);
    m_scrollController.clear();
    m_last.setWidth(0);
    m_last.setHeight(0);

    JNIEnv* env = m_javaScroller.env();
    env->CallVoidMethod(m_javaScroller.obj(), m_abortMethodId);
    CheckException(env);
}

bool FlingAnimator::update()
{
    JNIEnv* env = m_javaScroller.env();
    if (!env->CallBooleanMethod(m_javaScroller.obj(), m_computeMethodId)) {
        CheckException(env);
        stop();
        return false;
    }
    WebCore::IntSize curr(env->CallIntMethod(m_javaScroller.obj(), m_getXMethodId),
                 env->CallIntMethod(m_javaScroller.obj(), m_getYMethodId));
    CheckException(env);

    m_scrollController->scrollBy(curr - m_last);
    m_last = curr;
    return true;
}

bool FlingAnimator::isActive() const
{
    return m_scrollController.get();
}

} // namespace WebKit
