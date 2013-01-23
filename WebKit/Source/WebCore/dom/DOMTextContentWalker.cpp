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
#include "DOMTextContentWalker.h"

#if OS(ANDROID)

#include "Document.h"
#include "Range.h"
#include "TextIterator.h"
#include "VisiblePosition.h"
#include "VisibleSelection.h"
#include "visible_units.h"

namespace WebCore {

DOMTextContentWalker::DOMTextContentWalker(const VisiblePosition& visiblePosition, unsigned maxLength)
    : m_positionOffsetInContent(0)
{
    if (visiblePosition.isNull())
        return;

    const unsigned halfMaxLength = maxLength / 2;
    CharacterIterator forwardIterator(makeRange(visiblePosition, endOfDocument(visiblePosition)).get(), TextIteratorStopsOnFormControls);
    if (!forwardIterator.atEnd())
        forwardIterator.advance(maxLength - halfMaxLength);

    Position position = visiblePosition.deepEquivalent().parentAnchoredEquivalent();
    Document* document = position.document();
    RefPtr<Range> forwardRange = forwardIterator.range();
    if (!forwardRange || !Range::create(document, position, forwardRange->startPosition())->text().length()) {
        ASSERT(forwardRange);
        return;
    }

    BackwardsCharacterIterator backwardsIterator(makeRange(startOfDocument(visiblePosition), visiblePosition).get(), TextIteratorStopsOnFormControls);
    if (!backwardsIterator.atEnd())
        backwardsIterator.advance(halfMaxLength);

    RefPtr<Range> backwardsRange = backwardsIterator.range();
    if (!backwardsRange) {
        ASSERT(backwardsRange);
        return;
    }

    m_positionOffsetInContent = Range::create(document, backwardsRange->endPosition(), position)->text().length();
    m_contentRange = Range::create(document, backwardsRange->endPosition(), forwardRange->startPosition());
    ASSERT(m_contentRange);
}

PassRefPtr<Range> DOMTextContentWalker::contentOffsetsToRange(unsigned startOffsetInContent, unsigned endOffsetInContent)
{
    if (startOffsetInContent >= endOffsetInContent || endOffsetInContent > content().length())
        return 0;

    CharacterIterator iterator(m_contentRange.get());

    ASSERT(!iterator.atEnd());
    iterator.advance(startOffsetInContent);

    ASSERT(iterator.range());
    Position start = iterator.range()->startPosition();

    ASSERT(!iterator.atEnd());
    iterator.advance(endOffsetInContent - startOffsetInContent);

    ASSERT(iterator.range());
    Position end = iterator.range()->startPosition();

    return Range::create(start.document(), start, end);
}

String DOMTextContentWalker::content() const
{
    if (m_contentRange)
        return m_contentRange->text();
    return String();
}

unsigned DOMTextContentWalker::hitOffsetInContent() const
{
    return m_positionOffsetInContent;
}

} // namespace WebCore

#endif // OS(ANDROID)
