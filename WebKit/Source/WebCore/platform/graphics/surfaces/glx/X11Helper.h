/*
 * Copyright (C) 2013 Intel Corporation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef X11Helper_h
#define X11Helper_h

#include "IntRect.h"
#include "OwnPtrX11.h"

#if USE(GRAPHICS_SURFACE)

#if USE(EGL)
#include <opengl/GLDefs.h>
#endif

#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#endif

#include <X11/Xlib.h>

namespace WebCore {

class X11Helper {

public:
    static void createOffScreenWindow(uint32_t*, const XVisualInfo&, const IntSize& = IntSize(1, 1));
#if USE(EGL)
    static void createOffScreenWindow(uint32_t*, const EGLint, const IntSize& = IntSize(1, 1));
#endif
    static void destroyWindow(const uint32_t);
    static void resizeWindow(const IntRect&, const uint32_t);
    static bool isXRenderExtensionSupported();
    static Display* nativeDisplay();
    static Window offscreenRootWindow();
};

class ScopedXPixmapCreationErrorHandler {

public:
    ScopedXPixmapCreationErrorHandler();
    ~ScopedXPixmapCreationErrorHandler();
    bool isValidOperation() const;

private:
    XErrorHandler m_previousErrorHandler;
};

}

#endif
