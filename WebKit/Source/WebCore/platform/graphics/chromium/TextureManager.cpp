/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
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

#if USE(ACCELERATED_COMPOSITING)

#include "TextureManager.h"

#include "LayerRendererChromium.h"
#include "PlatformSupport.h"

using namespace std;

namespace WebCore {


namespace {

size_t maxMemoryLimitForDevice()
{
    static int maxTextureLimit = -1;
    if (maxTextureLimit == -1)
        maxTextureLimit = PlatformSupport::maxTextureMemoryUsageMB();
    return maxTextureLimit;
}

size_t memoryLimitBytes(size_t viewportMultiplier, const IntSize& viewportSize, size_t minMegabytes, size_t maxMegabytes)
{
    if (viewportSize.isEmpty())
        return minMegabytes * 1024 * 1024;
    return max(minMegabytes * 1024 * 1024, min(maxMegabytes * 1024 * 1024, viewportMultiplier * TextureManager::memoryUseBytes(viewportSize, GraphicsContext3D::RGBA)));
}
}

size_t TextureManager::defaultTileSize(const IntSize& viewportSize)
{
    int num256Tiles = (viewportSize.height() * viewportSize.width()) / (256 * 256);
    if (num256Tiles <= 36)
        return 256;
    else
        return 512;
}

size_t TextureManager::maxUploadsPerFrame(const IntSize& viewportSize, bool redrawPending)
{
    // Here is some data that guided these choices
    // Nexus 7 / Galaxy Nexus / Manta / Mako:
    // - 4/6 tiles per row for portrait/landscape
    // - 18-24 tiles visible on tablets
    // - 15-20 tiles visible on phones.

    // 6 256x256 textures is better than 5 as we we can
    // upload an entire row and prevent an extra 16ms delay
    // before painting again. Similarly 3 512x512 tiles does
    // this in 2 frames.
    // When not animating/scrolling, 24 textures will always
    // complete a full frame at once, while only adding
    // ~1 frame of initial scroll-start latency.

    size_t tileSize = defaultTileSize(viewportSize);
    if (tileSize == 256)
        return redrawPending ? 6 : 24;
    if (tileSize == 512)
        return redrawPending ? 3 : 24;
    ASSERT_NOT_REACHED();
    return 5;
}

size_t TextureManager::highLimitBytes(const IntSize& viewportSize)
{
    size_t viewportMultiplier, minMegabytes, maxMegabytes;
    viewportMultiplier = 24;
#if OS(ANDROID)
    minMegabytes = 48;
    maxMegabytes = maxMemoryLimitForDevice();
#else
    minMegabytes = 64;
    maxMegabytes = 128;
#endif
    return memoryLimitBytes(viewportMultiplier, viewportSize, minMegabytes, maxMegabytes);
}

size_t TextureManager::reclaimLimitBytes(const IntSize& viewportSize)
{
    size_t viewportMultiplier, minMegabytes, maxMegabytes;
    viewportMultiplier = 18;
#if OS(ANDROID)
    minMegabytes = 32;
    maxMegabytes = maxMemoryLimitForDevice() * 3 / 4;
#else
    minMegabytes = 32;
    maxMegabytes = 64;
#endif
    return memoryLimitBytes(viewportMultiplier, viewportSize, minMegabytes, maxMegabytes);
}

size_t TextureManager::lowLimitBytes(const IntSize& viewportSize)
{
#if OS(ANDROID)
    // TODO: please remove this when http://b/issue?id=5721448 is fixed.
    return 0;
#else
    size_t viewportMultiplier, minMegabytes, maxMegabytes;
    viewportMultiplier = 1;
    minMegabytes = 2;
    maxMegabytes = 3;
    return memoryLimitBytes(viewportMultiplier, viewportSize, minMegabytes, maxMegabytes);
#endif
}

size_t TextureManager::memoryUseBytes(const IntSize& size, GC3Denum textureFormat)
{
    // FIXME: This assumes all textures are 1 byte/component.
    const GC3Denum type = GraphicsContext3D::UNSIGNED_BYTE;
    unsigned int componentsPerPixel = 4;
    unsigned int bytesPerComponent = 1;
    if (!GraphicsContext3D::computeFormatAndTypeParameters(textureFormat, type, &componentsPerPixel, &bytesPerComponent))
        ASSERT_NOT_REACHED();

    return size.width() * size.height() * componentsPerPixel * bytesPerComponent;
}


TextureManager::TextureManager(size_t maxMemoryLimitBytes, size_t preferredMemoryLimitBytes, int maxTextureSize)
    : m_maxMemoryLimitBytes(maxMemoryLimitBytes)
    , m_preferredMemoryLimitBytes(preferredMemoryLimitBytes)
    , m_memoryUseBytes(0)
    , m_maxTextureSize(maxTextureSize)
    , m_defaultSize(256)
    , m_defaultFormat(GraphicsContext3D::RGBA)
    , m_nextToken(1)
{
}

void TextureManager::setMaxMemoryLimitBytes(size_t memoryLimitBytes)
{
    reduceMemoryToLimit(memoryLimitBytes);
    ASSERT(currentMemoryUseBytes() <= memoryLimitBytes);
    m_maxMemoryLimitBytes = memoryLimitBytes;
}

void TextureManager::setPreferredMemoryLimitBytes(size_t memoryLimitBytes)
{
    m_preferredMemoryLimitBytes = memoryLimitBytes;
}

TextureToken TextureManager::getToken()
{
    return m_nextToken++;
}

void TextureManager::releaseToken(TextureToken token)
{
    // It is unsafe to call find() with a null token
    ASSERT(token);
    if (!token)
        return;
    TextureMap::iterator it = m_textures.find(token);
    if (it != m_textures.end())
        removeTexture(token, it->second);
}

bool TextureManager::hasTexture(TextureToken token)
{
    return m_textures.contains(token);
}

bool TextureManager::isProtected(TextureToken token)
{
    return token && hasTexture(token) && m_textures.get(token).isProtected;
}

void TextureManager::protectTexture(TextureToken token)
{
    ASSERT(hasTexture(token));
    TextureInfo info = m_textures.take(token);
    info.isProtected = true;
    m_textures.add(token, info);
    // If someone protects a texture, put it at the end of the LRU list.
    m_textureLRUSet.remove(token);
    m_textureLRUSet.add(token);
}

void TextureManager::unprotectTexture(TextureToken token)
{
    TextureMap::iterator it = m_textures.find(token);
    if (it != m_textures.end())
        it->second.isProtected = false;
}

void TextureManager::unprotectAllTextures()
{
    for (TextureMap::iterator it = m_textures.begin(); it != m_textures.end(); ++it)
        it->second.isProtected = false;
}

void TextureManager::reduceMemoryToLimit(size_t limit)
{
    while (m_memoryUseBytes > limit) {
        ASSERT(!m_textureLRUSet.isEmpty());
        bool foundCandidate = false;
        for (ListHashSet<TextureToken>::iterator lruIt = m_textureLRUSet.begin(); lruIt != m_textureLRUSet.end(); ++lruIt) {
            TextureToken token = *lruIt;
            TextureInfo info = m_textures.get(token);
            if (info.isProtected)
                continue;
            removeTexture(token, info);
            foundCandidate = true;
            break;
        }
        if (!foundCandidate)
            return;
    }
}

unsigned TextureManager::recycleTexture(TextureToken newToken, TextureInfo newInfo)
{
#if !ASSERT_DISABLED
    bool reachedNotFree = false;
    for (ListHashSet<TextureToken>::iterator lruIt = m_textureLRUSet.begin(); lruIt != m_textureLRUSet.end(); ++lruIt) {
        TextureToken token = *lruIt;
        TextureInfo info = m_textures.get(token);
        // Check that free textures are always first in the list.
        if (!info.isFree)
            reachedNotFree = true;
        ASSERT(reachedNotFree || info.isFree);
    }
#endif
    if (!hasDefaultDimensions(newInfo))
        return 0;
    for (ListHashSet<TextureToken>::iterator lruIt = m_textureLRUSet.begin(); lruIt != m_textureLRUSet.end(); ++lruIt) {
        TextureToken token = *lruIt;
        TextureInfo info = m_textures.get(token);
        // If we have reached non-free textures, exit early.
        if (!info.isFree)
            break;
        newInfo.textureId = info.textureId;
#ifndef NDEBUG
        newInfo.allocator = info.allocator;
#endif
        m_textures.remove(token);
        m_textureLRUSet.remove(token);
        m_textures.set(newToken, newInfo);
        m_textureLRUSet.add(newToken);
        return info.textureId;
    }
    return 0;
}

void TextureManager::addTexture(TextureToken token, TextureInfo info)
{
    ASSERT(!m_textureLRUSet.contains(token));
    ASSERT(!m_textures.contains(token));
    m_memoryUseBytes += memoryUseBytes(info.size, info.format);
    m_textures.set(token, info);
    m_textureLRUSet.add(token);
}

void TextureManager::deleteEvictedTextures(TextureAllocator* allocator, bool recycle)
{
    if (!allocator) {
        m_evictedTextures.clear();
        return;
    }
    for (size_t i = 0; i < m_evictedTextures.size(); ++i) {
        if (!m_evictedTextures[i].textureId)
            continue;
#ifndef NDEBUG
        ASSERT(m_evictedTextures[i].allocator == allocator);
#endif
        if (!recycle || !hasDefaultDimensions(m_evictedTextures[i]) || currentMemoryUseBytes() >= preferredMemoryLimitBytes()) {
            allocator->deleteTexture(m_evictedTextures[i].textureId, m_evictedTextures[i].size, m_evictedTextures[i].format);
            continue;
        }
        TextureInfo info;
        info.size = m_evictedTextures[i].size;
        info.format = m_evictedTextures[i].format;
        info.textureId = m_evictedTextures[i].textureId;
        info.isProtected = false;
        info.isFree = true;
#ifndef NDEBUG
        info.allocator = m_evictedTextures[i].allocator;
#endif
        TextureToken token = getToken();
        m_textures.add(token, info);
        m_textureLRUSet.insertBefore(m_textureLRUSet.begin(), token);
        m_memoryUseBytes += memoryUseBytes(info.size, info.format);
    }
    m_evictedTextures.clear();
}

void TextureManager::evictAndDeleteAllTextures(TextureAllocator* allocator)
{
    unprotectAllTextures();
    reduceMemoryToLimit(0);
    deleteEvictedTextures(allocator, false);
}

void TextureManager::removeTexture(TextureToken token, TextureInfo info)
{
    ASSERT(m_textureLRUSet.contains(token));
    ASSERT(m_textures.contains(token));
    m_memoryUseBytes -= memoryUseBytes(info.size, info.format);
    m_textures.remove(token);
    ASSERT(m_textureLRUSet.contains(token));
    m_textureLRUSet.remove(token);
    EvictionEntry entry;
    entry.textureId = info.textureId;
    entry.size = info.size;
    entry.format = info.format;
#ifndef NDEBUG
    entry.allocator = info.allocator;
#endif
    m_evictedTextures.append(entry);
}

unsigned TextureManager::allocateTexture(TextureAllocator* allocator, TextureToken token)
{
    TextureMap::iterator it = m_textures.find(token);
    ASSERT(it != m_textures.end());
    TextureInfo* info = &it.get()->second;
    ASSERT(info->isProtected);

    unsigned textureId = allocator->createTexture(info->size, info->format);
    info->textureId = textureId;
#ifndef NDEBUG
    info->allocator = allocator;
#endif
    return textureId;
}

bool TextureManager::requestTexture(TextureToken token, IntSize size, unsigned format, unsigned& textureId)
{
    textureId = 0;

    if (size.width() > m_maxTextureSize || size.height() > m_maxTextureSize)
        return false;

    TextureMap::iterator it = m_textures.find(token);
    if (it != m_textures.end()) {
        ASSERT(it->second.size != size || it->second.format != format);
        removeTexture(token, it->second);
    }

    size_t memoryRequiredBytes = memoryUseBytes(size, format);
    if (memoryRequiredBytes > m_maxMemoryLimitBytes)
        return false;

    reduceMemoryToLimit(m_maxMemoryLimitBytes - memoryRequiredBytes);
    if (m_memoryUseBytes + memoryRequiredBytes > m_maxMemoryLimitBytes)
        return false;

    TextureInfo info;
    info.size = size;
    info.format = format;
    info.textureId = 0;
    info.isProtected = true;
    info.isFree = false;
#ifndef NDEBUG
    info.allocator = 0;
#endif
    // Avoid churning by reusing same-sized textures that are free.
    textureId = recycleTexture(token, info);
    if (textureId)
        return true;

    addTexture(token, info);
    return true;
}

size_t TextureManager::desiredPreAllocationsRemaining()
{
    if (currentMemoryUseBytes() >= preferredMemoryLimitBytes())
        return 0;

    size_t freeMemoryBytes = 0;
    for (ListHashSet<TextureToken>::iterator tokenIt = m_textureLRUSet.begin(); tokenIt != m_textureLRUSet.end(); ++tokenIt) {
        TextureInfo info = m_textures.get(*tokenIt);
        // All free textures are at the start of the list.
        if (!info.isFree)
            break;
        ASSERT(info.textureId);
        if (!info.isProtected)
            freeMemoryBytes += memoryUseBytes(info.size, info.format);
    }

    // Preallocate at most 20% of memory as free textures.
    size_t maxPreallocatedBytes = preferredMemoryLimitBytes() / 5;
    if (freeMemoryBytes >= maxPreallocatedBytes)
        return 0;
    maxPreallocatedBytes -= freeMemoryBytes;

    // Count evicted recyclable as part of our current memory use,
    // since they will be reclaimed for recycling.
    // FIXME: This should probably be accounted for in currentMemoryUseBytes()
    size_t willBeFreeBytes = 0;
    for (Vector<EvictionEntry>::iterator it = m_evictedTextures.begin(); it != m_evictedTextures.end(); ++it) {
        if (hasDefaultDimensions(*it));
            willBeFreeBytes += memoryUseBytes(it->size, it->format);
    }
    size_t actualMemoryUseBytes = currentMemoryUseBytes() + willBeFreeBytes;
    if (actualMemoryUseBytes >= preferredMemoryLimitBytes())
        return 0;

    size_t memoryRemaining = preferredMemoryLimitBytes() - actualMemoryUseBytes;
    size_t desiredPreallocatedBytes = min(maxPreallocatedBytes, memoryRemaining);

    return desiredPreallocatedBytes / memoryUseBytes(IntSize(m_defaultSize, m_defaultSize), m_defaultFormat);
}

void TextureManager::takePreAllocatedTextures(Vector<unsigned>& textureIds, IntSize size, GC3Denum format, TextureAllocator* allocator)
{
    ASSERT(size.width() == (int)m_defaultSize && size.height() == (int)m_defaultSize);
    ASSERT(format == m_defaultFormat);

    // For each texture, create a free texture token that is available for immediate recycling.
    for (size_t i = 0; i < textureIds.size(); i++) {
        TextureInfo info;
        info.size = size;
        info.format = format;
        info.textureId = textureIds[i];
        info.isProtected = false;
        info.isFree = true;
#ifndef NDEBUG
        info.allocator = allocator;
#endif
        TextureToken token = getToken();
        m_textures.add(token, info);
        m_textureLRUSet.insertBefore(m_textureLRUSet.begin(), token);
        m_memoryUseBytes += memoryUseBytes(size, format);
    }
    textureIds.clear();
}

bool TextureManager::hasDefaultDimensions(const TextureInfo& info)
{
    return info.size.width() == (int)defaultSize()
        && info.size.height() == (int)defaultSize()
        && info.format == defaultFormat();
}

bool TextureManager::hasDefaultDimensions(const EvictionEntry& info)
{
    return info.size.width() == (int)defaultSize()
        && info.size.height() == (int)defaultSize()
        && info.format == defaultFormat();
}

}

#endif // USE(ACCELERATED_COMPOSITING)
