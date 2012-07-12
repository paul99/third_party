/*
 * Copyright (C) 2010 Google Inc. All rights reserved.
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


#ifndef LayerChromium_h
#define LayerChromium_h

#if USE(ACCELERATED_COMPOSITING)

#include "FloatPoint.h"
#include "GraphicsContext.h"
#include "PlatformString.h"
#include "ProgramBinding.h"
#include "Region.h"
#include "RenderSurfaceChromium.h"
#include "ShaderChromium.h"
#include "TransformationMatrix.h"
#include "cc/CCLayerAnimationController.h"

#include <wtf/OwnPtr.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/Vector.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

struct CCAnimationStartedEvent;
class CCLayerAnimationDelegate;
class CCLayerImpl;
class CCLayerTreeHost;
class CCTextureUpdater;
class GraphicsContext3D;

// Base class for composited layers. Special layer types are derived from
// this class.
class LayerChromium : public RefCounted<LayerChromium>, public CCLayerAnimationControllerClient {
public:
    static PassRefPtr<LayerChromium> create();

    virtual ~LayerChromium();

    // CCLayerAnimationControllerClient implementation
    virtual int id() const { return m_layerId; }
    virtual void setOpacityFromAnimation(float);
    virtual float opacity() const { return m_opacity; }
    virtual void setTransformFromAnimation(const TransformationMatrix&);
    virtual const TransformationMatrix& transform() const { return m_transform; }
    virtual const IntSize& bounds() const { return m_bounds; }

    const LayerChromium* rootLayer() const;
    LayerChromium* parent() const;
    void addChild(PassRefPtr<LayerChromium>);
    void insertChild(PassRefPtr<LayerChromium>, size_t index);
    void replaceChild(LayerChromium* reference, PassRefPtr<LayerChromium> newLayer);
    void removeFromParent();
    void removeAllChildren();
    void setChildren(const Vector<RefPtr<LayerChromium> >&);
    const Vector<RefPtr<LayerChromium> >& children() const { return m_children; }

    void setAnchorPoint(const FloatPoint&);
    FloatPoint anchorPoint() const { return m_anchorPoint; }

    void setAnchorPointZ(float);
    float anchorPointZ() const { return m_anchorPointZ; }

    void setBackgroundColor(const Color&);
    Color backgroundColor() const { return m_backgroundColor; }

    void setBackgroundCoversViewport(bool);
    bool backgroundCoversViewport() const { return m_backgroundCoversViewport; }

    void setBounds(const IntSize&);
    virtual IntSize contentBounds() const { return bounds(); }

    void setMasksToBounds(bool);
    bool masksToBounds() const { return m_masksToBounds; }

    void setName(const String&);
    const String& name() const { return m_name; }

    void setMaskLayer(LayerChromium*);
    LayerChromium* maskLayer() const { return m_maskLayer.get(); }

    virtual void setNeedsDisplayRect(const FloatRect& dirtyRect);
    void setNeedsDisplay() { setNeedsDisplayRect(FloatRect(FloatPoint(), bounds())); }
    virtual bool needsDisplay() const { return m_needsDisplay; }
    void resetNeedsDisplay();

    void setOpacity(float);
    bool opacityIsAnimating() const;

    virtual void setOpaque(bool);
    bool opaque() const { return m_opaque; }

    void setPosition(const FloatPoint&);
    FloatPoint position() const { return m_position; }

#if OS(ANDROID)
    void setIsContainerLayer(bool isContainerLayer) { m_isContainerLayer = isContainerLayer; }
    bool isContainerLayer() const { return m_isContainerLayer; }

    void setFixedToContainerLayerVisibleRect(bool fixedToContainerLayerVisibleRect) { m_fixedToContainerLayerVisibleRect = fixedToContainerLayerVisibleRect; setNeedsCommit(); }
    bool fixedToContainerLayerVisibleRect() const { return m_fixedToContainerLayerVisibleRect; }
#endif

    void setSublayerTransform(const TransformationMatrix&);
    const TransformationMatrix& sublayerTransform() const { return m_sublayerTransform; }

    void setTransform(const TransformationMatrix&);
    bool transformIsAnimating() const;

    const IntRect& visibleLayerRect() const { return m_visibleLayerRect; }
    void setVisibleLayerRect(const IntRect& visibleLayerRect) { m_visibleLayerRect = visibleLayerRect; }

    void setScrollPosition(const IntPoint&);
    const IntPoint& scrollPosition() const { return m_scrollPosition; }

    void setMaxScrollPosition(const IntSize&);
    const IntSize& maxScrollPosition() const { return m_maxScrollPosition; }

    void setScrollable(bool);
    bool scrollable() const { return m_scrollable; }

    void setAllowScrollingAncestors(bool allowScrollingAncestors) { m_allowScrollingAncestors = allowScrollingAncestors; }
    bool allowScrollingAncestors() const { return m_allowScrollingAncestors; }

    IntSize scrollDelta() const { return IntSize(); }
#if OS(ANDROID)
    IntSize sentScrollDelta() const { return IntSize(); }

    const IntSize& scrollPrediction() const { return m_scrollPrediction; }
    void setScrollPrediction(const IntSize& scrollPrediction) { m_scrollPrediction = scrollPrediction; }
#endif

    float pageScaleDelta() const { return 1; }

    void setDoubleSided(bool);
    bool doubleSided() const { return m_doubleSided; }

    void setPreserves3D(bool preserve3D) { m_preserves3D = preserve3D; }
    bool preserves3D() const { return m_preserves3D; }

    void setUsesLayerClipping(bool usesLayerClipping) { m_usesLayerClipping = usesLayerClipping; }
    bool usesLayerClipping() const { return m_usesLayerClipping; }

    virtual void setIsNonCompositedContent(bool);
    bool isNonCompositedContent() const { return m_isNonCompositedContent; }

    virtual void setLayerTreeHost(CCLayerTreeHost*);

    void setIsDrawable(bool);

    void setReplicaLayer(LayerChromium*);
    LayerChromium* replicaLayer() const { return m_replicaLayer.get(); }

    // These methods typically need to be overwritten by derived classes.
    virtual bool drawsContent() const { return m_isDrawable; }
    virtual void paintContentsIfDirty() { }
    virtual void idlePaintContentsIfDirty() { }
    virtual void updateCompositorResources(GraphicsContext3D*, CCTextureUpdater&) { }
    virtual void setIsMask(bool) { }
    virtual void unreserveContentsTexture() { }
    virtual void bindContentsTexture() { }
    virtual void pageScaleChanged() { m_pageScaleDirty = true; }
    virtual void protectVisibleTileTextures() { }
    virtual bool needsContentsScale() const { return false; }

    void setDebugBorderColor(const Color&);
    void setDebugBorderWidth(float);

    virtual void pushPropertiesTo(CCLayerImpl*);

    typedef ProgramBinding<VertexShaderPos, FragmentShaderColor> BorderProgram;

    void clearRenderSurface() { m_renderSurface.clear(); }
    RenderSurfaceChromium* renderSurface() const { return m_renderSurface.get(); }
    void createRenderSurface();

    float drawOpacity() const { return m_drawOpacity; }
    void setDrawOpacity(float opacity) { m_drawOpacity = opacity; }

    bool drawOpacityIsAnimating() const { return m_drawOpacityIsAnimating; }
    void setDrawOpacityIsAnimating(bool drawOpacityIsAnimating) { m_drawOpacityIsAnimating = drawOpacityIsAnimating; }

    const IntRect& clipRect() const { return m_clipRect; }
    void setClipRect(const IntRect& clipRect) { m_clipRect = clipRect; }
    RenderSurfaceChromium* targetRenderSurface() const { return m_targetRenderSurface; }
    void setTargetRenderSurface(RenderSurfaceChromium* surface) { m_targetRenderSurface = surface; }

    bool drawTransformIsAnimating() const { return m_drawTransformIsAnimating; }
    void setDrawTransformIsAnimating(bool animating) { m_drawTransformIsAnimating = animating; }
    bool screenSpaceTransformIsAnimating() const { return m_screenSpaceTransformIsAnimating; }
    void setScreenSpaceTransformIsAnimating(bool animating) { m_screenSpaceTransformIsAnimating = animating; }

    const TransformationMatrix& drawTransform() const { return m_drawTransform; }
    void setDrawTransform(const TransformationMatrix& matrix) { m_drawTransform = matrix; }
    const TransformationMatrix& screenSpaceTransform() const { return m_screenSpaceTransform; }
    void setScreenSpaceTransform(const TransformationMatrix& matrix) { m_screenSpaceTransform = matrix; }
    const IntRect& drawableContentRect() const { return m_drawableContentRect; }
    void setDrawableContentRect(const IntRect& rect) { m_drawableContentRect = rect; }
    float contentsScale() const { return m_contentsScale; }
    void setContentsScale(float);
    const Region& inputEventRegion() const { return m_inputEventRegion; }
    void setInputEventRegion(const Region& region) { m_inputEventRegion = region; }

    // Returns true if any of the layer's descendants has content to draw.
    bool descendantDrawsContent();
    virtual void contentChanged() { }

    CCLayerTreeHost* layerTreeHost() const { return m_layerTreeHost.get(); }

    // Reserve any textures needed for this layer.
    virtual void reserveTextures() { }

    void setAlwaysReserveTextures(bool alwaysReserveTextures) { m_alwaysReserveTextures = alwaysReserveTextures; }
    bool alwaysReserveTextures() const { return m_alwaysReserveTextures; }

    bool addAnimation(const KeyframeValueList&, const IntSize& boxSize, const Animation*, int animationId, int groupId, double timeOffset);
    void pauseAnimation(int animationId, double timeOffset);
    void removeAnimation(int animationId);

    void suspendAnimations(double monotonicTime);
    void resumeAnimations(double monotonicTime);

    CCLayerAnimationController* layerAnimationController() { return m_layerAnimationController.get(); }
    void setLayerAnimationController(PassOwnPtr<CCLayerAnimationController>);

    void setLayerAnimationDelegate(CCLayerAnimationDelegate* layerAnimationDelegate) { m_layerAnimationDelegate = layerAnimationDelegate; }

    bool hasActiveAnimation() const;

    void notifyAnimationStarted(const CCAnimationStartedEvent&, double wallClockTime);

protected:
    friend class CCLayerImpl;
    friend class LayerTilerChromium;
    friend class TreeSynchronizer;

    LayerChromium();

    // This is called to clean up resources being held in the same context as
    // layerRendererContext(). Subclasses should override this method if they
    // hold context-dependent resources such as textures.
    virtual void cleanupResources();

    void setNeedsCommit();

    // The dirty rect is the union of damaged regions that need repainting/updating.
    FloatRect m_dirtyRect;

    // This flag is set when layer need repainting/updating.
    bool m_needsDisplay;

    // The update rect is the region of the compositor resource that was actually updated by the compositor.
    // For layers that may do updating outside the compositor's control (i.e. plugin layers), this information
    // is not available and the update rect will remain empty.
    // Note this rect is in layer space (not content space).
    FloatRect m_updateRect;

    RefPtr<LayerChromium> m_maskLayer;

    // Constructs a CCLayerImpl of the correct runtime type for this LayerChromium type.
    virtual PassRefPtr<CCLayerImpl> createCCLayerImpl();
    int m_layerId;

private:
    void setParent(LayerChromium*);
    bool hasAncestor(LayerChromium*) const;

    size_t numChildren() const { return m_children.size(); }

    // Returns the index of the child or -1 if not found.
    int indexOfChild(const LayerChromium*);

    // This should only be called from removeFromParent.
    void removeChild(LayerChromium*);

    Vector<RefPtr<LayerChromium> > m_children;
    LayerChromium* m_parent;

    RefPtr<CCLayerTreeHost> m_layerTreeHost;

    OwnPtr<CCLayerAnimationController> m_layerAnimationController;

    // Layer properties.
    IntSize m_bounds;
    IntRect m_visibleLayerRect;
    IntPoint m_scrollPosition;
    IntSize m_maxScrollPosition;
    bool m_scrollable;
    bool m_allowScrollingAncestors;
    FloatPoint m_position;
    FloatPoint m_anchorPoint;
    Color m_backgroundColor;
    bool m_backgroundCoversViewport;
    Color m_debugBorderColor;
    float m_debugBorderWidth;
    float m_opacity;
    float m_anchorPointZ;
#if OS(ANDROID)
    bool m_isContainerLayer;
    bool m_fixedToContainerLayerVisibleRect;
    IntSize m_scrollPrediction;
#endif
    bool m_isDrawable;
    bool m_masksToBounds;
    bool m_opaque;
    bool m_doubleSided;
    bool m_usesLayerClipping;
    bool m_isNonCompositedContent;
    bool m_preserves3D;
    bool m_alwaysReserveTextures;
    Region m_inputEventRegion;

    TransformationMatrix m_transform;
    TransformationMatrix m_sublayerTransform;

    // Replica layer used for reflections.
    RefPtr<LayerChromium> m_replicaLayer;

    // Transient properties.
    OwnPtr<RenderSurfaceChromium> m_renderSurface;
    float m_drawOpacity;
    bool m_drawOpacityIsAnimating;
    IntRect m_clipRect;
    RenderSurfaceChromium* m_targetRenderSurface;
    TransformationMatrix m_drawTransform;
    TransformationMatrix m_screenSpaceTransform;
    bool m_drawTransformIsAnimating;
    bool m_screenSpaceTransformIsAnimating;
    IntRect m_drawableContentRect;
    float m_contentsScale;

    String m_name;

    bool m_pageScaleDirty;

    CCLayerAnimationDelegate* m_layerAnimationDelegate;
};

void sortLayers(Vector<RefPtr<LayerChromium> >::iterator, Vector<RefPtr<LayerChromium> >::iterator, void*);

}
#endif // USE(ACCELERATED_COMPOSITING)

#endif
