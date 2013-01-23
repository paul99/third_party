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

#if USE(ACCELERATED_COMPOSITING)

#include "TiledLayerChromium.h"

#include "GraphicsContext3D.h"
#include "LayerRendererChromium.h"
#include "ManagedTexture.h"
#include "MathExtras.h"
#include "Region.h"
#include "TextStream.h"
#include "cc/CCLayerImpl.h"
#include "cc/CCTextureUpdater.h"
#include "cc/CCTiledLayerImpl.h"
#include <wtf/CurrentTime.h>

using namespace std;

namespace {
// Amount to downsample newly painted tiles during a fling. This is used to
// reduce paint times and avoid checkerboarding during quick fling animations.
const int kFlingDownsamplingFactor = 4;
}

namespace WebCore {

class UpdatableTile : public CCLayerTilingData::Tile {
    WTF_MAKE_NONCOPYABLE(UpdatableTile);
public:
    explicit UpdatableTile(PassOwnPtr<LayerTextureUpdater::Texture> texture)
        : m_partialUpdate(false)
        , m_updated(false)
        , isInUseOnImpl(false)
        , m_hasValidContents(false)
        , m_downsamplingFactor(1)
        , m_texture(texture) { }

    LayerTextureUpdater::Texture* texture() { return m_texture.get(); }
    ManagedTexture* managedTexture() { return m_texture->texture(); }

    bool isDirty() const { return !m_dirtyRect.isEmpty(); }
    void copyAndClearDirty()
    {
        m_updateRect = m_dirtyRect;
        m_dirtyRect = IntRect();
    }
    bool isDirtyForCurrentFrame() { return !m_dirtyRect.isEmpty() && !m_updated; }

    IntRect m_dirtyRect;
    IntRect m_updateRect;
    IntRect m_opaqueRect;
    bool m_partialUpdate;
    bool m_updated;
    bool isInUseOnImpl;
    // If the tile has a valid managed texture, this flag indicates whether the
    // contents of the texture are valid (but still possibly stale).
    bool m_hasValidContents;
    int m_downsamplingFactor;
private:
    OwnPtr<LayerTextureUpdater::Texture> m_texture;
};

TiledLayerChromium::TiledLayerChromium()
    : LayerChromium()
    , m_textureFormat(GraphicsContext3D::INVALID_ENUM)
    , m_skipsDraw(false)
    , m_skipsIdlePaint(false)
    , m_sampledTexelFormat(LayerTextureUpdater::SampledTexelFormatInvalid)
    , m_tilingOption(AutoTile)
{
    // When tiling is enabled, use tiles of this dimension squared.
    int defaultTileSize = 256;
    m_tiler = CCLayerTilingData::create(IntSize(defaultTileSize, defaultTileSize), CCLayerTilingData::HasBorderTexels);
}

TiledLayerChromium::~TiledLayerChromium()
{
}

PassRefPtr<CCLayerImpl> TiledLayerChromium::createCCLayerImpl()
{
    return CCTiledLayerImpl::create(id());
}

void TiledLayerChromium::cleanupResources()
{
    LayerChromium::cleanupResources();

    m_tiler->reset();
    m_paintRect = IntRect();
    m_requestedUpdateTilesRect = IntRect();
}

void TiledLayerChromium::updateTileSizeAndTilingOption()
{
    // When tiling is enabled, use tiles of this dimension squared.
    int defaultTileSize = TextureManager::defaultTileSize(layerTreeHost()->viewportSize());
    // Start tiling when the width and height of a layer are larger than this size.
    int maxUntiledSize = defaultTileSize * 2;

    const IntSize tileSize(min(defaultTileSize, contentBounds().width()), min(defaultTileSize, contentBounds().height()));

    // Tile if both dimensions large, or any one dimension large and the other
    // extends into a second tile. This heuristic allows for long skinny layers
    // (e.g. scrollbars) that are Nx1 tiles to minimize wasted texture space.
    const bool anyDimensionLarge = contentBounds().width() > maxUntiledSize || contentBounds().height() > maxUntiledSize;
    const bool anyDimensionOneTile = contentBounds().width() <= defaultTileSize || contentBounds().height() <= defaultTileSize;
    const bool autoTiled = anyDimensionLarge && !anyDimensionOneTile;

    bool isTiled;
    if (m_tilingOption == AlwaysTile)
        isTiled = true;
    else if (m_tilingOption == NeverTile)
        isTiled = false;
    else
        isTiled = autoTiled;

    IntSize requestedSize = isTiled ? tileSize : contentBounds();
    const int maxSize = layerTreeHost()->layerRendererCapabilities().maxTextureSize;
    IntSize clampedSize = requestedSize.shrunkTo(IntSize(maxSize, maxSize));
    setTileSize(clampedSize);
}

void TiledLayerChromium::updateBounds()
{
    IntSize oldBounds = m_tiler->bounds();
    IntSize newBounds = contentBounds();
    if (oldBounds == newBounds)
        return;
    m_tiler->setBounds(newBounds);

    // Invalidate any areas that the new bounds exposes.
    Region oldRegion(IntRect(IntPoint(), oldBounds));
    Region newRegion(IntRect(IntPoint(), newBounds));
    newRegion.subtract(oldRegion);
    Vector<IntRect> rects = newRegion.rects();
    for (size_t i = 0; i < rects.size(); ++i)
        invalidateRect(rects[i]);
}

void TiledLayerChromium::setTileSize(const IntSize& size)
{
    m_tiler->setTileSize(size);
}

void TiledLayerChromium::setBorderTexelOption(CCLayerTilingData::BorderTexelOption borderTexelOption)
{
    m_tiler->setBorderTexelOption(borderTexelOption, std::max(1, kFlingDownsamplingFactor / 2));
}

bool TiledLayerChromium::drawsContent() const
{
    if (!LayerChromium::drawsContent())
        return false;

    if (m_tilingOption == NeverTile && m_tiler->numTiles() > 1)
        return false;

    return true;
}

bool TiledLayerChromium::needsContentsScale() const
{
    return true;
}

IntSize TiledLayerChromium::contentBounds() const
{
    return IntSize(lroundf(bounds().width() * contentsScale()), lroundf(bounds().height() * contentsScale()));
}

FloatRect TiledLayerChromium::dirtyRect() const
{
    // TODO: consider to adjust the pageScaleFactor in the LayerChromium in the future.
    FloatRect dirtyRect = m_dirtyRect;
    dirtyRect.scale(contentsScale());
    return dirtyRect;
}

void TiledLayerChromium::setLayerTreeHost(CCLayerTreeHost* host)
{
    if (host == layerTreeHost())
        return;

    LayerChromium::setLayerTreeHost(host);

    if (!host)
        return;

    createTextureUpdater(host);
    setTextureFormat(host->layerRendererCapabilities().bestTextureFormat);
    m_sampledTexelFormat = textureUpdater()->sampledTexelFormat(m_textureFormat);
}

void TiledLayerChromium::updateCompositorResources(GraphicsContext3D*, CCTextureUpdater& updater)
{
    // Painting could cause compositing to get turned off, which may cause the tiler to become invalidated mid-update.
    if (m_skipsDraw || m_requestedUpdateTilesRect.isEmpty() || m_tiler->isEmpty())
        return;

    int left = m_requestedUpdateTilesRect.x();
    int top = m_requestedUpdateTilesRect.y();
    int right = m_requestedUpdateTilesRect.maxX() - 1;
    int bottom = m_requestedUpdateTilesRect.maxY() - 1;
    for (int j = top; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            UpdatableTile* tile = tileAt(i, j);

            // Required tiles are created in prepareToUpdate(). A tile should
            // never be removed between the call to prepareToUpdate() and the
            // call to updateCompositorResources().
            if (!tile)
                CRASH();

            IntRect sourceRect = tile->m_updateRect;
            if (tile->m_updateRect.isEmpty())
                continue;

            ASSERT(tile->managedTexture()->isReserved());
            const IntPoint anchor = m_tiler->tileRect(tile).location();

            // Calculate tile-space rectangle to upload into.
            IntRect destRect(IntPoint(sourceRect.x() - anchor.x(), sourceRect.y() - anchor.y()), sourceRect.size());
            if (destRect.x() < 0)
                CRASH();
            if (destRect.y() < 0)
                CRASH();

            // Offset from paint rectangle to this tile's dirty rectangle.
            IntPoint paintOffset(sourceRect.x() - m_paintRect.x(), sourceRect.y() - m_paintRect.y());
            if (paintOffset.x() < 0)
                CRASH();
            if (paintOffset.y() < 0)
                CRASH();
            if (paintOffset.x() + destRect.width() > m_paintRect.width())
                CRASH();
            if (paintOffset.y() + destRect.height() > m_paintRect.height())
                CRASH();

            if (tile->m_partialUpdate)
                updater.appendPartial(tile->texture(), sourceRect, destRect);
            else
                updater.append(tile->texture(), sourceRect, destRect);
        }
    }

    // The updateRect should be in layer space. So we have to convert the paintRect from content space to layer space.
    m_updateRect = FloatRect(m_paintRect);
    float widthScale = bounds().width() / static_cast<float>(contentBounds().width());
    float heightScale = bounds().height() / static_cast<float>(contentBounds().height());
    m_updateRect.scale(widthScale, heightScale);
}

void TiledLayerChromium::setTilingOption(TilingOption tilingOption)
{
    m_tilingOption = tilingOption;
}

void TiledLayerChromium::setIsMask(bool isMask)
{
    setTilingOption(isMask ? NeverTile : AutoTile);
}

void TiledLayerChromium::pushPropertiesTo(CCLayerImpl* layer)
{
    LayerChromium::pushPropertiesTo(layer);

    CCTiledLayerImpl* tiledLayer = static_cast<CCTiledLayerImpl*>(layer);

    tiledLayer->setSkipsDraw(m_skipsDraw);
    tiledLayer->setContentsSwizzled(m_sampledTexelFormat != LayerTextureUpdater::SampledTexelFormatRGBA);
    tiledLayer->setTilingData(*m_tiler);
    Vector<UpdatableTile*> invalidTiles;

    for (CCLayerTilingData::TileMap::const_iterator iter = m_tiler->tiles().begin(); iter != m_tiler->tiles().end(); ++iter) {
        int i = iter->first.first;
        int j = iter->first.second;
        UpdatableTile* tile = static_cast<UpdatableTile*>(iter->second.get());
        tile->isInUseOnImpl = false;
        if (!tile->managedTexture()->isValid(m_tiler->tileSize(), m_textureFormat)) {
            invalidTiles.append(tile);
            continue;
        }
#if !OS(ANDROID)
        // While this is strictly correct, some pages cause a lot of invalidations! We always
        // paint all the dirty tiles in the "visible rect" (webkit's view of visible), so this
        // only really effects "off-screen" tiles. But while scrolling, "off-screen" tiles can
        // become quickly visible right after they are invalidated. We can't paint all the
        // off-screen tiles instantly, so this just keeps the old ones around until we have
        // time to paint them. The trade-off here is there is a chance of seeing a seam in the
        // content (but only while scrolling) instead of seeing big flashes of background color.
        if (tile->isDirtyForCurrentFrame())
            continue;
#endif

        tiledLayer->pushTileProperties(i, j, tile->managedTexture()->textureId(), tile->m_opaqueRect, tile->m_downsamplingFactor);
        tile->isInUseOnImpl = true;
    }
    for (Vector<UpdatableTile*>::const_iterator iter = invalidTiles.begin(); iter != invalidTiles.end(); ++iter)
        m_tiler->takeTile((*iter)->i(), (*iter)->j());
}


void TiledLayerChromium::pageScaleChanged() {
    LayerChromium::pageScaleChanged();
    for (CCLayerTilingData::TileMap::const_iterator iter = m_tiler->tiles().begin(); iter != m_tiler->tiles().end(); ++iter) {
        UpdatableTile* tile = static_cast<UpdatableTile*>(iter->second.get());
        ASSERT(tile);
        // FIXME: All tiles should be invalidated elsewhere during a page scale,
        // but this wasn't always happening on the first pinch-zoom after the layer's
        // size change. This doesn't fix that issue, but since we have this
        // extra type of invalidation, we can fix the bug here just as easily by
        // dropping all textures as well.
        tile->managedTexture()->releaseTexture();
    }
}

TextureManager* TiledLayerChromium::textureManager() const
{
    if (!layerTreeHost())
        return 0;
    return layerTreeHost()->contentsTextureManager();
}

UpdatableTile* TiledLayerChromium::tileAt(int i, int j) const
{
    return static_cast<UpdatableTile*>(m_tiler->tileAt(i, j));
}

UpdatableTile* TiledLayerChromium::createTile(int i, int j)
{
    RefPtr<UpdatableTile> tile = adoptRef(new UpdatableTile(textureUpdater()->createTexture(textureManager())));
    m_tiler->addTile(tile, i, j);
    tile->m_dirtyRect = m_tiler->tileRect(tile.get());

    return tile.get();
}

void TiledLayerChromium::setNeedsDisplayRect(const FloatRect& dirtyRect)
{
    FloatRect scaledDirtyRect(dirtyRect);
    scaledDirtyRect.scale(contentsScale());
    IntRect dirty = enclosingIntRect(scaledDirtyRect);
    invalidateRect(dirty);
    LayerChromium::setNeedsDisplayRect(dirtyRect);
}

void TiledLayerChromium::setIsNonCompositedContent(bool isNonCompositedContent)
{
    LayerChromium::setIsNonCompositedContent(isNonCompositedContent);

    CCLayerTilingData::BorderTexelOption borderTexelOption;
#if OS(ANDROID)
    // Always want border texels and GL_LINEAR due to pinch zoom.
    borderTexelOption = CCLayerTilingData::HasBorderTexels;
#else
    borderTexelOption = isNonCompositedContent ? CCLayerTilingData::NoBorderTexels : CCLayerTilingData::HasBorderTexels;
#endif
    setBorderTexelOption(borderTexelOption);
}

void TiledLayerChromium::invalidateRect(const IntRect& layerRect)
{
    updateBounds();
    if (m_tiler->isEmpty() || layerRect.isEmpty() || m_skipsDraw)
        return;

    for (CCLayerTilingData::TileMap::const_iterator iter = m_tiler->tiles().begin(); iter != m_tiler->tiles().end(); ++iter) {
        UpdatableTile* tile = static_cast<UpdatableTile*>(iter->second.get());
        ASSERT(tile);
        IntRect bound = m_tiler->tileRect(tile);
        bound.intersect(layerRect);
        tile->m_dirtyRect.unite(bound);
    }
}

// Returns true if tile is dirty and only part of it needs to be updated.
bool TiledLayerChromium::tileOnlyNeedsPartialUpdate(UpdatableTile* tile)
{
    return !tile->m_dirtyRect.contains(m_tiler->tileRect(tile));
}

// Dirty tiles with valid textures needs buffered update to guarantee that
// we don't modify textures currently used for drawing by the impl thread.
bool TiledLayerChromium::tileNeedsBufferedUpdate(UpdatableTile* tile)
{
    // No impl thread?.
    if (!CCProxy::hasImplThread())
        return false;

    if (!tile->managedTexture()->isValid(m_tiler->tileSize(), m_textureFormat))
        return false;

    if (!tile->isDirty())
        return false;

    if (!tile->isInUseOnImpl)
        return false;

    return true;
}

void TiledLayerChromium::prepareToUpdateTiles(bool idle, int left, int top, int right, int bottom, int downsamplingFactor)
{
    // If we are about to paint some low resolution tiles, schedule a commit to
    // ensure they are eventually repainted at full resolution.
    if (downsamplingFactor > 1)
        setNeedsCommit();

    // Clamp to visible tiles.
    left   = std::min(std::max(left, 0), m_tiler->numTilesX() - 1);
    top    = std::min(std::max(top, 0), m_tiler->numTilesY() - 1);
    right  = std::min(std::max(right, 0), m_tiler->numTilesX() - 1);
    bottom = std::min(std::max(bottom, 0), m_tiler->numTilesY() - 1);

    // Create tiles as needed, expanding a dirty rect to contain all
    // the dirty regions currently being drawn. All dirty tiles that are to be painted
    // get their m_updateRect set to m_dirtyRect and m_dirtyRect cleared. This way if
    // invalidateRect is invoked during prepareToUpdate we don't lose the request.
    IntRect dirtyLayerRect;
    for (int j = top; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            UpdatableTile* tile = tileAt(i, j);
            if (!tile)
                tile = createTile(i, j);

            // If we are downsampling and the tile has previous contents that were painted at its
            // current location, reuse the tile instead of repainting. This is done to prefer stale
            // high resolution tiles over fuzzy but up-to-date ones during flings.
            if (downsamplingFactor > 1
                && tile->managedTexture()->isValid(m_tiler->tileSize(), m_textureFormat)
                && tile->m_hasValidContents)
                continue;

            // If we no longer want to downsample, mark downsampled tiles as dirty.
            if (tile->m_downsamplingFactor > downsamplingFactor)
                tile->m_dirtyRect = m_tiler->tileRect(tile);

            if (layerTreeHost() && layerTreeHost()->bufferedUpdates() && tileNeedsBufferedUpdate(tile)) {
                // FIXME: Decide if partial update should be allowed based on cost
                // of update. https://bugs.webkit.org/show_bug.cgi?id=77376
                if (tileOnlyNeedsPartialUpdate(tile) && layerTreeHost()->requestPartialTextureUpdate())
                    tile->m_partialUpdate = true;
                else {
                    // We no longer need to 'steal' the texture. If we just release the texture,
                    // the manager will not delete it while it is in-use.
                    // layerTreeHost()->deleteTextureAfterCommit(tile->managedTexture()->steal());
                    tile->managedTexture()->releaseTexture();
                }
            }

            if (!tile->managedTexture()->isValid(m_tiler->tileSize(), m_textureFormat)) {
                tile->m_dirtyRect = m_tiler->tileRect(tile);
                tile->m_hasValidContents = false;
            }

            tile->m_updated = true;

            if (!tile->managedTexture()->reserve(m_tiler->tileSize(), m_textureFormat)) {
                m_skipsIdlePaint = true;
                if (!idle) {
                    // If the background covers the viewport, always draw this
                    // layer so that checkerboarded tiles will still draw.
                    if (!backgroundCoversViewport())
                        m_skipsDraw = true;
                    cleanupResources();
                }
                return;
            }

            dirtyLayerRect.unite(tile->m_dirtyRect);
        }
    }

    // We are going to update the area currently marked as dirty. So
    // clear that dirty area and mark it for update instead.
    for (int j = top; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            UpdatableTile* tile = tileAt(i, j);
            if (tile->m_updated) {
                tile->copyAndClearDirty();
            }
        }
    }

    m_paintRect = dirtyLayerRect;
    if (dirtyLayerRect.isEmpty())
        return;

    // Due to borders, when the paint rect is extended to tile boundaries, it
    // may end up overlapping more tiles than the original content rect. Record
    // the original tiles so we don't upload more tiles than necessary.
    if (!m_paintRect.isEmpty())
        m_requestedUpdateTilesRect = IntRect(left, top, right - left + 1, bottom - top + 1);

    // Calling prepareToUpdate() calls into WebKit to paint, which may have the side
    // effect of disabling compositing, which causes our reference to the texture updater to be deleted.
    // However, we can't free the memory backing the GraphicsContext until the paint finishes,
    // so we grab a local reference here to hold the updater alive until the paint completes.
    RefPtr<LayerTextureUpdater> protector(textureUpdater());
    IntRect paintedOpaqueRect;
    textureUpdater()->prepareToUpdate(m_paintRect, m_tiler->tileSize(), m_tiler->hasBorderTexels(), contentsScale(), &paintedOpaqueRect, downsamplingFactor);
    for (int j = top; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            UpdatableTile* tile = tileAt(i, j);
            // Tiles are created before prepareToUpdate() is called.
            if (!tile)
                CRASH();

            IntRect tileRect = m_tiler->tileBounds(i, j);

            // Save what was painted opaque in the tile. If everything painted in the tile was opaque, and the area is a subset of an
            // already opaque area, keep the old area.
            IntRect tilePaintedRect = intersection(tileRect, m_paintRect);
            IntRect tilePaintedOpaqueRect = intersection(tileRect, paintedOpaqueRect);
            if (tilePaintedOpaqueRect != tilePaintedRect || !tile->m_opaqueRect.contains(tilePaintedOpaqueRect))
                tile->m_opaqueRect = tilePaintedOpaqueRect;

            // Use m_updateRect as copyAndClearDirty above moved the existing dirty rect to m_updateRect.
            const IntRect& dirtyRect = tile->m_updateRect;
            if (dirtyRect.isEmpty())
                continue;

            // sourceRect starts as a full-sized tile with border texels included.
            IntRect sourceRect = m_tiler->tileRect(tile);
            sourceRect.intersect(dirtyRect);
            // Paint rect not guaranteed to line up on tile boundaries, so
            // make sure that sourceRect doesn't extend outside of it.
            sourceRect.intersect(m_paintRect);

            tile->m_updateRect = sourceRect;
            if (sourceRect.isEmpty())
                continue;

            tile->m_downsamplingFactor = downsamplingFactor;
            tile->m_hasValidContents = true;
            tile->texture()->prepareRect(sourceRect);
        }
    }
}

void TiledLayerChromium::reserveTextures()
{
    updateBounds();

    const IntRect& layerRect = visibleLayerRect();
    if (layerRect.isEmpty() || !m_tiler->numTiles())
        return;

    int left, top, right, bottom;
    m_tiler->layerRectToTileIndices(layerRect, left, top, right, bottom);

    reserveTiles(left, top, right, bottom);
}

bool TiledLayerChromium::reserveTiles(int left, int top, int right, int bottom)
{
    // Clamp to visible tiles.
    left   = std::min(std::max(left, 0), m_tiler->numTilesX() - 1);
    top    = std::min(std::max(top, 0), m_tiler->numTilesY() - 1);
    right  = std::min(std::max(right, 0), m_tiler->numTilesX() - 1);
    bottom = std::min(std::max(bottom, 0), m_tiler->numTilesY() - 1);

    for (int j = top; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            UpdatableTile* tile = tileAt(i, j);
            if (!tile)
                tile = createTile(i, j);

            if (!tile->managedTexture()->isValid(m_tiler->tileSize(), m_textureFormat)) {
                tile->m_dirtyRect = m_tiler->tileRect(tile);
                tile->m_hasValidContents = false;
            }

            if (!tile->managedTexture()->reserve(m_tiler->tileSize(), m_textureFormat))
                return false;
        }
    }
    return true;
}

void TiledLayerChromium::resetUpdateState()
{
    // Reset m_updateRect for all tiles.
    CCLayerTilingData::TileMap::const_iterator end = m_tiler->tiles().end();
    for (CCLayerTilingData::TileMap::const_iterator iter = m_tiler->tiles().begin(); iter != end; ++iter) {
        UpdatableTile* tile = static_cast<UpdatableTile*>(iter->second.get());
        tile->m_updateRect = IntRect();
        tile->m_partialUpdate = false;
#if OS(ANDROID)
        tile->m_updated = false;
#endif
    }
}


#if OS(ANDROID)
namespace {
class QueryAnimationClient : public WebCore::CCLayerAnimationControllerClient {
public:
    QueryAnimationClient() : m_opacity(0) {}
    virtual ~QueryAnimationClient() {}

    // CCLayerAnimationControllerClient implementation
    virtual int id() const OVERRIDE { return 0; }
    virtual void setOpacityFromAnimation(float opacity) OVERRIDE { m_opacity = opacity; }
    virtual float opacity() const OVERRIDE { return m_opacity; }
    virtual void setTransformFromAnimation(const WebCore::TransformationMatrix& transform) OVERRIDE { m_transform = transform; }
    virtual const WebCore::TransformationMatrix& transform() const OVERRIDE { return m_transform; }
    virtual const WebCore::IntSize& bounds() const OVERRIDE { return m_bounds; }

private:
    float m_opacity;
    WebCore::TransformationMatrix m_transform;
    WebCore::IntSize m_bounds;
};

// This is the same as isScaleOrTranslation in CCLayerTreeHostCommon upstream,
// except this allows m43 to be set, and we don't allow negative scales.
// z-translation (m43) doesn't effect x/y directions which is what we care
// about here.
bool transformPreservesDirection(const TransformationMatrix& m)
{
    return    !m.m12() && !m.m13() && !m.m14()
           && !m.m21() && !m.m23() && !m.m24()
           && !m.m31() && !m.m32() &&  m.m44()
           && m.m11() > 0
           && m.m22() > 0;
}

// This is the same as in CCLayerTreeHostCommon upstream, except this allows
// m43() to be set. z-translation doesn't effect x/y directions which is
// what we care about here.
bool isScaleOrTranslation(const TransformationMatrix& m)
{
    return    !m.m12() && !m.m13() && !m.m14()
           && !m.m21() && !m.m23() && !m.m24()
           && !m.m31() && !m.m32() && m.m44();
}

CCLayerAnimationController* simpleAnimationController(LayerChromium* layer) {
    CCLayerAnimationController* controller = NULL;
    while (layer) {
        // Bail if any transform does not preserve x/y directions.
        if (!transformPreservesDirection(layer->drawTransform()))
            return NULL;
        if (layer->transformIsAnimating()) {
            // Bail if more than one animation effects the layer.
            if (controller)
                return NULL;
            controller = layer->layerAnimationController();
        }
        layer = layer->parent();
    }
    if (controller && controller->allActiveAnimationsAreTransitions())
       return controller;
    return NULL;
}

bool isAnimating(LayerChromium* layer) {
    while (layer) {
        if (layer->transformIsAnimating())
            return true;
        layer = layer->parent();
    }
    return false;
}

} // namespace

#endif // ANDROID

IntRect TiledLayerChromium::unclippedVisibleRect() {
    // The unclipped rect calculated below is more useful,
    // but to reduce risk of new code, we fall back when:
    // - the layer drawTransform() is not a simple scale/translate
    // - there is no target surface (not sure why, but this can happen)
    // We also skip this step to avoid extra work when:
    // - the layer or surface rect is empty
    // - the layer is completely contained in the target surface rect
    const IntRect layerBoundRect = IntRect(IntPoint(), contentBounds());
    IntRect targetSurfaceRect = targetRenderSurface() ? targetRenderSurface()->contentRect() : IntRect();
    TransformationMatrix transform = drawTransform();
    if (layerBoundRect == visibleLayerRect() ||
            !isAnimating(this) ||
            targetSurfaceRect.isEmpty() ||
            contentBounds().isEmpty() ||
            !isScaleOrTranslation(transform)) {
        return visibleLayerRect();
    }

    // Note: Adding this functionality is being discussed upstream here:
    // https://bugs.webkit.org/show_bug.cgi?id=82251
    //
    // This is code is similar to calculateVisibleLayerRect in CCLayerTreeHostCommon.cpp
    // The visibleLayerRect is the target surface rect transformed into layer
    // space. However, it is clipped several times, such that it will be empty
    // for offscreen layers. The code below calculates the unclipped rect instead.
    transform.scaleNonUniform(bounds().width() / static_cast<double>(contentBounds().width()),
                              bounds().height() / static_cast<double>(contentBounds().height()));
    transform.translate(-contentBounds().width() / 2.0, -contentBounds().height() / 2.0);
    const TransformationMatrix surfaceToLayer = transform.inverse();
    IntRect unclippedLayerRect = surfaceToLayer.projectQuad(FloatQuad(FloatRect(targetSurfaceRect))).enclosingBoundingBox();
    return unclippedLayerRect;
}

void TiledLayerChromium::prepareToUpdate(const IntRect& layerRect)
{
    m_skipsDraw = false;
    m_skipsIdlePaint = false;
    m_requestedUpdateTilesRect = IntRect();
    m_paintRect = IntRect();

    updateBounds();

    resetUpdateState();

    if (layerRect.isEmpty() || !m_tiler->numTiles())
        return;

    int visibleLeft, visibleTop, visibleRight, visibleBottom;
    m_tiler->layerRectToTileIndices(layerRect, visibleLeft, visibleTop, visibleRight, visibleBottom);

    int left = visibleLeft;
    int top = visibleTop;
    int right = visibleRight;
    int bottom = visibleBottom;

#if !OS(ANDROID)
    prepareToUpdateTiles(false, left, top, right, bottom);
#else
    // Page-transitions are quite identifiable since the layers involved are exactly
    // the viewport size. Paint these layers immediately even if they are offscreen
    // so we don't see any checkerboard.
    // FIXME: On Nikasi I was seeing the viewport size being two pixels larger than
    //        the content size. For this reason I've added 64 pixels of padding below
    //        to be sure we catch viewport sized layers.
    IntSize viewportSize = layerTreeHost() ? layerTreeHost()->viewportSize() : IntSize();
    IntSize contentSize = contentBounds();
    if (isAnimating(this) && contentSize.width() <= viewportSize.width() + 64
                          && contentSize.height() <= viewportSize.height() + 64) {
        IntRect fullAnimatedLayerRect = IntRect(IntPoint::zero(), contentSize);
        m_tiler->layerRectToTileIndices(fullAnimatedLayerRect, left, top, right, bottom);
        if (reserveTiles(left, top, right, bottom))
            prepareToUpdateTiles(false, left, top, right, bottom);
        else
            prepareToUpdateTiles(false, visibleLeft, visibleTop, visibleRight, visibleBottom);
        return;
    }

    // If our scroll prediction is small don't change our painting behavior (let
    // idle painting fill the surrounding tiles slowly). However, if the scroll
    // prediction is too large, sacrifice paint responsiveness and paint an entire
    // viewport worth of tiles in the scroll direction.
    IntSize scroll = scrollPrediction();
    float currentPageScalePrediction = pageScalePrediction();
    setScrollPrediction(IntSize());
    setPageScalePrediction(1);

    // If the layer is animating in a predictable way (only scale/translate), we
    // didn't paint the entire layer above and we aren't scrolling, then use
    // animation direction. This is usefull for sites like gmail that have
    // large animated layers.
    CCLayerAnimationController* animation = NULL;
    if (scroll.isEmpty() && (animation = simpleAnimationController(this))) {
        m_tiler->layerRectToUnclampedTileIndices(layerRect, left, top, right, bottom);
        QueryAnimationClient client;
        animation->animateClient(0, &client);
        TransformationMatrix start = client.transform();
        animation->animateClient(numeric_limits<double>::max(), &client);
        TransformationMatrix end = client.transform();
        // Scroll direction is opposite animation direction.
        if (transformPreservesDirection(start) && transformPreservesDirection(end))
            scroll = IntSize(start.m41() - end.m41(), start.m42() - end.m42());
    }

    int minScroll = std::min(abs(scroll.width()), abs(scroll.height()));
    int maxScroll = std::max(abs(scroll.width()), abs(scroll.height()));
    bool diagonalScroll = maxScroll < 2 * minScroll;
    int scrollThreshold = m_tiler->tileSize().width() / 4;

    if (maxScroll < scrollThreshold || diagonalScroll) {
        prepareToUpdateTiles(false, visibleLeft, visibleTop, visibleRight, visibleBottom);
        return;
    }

    if (abs(scroll.width()) > abs(scroll.height())) {
        if (scroll.width() > 0) {
            // Scrolling right
            int offset = firstDirtyColumn(left, top, right + 1, bottom) - left;
            ASSERT(offset >= 0 && offset <= (right - left + 2));
            if (animation)
                right += offset;
            else
                right = min(right + offset, max(right, left + offset + 1));
        } else {
            // Scrolling left
            int offset = lastDirtyColumn(left - 1, top, right, bottom) - right;
            ASSERT(offset <= 0 && offset >= -(right - left + 2));
            if (animation)
                left += offset;
            else
                left = max(left + offset, min(left, right + offset - 1));
        }
    } else {
        if (scroll.height() > 0) {
            // Scrolling down
            int offset = firstDirtyRow(left, top, right, bottom + 1) - top;
            ASSERT(offset >= 0 && offset <= (bottom - top + 2));
            if (animation)
                bottom += offset;
            else
                bottom = min(bottom + offset, max(bottom, top + offset + 1));
        } else {
            // Scrolling up
            int offset = lastDirtyRow(left, top - 1, right, bottom) - bottom;
            ASSERT(offset <= 0 && offset >= -(bottom - top + 2));
            if (animation)
                top += offset;
            else
                top = max(top + offset, min(top, bottom + offset - 1));
        }
    }

    // Downsample tiles while flinging on high dpi devices. Avoid doing this
    // right after the page scale has changed, because all the tiles would need
    // to be repainted at high resolution soon anyway.
    bool shouldDownsampleWhileFlinging = m_tiler->tileSize().width() >= 512;
    bool pageScaleChanged = fabs(currentPageScalePrediction - 1) > 1e-3;
    int downsamplingFactor = shouldDownsampleWhileFlinging && !pageScaleChanged && textureUpdater()->canDownsample() ? kFlingDownsamplingFactor : 1;

    if (reserveTiles(left, top, right, bottom))
        prepareToUpdateTiles(false, left, top, right, bottom, downsamplingFactor);
    else
        prepareToUpdateTiles(false, visibleLeft, visibleTop, visibleRight, visibleBottom, downsamplingFactor);
#endif
}

void TiledLayerChromium::prepareToUpdateIdle(const IntRect& layerRect)
{
    // Abort if we have already prepared a paint or run out of memory.
    if (m_skipsIdlePaint || !m_paintRect.isEmpty())
        return;

    ASSERT(m_tiler);

    updateBounds();

    if (m_tiler->isEmpty())
        return;

    // Protect any textures in the pre-paint area so we don't end up just
    // reclaiming them below.
    IntRect idlePaintLayerRect = idlePaintRect(layerRect);

    // Expand outwards until we find a dirty row or column to update.
    int left, top, right, bottom;
    m_tiler->layerRectToTileIndices(layerRect, left, top, right, bottom);
    int prepaintLeft, prepaintTop, prepaintRight, prepaintBottom;
    m_tiler->layerRectToTileIndices(idlePaintLayerRect, prepaintLeft, prepaintTop, prepaintRight, prepaintBottom);
    while (!m_skipsIdlePaint && (left > prepaintLeft || top > prepaintTop || right < prepaintRight || bottom < prepaintBottom)) {
        if (bottom < prepaintBottom) {
            ++bottom;
            prepareToUpdateTiles(true, left, bottom, right, bottom);
            if (!m_paintRect.isEmpty() || m_skipsIdlePaint)
                break;
        }
        if (top > prepaintTop) {
            --top;
            prepareToUpdateTiles(true, left, top, right, top);
            if (!m_paintRect.isEmpty() || m_skipsIdlePaint)
                break;
        }
        if (left > prepaintLeft) {
            --left;
            prepareToUpdateTiles(true, left, top, left, bottom);
            if (!m_paintRect.isEmpty() || m_skipsIdlePaint)
                break;
        }
        if (right < prepaintRight) {
            ++right;
            prepareToUpdateTiles(true, right, top, right, bottom);
            if (!m_paintRect.isEmpty() || m_skipsIdlePaint)
                break;
        }
    }
}

bool TiledLayerChromium::needsIdlePaint(const IntRect& layerRect)
{
    if (m_skipsIdlePaint)
        return false;

    IntRect idlePaintLayerRect = idlePaintRect(layerRect);

    int left, top, right, bottom;
    m_tiler->layerRectToTileIndices(idlePaintLayerRect, left, top, right, bottom);
    for (int j = top; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            if (m_requestedUpdateTilesRect.contains(IntPoint(i, j)))
                continue;
            UpdatableTile* tile = tileAt(i, j);
            if (!tile || !tile->managedTexture()->isValid(m_tiler->tileSize(), m_textureFormat) || tile->isDirty())
                return true;
        }
    }
    return false;
}

IntRect TiledLayerChromium::idlePaintRect(const IntRect& visibleLayerRect)
{
    IntRect prepaintRect = visibleLayerRect;
    // FIXME: This can be made a lot larger if we can:
    // - reserve memory at a lower priority than for visible content
    // - only reserve idle paint tiles up to a memory reclaim threshold and
    // - insure we play nicely with other layers
    prepaintRect.inflateX(m_tiler->tileSize().width());
    prepaintRect.inflateY(m_tiler->tileSize().height() * (isRootScrollingLayer() ? 6 : 2));
    prepaintRect.intersect(IntRect(IntPoint::zero(), contentBounds()));
    return prepaintRect;
}


#if OS(ANDROID)

bool TiledLayerChromium::tileIsDirty(int i, int j)
{
    // Out-of-bounds tiles are not dirty.
    if (i < 0 || i >= m_tiler->numTilesX() || j < 0 || j >= m_tiler->numTilesY())
        return false;

    UpdatableTile* tile = tileAt(i, j);
    return !tile ||
            tile->isDirty() ||
           !tile->managedTexture()->isValid(m_tiler->tileSize(), m_textureFormat);
}

int TiledLayerChromium::firstDirtyRow(int left, int top, int right, int bottom)
{
    int j = top;
    for (; j <= bottom; ++j) {
        for (int i = left; i <= right; ++i) {
            if (tileIsDirty(i, j))
                return j;
        }
    }
    return j;
}

int TiledLayerChromium::firstDirtyColumn(int left, int top, int right, int bottom)
{
    int i = left;
    for (; i <= right; ++i) {
        for (int j = top; j <= bottom; ++j) {
            if (tileIsDirty(i, j))
                return i;
        }
    }
    return i;
}

int TiledLayerChromium::lastDirtyRow(int left, int top, int right, int bottom)
{
    int j = bottom;
    for (; j >= top; --j) {
        for (int i = left; i <= right; ++i) {
            if (tileIsDirty(i, j))
                return j;
        }
    }
    return j;
}

int TiledLayerChromium::lastDirtyColumn(int left, int top, int right, int bottom)
{
    int i = right;
    for (; i >= left; --i) {
        for (int j = top; j <= bottom; ++j) {
            if (tileIsDirty(i, j))
                return i;
        }
    }
    return i;
}

#endif


}
#endif // USE(ACCELERATED_COMPOSITING)
