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

#if OS(ANDROID) && ENABLE(FONT_BOOSTING)

#include "FontBoostingCluster.h"

#include "Document.h"
#include "Frame.h"
#include "FrameView.h"
#include "InlineTextBox.h"
#include "Page.h"
#include "RenderBlock.h"
#include "RenderStyle.h"
#include "RenderText.h"

using std::max;
using std::min;

namespace WebCore {

PassOwnPtr<FontBoostingCluster> FontBoostingCluster::create(Document* document, RenderBlock* parentBlock, RenderText* firstTextNode)
{
    return adoptPtr(new FontBoostingCluster(document, parentBlock, firstTextNode));
}

FontBoostingCluster::FontBoostingCluster(Document* document, RenderBlock* parentBlock, RenderText* firstTextNode)
    : m_document(document)
    , m_textNodes(1, firstTextNode)
{
    // FIXME(johnme): Force pixel lineHeight?

    m_blocks.add(parentBlock);

    // Determine text alignment.
    switch (parentBlock->style()->textAlign()) {
    case LEFT:
    case WEBKIT_LEFT:
        m_simplifiedTextAlign = LEFT;
        break;
    case RIGHT:
    case WEBKIT_RIGHT:
        m_simplifiedTextAlign = RIGHT;
        break;
    case CENTER:
    case WEBKIT_CENTER:
        m_simplifiedTextAlign = CENTER;
        break;
    case JUSTIFY:
    case TAAUTO:
    case TASTART:
        m_simplifiedTextAlign = parentBlock->style()->isLeftToRightDirection() ? LEFT : RIGHT;
        break;
    case TAEND:
        m_simplifiedTextAlign = parentBlock->style()->isLeftToRightDirection() ? RIGHT : LEFT;
        break;
    }

    m_boundingRect = firstTextNode->absoluteBoundingBoxRectIgnoringTransforms();

    IntRect parentRect = parentBlock->absoluteBoundingBoxRectIgnoringTransforms();
    m_columnX = parentRect.x();
    m_columnWidth = parentBlock->widthForTextAutosizing();
    m_columnMaxX = m_columnX + m_columnWidth;

    // Need to round because the first line often isn't exactly lineHeight tall (but additional lines are). FIXME(johnme): Calculate this exactly.
    int lineHeight = max(1, max(firstTextNode->style()->computedLineHeight(), firstTextNode->style()->fontSize()));
    m_numLinesOfText = static_cast<int>(roundf(m_boundingRect.height() / static_cast<float>(lineHeight)));

    FB_LOGF("::FontBoostingCluster %08x = [\"%s\"] (%d,%d; %d,%d), parent %08x (%d,%d; %d,%d), fsf %f", cid(this), String(firstTextNode->text()->stripWhiteSpace()).utf8().data(), m_boundingRect.x(), m_boundingRect.y(), m_boundingRect.width(), m_boundingRect.height(), reinterpret_cast<unsigned>(parentBlock), parentRect.x(), parentRect.y(), parentRect.width(), parentRect.height(), firstTextNode->frame()->frameScaleFactor());
}

void FontBoostingCluster::merge(PassOwnPtr<FontBoostingCluster> other, MergeType mergeType)
{
    ASSERT(other != this);
    //ASSERT(m_simplifiedTextAlign == other->m_simplifiedTextAlign); // FIXME(johnme): Fix bidi issues and re-enable this assert (b/6488543).

    if (mergeType == MergeClusters)
        m_numLinesOfText = max(m_numLinesOfText, other->m_numLinesOfText);
    else {
        m_numLinesOfText += other->m_numLinesOfText;
        int lineHeight = m_textNodes.last()->style()->computedLineHeight();
        if (other->m_boundingRect.y() < m_boundingRect.maxY() - lineHeight / 2) {
            m_numLinesOfText--; // Other overlaps by one line.
            ASSERT(m_numLinesOfText >= 0);
        }
    }

    for (ListHashSet<RenderBlock*>::iterator it = other->m_blocks.begin(); it != other->m_blocks.end(); ++it)
        m_blocks.add(*it); // ListHashSet ignores duplicates.

    m_textNodes.append(other->m_textNodes);

#if FB_DEBUG
    IntRect oldBoundingRect = m_boundingRect;
#endif
    m_boundingRect.unite(other->m_boundingRect);

    // FIXME(johnme): Alternatively, average these instead of taking max?
    m_columnX    = min(m_boundingRect.x(),    max(m_columnX,    other->m_columnX));
    m_columnMaxX = max(m_boundingRect.maxX(), min(m_columnMaxX, other->m_columnMaxX));

    if (mergeType == MergeClusters) {
        // When merging columns that are side by side, need to boost
        // according to individual width not combined width.
        m_columnWidth = max(m_columnWidth, other->m_columnWidth);
    } else {
        m_columnWidth = m_columnMaxX - m_columnX;
    }

    FB_LOGF("::merge %d %08x += %08x;  (%d,%d; %d,%d) + (%d,%d; %d,%d) => (%d,%d; %d,%d) colW %d", mergeType, cid(this), cid(other.get()), oldBoundingRect.x(), oldBoundingRect.y(), oldBoundingRect.width(), oldBoundingRect.height(), other->m_boundingRect.x(), other->m_boundingRect.y(), other->m_boundingRect.width(), other->m_boundingRect.height(), m_boundingRect.x(), m_boundingRect.y(), m_boundingRect.width(), m_boundingRect.height(), m_columnWidth);

    // other will be deleted when this returns.
}

float FontBoostingCluster::fontSizeAtIntervalBetweenClusters(const FontBoostingCluster* other) const
{
    return max(m_textNodes.last()->style()->fontDescription().specifiedSize(),
               other->m_textNodes.first()->style()->fontDescription().specifiedSize());
}

bool FontBoostingCluster::shouldMergeInlines(const FontBoostingCluster* other) const
{
    float em = fontSizeAtIntervalBetweenClusters(other);
    if (other->m_boundingRect.y() - m_boundingRect.maxY() > 3 * em) {
        FB_LOGF("!shouldMergeInlines %08x %08x  too far apart  (%d - %d) > 3 * %f", cid(this), cid(other), other->m_boundingRect.y(), m_boundingRect.maxY(), em);
        return false;
    }
    return true;
}

bool FontBoostingCluster::shouldMergeBlocks(const FontBoostingCluster* other) const
{
    // 1. Must have same align.
    if (m_simplifiedTextAlign != other->m_simplifiedTextAlign) {
        FB_LOGF("!shouldMergeBlocks %08x %08x  1: different align  %d != %d", cid(this), cid(other), m_simplifiedTextAlign, other->m_simplifiedTextAlign);
        return false;
    }

    float em = fontSizeAtIntervalBetweenClusters(other);

    // 2a. Must have same column start.
    // FIXME(johnme): Allow blockquote, ul, etc (bearing in mind m_simplifiedTextAlign).
    // FIXME(johnme): Allow float:left temporarily changing left, but not float:left creating a different column.
    if (abs(m_columnX - other->m_columnX) > 5 * em) {
        FB_LOGF("!shouldMergeBlocks %08x %08x  2a: different column left  abs(%d - %d) > 5 * %f", cid(this), cid(other), m_columnX, other->m_columnX, em);
        return false;
    }
    // 2b. Must have same column end.
    // FIXME(johnme): Take into account text widths [not sure what this means anymore!].
    if (abs(m_columnMaxX - other->m_columnMaxX) > 5 * em) {
        FB_LOGF("!shouldMergeBlocks %08x %08x  2b: different column right  abs(%d - %d) > 5 * %f", cid(this), cid(other), m_columnMaxX, other->m_columnMaxX, em);
        return false;
    }

    int gap = other->m_boundingRect.y() - m_boundingRect.maxY();

    // 3a. other must be below this.
    if (gap < -0.5 * em) {
        FB_LOGF("!shouldMergeBlocks %08x %08x  3a: not below  (%d - %d) < -0.5 * %f", cid(this), cid(other), other->m_boundingRect.y(), m_boundingRect.maxY(), em);
        return false;
    }
    // 3b. other must be close to this.
    if (gap > 2 * em) {
        FB_LOGF("!shouldMergeBlocks %08x %08x  3b: not close  (%d - %d) > 2 * %f", cid(this), cid(other), other->m_boundingRect.y(), m_boundingRect.maxY(), em);
        return false;
    }

    // 4. 1st line mustn't be smaller than 2nd (e.g. shouldn't boost breadcrumbs on:
    // http://www.independent.co.uk/news/business/news/business-as-usual-top-directors-get-49-per-cent-pay-rise-2376929.html)
    if (m_numLinesOfText == 1) {
        float thisFontSize = m_textNodes.first()->style()->fontDescription().specifiedSize();
        float otherFontSize = other->m_textNodes.first()->style()->fontDescription().specifiedSize();
        if (otherFontSize - thisFontSize > 5) {
            FB_LOGF("!shouldMergeBlocks %08x %08x  4: font size increase  (%f - %f) > 5", cid(this), cid(other), thisFontSize, otherFontSize);
            return false;
        }
    }

    return true;
}

bool FontBoostingCluster::shouldMergeClusters(const FontBoostingCluster* other) const
{
    // We already know the styles of at least one pair of their blocks match.
    // Now just check they're in the same column, or columns of the same width.

    float em = fontSizeAtIntervalBetweenClusters(other);

    // FIXME(johnme): Take into account float:left/right columns like http://www.amazon.co.uk/gp/product/B003U9VLL0?ie=UTF8&force-full-site=1
    if (abs(m_columnWidth - other->m_columnWidth) > 5 * em)
        return false;

    // Somewhat arbitrary limits on how far apart clusters can be and still
    // qualify for style-based clustering.
    const int MAX_X_DISTANCE = 980;
    const int MAX_Y_DISTANCE = 980;
    int xDist = m_boundingRect.x() < other->m_boundingRect.x()
              ? other->m_boundingRect.x() - m_boundingRect.maxX()
              : m_boundingRect.x() - other->m_boundingRect.maxX();
    int yDist = m_boundingRect.y() < other->m_boundingRect.y()
              ? other->m_boundingRect.y() - m_boundingRect.maxY()
              : m_boundingRect.y() - other->m_boundingRect.maxY();
    xDist = max(0, xDist);
    yDist = max(0, yDist);
    if (xDist > MAX_X_DISTANCE || yDist > MAX_Y_DISTANCE)
        return false;

    return true;
}

void FontBoostingCluster::scaleForWidth(float minZoomFontSize, float fontScaleFactor, float visibleWidth)
{
    if (m_numLinesOfText < 3) {
        FB_LOGF("!scaleForWidth %08x  m_numLinesOfText %d < 3", cid(this), m_numLinesOfText);
        return;
    }

    float scale = fontScaleFactor * m_columnWidth / visibleWidth;
    if (scale <= 1) {
        FB_LOGF("!scaleForWidth %08x  scale = %d / %f <= 1", cid(this), m_columnWidth, visibleWidth);
        return;
    }

    Page* page = m_document->page();
    if (!page || !page->mainFrame() || !page->mainFrame()->view())
        return;
    int fixedLayoutWidth = page->mainFrame()->view()->fixedLayoutSize().width();
    // Generous limit will only reduce boosting on pages with text that's wider than
    // the fixedLayoutWidth.
    float maxScale = fontScaleFactor * fixedLayoutWidth / visibleWidth;
    scale = min(scale, maxScale);

    // Limit the amount of boosting to 'scale'; pre-reflow to make up the difference,
    // maintaining legibility. And lock the max-width of the block(s) such that fonts
    // need never be boosted by more, since we don't want Font Boosting to change font
    // sizes during future layouts.
    int clusterMaxTextWidth = static_cast<int>((scale * visibleWidth / fontScaleFactor) + 0.5f);
    for (ListHashSet<RenderBlock*>::iterator it = m_blocks.begin(); it != m_blocks.end(); ++it) {
        RenderBlock* block = *it;
        // Don't make the block narrower just to lock it, but do make sure it's <= fixedLayoutWidth.
        int blockMaxTextWidth = min(fixedLayoutWidth, max(clusterMaxTextWidth, block->widthForTextAutosizing()));
        FB_LOGF("setMaxTextWidthAfterFontBoosting %08x < o%04x  min(%f * %d / %f, %f * %d / %f) = %f -> %d -> min(%d, max(%d, %d)) = %d", cid(this), reinterpret_cast<unsigned>(block) % 0x10000, fontScaleFactor, m_columnWidth, visibleWidth, fontScaleFactor, fixedLayoutWidth, visibleWidth, scale, clusterMaxTextWidth, fixedLayoutWidth, clusterMaxTextWidth, block->widthForTextAutosizing(), blockMaxTextWidth);
        block->setMaxTextWidthAfterFontBoosting(blockMaxTextWidth);
    }

    // Boost fonts that the page author has specified to be larger than
    // minZoomFontSize by less and less, until huge fonts are not boosted at all.
    // For specifiedSize between 0 and minZoomFontSize we directly apply the
    // scale; hence for specifiedSize == minZoomFontSize, boostedSize (output
    // font size) will be scale * minZoomFontSize. For greater specifiedSizes
    // we want to gradually fade out the amount of font boosting, so for
    // every 1px increase in specifiedSize beyond minZoomFontSize we will only
    // increase boostedSize by RATE_OF_INCREASE_OF_BOOSTED_SIZE_AFTER_MIN_SIZE
    // until no font boosting is occuring, after which we stop boosting the text
    // (which is equivalent to setting boostedSize to specifiedSize, i.e. for
    // every 1px increase in specifiedSize, boostedSize will increase by 1px).
    const float RATE_OF_INCREASE_OF_BOOSTED_SIZE_AFTER_MIN_SIZE = 0.5f;

    for (size_t i = 0; i < m_textNodes.size(); ++i) {
        RenderText* text = m_textNodes[i];
        if (m_document->textWasDestroyedDuringBoosting(text))
            continue;
        float specifiedSize = text->style()->fontDescription().specifiedSize();
        float boostedSize;

        if (specifiedSize <= minZoomFontSize)
            boostedSize = scale * specifiedSize;
        else {
            boostedSize = scale * minZoomFontSize + RATE_OF_INCREASE_OF_BOOSTED_SIZE_AFTER_MIN_SIZE * (specifiedSize - minZoomFontSize);
            if (boostedSize <= specifiedSize) {
                FB_LOGF("::scaleForWidth %08x > %08x  lines %d, scale %f = %d / %f, min %f, spec %f, size %f SKIP", cid(this), tid(text), m_numLinesOfText, scale, m_columnWidth, visibleWidth, minZoomFontSize, specifiedSize, boostedSize);
                continue; // i.e. boostedSize = specifiedSize
            }
        }
        boostedSize = roundf(boostedSize);

        FB_LOGF("::scaleForWidth %08x > %08x  lines %d, scale %f = %d / %f, min %f, spec %f, size %f", cid(this), tid(text), m_numLinesOfText, scale, m_columnWidth, visibleWidth, minZoomFontSize, specifiedSize, boostedSize);
        m_document->setTextSize(text, boostedSize);
    }
}

const ListHashSet<RenderBlock*>& FontBoostingCluster::blocks() const
{
    return m_blocks;
}

const int FontBoostingCluster::numLinesOfText() const
{
    return m_numLinesOfText;
}

const IntRect& FontBoostingCluster::boundingRect() const
{
    return m_boundingRect;
}

#if FB_DEBUG
unsigned FontBoostingCluster::tid(const RenderText* text)
{
    return ((reinterpret_cast<unsigned>(text) & 0xff) << 24) + (text->text()->hash() & 0xffff);
}

unsigned FontBoostingCluster::cid(const FontBoostingCluster* node)
{
    return tid(node->m_textNodes.first());
}
#endif

} // namespace WebCore

#endif // OS(ANDROID) && ENABLE(FONT_BOOSTING)
