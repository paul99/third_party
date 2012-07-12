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
#include "WebContentLayerImpl.h"

#include "platform/WebContentLayerClient.h"
#include "platform/WebRect.h"
#include "GraphicsContext.h"
#include "platform/WebCanvas.h"
#if WEBKIT_USING_SKIA
#include "PlatformContextSkia.h"
#endif

using namespace WebCore;

namespace WebKit {

PassRefPtr<WebContentLayerImpl> WebContentLayerImpl::create(WebContentLayerClient* contentClient)
{
    return adoptRef(new WebContentLayerImpl(contentClient));
}

WebContentLayerImpl::WebContentLayerImpl(WebContentLayerClient* contentClient)
    : ContentLayerChromium(this)
    , m_contentClient(contentClient)
{
    setIsDrawable(true);
}

WebContentLayerImpl::~WebContentLayerImpl()
{
    clearDelegate();
}

void WebContentLayerImpl::setDrawsContent(bool drawsContent)
{
    setIsDrawable(drawsContent);
}

void WebContentLayerImpl::paintContents(GraphicsContext& gc, const IntRect& clip)
{
    if (!m_contentClient)
        return;
#if WEBKIT_USING_SKIA
    WebCanvas* canvas = gc.platformContext()->canvas();
#elif WEBKIT_USING_CG
    WebCanvas* canvas = gc.platformContext();
#endif
    m_contentClient->paintContents(canvas, WebRect(clip));
}

void WebContentLayerImpl::wasScrolled(const WebCore::IntSize&)
{
}

} // namespace WebKit
