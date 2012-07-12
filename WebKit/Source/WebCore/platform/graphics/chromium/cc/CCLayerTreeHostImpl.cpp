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

#include "config.h"

#include "cc/CCLayerTreeHostImpl.h"

#include "Extensions3D.h"
#include "GraphicsContext3D.h"
#include "LayerRendererChromium.h"
#include "TraceEvent.h"
#include "cc/CCCompletionEvent.h"
#include "cc/CCDamageTracker.h"
#include "cc/CCDelayBasedTimeSource.h"
#include "cc/CCLayerIterator.h"
#include "cc/CCLayerTreeHost.h"
#include "cc/CCLayerTreeHostCommon.h"
#include "cc/CCPageScaleAnimation.h"
#include "cc/CCRenderSurfaceDrawQuad.h"
#include "cc/CCThreadTask.h"
#include <wtf/CurrentTime.h>

#if OS(ANDROID)
#include "cc/CCTimer.h"
#endif

namespace {
const double lowFrequencyAnimationInterval = 1;
} // namespace

namespace WebCore {

#if OS(ANDROID)
class UpdateHighlight : public CCTimerClient {
public:
    explicit UpdateHighlight(CCLayerTreeHostImpl* layerTreeHostImpl)
        : m_ccLayerTreeHostImpl(layerTreeHostImpl) { }
    virtual ~UpdateHighlight() { }

    virtual void onTimerFired() { m_ccLayerTreeHostImpl->setNeedsRedraw(); }
private:
    CCLayerTreeHostImpl* m_ccLayerTreeHostImpl;
};
#endif

class CCLayerTreeHostImplTimeSourceAdapter : public CCTimeSourceClient {
    WTF_MAKE_NONCOPYABLE(CCLayerTreeHostImplTimeSourceAdapter);
public:
    static PassOwnPtr<CCLayerTreeHostImplTimeSourceAdapter> create(CCLayerTreeHostImpl* layerTreeHostImpl, PassRefPtr<CCDelayBasedTimeSource> timeSource)
    {
        return adoptPtr(new CCLayerTreeHostImplTimeSourceAdapter(layerTreeHostImpl, timeSource));
    }
    virtual ~CCLayerTreeHostImplTimeSourceAdapter()
    {
        m_timeSource->setClient(0);
        m_timeSource->setActive(false);
    }

    virtual void onTimerTick()
    {
        m_layerTreeHostImpl->animate(monotonicallyIncreasingTime(), currentTime());
    }

    void setActive(bool active)
    {
        if (active != m_timeSource->active())
            m_timeSource->setActive(active);
    }

private:
    CCLayerTreeHostImplTimeSourceAdapter(CCLayerTreeHostImpl* layerTreeHostImpl, PassRefPtr<CCDelayBasedTimeSource> timeSource)
        : m_layerTreeHostImpl(layerTreeHostImpl)
        , m_timeSource(timeSource)
    {
        m_timeSource->setClient(this);
    }

    CCLayerTreeHostImpl* m_layerTreeHostImpl;
    RefPtr<CCDelayBasedTimeSource> m_timeSource;
};

PassOwnPtr<CCLayerTreeHostImpl> CCLayerTreeHostImpl::create(const CCSettings& settings, CCLayerTreeHostImplClient* client)
{
    return adoptPtr(new CCLayerTreeHostImpl(settings, client));
}

CCLayerTreeHostImpl::CCLayerTreeHostImpl(const CCSettings& settings, CCLayerTreeHostImplClient* client)
    : m_client(client)
    , m_sourceFrameNumber(-1)
    , m_frameNumber(0)
    , m_settings(settings)
#if OS(ANDROID)
    , m_highlightTimeout(0)
#endif
    , m_visible(true)
    , m_haveWheelEventHandlers(false)
    , m_pageScale(1)
    , m_pageScaleDelta(1)
    , m_sentPageScaleDelta(1)
    , m_minPageScale(0)
    , m_maxPageScale(0)
    , m_needsAnimateLayers(false)
    , m_pinchGestureActive(false)
    , m_timeSourceClientAdapter(CCLayerTreeHostImplTimeSourceAdapter::create(this, CCDelayBasedTimeSource::create(lowFrequencyAnimationInterval * 1000.0, CCProxy::currentThread())))
{
    ASSERT(CCProxy::isImplThread());
#if OS(ANDROID)
    m_highlightUpdateTimer = adoptPtr(new CCTimer(CCProxy::implThread(), new UpdateHighlight(this)));
#endif
}

CCLayerTreeHostImpl::~CCLayerTreeHostImpl()
{
    ASSERT(CCProxy::isImplThread());
    TRACE_EVENT("CCLayerTreeHostImpl::~CCLayerTreeHostImpl()", this, 0);
    if (m_layerRenderer)
        m_layerRenderer->close();
}

void CCLayerTreeHostImpl::beginCommit()
{
}

void CCLayerTreeHostImpl::commitComplete()
{
    // Recompute max scroll position; must be after layer content bounds are
    // updated.
    updateMaxScrollPosition();
}

bool CCLayerTreeHostImpl::canDraw()
{
    if (!rootLayer())
        return false;
    if (viewportSize().isEmpty())
        return false;
    return true;
}

GraphicsContext3D* CCLayerTreeHostImpl::context()
{
    return m_layerRenderer ? m_layerRenderer->context() : 0;
}

void CCLayerTreeHostImpl::animate(double monotonicTime, double wallClockTime)
{
    animatePageScale(monotonicTime);
    animateLayers(monotonicTime, wallClockTime);
}

void CCLayerTreeHostImpl::startPageScaleAnimation(const IntSize& targetPosition, bool anchorPoint, float pageScale, double startTime, double duration)
{
    if (!m_scrollLayerImpl)
        return;

    IntSize scrollTotal = toSize(m_scrollLayerImpl->scrollPosition() + flooredIntPoint(m_scrollLayerImpl->scrollDelta()));
    scrollTotal.scale(m_pageScaleDelta);
    float scaleTotal = m_pageScale * m_pageScaleDelta;
    IntSize scaledContentSize = contentSize();
    scaledContentSize.scale(m_pageScaleDelta);

    m_pageScaleAnimation = CCPageScaleAnimation::create(scrollTotal, scaleTotal, m_viewportSize, scaledContentSize, startTime);

    if (anchorPoint) {
        IntSize windowAnchor(targetPosition);
        windowAnchor.scale(scaleTotal / pageScale);
        windowAnchor -= scrollTotal;
        m_pageScaleAnimation->zoomWithAnchor(windowAnchor, pageScale, duration);
    } else
        m_pageScaleAnimation->zoomTo(targetPosition, pageScale, duration);

    m_client->setNeedsRedrawOnImplThread();
}

void CCLayerTreeHostImpl::scheduleAnimation()
{
    m_client->setNeedsRedrawOnImplThread();
}

void CCLayerTreeHostImpl::trackDamageForAllSurfaces(CCLayerImpl* rootDrawLayer, const CCLayerList& renderSurfaceLayerList)
{
    // For now, we use damage tracking to compute a global scissor. To do this, we must
    // compute all damage tracking before drawing anything, so that we know the root
    // damage rect. The root damage rect is then used to scissor each surface.

    for (int surfaceIndex = renderSurfaceLayerList.size() - 1; surfaceIndex >= 0 ; --surfaceIndex) {
        CCLayerImpl* renderSurfaceLayer = renderSurfaceLayerList[surfaceIndex].get();
        CCRenderSurface* renderSurface = renderSurfaceLayer->renderSurface();
        ASSERT(renderSurface);
        renderSurface->damageTracker()->updateDamageRectForNextFrame(renderSurface->layerList(), renderSurfaceLayer->id(), renderSurfaceLayer->maskLayer());
    }
}

static TransformationMatrix computeScreenSpaceTransformForSurface(CCLayerImpl* renderSurfaceLayer)
{
    // The layer's screen space transform can be written as:
    //   layerScreenSpaceTransform = surfaceScreenSpaceTransform * layerOriginTransform
    // So, to compute the surface screen space, we can do:
    //   surfaceScreenSpaceTransform = layerScreenSpaceTransform * inverse(layerOriginTransform)

    TransformationMatrix layerOriginTransform = renderSurfaceLayer->drawTransform();
    layerOriginTransform.translate(-0.5 * renderSurfaceLayer->bounds().width(), -0.5 * renderSurfaceLayer->bounds().height());
    TransformationMatrix surfaceScreenSpaceTransform = renderSurfaceLayer->screenSpaceTransform();
    surfaceScreenSpaceTransform.multiply(layerOriginTransform.inverse());

    return surfaceScreenSpaceTransform;
}

static FloatRect damageInSurfaceSpace(CCLayerImpl* renderSurfaceLayer, const FloatRect& rootDamageRect)
{
    FloatRect surfaceDamageRect;
    // For now, we conservatively use the root damage as the damage for
    // all surfaces, except perspective transforms.
    TransformationMatrix screenSpaceTransform = computeScreenSpaceTransformForSurface(renderSurfaceLayer);
    if (screenSpaceTransform.hasPerspective()) {
        // Perspective projections do not play nice with mapRect of
        // inverse transforms. In this uncommon case, its simpler to
        // just redraw the entire surface.
        // FIXME: use calculateVisibleRect to handle projections.
        CCRenderSurface* renderSurface = renderSurfaceLayer->renderSurface();
        surfaceDamageRect = renderSurface->contentRect();
    } else {
        TransformationMatrix inverseScreenSpaceTransform = screenSpaceTransform.inverse();
        surfaceDamageRect = inverseScreenSpaceTransform.mapRect(rootDamageRect);
    }
    return surfaceDamageRect;
}

void CCLayerTreeHostImpl::calculateRenderSurfaces(CCLayerList& renderSurfaceLayerList)
{
    renderSurfaceLayerList.append(rootLayer());

    if (!rootLayer()->renderSurface())
        rootLayer()->createRenderSurface();
    rootLayer()->renderSurface()->clearLayerList();
    rootLayer()->renderSurface()->setContentRect(IntRect(IntPoint(), viewportSize()));

    rootLayer()->setClipRect(IntRect(IntPoint(), viewportSize()));

    // During testing we may not have an active renderer.
    const int kDefaultMaxTextureSize = 256;
    int maxTextureSize = m_layerRenderer ? layerRendererCapabilities().maxTextureSize : kDefaultMaxTextureSize;

    {
        TransformationMatrix identityMatrix;
        TRACE_EVENT("CCLayerTreeHostImpl::calcDrawEtc", this, 0);
        CCLayerTreeHostCommon::calculateDrawTransformsAndVisibility(rootLayer(), rootLayer(), identityMatrix, identityMatrix, renderSurfaceLayerList, rootLayer()->renderSurface()->layerList(), &m_layerSorter, maxTextureSize);
    }
}

void CCLayerTreeHostImpl::calculateRenderPasses(CCRenderPassList& passes)
{
    CCLayerList renderSurfaceLayerList;
    calculateRenderSurfaces(renderSurfaceLayerList);

    if (layerRendererCapabilities().usingPartialSwap)
        trackDamageForAllSurfaces(rootLayer(), renderSurfaceLayerList);
    m_rootDamageRect = rootLayer()->renderSurface()->damageTracker()->currentDamageRect();

    for (int surfaceIndex = renderSurfaceLayerList.size() - 1; surfaceIndex >= 0 ; --surfaceIndex) {
        CCLayerImpl* renderSurfaceLayer = renderSurfaceLayerList[surfaceIndex].get();
        CCRenderSurface* renderSurface = renderSurfaceLayer->renderSurface();

        OwnPtr<CCRenderPass> pass = CCRenderPass::create(renderSurface);

        FloatRect surfaceDamageRect;
        if (layerRendererCapabilities().usingPartialSwap)
            surfaceDamageRect = damageInSurfaceSpace(renderSurfaceLayer, m_rootDamageRect);
        pass->setSurfaceDamageRect(surfaceDamageRect);

        const CCLayerList& layerList = renderSurface->layerList();
        for (unsigned layerIndex = 0; layerIndex < layerList.size(); ++layerIndex) {
            CCLayerImpl* layer = layerList[layerIndex].get();
            if (layer->visibleLayerRect().isEmpty())
                continue;

            if (CCLayerTreeHostCommon::renderSurfaceContributesToTarget(layer, renderSurfaceLayer->id())) {
                pass->appendQuadsForRenderSurfaceLayer(layer);
                continue;
            }

            pass->appendQuadsForLayer(layer);
        }

        passes.append(pass.release());
    }
}

void CCLayerTreeHostImpl::optimizeRenderPasses(CCRenderPassList& passes)
{
    for (unsigned i = 0; i < passes.size(); ++i)
        passes[i]->optimizeQuads();
}

void CCLayerTreeHostImpl::animateLayersRecursive(CCLayerImpl* current, double monotonicTime, double wallClockTime, CCAnimationEventsVector* events, bool& didAnimate, bool& needsAnimateLayers)
{
    bool subtreeNeedsAnimateLayers = false;

    CCLayerAnimationController* currentController = current->layerAnimationController();

    bool hadActiveAnimation = currentController->hasActiveAnimation();
    // FIXME: Android requires an extra tick to start animations,
    // to synchronize with the GPU Process. "animate" now ticks animations
    // twice to fix many unit tests. animateForReal ticks once.
    currentController->animateForReal(monotonicTime, events);
    bool startedAnimation = events->size() > 0;

    // We animated if we either ticked a running animation, or started a new animation.
    if (hadActiveAnimation || startedAnimation)
        didAnimate = true;

    // If the current controller still has an active animation, we must continue animating layers.
    if (currentController->hasActiveAnimation())
         subtreeNeedsAnimateLayers = true;

    for (size_t i = 0; i < current->children().size(); ++i) {
        bool childNeedsAnimateLayers = false;
        animateLayersRecursive(current->children()[i].get(), monotonicTime, wallClockTime, events, didAnimate, childNeedsAnimateLayers);
        if (childNeedsAnimateLayers)
            subtreeNeedsAnimateLayers = true;
    }

    needsAnimateLayers = subtreeNeedsAnimateLayers;
}

IntSize CCLayerTreeHostImpl::contentSize() const
{
    // TODO(aelias): Hardcoding the first child here is weird. Think of
    // a cleaner way to get the contentBounds on the Impl side.
    if (!m_scrollLayerImpl || m_scrollLayerImpl->children().isEmpty())
        return IntSize();
    return m_scrollLayerImpl->children()[0]->contentBounds();
}

void CCLayerTreeHostImpl::drawLayers()
{
    TRACE_EVENT("CCLayerTreeHostImpl::drawLayers", this, 0);
    ASSERT(m_layerRenderer);

    if (!rootLayer())
        return;

    CCRenderPassList passes;
    calculateRenderPasses(passes);

    m_layerRenderer->beginDrawingFrame();
    for (size_t i = 0; i < passes.size(); ++i)
        m_layerRenderer->drawRenderPass(passes[i].get());
    m_layerRenderer->finishDrawingFrame();

    ++m_frameNumber;

    // The next frame should start by assuming nothing has changed, and changes are noted as they occur.
    rootLayer()->resetAllChangeTrackingForSubtree();
}

void CCLayerTreeHostImpl::finishAllRendering()
{
    m_layerRenderer->finish();
}

bool CCLayerTreeHostImpl::isContextLost()
{
    ASSERT(m_layerRenderer);
    return m_layerRenderer->isContextLost();
}

const LayerRendererCapabilities& CCLayerTreeHostImpl::layerRendererCapabilities() const
{
    return m_layerRenderer->capabilities();
}

TextureAllocator* CCLayerTreeHostImpl::contentsTextureAllocator() const
{
    return m_layerRenderer ? m_layerRenderer->contentsTextureAllocator() : 0;
}

void CCLayerTreeHostImpl::swapBuffers()
{
    ASSERT(m_layerRenderer && !isContextLost());
    m_layerRenderer->swapBuffers(enclosingIntRect(m_rootDamageRect));
}

void CCLayerTreeHostImpl::onSwapBuffersComplete()
{
    m_client->onSwapBuffersCompleteOnImplThread();
}

void CCLayerTreeHostImpl::readback(void* pixels, const IntRect& rect)
{
    ASSERT(m_layerRenderer && !isContextLost());
    m_layerRenderer->getFramebufferPixels(pixels, rect);
}

static CCLayerImpl* findScrollLayer(CCLayerImpl* layer)
{
    if (!layer)
        return 0;

    if (layer->scrollable())
        return layer;

    for (size_t i = 0; i < layer->children().size(); ++i) {
        CCLayerImpl* found = findScrollLayer(layer->children()[i].get());
        if (found)
            return found;
    }

    return 0;
}

void CCLayerTreeHostImpl::setRootLayer(PassRefPtr<CCLayerImpl> layer)
{
    m_rootLayerImpl = layer;
    m_scrollLayerImpl = findScrollLayer(m_rootLayerImpl.get());

    if (m_currentlyScrollingLayerImpl) {
        if (!m_rootLayerImpl || !m_rootLayerImpl->isLayerInDescendants(m_currentlyScrollingLayerImpl->id()))
            m_currentlyScrollingLayerImpl.clear();
    }
}

void CCLayerTreeHostImpl::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    TRACE_EVENT("CCLayerTreeHostImpl::setVisible", this, visible ? "true" : "false");
    m_visible = visible;

    if (m_layerRenderer)
        m_layerRenderer->setVisible(visible);

    const bool shouldTickInBackground = !visible && m_needsAnimateLayers;
    m_timeSourceClientAdapter->setActive(shouldTickInBackground);
}

bool CCLayerTreeHostImpl::initializeLayerRenderer(PassRefPtr<GraphicsContext3D> context)
{
    OwnPtr<LayerRendererChromium> layerRenderer;
    layerRenderer = LayerRendererChromium::create(this, context);

    if (m_layerRenderer)
        m_layerRenderer->close();

    m_layerRenderer = layerRenderer.release();
    return m_layerRenderer;
}

void CCLayerTreeHostImpl::setViewportSize(const IntSize& viewportSize)
{
    if (viewportSize == m_viewportSize)
        return;

    m_viewportSize = viewportSize;
    updateMaxScrollPosition();

    if (m_layerRenderer)
        m_layerRenderer->viewportChanged();
}

void CCLayerTreeHostImpl::setPageScaleFactorAndLimits(float pageScale, float minPageScale, float maxPageScale)
{
    if (!pageScale)
        return;

    if (m_sentPageScaleDelta == 1 && pageScale == m_pageScale && minPageScale == m_minPageScale && maxPageScale == m_maxPageScale)
        return;

    m_minPageScale = minPageScale;
    m_maxPageScale = maxPageScale;

    float pageScaleChange = pageScale / m_pageScale;
    m_pageScale = pageScale;

    adjustScrollsForPageScaleChange(pageScaleChange);

    // Clamp delta to limits and refresh display matrix.
    setPageScaleDelta(m_pageScaleDelta / m_sentPageScaleDelta);
    m_sentPageScaleDelta = 1;
    applyPageScaleDeltaToScrollLayer();
}

void CCLayerTreeHostImpl::adjustScrollsForPageScaleChange(float pageScaleChange)
{
    if (!m_scrollLayerImpl || pageScaleChange == 1)
        return;

    // We also need to convert impl-side scroll delta for the root layer to pageScale space.
    FloatSize scrollDelta = m_scrollLayerImpl->scrollDelta();
    scrollDelta.scale(pageScaleChange);
    m_scrollLayerImpl->setScrollDelta(scrollDelta);
}

void CCLayerTreeHostImpl::setPageScaleDelta(float delta)
{
    // Clamp to the current min/max limits.
    float finalMagnifyScale = m_pageScale * delta;
    if (m_minPageScale && finalMagnifyScale < m_minPageScale)
        delta = m_minPageScale / m_pageScale;
    else if (m_maxPageScale && finalMagnifyScale > m_maxPageScale)
        delta = m_maxPageScale / m_pageScale;

    if (delta == m_pageScaleDelta)
        return;

    m_pageScaleDelta = delta;

    updateMaxScrollPosition();
    applyPageScaleDeltaToScrollLayer();
}

void CCLayerTreeHostImpl::applyPageScaleDeltaToScrollLayer()
{
    if (m_scrollLayerImpl)
        m_scrollLayerImpl->setPageScaleDelta(m_pageScaleDelta);
}

void CCLayerTreeHostImpl::updateMaxScrollPosition()
{
    if (!m_scrollLayerImpl || !m_scrollLayerImpl->children().size())
        return;

    FloatSize viewBounds = m_viewportSize;
    viewBounds.scale(1 / m_pageScaleDelta);

    IntSize maxScroll = contentSize() - expandedIntSize(viewBounds);
    // The viewport may be larger than the contents in some cases, such as
    // having a vertical scrollbar but no horizontal overflow.
    maxScroll.clampNegativeToZero();

    // We only need to update the root layer scroll range, since the child
    // layers use unscaled scroll coordinates.
    m_scrollLayerImpl->setMaxScrollPosition(maxScroll);
}

void CCLayerTreeHostImpl::didVSync(double frameBeginMonotonic, double currentFrameIntervalInSec)
{
    m_client->didVSyncOnImplThread(frameBeginMonotonic, currentFrameIntervalInSec);
}

void CCLayerTreeHostImpl::setNeedsRedraw()
{
    m_client->setNeedsRedrawOnImplThread();
}

bool CCLayerTreeHostImpl::isContentPointWithinLayer(CCLayerImpl* layerImpl, const IntPoint& contentPoint) const
{
    IntRect layerContentRect(layerImpl->visibleLayerRect());
    // The visible layer rect is in scaled coordinates, so undo the page scale unless it is the
    // non-composited content where the content point is also in scaled coordinates. Note that
    // there is no need to undo the page scale delta, because the layer content rect is only
    // scaled with the original page scale.
    if (!layerImpl->isNonCompositedContent())
        layerContentRect.scale(1 / m_pageScale);
    return layerContentRect.contains(contentPoint);
}

bool CCLayerTreeHostImpl::isInsideInputEventRegionRecursive(CCLayerImpl* layerImpl, const IntPoint& viewportPoint) const
{
    for (size_t i = 0; i < layerImpl->children().size(); i++)
        if (isInsideInputEventRegionRecursive(layerImpl->children()[i].get(), viewportPoint))
            return true;

    if (!layerImpl->screenSpaceTransform().isInvertible())
        return false;

    IntPoint contentPoint(layerImpl->screenSpaceTransform().inverse().mapPoint(viewportPoint));
    if (layerImpl->drawsContent() && !isContentPointWithinLayer(layerImpl, contentPoint))
        return false;

    if (layerImpl->isInsideInputEventRegion(contentPoint))
        return true;

    return false;
}

CCInputHandlerClient::ScrollStatus CCLayerTreeHostImpl::beginScrollingLayer(CCLayerImpl* layerImpl, const IntPoint& viewportPoint)
{
    if (!layerImpl)
        return CCInputHandlerClient::ScrollFailed;

    if (!layerImpl->screenSpaceTransform().isInvertible())
        return CCInputHandlerClient::ScrollFailed;

    IntPoint contentPoint(layerImpl->screenSpaceTransform().inverse().mapPoint(viewportPoint));
    if (layerImpl->drawsContent() && !isContentPointWithinLayer(layerImpl, contentPoint))
        return CCInputHandlerClient::ScrollFailed;

    if (!layerImpl->scrollable())
        return CCInputHandlerClient::ScrollFailed;

    m_currentlyScrollingLayerImpl = layerImpl;
    return CCInputHandlerClient::ScrollStarted;
}

CCInputHandlerClient::ScrollStatus CCLayerTreeHostImpl::scrollBegin(const IntPoint& viewportPoint)
{
    m_currentlyScrollingLayerImpl.clear();

    if (!rootLayer())
        return CCInputHandlerClient::ScrollIgnored;

    // If the point is within any input event region, we must delegate it to the main thread.
    // This is because even though there was a scrollable layer under the query point, we might
    // need to move any of its ancestors during scrolling. If one of those ancestors is a
    // input event region, we would need to transition to scrolling it in the main thread, which
    // is currently not possible.
    if (isInsideInputEventRegionRecursive(rootLayer(), viewportPoint))
        return CCInputHandlerClient::ScrollFailed;

    // Look for a scrollable layer in front to back order.
    typedef CCLayerIterator<CCLayerImpl, CCRenderSurface, CCLayerIteratorActions::FrontToBack> CCLayerIteratorType;
    CCLayerList renderSurfaceLayerList;
    calculateRenderSurfaces(renderSurfaceLayerList);

    CCLayerIteratorType end = CCLayerIteratorType::end(&renderSurfaceLayerList);
    for (CCLayerIteratorType it = CCLayerIteratorType::begin(&renderSurfaceLayerList); it != end; ++it) {
        CCLayerImpl* layerImpl = *it;
        // A non-composited content layer should be scrolled via the root scroll layer.
        if (layerImpl->isNonCompositedContent())
            layerImpl = m_scrollLayerImpl.get();
        CCInputHandlerClient::ScrollStatus status = beginScrollingLayer(layerImpl, viewportPoint);
        if (status != CCInputHandlerClient::ScrollFailed)
            return status;
    }
    return CCInputHandlerClient::ScrollIgnored;
}

void CCLayerTreeHostImpl::scrollBy(const IntSize& scrollDelta)
{
    TRACE_EVENT("CCLayerTreeHostImpl::scrollBy", this, 0);
    if (!m_currentlyScrollingLayerImpl)
        return;

    IntSize pendingDelta(scrollDelta);
    CCLayerImpl* layerImpl = m_currentlyScrollingLayerImpl.get();
    while (layerImpl && !pendingDelta.isZero()) {
        if (layerImpl->scrollable()) {
            FloatSize previousDelta(layerImpl->scrollDelta());
            FloatSize scaledPendingDelta(pendingDelta);
            // Since scrollDelta is in window coordinates, it already has the page scale applied.
            // This matches what the root scroll layer expects, but child layers are scrolled using
            // unscaled content coordinates instead, so we have to undo the scaling for them. The
            // page scale delta needs to be unapplied with both layer types since the scroll
            // coordinates do not respect it.
            if (layerImpl == m_scrollLayerImpl) {
                scaledPendingDelta.scale(1 / m_pageScaleDelta);
                layerImpl->scrollBy(scaledPendingDelta);
            } else {
                scaledPendingDelta.scale(1 / (m_pageScale * m_pageScaleDelta));
                layerImpl->scrollBy(scaledPendingDelta);
            }
            // Reset the pending scroll delta to zero if the layer was able to move along the requested
            // axis. This is to ensure it is possible to scroll exactly to the beginning or end of a
            // scroll area regardless of the scroll step. For diagonal scrolls this also avoids applying
            // the scroll on one axis to multiple layers.
            if (previousDelta.width() != layerImpl->scrollDelta().width())
                pendingDelta.setWidth(0);
            if (previousDelta.height() != layerImpl->scrollDelta().height())
                pendingDelta.setHeight(0);
            if (!layerImpl->allowScrollingAncestors())
                break;
        }
        layerImpl = layerImpl->parent();
    }

    if (pendingDelta != scrollDelta) {
        m_client->setNeedsCommitOnImplThread();
        m_client->setNeedsRedrawOnImplThread();
    }
}

void CCLayerTreeHostImpl::scrollEnd()
{
    m_currentlyScrollingLayerImpl.clear();
}

#if OS(ANDROID)
bool CCLayerTreeHostImpl::isScrolling() const
{
    return m_currentlyScrollingLayerImpl;
}

static bool isValidScrollRange(const IntRect& scrollRange)
{
    return scrollRange.x() < 0 && scrollRange.y() < 0 && scrollRange.maxX() > 0 && scrollRange.maxY() > 0;
}

static void expandScrollRange(IntRect& scrollRange, float scale, IntPoint scrollPosition, IntSize maxScrollPosition)
{
    // TODO: Merge this with the same code in WebViewImpl.
    scrollPosition.scale(scale, scale);
    maxScrollPosition.scale(scale);
    scrollPosition.clampNegativeToZero();
    maxScrollPosition = maxScrollPosition.expandedTo(toSize(scrollPosition));
    // Only expand the scroll range along an axis if there previously was no room to scroll in that direction.
    // This is to ensure that flings do not overshoot the boundaries of a scrollable element and start scrolling
    // its parent instead.
    if (!scrollRange.x()) {
        scrollRange.setX(-scrollPosition.x());
        scrollRange.setWidth(scrollRange.width() + scrollPosition.x());
    }
    if (!scrollRange.y()) {
        scrollRange.setY(-scrollPosition.y());
        scrollRange.setHeight(scrollRange.height() + scrollPosition.y());
    }
    if (!scrollRange.maxX())
        scrollRange.setWidth(maxScrollPosition.width() - scrollPosition.x() - scrollRange.x());
    if (!scrollRange.maxY())
        scrollRange.setHeight(maxScrollPosition.height() - scrollPosition.y() - scrollRange.y());
    ASSERT(scrollRange.x() <= 0 && scrollRange.maxX() >= 0);
    ASSERT(scrollRange.y() <= 0 && scrollRange.maxY() >= 0);
}

IntRect CCLayerTreeHostImpl::scrollRange() const
{
    const CCLayerImpl* layerImpl = m_currentlyScrollingLayerImpl.get();
    IntRect scrollRange;

    while (layerImpl && !isValidScrollRange(scrollRange)) {
        if (!layerImpl->scrollable()) {
            layerImpl = layerImpl->parent();
            continue;
        }
        IntPoint scrollPosition(flooredIntPoint(layerImpl->scrollPosition() + layerImpl->scrollDelta()));
        // Sublayer scroll deltas are scaled with the page scale. See scrollBy().
        if (layerImpl == m_scrollLayerImpl)
            expandScrollRange(scrollRange, m_pageScaleDelta, scrollPosition, layerImpl->maxScrollPosition());
        else
            expandScrollRange(scrollRange, m_pageScaleDelta * m_pageScale, scrollPosition, layerImpl->maxScrollPosition());
        if (!layerImpl->allowScrollingAncestors())
              break;
        layerImpl = layerImpl->parent();
    }
    return scrollRange;
}
#endif // OS(ANDROID)

bool CCLayerTreeHostImpl::haveWheelEventHandlers()
{
    return m_haveWheelEventHandlers;
}

void CCLayerTreeHostImpl::pinchGestureBegin()
{
    m_pinchGestureActive = true;
    m_prevPinchAnchor = IntPoint();
}

void CCLayerTreeHostImpl::pinchGestureUpdate(float magnifyDelta,
                                             const IntPoint& anchor)
{
    TRACE_EVENT("CCLayerTreeHostImpl::pinchGestureUpdate", this, 0);

    if (!m_scrollLayerImpl)
        return;

    if (m_prevPinchAnchor == IntPoint())
        m_prevPinchAnchor = anchor;

    // Keep the center-of-pinch anchor specified by (x, y) in a stable
    // position over the course of the magnify.
    FloatPoint prevScaleAnchor(m_prevPinchAnchor.x() / m_pageScaleDelta, m_prevPinchAnchor.y() / m_pageScaleDelta);
    setPageScaleDelta(m_pageScaleDelta * magnifyDelta);
    FloatPoint newScaleAnchor(anchor.x() / m_pageScaleDelta, anchor.y() / m_pageScaleDelta);
    FloatSize move = prevScaleAnchor - newScaleAnchor;

    m_prevPinchAnchor = anchor;

    m_scrollLayerImpl->scrollBy(roundedIntSize(move));
    m_client->setNeedsCommitOnImplThread();
    m_client->setNeedsRedrawOnImplThread();
}

void CCLayerTreeHostImpl::pinchGestureEnd()
{
    m_pinchGestureActive = false;

    m_client->setNeedsCommitOnImplThread();
}

bool CCLayerTreeHostImpl::isMagnifying() const
{
    return m_pinchGestureActive;
}

void CCLayerTreeHostImpl::computeDoubleTapZoomDeltas(CCScrollAndScaleSet* scrollInfo)
{
    float pageScale = m_pageScaleAnimation->finalPageScale();
    IntSize scrollOffset = m_pageScaleAnimation->finalScrollOffset();
    scrollOffset.scale(m_pageScale / pageScale);
    makeScrollAndScaleSet(scrollInfo, scrollOffset, pageScale);
}

void CCLayerTreeHostImpl::computePinchZoomDeltas(CCScrollAndScaleSet* scrollInfo)
{
    if (!m_scrollLayerImpl)
        return;

    // Only send fake scroll/zoom deltas if we're pinch zooming out. This also
    // ensures only one fake delta set will be sent.
    if (m_pageScaleDelta > 0.95)
        return;

    // Cmpute where the scroll offset/page scale would be if fully pinch-zoomed
    // out from the anchor point.
    FloatSize scrollBegin = FloatSize(toSize(m_scrollLayerImpl->scrollPosition())) + m_scrollLayerImpl->scrollDelta();
    scrollBegin.scale(m_pageScaleDelta);
    float scaleBegin = m_pageScale * m_pageScaleDelta;
    float pageScaleDeltaToSend = m_minPageScale / m_pageScale;
    FloatSize scaledContentsSize = m_scrollLayerImpl->children()[0]->contentBounds();
    scaledContentsSize.scale(pageScaleDeltaToSend);

    FloatSize anchor = toSize(m_prevPinchAnchor);
    FloatSize scrollEnd = scrollBegin + anchor;
    scrollEnd.scale(m_minPageScale / scaleBegin);
    scrollEnd -= anchor;
    scrollEnd = scrollEnd.shrunkTo(roundedIntSize(scaledContentsSize - m_viewportSize)).expandedTo(FloatSize(0, 0));
    scrollEnd.scale(1 / pageScaleDeltaToSend);

    makeScrollAndScaleSet(scrollInfo, roundedIntSize(scrollEnd), m_minPageScale);
}

void CCLayerTreeHostImpl::makeScrollAndScaleSet(CCScrollAndScaleSet* scrollInfo, const IntSize& scrollOffset, float pageScale)
{
    if (!m_scrollLayerImpl)
        return;

    scrollInfo->rootScrollDelta = scrollOffset - toSize(m_scrollLayerImpl->scrollPosition());
    m_scrollLayerImpl->setSentScrollDelta(scrollInfo->rootScrollDelta);
    m_sentPageScaleDelta = scrollInfo->pageScaleDelta = pageScale / m_pageScale;
}

static bool didScrollSubtree(CCLayerImpl* layerImpl)
{
    if (!layerImpl->scrollDelta().isZero())
        return true;

    for (size_t i = 0; i < layerImpl->children().size(); ++i)
        if (didScrollSubtree(layerImpl->children()[i].get()))
            return true;

    return false;
}

void CCLayerTreeHostImpl::collectScrollDeltas(CCScrollAndScaleSet* scrollInfo, CCLayerImpl* layerImpl) const
{
    if (!layerImpl->scrollDelta().isZero()) {
        IntSize scrollDelta = toSize(flooredIntPoint(layerImpl->scrollDelta()));
        if (layerImpl != m_scrollLayerImpl) {
            CCLayerTreeHostCommon::ScrollUpdateInfo scroll;
            scroll.layerId = layerImpl->id();
            scroll.scrollDelta = scrollDelta;
            scrollInfo->scrolls.append(scroll);
        } else
            scrollInfo->rootScrollDelta = scrollDelta;
        layerImpl->setSentScrollDelta(layerImpl->scrollDelta());
    }

    for (size_t i = 0; i < layerImpl->children().size(); ++i)
        collectScrollDeltas(scrollInfo, layerImpl->children()[i].get());
}

PassOwnPtr<CCScrollAndScaleSet> CCLayerTreeHostImpl::processScrollDeltas()
{
    OwnPtr<CCScrollAndScaleSet> scrollInfo = adoptPtr(new CCScrollAndScaleSet());
    bool didMove = m_scrollLayerImpl && (didScrollSubtree(m_scrollLayerImpl.get()) || m_pageScaleDelta != 1);
    if (!didMove || m_pinchGestureActive || m_pageScaleAnimation) {
        m_sentPageScaleDelta = scrollInfo->pageScaleDelta = 1;
        if (m_pinchGestureActive)
            computePinchZoomDeltas(scrollInfo.get());
        else if (m_pageScaleAnimation.get())
            computeDoubleTapZoomDeltas(scrollInfo.get());
        return scrollInfo.release();
    }

    collectScrollDeltas(scrollInfo.get(), m_scrollLayerImpl.get());
    m_sentPageScaleDelta = scrollInfo->pageScaleDelta = m_pageScaleDelta;

    return scrollInfo.release();
}

#if OS(ANDROID)
void CCLayerTreeHostImpl::setHighlight(const Vector<FloatQuad>& highlight, const Color& highlightColor)
{
    m_highlight = highlight;
    m_highlightColor = highlightColor;
    if (highlight.size()) {
        m_highlightTimeout = monotonicallyIncreasingTime() + highlightTimeoutMS * 0.001;
        m_highlightUpdateTimer->startOneShot(highlightTimeoutMS);
    }
}

void CCLayerTreeHostImpl::getHighlight(Vector<FloatQuad>& highlight) const
{
    if (monotonicallyIncreasingTime() > m_highlightTimeout)
        highlight = Vector<FloatQuad>();
    else
        highlight = m_highlight;
}
#endif

void CCLayerTreeHostImpl::animatePageScale(double monotonicTime)
{
    if (!m_pageScaleAnimation || !m_scrollLayerImpl)
        return;

    IntSize scrollTotal = toSize(m_scrollLayerImpl->scrollPosition() + flooredIntPoint(m_scrollLayerImpl->scrollDelta()));

    setPageScaleDelta(m_pageScaleAnimation->pageScaleAtTime(monotonicTime) / m_pageScale);
    IntSize nextScroll = m_pageScaleAnimation->scrollOffsetAtTime(monotonicTime);
    nextScroll.scale(1 / m_pageScaleDelta);
    m_scrollLayerImpl->scrollBy(nextScroll - scrollTotal);
    m_client->setNeedsRedrawOnImplThread();

    if (m_pageScaleAnimation->isAnimationCompleteAtTime(monotonicTime)) {
        m_pageScaleAnimation.clear();
        m_client->setNeedsCommitOnImplThread();
    }
}

void CCLayerTreeHostImpl::animateLayers(double monotonicTime, double wallClockTime)
{
    if (!m_settings.threadedAnimationEnabled || !m_needsAnimateLayers || !m_rootLayerImpl)
        return;

    TRACE_EVENT("CCLayerTreeHostImpl::animateLayers", this, 0);

    OwnPtr<CCAnimationEventsVector> events(adoptPtr(new CCAnimationEventsVector));

    bool didAnimate = false;
    animateLayersRecursive(m_rootLayerImpl.get(), monotonicTime, wallClockTime, events.get(), didAnimate, m_needsAnimateLayers);

    if (!events->isEmpty())
        m_client->postAnimationEventsToMainThreadOnImplThread(events.release(), wallClockTime);

    if (didAnimate)
        m_client->setNeedsRedrawOnImplThread();

    const bool shouldTickInBackground = m_needsAnimateLayers && !m_visible;
    m_timeSourceClientAdapter->setActive(shouldTickInBackground);
}

} // namespace WebCore
