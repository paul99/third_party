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

#include "cc/CCLayerTreeHost.h"

#include "LayerChromium.h"
#include "LayerPainterChromium.h"
#include "LayerRendererChromium.h"
#include "TraceEvent.h"
#include "TreeSynchronizer.h"
#include "cc/CCLayerAnimationController.h"
#include "cc/CCLayerIterator.h"
#include "cc/CCLayerTreeHostCommon.h"
#include "cc/CCLayerTreeHostImpl.h"
#include "cc/CCSingleThreadProxy.h"
#include "cc/CCThread.h"
#include "cc/CCThreadProxy.h"

using namespace std;

namespace {
static int numLayerTreeInstances;
}

namespace WebCore {

bool CCLayerTreeHost::anyLayerTreeHostInstanceExists()
{
    return numLayerTreeInstances > 0;
}

PassRefPtr<CCLayerTreeHost> CCLayerTreeHost::create(CCLayerTreeHostClient* client, const CCSettings& settings)
{
    RefPtr<CCLayerTreeHost> layerTreeHost = adoptRef(new CCLayerTreeHost(client, settings));
    if (!layerTreeHost->initialize())
        return 0;
    return layerTreeHost;
}

CCLayerTreeHost::CCLayerTreeHost(CCLayerTreeHostClient* client, const CCSettings& settings)
    : m_compositorIdentifier(-1)
    , m_animating(false)
    , m_needsAnimateLayers(false)
    , m_client(client)
    , m_frameNumber(0)
    , m_settings(settings)
    , m_visible(true)
    , m_haveWheelEventHandlers(false)
    , m_pageScaleFactor(1)
    , m_minPageScaleFactor(1)
    , m_maxPageScaleFactor(1)
    , m_triggerIdlePaints(true)
    , m_partialTextureUpdateRequests(0)
{
    ASSERT(CCProxy::isMainThread());
    numLayerTreeInstances++;
}

bool CCLayerTreeHost::initialize()
{
    TRACE_EVENT("CCLayerTreeHost::initialize", this, 0);
    if (CCProxy::hasImplThread()) {
        // The HUD does not work in threaded mode. Turn it off.
        m_settings.showFPSCounter = false;
        m_settings.showPlatformLayerTree = false;

        m_proxy = CCThreadProxy::create(this);
    } else
        m_proxy = CCSingleThreadProxy::create(this);
    m_proxy->start();

    // Create the texture manager so the layer renderer can give it some pre-allocated textures.
    // FIXME: We don't have the maximum texture size or format yet, so those are set in
    // CCThreadProxy::initializeLayerRendererOnImplThread(), which is called directly below.
    m_contentsTextureManager = TextureManager::create(TextureManager::highLimitBytes(m_settings.viewportSize),
                                                      TextureManager::reclaimLimitBytes(m_settings.viewportSize),
                                                      1024);
    if (!m_proxy->initializeLayerRenderer())
        return false;

    m_compositorIdentifier = m_proxy->compositorIdentifier();

    // Update m_settings based on capabilities that we got back from the renderer.
    m_settings.acceleratePainting = m_proxy->layerRendererCapabilities().usingAcceleratedPainting;

    // Update m_settings based on partial update capability.
    m_settings.maxPartialTextureUpdates = min(m_settings.maxPartialTextureUpdates, m_proxy->maxPartialTextureUpdates());

    return true;
}

CCLayerTreeHost::~CCLayerTreeHost()
{
    ASSERT(CCProxy::isMainThread());
    TRACE_EVENT("CCLayerTreeHost::~CCLayerTreeHost", this, 0);
    ASSERT(m_proxy);
    m_proxy->stop();
    m_proxy.clear();
    clearPendingUpdate();
    numLayerTreeInstances--;
}

void CCLayerTreeHost::deleteContentsTexturesOnImplThread(TextureAllocator* allocator)
{
    ASSERT(CCProxy::isImplThread());
    if (m_contentsTextureManager)
        m_contentsTextureManager->evictAndDeleteAllTextures(allocator);
}

void CCLayerTreeHost::updateAnimations(double wallClockTime)
{
    m_animating = true;
    m_client->updateAnimations(wallClockTime);
    animateLayers(monotonicallyIncreasingTime());
    m_animating = false;
}

void CCLayerTreeHost::layout()
{
    m_client->layout();
}

#if OS(ANDROID)
void CCLayerTreeHost::updateNonFastScrollableRegionForLayers()
{
    m_client->updateNonFastScrollableRegionForLayers();
}
#endif

void CCLayerTreeHost::beginCommitOnImplThread(CCLayerTreeHostImpl* hostImpl)
{
    ASSERT(CCProxy::isImplThread());
    TRACE_EVENT("CCLayerTreeHost::commitTo", this, 0);

    // Make space for 10% of free textures.
    size_t reclaimLimit = contentsTextureManager()->preferredMemoryLimitBytes();
    size_t desiredFreeBytes = reclaimLimit / 10;

    // Make room for some free textures. This may evict free textures too,
    // but they will just be converted back to free textures below.
    contentsTextureManager()->reduceMemoryToLimit(reclaimLimit - desiredFreeBytes);
    contentsTextureManager()->deleteEvictedTextures(hostImpl->contentsTextureAllocator(), true);
}

// This function commits the CCLayerTreeHost to an impl tree. When modifying
// this function, keep in mind that the function *runs* on the impl thread! Any
// code that is logically a main thread operation, e.g. deletion of a LayerChromium,
// should be delayed until the CCLayerTreeHost::commitComplete, which will run
// after the commit, but on the main thread.
void CCLayerTreeHost::finishCommitOnImplThread(CCLayerTreeHostImpl* hostImpl)
{
    ASSERT(CCProxy::isImplThread());

#if OS(ANDROID)
    hostImpl->setHighlight(m_highlight, m_highlightColor);
    m_highlight.clear();
#endif

    if (rootLayer()) {
        hostImpl->setRootLayer(TreeSynchronizer::synchronizeTrees(rootLayer(), hostImpl->rootLayer()));
    } else
        hostImpl->setRootLayer(0);

    if (rootLayer()) {
        hostImpl->setNeedsAnimateLayers();
        m_needsAnimateLayers = true;
    }

    hostImpl->setSourceFrameNumber(frameNumber());
    hostImpl->setHaveWheelEventHandlers(m_haveWheelEventHandlers);
    hostImpl->setViewportSize(viewportSize());
    hostImpl->setPageScaleFactorAndLimits(m_pageScaleFactor, m_minPageScaleFactor, m_maxPageScaleFactor);

    m_frameNumber++;
}

void CCLayerTreeHost::commitComplete()
{
    m_deleteTextureAfterCommitList.clear();
    clearPendingUpdate();
    m_contentsTextureManager->unprotectAllTextures();
}

PassRefPtr<GraphicsContext3D> CCLayerTreeHost::createLayerTreeHostContext3D()
{
    return m_client->createLayerTreeHostContext3D();
}

PassOwnPtr<CCLayerTreeHostImpl> CCLayerTreeHost::createLayerTreeHostImpl(CCLayerTreeHostImplClient* client)
{
    return CCLayerTreeHostImpl::create(m_settings, client);
}

void CCLayerTreeHost::didRecreateGraphicsContext(bool success)
{
    m_client->didRecreateGraphicsContext(success);
}

// Temporary hack until WebViewImpl context creation gets simplified
GraphicsContext3D* CCLayerTreeHost::context()
{
    ASSERT(!CCProxy::hasImplThread());
    // ASSERT(!m_settings.enableCompositorThread);
    return m_proxy->context();
}

bool CCLayerTreeHost::compositeAndReadback(void *pixels, const IntRect& rect)
{
    m_triggerIdlePaints = false;
    bool ret = m_proxy->compositeAndReadback(pixels, rect);
    m_triggerIdlePaints = true;
    return ret;
}

void CCLayerTreeHost::finishAllRendering()
{
    m_proxy->finishAllRendering();
}

const LayerRendererCapabilities& CCLayerTreeHost::layerRendererCapabilities() const
{
    return m_proxy->layerRendererCapabilities();
}

void CCLayerTreeHost::setNeedsAnimate()
{
    ASSERT(CCProxy::hasImplThread());
    m_proxy->setNeedsAnimate();
}

void CCLayerTreeHost::setNeedsCommit()
{
    if (CCThreadProxy::implThread())
        m_proxy->setNeedsCommit();
    else
        m_client->scheduleComposite();
}

void CCLayerTreeHost::setNeedsRedraw()
{
    if (CCThreadProxy::implThread())
        m_proxy->setNeedsRedraw();
    else
        m_client->scheduleComposite();
}

void CCLayerTreeHost::setAnimationEvents(PassOwnPtr<CCAnimationEventsVector> events, double wallClockTime)
{
    ASSERT(CCThreadProxy::isMainThread());
    setAnimationEventsRecursive(*events, m_rootLayer.get(), wallClockTime);
}

void CCLayerTreeHost::setRootLayer(PassRefPtr<LayerChromium> rootLayer)
{
    if (m_rootLayer == rootLayer)
        return;

    if (m_rootLayer)
        m_rootLayer->setLayerTreeHost(0);
    m_rootLayer = rootLayer;
    if (m_rootLayer)
        m_rootLayer->setLayerTreeHost(this);
    setNeedsCommit();
}

void CCLayerTreeHost::setViewportSize(const IntSize& viewportSize)
{
    if (viewportSize == m_viewportSize)
        return;

    contentsTextureManager()->setMaxMemoryLimitBytes(TextureManager::highLimitBytes(viewportSize));
    contentsTextureManager()->setPreferredMemoryLimitBytes(TextureManager::reclaimLimitBytes(viewportSize));
    m_viewportSize = viewportSize;
    setNeedsCommit();
}

void CCLayerTreeHost::setPageScaleFactorAndLimits(float pageScaleFactor, float minPageScaleFactor, float maxPageScaleFactor)
{
    if (pageScaleFactor == m_pageScaleFactor && minPageScaleFactor == m_minPageScaleFactor && maxPageScaleFactor == m_maxPageScaleFactor)
        return;

    m_pageScaleFactor = pageScaleFactor;
    m_minPageScaleFactor = minPageScaleFactor;
    m_maxPageScaleFactor = maxPageScaleFactor;
    setNeedsCommit();
}

void CCLayerTreeHost::setVisible(bool visible)
{
    if (m_visible == visible)
        return;

    m_visible = visible;
    if (!visible) {
        m_contentsTextureManager->reduceMemoryToLimit(TextureManager::lowLimitBytes(viewportSize()));
        m_contentsTextureManager->unprotectAllTextures();
    }

    // Tells the proxy that visibility state has changed. This will in turn call
    // CCLayerTreeHost::didBecomeInvisibleOnImplThread on the appropriate thread, for
    // the case where !visible.
    m_proxy->setVisible(visible);
}

void CCLayerTreeHost::didBecomeInvisibleOnImplThread(CCLayerTreeHostImpl* hostImpl)
{
    ASSERT(CCProxy::isImplThread());
    if (m_proxy->layerRendererCapabilities().contextHasCachedFrontBuffer)
        contentsTextureManager()->evictAndDeleteAllTextures(hostImpl->contentsTextureAllocator());
    else {
        contentsTextureManager()->reduceMemoryToLimit(TextureManager::reclaimLimitBytes(viewportSize()));
        contentsTextureManager()->deleteEvictedTextures(hostImpl->contentsTextureAllocator());
    }

    // Ensure that the dropped tiles are propagated to the impl tree.
    // If the frontbuffer is cached, then clobber the impl tree. Otherwise,
    // push over the tree changes.
    if (m_proxy->layerRendererCapabilities().contextHasCachedFrontBuffer) {
        hostImpl->setRootLayer(0);
        return;
    }

    if (rootLayer()) {
        hostImpl->setRootLayer(TreeSynchronizer::synchronizeTrees(rootLayer(), hostImpl->rootLayer()));
    } else
        hostImpl->setRootLayer(0);

    // We may have added an animation during the tree sync. This will cause both layer tree hosts
    // to visit their controllers.
    if (rootLayer()) {
        hostImpl->setNeedsAnimateLayers();
        m_needsAnimateLayers = true;
    }
}

void CCLayerTreeHost::setHaveWheelEventHandlers(bool haveWheelEventHandlers)
{
    if (m_haveWheelEventHandlers == haveWheelEventHandlers)
        return;
    m_haveWheelEventHandlers = haveWheelEventHandlers;
    m_proxy->setNeedsCommit();
}


void CCLayerTreeHost::loseCompositorContext(int numTimes)
{
    m_proxy->loseCompositorContext(numTimes);
}

TextureManager* CCLayerTreeHost::contentsTextureManager() const
{
    return m_contentsTextureManager.get();
}

void CCLayerTreeHost::composite()
{
    ASSERT(!CCThreadProxy::implThread());
    static_cast<CCSingleThreadProxy*>(m_proxy.get())->compositeImmediately();
}

#if OS(ANDROID)
void CCLayerTreeHost::pendHighlightForNextComposite(const Vector<FloatQuad>& highlight, const Color& highlightColor)
{
    m_highlight = highlight;
    m_highlightColor = highlightColor;
}
#endif

void CCLayerTreeHost::updateLayers()
{
    if (!rootLayer())
        return;

    if (viewportSize().isEmpty())
        return;

    updateLayers(rootLayer());
}

void CCLayerTreeHost::updateLayers(LayerChromium* rootLayer)
{
    TRACE_EVENT("CCLayerTreeHost::updateLayers", this, 0);

    if (!rootLayer->renderSurface())
        rootLayer->createRenderSurface();
    rootLayer->renderSurface()->setContentRect(IntRect(IntPoint(0, 0), viewportSize()));

    IntRect rootClipRect(IntPoint(), viewportSize());
    rootLayer->setClipRect(rootClipRect);

    // This assert fires if updateCompositorResources wasn't called after
    // updateLayers. Only one update can be pending at any given time.
    ASSERT(!m_updateList.size());
    m_updateList.append(rootLayer);

    RenderSurfaceChromium* rootRenderSurface = rootLayer->renderSurface();
    rootRenderSurface->clearLayerList();

    TransformationMatrix identityMatrix;
    {
        TRACE_EVENT("CCLayerTreeHost::updateLayers::calcDrawEtc", this, 0);
        CCLayerTreeHostCommon::calculateDrawTransformsAndVisibility(rootLayer, rootLayer, identityMatrix, identityMatrix, m_updateList, rootRenderSurface->layerList(), layerRendererCapabilities().maxTextureSize);
    }

    // Reset partial texture update requests.
    m_partialTextureUpdateRequests = 0;

    reserveUiTextures();
    reserveVisibleTextures();

    paintLayerContents(m_updateList, PaintVisible);
    if (!m_triggerIdlePaints)
        return;

    size_t preferredLimitBytes = TextureManager::reclaimLimitBytes(m_viewportSize);
    size_t maxLimitBytes = TextureManager::highLimitBytes(m_viewportSize);
    contentsTextureManager()->reduceMemoryToLimit(preferredLimitBytes);
    if (contentsTextureManager()->currentMemoryUseBytes() > preferredLimitBytes)
        return;

    // Idle painting should fail when we hit the preferred memory limit,
    // otherwise it will always push us towards the maximum limit.
    m_contentsTextureManager->setMaxMemoryLimitBytes(preferredLimitBytes);
    // The second (idle) paint will be a no-op in layers where painting already occured above.
    paintLayerContents(m_updateList, PaintIdle);
    m_contentsTextureManager->setMaxMemoryLimitBytes(maxLimitBytes);
}

void CCLayerTreeHost::reserveUiTextures()
{
    // Use BackToFront since it's cheap and this isn't order-dependent.
    typedef CCLayerIterator<LayerChromium, RenderSurfaceChromium, CCLayerIteratorActions::BackToFront> CCLayerIteratorType;

    CCLayerIteratorType end = CCLayerIteratorType::end(&m_updateList);
    for (CCLayerIteratorType it = CCLayerIteratorType::begin(&m_updateList); it != end; ++it) {
        if (!it.representsItself() || !it->alwaysReserveTextures())
            continue;
        it->reserveTextures();
    }
}

void CCLayerTreeHost::reserveVisibleTextures()
{
    // Use BackToFront since it's cheap and this isn't order-dependent.
    typedef CCLayerIterator<LayerChromium, RenderSurfaceChromium, CCLayerIteratorActions::BackToFront> CCLayerIteratorType;

    CCLayerIteratorType end = CCLayerIteratorType::end(&m_updateList);
    for (CCLayerIteratorType it = CCLayerIteratorType::begin(&m_updateList); it != end; ++it) {
        // Assume the layers with alwaysReserveTextures have reserved their textures.
        if (!it.representsItself() || it->alwaysReserveTextures())
            continue;
        it->reserveTextures();
    }
}

// static
void CCLayerTreeHost::paintContentsIfDirty(LayerChromium* layer, PaintType paintType)
{
    ASSERT(layer);
    ASSERT(PaintVisible == paintType || PaintIdle == paintType);
    if (PaintVisible == paintType)
        layer->paintContentsIfDirty();
    else
        layer->idlePaintContentsIfDirty();
}

void CCLayerTreeHost::paintMaskAndReplicaForRenderSurface(LayerChromium* renderSurfaceLayer, PaintType paintType)
{
    // Note: Masks and replicas only exist for layers that own render surfaces. If we reach this point
    // in code, we already know that at least something will be drawn into this render surface, so the
    // mask and replica should be painted.

    if (renderSurfaceLayer->maskLayer()) {
        renderSurfaceLayer->maskLayer()->setVisibleLayerRect(IntRect(IntPoint(), renderSurfaceLayer->contentBounds()));
        paintContentsIfDirty(renderSurfaceLayer->maskLayer(), paintType);
    }

    LayerChromium* replicaLayer = renderSurfaceLayer->replicaLayer();
    if (replicaLayer) {
        paintContentsIfDirty(replicaLayer, paintType);

        if (replicaLayer->maskLayer()) {
            replicaLayer->maskLayer()->setVisibleLayerRect(IntRect(IntPoint(), replicaLayer->maskLayer()->contentBounds()));
            paintContentsIfDirty(replicaLayer->maskLayer(), paintType);
        }
    }
}

void CCLayerTreeHost::paintLayerContents(const LayerList& renderSurfaceLayerList, PaintType paintType)
{
    // Use FrontToBack to allow for testing occlusion and performing culling during the tree walk.
    typedef CCLayerIterator<LayerChromium, RenderSurfaceChromium, CCLayerIteratorActions::FrontToBack> CCLayerIteratorType;

    CCLayerIteratorType end = CCLayerIteratorType::end(&renderSurfaceLayerList);
    for (CCLayerIteratorType it = CCLayerIteratorType::begin(&renderSurfaceLayerList); it != end; ++it) {
        if (it.representsTargetRenderSurface()) {
            ASSERT(it->renderSurface()->drawOpacity() || it->renderSurface()->drawOpacityIsAnimating());
            paintMaskAndReplicaForRenderSurface(*it, paintType);
        } else if (it.representsItself()) {
            ASSERT(!it->bounds().isEmpty());
            paintContentsIfDirty(*it, paintType);
        }
    }
}

void CCLayerTreeHost::updateCompositorResources(GraphicsContext3D* context, CCTextureUpdater& updater)
{
    // Use BackToFront since it's cheap and this isn't order-dependent.
    typedef CCLayerIterator<LayerChromium, RenderSurfaceChromium, CCLayerIteratorActions::BackToFront> CCLayerIteratorType;

    CCLayerIteratorType end = CCLayerIteratorType::end(&m_updateList);
    for (CCLayerIteratorType it = CCLayerIteratorType::begin(&m_updateList); it != end; ++it) {
        if (it.representsTargetRenderSurface()) {
            if (it->maskLayer())
                it->maskLayer()->updateCompositorResources(context, updater);

            if (it->replicaLayer()) {
                it->replicaLayer()->updateCompositorResources(context, updater);
                if (it->replicaLayer()->maskLayer())
                    it->replicaLayer()->maskLayer()->updateCompositorResources(context, updater);
            }
        } else if (it.representsItself())
            it->updateCompositorResources(context, updater);
    }
}

void CCLayerTreeHost::clearPendingUpdate()
{
    for (size_t surfaceIndex = 0; surfaceIndex < m_updateList.size(); ++surfaceIndex) {
        LayerChromium* layer = m_updateList[surfaceIndex].get();
        ASSERT(layer->renderSurface());
        layer->clearRenderSurface();
    }
    m_updateList.clear();
}

static LayerChromium* findLayerById(LayerChromium* layer, int id)
{
    if (!layer)
        return 0;

    if (layer->id() == id)
        return layer;

    for (size_t i = 0; i < layer->children().size(); ++i) {
        LayerChromium* found = findLayerById(layer->children()[i].get(), id);
        if (found)
            return found;
    }

    return 0;
}

void CCLayerTreeHost::applyScrollAndScale(const CCScrollAndScaleSet& info)
{
    if (!info.rootScrollDelta.isZero() || info.pageScaleDelta != 1)
        m_client->applyScrollAndScale(info.rootScrollDelta, info.pageScaleDelta);

    for (size_t i = 0; i < info.scrolls.size(); ++i) {
        LayerChromium* layer = findLayerById(m_rootLayer.get(), info.scrolls[i].layerId);
        if (layer && layer->scrollable())
            static_cast<ContentLayerChromium*>(layer)->scrollBy(info.scrolls[i].scrollDelta);
    }
}

void CCLayerTreeHost::startRateLimiter(GraphicsContext3D* context)
{
    if (m_animating)
        return;
    ASSERT(context);
    RateLimiterMap::iterator it = m_rateLimiters.find(context);
    if (it != m_rateLimiters.end())
        it->second->start();
    else {
        RefPtr<RateLimiter> rateLimiter = RateLimiter::create(context);
        m_rateLimiters.set(context, rateLimiter);
        rateLimiter->start();
    }
}

void CCLayerTreeHost::stopRateLimiter(GraphicsContext3D* context)
{
    RateLimiterMap::iterator it = m_rateLimiters.find(context);
    if (it != m_rateLimiters.end()) {
        it->second->stop();
        m_rateLimiters.remove(it);
    }
}

bool CCLayerTreeHost::bufferedUpdates()
{
    return m_settings.maxPartialTextureUpdates != numeric_limits<size_t>::max();
}

bool CCLayerTreeHost::requestPartialTextureUpdate()
{
    if (m_partialTextureUpdateRequests >= m_settings.maxPartialTextureUpdates)
        return false;

    m_partialTextureUpdateRequests++;
    return true;
}

void CCLayerTreeHost::deleteTextureAfterCommit(PassOwnPtr<ManagedTexture> texture)
{
    m_deleteTextureAfterCommitList.append(texture);
}

void CCLayerTreeHost::animateLayers(double monotonicTime)
{
    if (!m_settings.threadedAnimationEnabled || !m_needsAnimateLayers)
        return;

    TRACE_EVENT("CCLayerTreeHostImpl::animateLayers", this, 0);
    m_needsAnimateLayers = animateLayersRecursive(m_rootLayer.get(), monotonicTime);
}

bool CCLayerTreeHost::animateLayersRecursive(LayerChromium* current, double monotonicTime)
{
    if (!current)
        return false;

    bool subtreeNeedsAnimateLayers = false;
    CCLayerAnimationController* currentController = current->layerAnimationController();

    // FIXME: Android requires an extra tick to start animations,
    // to synchronize with the GPU Process. "animate" now ticks animations
    // twice to fix many unit tests. animateForReal ticks once.
    currentController->animateForReal(monotonicTime, 0);

    // If the current controller still has an active animation, we must continue animating layers.
    if (currentController->hasActiveAnimation())
         subtreeNeedsAnimateLayers = true;

    for (size_t i = 0; i < current->children().size(); ++i) {
        if (animateLayersRecursive(current->children()[i].get(), monotonicTime))
            subtreeNeedsAnimateLayers = true;
    }

    return subtreeNeedsAnimateLayers;
}

void CCLayerTreeHost::setAnimationEventsRecursive(const CCAnimationEventsVector& events, LayerChromium* layer, double wallClockTime)
{
    if (!layer)
        return;

    for (size_t eventIndex = 0; eventIndex < events.size(); ++eventIndex) {
        if (layer->id() == events[eventIndex].layerId)
            layer->notifyAnimationStarted(events[eventIndex], wallClockTime);
    }

    for (size_t childIndex = 0; childIndex < layer->children().size(); ++childIndex)
        setAnimationEventsRecursive(events, layer->children()[childIndex].get(), wallClockTime);
}

} // namespace WebCore
