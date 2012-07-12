/*
 * Copyright (C) 2011 Google Inc. All rights reserved.
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

#ifndef WebInputEventFactory_h
#define WebInputEventFactory_h

#include "../platform/WebCommon.h"
#include "../WebInputEvent.h"

namespace WebKit {

class WebKeyboardEvent;

class WebInputEventFactory {
public:
    enum MouseEventType {
        MouseEventTypeDown = 0,
        MouseEventTypeUp,
        MouseEventTypeMove,
    };

    enum GestureEventType {
        SCROLL_BEGIN = 0,
        SCROLL_END,
        SCROLL_UPDATE,
        FLING_START,
        FLING_CANCEL,
        PINCH_BEGIN,
        PINCH_END,
        PINCH_UPDATE,
    };

    enum MouseWheelDirectionType {
        SCROLL_UP = 0,
        SCROLL_DOWN,
        SCROLL_LEFT,
        SCROLL_RIGHT,
    };

    WEBKIT_EXPORT static WebKeyboardEvent keyboardEvent(WebInputEvent::Type,
                                                        int modifiers,
                                                        double timeStampSeconds,
                                                        int keycode,
                                                        WebUChar unicodeCharacter,
                                                        bool isSystemKey);

    WEBKIT_EXPORT static WebMouseEvent mouseEvent(int x,
                                                  int y,
                                                  int windowX,
                                                  int windowY,
                                                  MouseEventType,
                                                  double timeStampSeconds,
                                                  WebMouseEvent::Button = WebMouseEvent::ButtonLeft);

    WEBKIT_EXPORT static WebGestureEvent gestureEvent(int x,
                                                      int y,
                                                      float delta_x,
                                                      float delta_y,
                                                      GestureEventType type,
                                                      double timeStampSeconds);

    WEBKIT_EXPORT static WebMouseWheelEvent mouseWheelEvent(int x, int y,
                                                            int windowX, int windowY,
                                                            double timeStampSeconds,
                                                            MouseWheelDirectionType direction);
#if defined(ANDROID)
    WEBKIT_EXPORT static WebPageScaleAnimationGestureEvent pageScaleAnimationGestureEvent(
        int x,
        int y,
        bool anchorPoint,
        float pageScale,
        double durationMs,
        double timeStampSeconds);
#endif
};

} // namespace WebKit

#endif
