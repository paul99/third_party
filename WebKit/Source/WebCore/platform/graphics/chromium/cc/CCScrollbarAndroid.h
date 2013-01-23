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

#ifndef CCScrollbarAndroid_h
#define CCScrollbarAndroid_h

#include <wtf/PassRefPtr.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class CCLayerImpl;
class FloatRect;
class LayerRendererChromium;

class CCScrollbarAndroid {
public:
    CCScrollbarAndroid(PassRefPtr<GraphicsContext3D> context, const CCLayerImpl* scrollLayer, const CCLayerImpl* drawLayer);
    virtual ~CCScrollbarAndroid();

    void draw(LayerRendererChromium*, const TransformationMatrix&, const IntSize& bounds, double timestamp);

    static bool drawScrollbarOverlay(LayerRendererChromium*, double timestamp);
    static void resetScrollbarOverlay(LayerRendererChromium*);

private:
    bool needsAnimation(double timestamp) const { return opacityAtTime(timestamp); }
    float opacityAtTime(double timestamp) const;

    void updateScrollbarRect(const CCLayerImpl* scrollLayer, const CCLayerImpl* drawLayer, double timestamp);
    FloatRect computeScrollbarRect(const CCLayerImpl* scrollLayer, const CCLayerImpl* drawLayer) const;

    RefPtr<GraphicsContext3D> m_context;
    double m_lastAwakenTime;
    FloatRect m_lastScrollbarRect;
    FloatPoint m_lastScrollPosition;
    Platform3DObject m_vertexBuffer;
};

} // namespace WebCore

#endif
