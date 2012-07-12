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

#ifndef FontBoostingCluster_h
#define FontBoostingCluster_h

#if OS(ANDROID) && ENABLE(FONT_BOOSTING)

// FIXME(johnme): Remove all FB_DEBUG code before upstreaming.
#define FB_DEBUG 0

#if FB_DEBUG
#include <cutils/log.h>
#define STRINGIFY(X) DO_STRINGIFY(X)
#define DO_STRINGIFY(X) #X
#define FB_LOGF(...) android_printLog(ANDROID_LOG_FATAL, "webkit", "[" __FILE__ "(" STRINGIFY(__LINE__) ")]  " __VA_ARGS__)
#else
#define FB_LOGF(...)
#endif

#include "IntRect.h"
#include "RenderStyleConstants.h"
#include <wtf/ListHashSet.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/Vector.h>

namespace WebCore {

class Document;
class RenderBlock;
class RenderText;

class FontBoostingCluster {
    WTF_MAKE_NONCOPYABLE(FontBoostingCluster);
public:
    static PassOwnPtr<FontBoostingCluster> create(Document*, RenderBlock* parentBlock, RenderText* firstTextNode);

    enum MergeType { MergeInlines, MergeBlocks, MergeClusters };

    void merge(PassOwnPtr<FontBoostingCluster>, MergeType);
    bool shouldMergeInlines(const FontBoostingCluster*) const;
    bool shouldMergeBlocks(const FontBoostingCluster*) const;
    bool shouldMergeClusters(const FontBoostingCluster*) const;

    void scaleForWidth(float minZoomFontSize, float fontScaleFactor, float visibleWidth);

    const ListHashSet<RenderBlock*>& blocks() const;

    const int numLinesOfText() const;
    const IntRect& boundingRect() const;

#if FB_DEBUG
    static unsigned tid(const RenderText*);
    static unsigned cid(const FontBoostingCluster*); // == tid of first text node.
#endif

private:
    FontBoostingCluster(Document*, RenderBlock* parentBlock, RenderText* firstTextNode);

    float fontSizeAtIntervalBetweenClusters(const FontBoostingCluster*) const;

    // Font Boosting runs in between layout passes, so Document, RenderText and
    // RenderBlock objects should never be destroyed during the lifetime of this class.
    Document* m_document;
    ListHashSet<RenderBlock*> m_blocks;
    Vector<RenderText*> m_textNodes;
    ETextAlign m_simplifiedTextAlign;
    IntRect m_boundingRect;
    int m_clusterWidth; // May be less than m_boundingRect.width() after merging clusters.
    int m_columnX;
    int m_columnMaxX;
    int m_columnWidth; // May be less than m_columnMaxX - m_columnX after merging clusters.
    int m_numLinesOfText;
};

} // namespace WebCore

#endif // OS(ANDROID) && ENABLE(FONT_BOOSTING)

#endif // FontBoostingCluster_h
