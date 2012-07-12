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

#include "config.h"

#include "FloatRect.h"
#include "IntRect.h"
#include "LayerRendererChromium.h"
#include "cc/CCScrollbarAndroid.h"
#include "cc/CCLayerImpl.h"

namespace WebCore {

CCScrollbarAndroid::CCScrollbarAndroid(const CCLayerImpl* scrollLayer, const CCLayerImpl* drawLayer)
    : m_lastAwakenTime(-1.0)
    , m_lastScrollbarRect(computeScrollbarRect(scrollLayer, drawLayer))
{
}

void CCScrollbarAndroid::draw(LayerRendererChromium* layerRenderer, const TransformationMatrix& transform, const IntSize& bounds, double timestamp)
{
    float opacity = opacityAtTime(timestamp);
    if (!opacity)
        return;

    GraphicsContext3D* context = layerRenderer->context();

    const LayerChromium::BorderProgram* program = layerRenderer->borderProgram();
    GLC(context, context->useProgram(program->program()));

    float matrix[16];
    layerRenderer->toGLMatrix(matrix, transform);
    GLC(context, context->uniformMatrix4fv(program->vertexShader().matrixLocation(), false, matrix, 1));

    GLC(context, context->enable(GraphicsContext3D::BLEND));
    GLC(context, context->blendFunc(GraphicsContext3D::ONE, GraphicsContext3D::ONE_MINUS_SRC_ALPHA));

    const int w = bounds.width();
    const int h = bounds.height();

    const float screenPixelDensity = layerRenderer->settings().screenPixelDensity;
    const float hScrollbarMarginL = 2.5 * screenPixelDensity;
    const float hScrollbarMarginR = 12.5 * screenPixelDensity;
    const float hScrollbarMarginB = 2.5 * screenPixelDensity;
    const float hScrollbarThickness = 4.0 * screenPixelDensity;
    FloatRect hScrollbar = m_lastScrollbarRect;
    hScrollbar.scale(w - hScrollbarMarginL - hScrollbarMarginR, 0.0);
    hScrollbar.move(hScrollbarMarginL, 0.0);
    hScrollbar.setY(h - hScrollbarMarginB - hScrollbarThickness);
    hScrollbar.setHeight(hScrollbarThickness);

    const float vScrollbarMarginT = 2.5 * screenPixelDensity;
    const float vScrollbarMarginB = 2.5 * screenPixelDensity;
    const float vScrollbarMarginR = 2.5 * screenPixelDensity;
    const float vScrollbarThickness = 4.0 * screenPixelDensity;
    FloatRect vScrollbar = m_lastScrollbarRect;
    vScrollbar.scale(0.0, h - vScrollbarMarginT - vScrollbarMarginB);
    vScrollbar.move(0.0, vScrollbarMarginT);
    vScrollbar.setX(w - vScrollbarMarginR - vScrollbarThickness);
    vScrollbar.setWidth(vScrollbarThickness);

    float vertices[24][4] = {                                          // draw 1 bar in 3 rects for smooth edge
         {hScrollbar.x()    + 1.0, hScrollbar.maxY()      , 0.0, 1.0},  //A
         {hScrollbar.maxX() - 1.0, hScrollbar.maxY()      , 0.0, 1.0},  //                           F|D||.
         {hScrollbar.maxX() - 1.0, hScrollbar.y()         , 0.0, 1.0},  //                           -####-
         {hScrollbar.x()    + 1.0, hScrollbar.y()         , 0.0, 1.0},  //                           -####-
         {hScrollbar.x()         , hScrollbar.maxY() - 1.0, 0.0, 1.0},  //B                          -####-
         {hScrollbar.maxX()      , hScrollbar.maxY() - 1.0, 0.0, 1.0},  //                           -####-
         {hScrollbar.maxX()      , hScrollbar.y()    + 1.0, 0.0, 1.0},  //                           -####-
         {hScrollbar.x()         , hScrollbar.y()    + 1.0, 0.0, 1.0},  //                           -####-
         {hScrollbar.x()         , hScrollbar.maxY()      , 0.0, 1.0},  //C                          -####-
         {hScrollbar.maxX()      , hScrollbar.maxY()      , 0.0, 1.0},  //                           -####-
         {hScrollbar.maxX()      , hScrollbar.y()         , 0.0, 1.0},  //                           -####-
         {hScrollbar.x()         , hScrollbar.y()         , 0.0, 1.0},  //                           E####-
         {vScrollbar.x()    + 1.0, vScrollbar.maxY()      , 0.0, 1.0},  //D                          -####-
         {vScrollbar.maxX() - 1.0, vScrollbar.maxY()      , 0.0, 1.0},  //                           -####-
         {vScrollbar.maxX() - 1.0, vScrollbar.y()         , 0.0, 1.0},  //                           -####-
         {vScrollbar.x()    + 1.0, vScrollbar.y()         , 0.0, 1.0},  //                           -####-
         {vScrollbar.x()         , vScrollbar.maxY() - 1.0, 0.0, 1.0},  //E                          -####-
         {vScrollbar.maxX()      , vScrollbar.maxY() - 1.0, 0.0, 1.0},  //                           -####-
         {vScrollbar.maxX()      , vScrollbar.y()    + 1.0, 0.0, 1.0},  //                           -####-
         {vScrollbar.x()         , vScrollbar.y()    + 1.0, 0.0, 1.0},  //   C|||||||||A||||||||||.  -####-
         {vScrollbar.x()         , vScrollbar.maxY()      , 0.0, 1.0},  //F  B####################-  -####-
         {vScrollbar.maxX()      , vScrollbar.maxY()      , 0.0, 1.0},  //   -####################-  -####-
         {vScrollbar.maxX()      , vScrollbar.y()         , 0.0, 1.0},  //   .||||||||||||||||||||.  .||||.
         {vScrollbar.x()         , vScrollbar.y()         , 0.0, 1.0}}; //

    Platform3DObject vertexBuffer = context->createBuffer();
    GLC(context, context->bindBuffer(GraphicsContext3D::ARRAY_BUFFER, vertexBuffer));
    GLC(context, context->bufferData(GraphicsContext3D::ARRAY_BUFFER, sizeof(float[24][4]), vertices, GraphicsContext3D::STREAM_DRAW));
    GLC(context, context->vertexAttribPointer(0, 4, GraphicsContext3D::FLOAT, false, 0, 0));
    GLC(context, context->enableVertexAttribArray(0));

    GLC(context, context->uniform4f(program->fragmentShader().colorLocation(), 0.5, 0.5, 0.5, 0.3 * opacity));
    GLC(context, context->drawArrays(GraphicsContext3D::TRIANGLE_FAN, 0, 4));
    GLC(context, context->drawArrays(GraphicsContext3D::TRIANGLE_FAN, 4, 4));
    GLC(context, context->drawArrays(GraphicsContext3D::TRIANGLE_FAN, 12, 4));
    GLC(context, context->drawArrays(GraphicsContext3D::TRIANGLE_FAN, 16, 4));

    GLC(context, context->uniform4f(program->fragmentShader().colorLocation(), 0.5, 0.5, 0.5, 0.06 * opacity));
    GLC(context, context->drawArrays(GraphicsContext3D::TRIANGLE_FAN, 8, 4));
    GLC(context, context->drawArrays(GraphicsContext3D::TRIANGLE_FAN, 20, 4));

    GLC(context, context->bindBuffer(GraphicsContext3D::ARRAY_BUFFER, 0));
    GLC(context, context->deleteBuffer(vertexBuffer));

    GLC(context, context->disable(GraphicsContext3D::BLEND));
}

bool CCScrollbarAndroid::drawScrollbarOverlay(LayerRendererChromium* layerRenderer, double timestamp)
{
    // FIXME: to support sublayers after scrollable layer for overflow has been landed.
    CCLayerImpl* scrollLayer = layerRenderer->scrollLayer();
    CCLayerImpl* drawLayer = scrollLayer ? scrollLayer->parent() : 0;
    if (!scrollLayer || !drawLayer)
        return false;

    // the creating should eventually go to the TreeSynchronizer maybe?
    if (!drawLayer->scrollbarAndroid())
        drawLayer->setScrollbarAndroid(adoptPtr(new CCScrollbarAndroid(scrollLayer, drawLayer)));

    // and the updating should go to CCLayerImpl maybe?
    CCScrollbarAndroid *scrollbar = drawLayer->scrollbarAndroid();
    scrollbar->updateScrollbarRect(scrollLayer, drawLayer, timestamp);

    // and the rendering should go to CCLayerImpl::draw() as a part of regular drawing.
    scrollbar->draw(layerRenderer, layerRenderer->projectionMatrix() * drawLayer->screenSpaceTransform(), drawLayer->bounds(), timestamp);

    return scrollbar->needsAnimation(timestamp);
}

float CCScrollbarAndroid::opacityAtTime(double timestamp) const
{
    static const double hideDelay = 0.3;
    static const double fadeoutLen = 0.3;

    double delta = timestamp - m_lastAwakenTime;

    if (delta <= hideDelay)
        return 1.0;
    else if (delta < hideDelay + fadeoutLen)
        return (hideDelay + fadeoutLen - delta) / fadeoutLen;
    else
        return 0.0;
}

void CCScrollbarAndroid::updateScrollbarRect(const CCLayerImpl* scrollLayer, const CCLayerImpl* drawLayer, double timestamp)
{
    FloatRect newRect = computeScrollbarRect(scrollLayer, drawLayer);
    FloatPoint scrollPosition = scrollLayer->scrollPosition() + scrollLayer->scrollDelta();

    // We try to avoid scrollbar showing too aggressively during page loading. Only show it if
    // 1. The scroll position actually changed (by link anchor or Javascript, etc...)
    // 2. or the scrollbar is not completely fade-out yet and its shape changed
    if (m_lastScrollPosition != scrollPosition || (m_lastScrollbarRect != newRect && opacityAtTime(timestamp)))
        m_lastAwakenTime = timestamp;
    m_lastScrollbarRect = newRect;
    m_lastScrollPosition = scrollPosition;
}

static IntSize contentSize(const CCLayerImpl* layer) {
    // copied from CCLayerTreeHostImpl
    // TODO(aelias): Hardcoding the first child here is weird. Think of
    // a cleaner way to get the contentBounds on the Impl side.

    if (!layer->children().size())
        return IntSize();
    return layer->children()[0]->contentBounds();
}

FloatRect CCScrollbarAndroid::computeScrollbarRect(const CCLayerImpl* scrollLayer, const CCLayerImpl* drawLayer) const {
    float scaleDelta = scrollLayer->pageScaleDelta();

    IntSize size = contentSize(scrollLayer);
    size.scale(scaleDelta);

    FloatPoint lt = scrollLayer->scrollPosition() + scrollLayer->scrollDelta();
    lt.scale(scaleDelta, scaleDelta);
    FloatRect viewport(lt, drawLayer->bounds());
    viewport.scale(1.0 / size.width(), 1.0 / size.height());
    viewport.intersect(FloatRect(0.0, 0.0, 1.0, 1.0));

    return viewport;
}

} // namespace WebCore
