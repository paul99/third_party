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

#ifndef CCLayerImpl_h
#define CCLayerImpl_h

#include "Color.h"
#include "FloatRect.h"
#include "IntRect.h"
#include "Region.h"
#include "TextStream.h"
#include "TransformationMatrix.h"
#include "cc/CCLayerAnimationController.h"
#include "cc/CCRenderPass.h"
#include "cc/CCRenderSurface.h"
#include <wtf/OwnPtr.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class CCLayerSorter;
class LayerChromium;
class LayerRendererChromium;
#if OS(ANDROID)
class CCScrollbarAndroid;
#endif

class CCLayerImpl : public RefCounted<CCLayerImpl>, public CCLayerAnimationControllerClient {
public:
    static PassRefPtr<CCLayerImpl> create(int id)
    {
        return adoptRef(new CCLayerImpl(id));
    }

    virtual ~CCLayerImpl();

    // CCLayerAnimationControllerClient implementation.
    virtual int id() const { return m_layerId; }
    virtual void setOpacityFromAnimation(float);
    virtual float opacity() const { return m_opacity; }
    virtual void setTransformFromAnimation(const TransformationMatrix&);
    virtual const TransformationMatrix& transform() const { return m_transform; }
    virtual const IntSize& bounds() const { return m_bounds; }

    // Tree structure.
    CCLayerImpl* parent() const { return m_parent; }
    const Vector<RefPtr<CCLayerImpl> >& children() const { return m_children; }
    void addChild(PassRefPtr<CCLayerImpl>);
    void removeFromParent();
    void removeAllChildren();
    bool isLayerInDescendants(int layerId) const;

    void setMaskLayer(PassRefPtr<CCLayerImpl>);
    CCLayerImpl* maskLayer() const { return m_maskLayer.get(); }

    void setReplicaLayer(PassRefPtr<CCLayerImpl>);
    CCLayerImpl* replicaLayer() const { return m_replicaLayer.get(); }

#ifndef NDEBUG
    int debugID() const { return m_debugID; }
#endif

    PassOwnPtr<CCSharedQuadState> createSharedQuadState() const;
    virtual void appendQuads(CCQuadList&, const CCSharedQuadState*);
    void appendDebugBorderQuad(CCQuadList&, const CCSharedQuadState*) const;

    virtual void draw(LayerRendererChromium*);
    void unreserveContentsTexture();
    virtual void bindContentsTexture(LayerRendererChromium*);

    // Returns true if this layer has content to draw.
    void setDrawsContent(bool);
    bool drawsContent() const { return m_drawsContent; }

    // Returns true if any of the layer's descendants has content to draw.
    bool descendantDrawsContent();

    void cleanupResources();

    void setAnchorPoint(const FloatPoint&);
    const FloatPoint& anchorPoint() const { return m_anchorPoint; }

    void setAnchorPointZ(float);
    float anchorPointZ() const { return m_anchorPointZ; }

    void setBackgroundColor(const Color&);
    Color backgroundColor() const { return m_backgroundColor; }

    void setBackgroundCoversViewport(bool);
    bool backgroundCoversViewport() const { return m_backgroundCoversViewport; }

    void setMasksToBounds(bool);
    bool masksToBounds() const { return m_masksToBounds; }

    void setOpaque(bool);
    bool opaque() const { return m_opaque; }

    void setOpacity(float);
    bool opacityIsAnimating() const;

    void setPosition(const FloatPoint&);
    const FloatPoint& position() const { return m_position; }

#if OS(ANDROID)
    void setIsContainerLayer(bool isContainerLayer) { m_isContainerLayer = isContainerLayer; }
    bool isContainerLayer() const { return m_isContainerLayer; }

    void setFixedToContainerLayerVisibleRect(bool fixedToContainerLayerVisibleRect = true) { m_fixedToContainerLayerVisibleRect = fixedToContainerLayerVisibleRect;}
    bool fixedToContainerLayerVisibleRect() const { return m_fixedToContainerLayerVisibleRect; }
#endif

    void setPreserves3D(bool);
    bool preserves3D() const { return m_preserves3D; }

    void setUsesLayerClipping(bool usesLayerClipping) { m_usesLayerClipping = usesLayerClipping; }
    bool usesLayerClipping() const { return m_usesLayerClipping; }

    void setIsNonCompositedContent(bool isNonCompositedContent) { m_isNonCompositedContent = isNonCompositedContent; }
    bool isNonCompositedContent() const { return m_isNonCompositedContent; }

    void setSublayerTransform(const TransformationMatrix&);
    const TransformationMatrix& sublayerTransform() const { return m_sublayerTransform; }

    void setName(const String& name) { m_name = name; }
    const String& name() const { return m_name; }

    // Debug layer border - visual effect only, do not change geometry/clipping/etc.
    void setDebugBorderColor(Color);
    Color debugBorderColor() const { return m_debugBorderColor; }
    void setDebugBorderWidth(float);
    float debugBorderWidth() const { return m_debugBorderWidth; }
    bool hasDebugBorders() const;

    CCRenderSurface* renderSurface() const { return m_renderSurface.get(); }
    void createRenderSurface();
    void clearRenderSurface() { m_renderSurface.clear(); }

    float drawOpacity() const { return m_drawOpacity; }
    void setDrawOpacity(float opacity) { m_drawOpacity = opacity; }

    bool drawOpacityIsAnimating() const { return m_drawOpacityIsAnimating; }
    void setDrawOpacityIsAnimating(bool drawOpacityIsAnimating) { m_drawOpacityIsAnimating = drawOpacityIsAnimating; }

    const IntRect& clipRect() const { return m_clipRect; }
    void setClipRect(const IntRect& rect) { m_clipRect = rect; }
    CCRenderSurface* targetRenderSurface() const { return m_targetRenderSurface; }
    void setTargetRenderSurface(CCRenderSurface* surface) { m_targetRenderSurface = surface; }

    void setBounds(const IntSize&);

    const IntSize& contentBounds() const { return m_contentBounds; }
    void setContentBounds(const IntSize&);

    const IntPoint& scrollPosition() const { return m_scrollPosition; }
    void setScrollPosition(const IntPoint&);

    const IntSize& maxScrollPosition() const {return m_maxScrollPosition; }
    void setMaxScrollPosition(const IntSize& maxScrollPosition) { m_maxScrollPosition = maxScrollPosition; }

    const FloatSize& scrollDelta() const { return m_scrollDelta; }
    void setScrollDelta(const FloatSize&);

    const Region& inputEventRegion() const { return m_inputEventRegion; }
    void setInputEventRegion(const Region& region) { m_inputEventRegion = region; }

    // Returns true if a point in content coordinates is inside the input event region.
    bool isInsideInputEventRegion(const IntPoint& contentPoint) const;

    float pageScaleDelta() const { return m_pageScaleDelta; }
    void setPageScaleDelta(float);

    const FloatSize& sentScrollDelta() const { return m_sentScrollDelta; }
    void setSentScrollDelta(const FloatSize& sentScrollDelta) { m_sentScrollDelta = sentScrollDelta; }

    void scrollBy(const FloatSize& scroll);

    bool scrollable() const { return m_scrollable; }
    void setScrollable(bool scrollable) { m_scrollable = scrollable; }

    void setAllowScrollingAncestors(bool allowScrollingAncestors) { m_allowScrollingAncestors = allowScrollingAncestors; }
    bool allowScrollingAncestors() const { return m_allowScrollingAncestors; }

    const IntRect& visibleLayerRect() const { return m_visibleLayerRect; }
    void setVisibleLayerRect(const IntRect& visibleLayerRect) { m_visibleLayerRect = visibleLayerRect; }

    bool doubleSided() const { return m_doubleSided; }
    void setDoubleSided(bool);

    // Returns the rect containtaining this layer in the current view's coordinate system.
    const IntRect getDrawRect() const;

    void setTransform(const TransformationMatrix&);
    bool transformIsAnimating() const;

    const TransformationMatrix& drawTransform() const { return m_drawTransform; }
    void setDrawTransform(const TransformationMatrix& matrix) { m_drawTransform = matrix; }
    const TransformationMatrix& screenSpaceTransform() const { return m_screenSpaceTransform; }
    void setScreenSpaceTransform(const TransformationMatrix& matrix) { m_screenSpaceTransform = matrix; }

    bool drawTransformIsAnimating() const { return m_drawTransformIsAnimating; }
    void setDrawTransformIsAnimating(bool animating) { m_drawTransformIsAnimating = animating; }
    bool screenSpaceTransformIsAnimating() const { return m_screenSpaceTransformIsAnimating; }
    void setScreenSpaceTransformIsAnimating(bool animating) { m_screenSpaceTransformIsAnimating = animating; }

    const IntRect& drawableContentRect() const { return m_drawableContentRect; }
    void setDrawableContentRect(const IntRect& rect) { m_drawableContentRect = rect; }
    const FloatRect& updateRect() const { return m_updateRect; }
    void setUpdateRect(const FloatRect& updateRect) { m_updateRect = updateRect; }

    String layerTreeAsText() const;

    bool layerPropertyChanged() const { return m_layerPropertyChanged; }
    void resetAllChangeTrackingForSubtree();

    CCLayerAnimationController* layerAnimationController() { return m_layerAnimationController.get(); }

#if OS(ANDROID)
    void setScrollbarAndroid(PassOwnPtr<CCScrollbarAndroid>);
    CCScrollbarAndroid* scrollbarAndroid() const { return m_scrollbarAndroid.get(); }
#endif

protected:
    explicit CCLayerImpl(int);

    virtual void dumpLayerProperties(TextStream&, int indent) const;
    static void writeIndent(TextStream&, int indent);

    // Transformation used to transform quads provided in appendQuads.
    virtual TransformationMatrix quadTransform() const;

    void appendGutterQuads(CCQuadList&, const CCSharedQuadState*);

private:
    void setParent(CCLayerImpl* parent) { m_parent = parent; }
    friend class TreeSynchronizer;
    void clearChildList(); // Warning: This does not preserve tree structure invariants and so is only exposed to the tree synchronizer.

    void noteLayerPropertyChangedForSubtree();

    // Note carefully this does not affect the current layer.
    void noteLayerPropertyChangedForDescendants();

    virtual const char* layerTypeAsString() const { return "LayerChromium"; }

    void dumpLayer(TextStream&, int indent) const;

    // Properties internal to CCLayerImpl
    CCLayerImpl* m_parent;
    Vector<RefPtr<CCLayerImpl> > m_children;
    RefPtr<CCLayerImpl> m_maskLayer;
    RefPtr<CCLayerImpl> m_replicaLayer;
    int m_layerId;

    // Properties synchronized from the associated LayerChromium.
    FloatPoint m_anchorPoint;
    float m_anchorPointZ;
    IntSize m_bounds;
    IntSize m_contentBounds;
    IntPoint m_scrollPosition;
    bool m_scrollable;
    bool m_allowScrollingAncestors;
    Color m_backgroundColor;
    bool m_backgroundCoversViewport;
    Region m_inputEventRegion;

    // Whether the "back" of this layer should draw.
    bool m_doubleSided;

    // Tracks if drawing-related properties have changed since last redraw.
    bool m_layerPropertyChanged;

    IntRect m_visibleLayerRect;
    bool m_masksToBounds;
    bool m_opaque;
    float m_opacity;
    FloatPoint m_position;
#if OS(ANDROID)
    bool m_isContainerLayer;
    bool m_fixedToContainerLayerVisibleRect;

    OwnPtr<CCScrollbarAndroid> m_scrollbarAndroid;
#endif
    bool m_preserves3D;
    TransformationMatrix m_sublayerTransform;
    TransformationMatrix m_transform;
    bool m_usesLayerClipping;
    bool m_isNonCompositedContent;

    bool m_drawsContent;

    FloatSize m_scrollDelta;
    FloatSize m_sentScrollDelta;
    IntSize m_maxScrollPosition;
    float m_pageScaleDelta;

    // Properties owned exclusively by this CCLayerImpl.
    // Debugging.
#ifndef NDEBUG
    int m_debugID;
#endif

    String m_name;

    // Render surface this layer draws into. This is a surface that can belong
    // either to this layer (if m_targetRenderSurface == m_renderSurface) or
    // to an ancestor of this layer. The target render surface determines the
    // coordinate system the layer's transforms are relative to.
    CCRenderSurface* m_targetRenderSurface;

    // The global depth value of the center of the layer. This value is used
    // to sort layers from back to front.
    float m_drawDepth;
    float m_drawOpacity;
    bool m_drawOpacityIsAnimating;

    // Debug borders.
    Color m_debugBorderColor;
    float m_debugBorderWidth;

    TransformationMatrix m_drawTransform;
    TransformationMatrix m_screenSpaceTransform;
    bool m_drawTransformIsAnimating;
    bool m_screenSpaceTransformIsAnimating;

    // The rect that contributes to the scissor when this layer is drawn.
    // Inherited by the parent layer and further restricted if this layer masks
    // to bounds.
    IntRect m_clipRect;

    // Render surface associated with this layer. The layer and its descendants
    // will render to this surface.
    OwnPtr<CCRenderSurface> m_renderSurface;

    // Hierarchical bounding rect containing the layer and its descendants.
    IntRect m_drawableContentRect;

    // Rect indicating what was repainted/updated during update.
    // Note that plugin layers bypass this and leave it empty.
    FloatRect m_updateRect;

    // Manages animations for this layer.
    OwnPtr<CCLayerAnimationController> m_layerAnimationController;
};

void sortLayers(Vector<RefPtr<CCLayerImpl> >::iterator first, Vector<RefPtr<CCLayerImpl> >::iterator end, CCLayerSorter*);

}

#endif // CCLayerImpl_h
