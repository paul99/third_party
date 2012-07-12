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
#include "AXObjectCache.h"

#include <wtf/PassRefPtr.h>

// TODO(satish): This file should be upstreamed to WebKit and we need to find
// a way to select this file instead of AXObjectCacheChromium.cpp when building
// Clank.

namespace WebCore {

bool AXObjectCache::gAccessibilityEnabled = false;
bool AXObjectCache::gAccessibilityEnhancedUserInterfaceEnabled = false;

AXObjectCache::AXObjectCache(const Document* doc)
    : m_notificationPostTimer(this, &AXObjectCache::notificationPostTimerFired)
{
    m_document = const_cast<Document*>(doc);
}

AXObjectCache::~AXObjectCache()
{
}

AccessibilityObject* AXObjectCache::focusedImageMapUIElement(HTMLAreaElement* areaElement)
{
    return 0;
}

AccessibilityObject* AXObjectCache::focusedUIElementForPage(const Page* page)
{
    return 0;
}

AccessibilityObject* AXObjectCache::get(Widget* widget)
{
    return 0;
}

AccessibilityObject* AXObjectCache::get(RenderObject* renderer)
{
    return 0;
}

bool nodeHasRole(Node* node, const String& role)
{
    return false;
}

AccessibilityObject* AXObjectCache::getOrCreate(Widget* widget)
{
    return 0;
}

AccessibilityObject* AXObjectCache::getOrCreate(RenderObject* renderer)
{
    return 0;
}

AccessibilityObject* AXObjectCache::rootObject()
{
    return 0;
}

AccessibilityObject* AXObjectCache::rootObjectForFrame(Frame* frame)
{
    return 0;
}

AccessibilityObject* AXObjectCache::getOrCreate(AccessibilityRole role)
{
    return 0;
}

void AXObjectCache::remove(AXID axID)
{
}

void AXObjectCache::remove(RenderObject* renderer)
{
}

void AXObjectCache::remove(Widget* view)
{
}

AXID AXObjectCache::platformGenerateAXID() const
{
    return 0;
}

AXID AXObjectCache::getAXID(AccessibilityObject* obj)
{
    return 0;
}

void AXObjectCache::removeAXID(AccessibilityObject* object)
{
}

#if HAVE(ACCESSIBILITY)
void AXObjectCache::contentChanged(RenderObject* renderer)
{
}
#endif

void AXObjectCache::childrenChanged(RenderObject* renderer)
{
}

void AXObjectCache::notificationPostTimerFired(Timer<AXObjectCache>*)
{
}

#if HAVE(ACCESSIBILITY)
void AXObjectCache::postNotification(RenderObject* renderer, AXNotification notification, bool postToElement, PostType postType)
{
}

void AXObjectCache::postNotification(AccessibilityObject* object, Document* document, AXNotification notification, bool postToElement, PostType postType)
{
}

void AXObjectCache::checkedStateChanged(RenderObject* renderer)
{
}

void AXObjectCache::selectedChildrenChanged(RenderObject* renderer)
{
}

void AXObjectCache::nodeTextChangeNotification(RenderObject* renderer, AXTextChange textChange, unsigned offset, const String&)
{
}
#endif

#if HAVE(ACCESSIBILITY)

void AXObjectCache::handleScrollbarUpdate(ScrollView* view)
{
}

void AXObjectCache::handleAriaExpandedChange(RenderObject *renderer)
{
}

void AXObjectCache::handleActiveDescendantChanged(RenderObject* renderer)
{
}

void AXObjectCache::handleAriaRoleChanged(RenderObject* renderer)
{
}
#endif

VisiblePosition AXObjectCache::visiblePositionForTextMarkerData(TextMarkerData& textMarkerData)
{
    return VisiblePosition();
}

void AXObjectCache::textMarkerDataForVisiblePosition(TextMarkerData& textMarkerData, const VisiblePosition& visiblePos)
{
}

void AXObjectCache::handleFocusedUIElementChanged(RenderObject*, RenderObject* newFocusedRenderer)
{
}

void AXObjectCache::handleScrolledToAnchor(const Node* anchorNode)
{
}

void AXObjectCache::frameLoadingEventNotification(Frame* frame, AXLoadingEvent loadingEvent)
{
}

const Element* AXObjectCache::rootAXEditableElement(const Node*)
{
    return NULL;
}

Element* AXObjectCache::rootAXEditableElement(Node*)
{
    return NULL;
}

// TODO(satish): Move this to a separate file before upstreaming to WebKit.
const String& AccessibilityObject::actionVerb() const
{
    DEFINE_STATIC_LOCAL(const String, noAction, ());
    return noAction;
}

} // namespace WebCore
