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

#ifndef CCScrollController_h
#define CCScrollController_h

#include <wtf/Noncopyable.h>

namespace WebCore {

class IntRect;
class IntSize;

class CCScrollController {
    WTF_MAKE_NONCOPYABLE(CCScrollController);
public:
    virtual bool haveWheelEventHandlers() = 0;
    virtual void pinchGestureBegin() = 0;
    virtual void pinchGestureUpdate(float magnifyDelta, const IntPoint& anchor) = 0;
    virtual void pinchGestureEnd() = 0;
    virtual bool scrollBegin(const IntPoint&) = 0;
    virtual void scrollBy(const IntSize&) = 0;
    virtual void scrollEnd() = 0;
    virtual bool isScrolling() const = 0;

    // Return the scroll range relative to the current position. top/left
    // value <= 0, and bottom/right value >= 0.
    virtual IntRect rootLayerScrollRange() const = 0;

protected:
    CCScrollController() { }
    virtual ~CCScrollController() { }
};

}

#endif
