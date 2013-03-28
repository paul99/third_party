/*
 * Copyright (C) 2012 Apple Inc. All rights reserved.
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

#ifndef WebProcessServiceEntryPoints_h
#define WebProcessServiceEntryPoints_h

#if HAVE(XPC)

#include "WKBase.h"

#ifdef __cplusplus
extern "C" {
#endif

// This entry point is used for the installed WebProcessService, which does not
// need to be re-execed, or mess around with DYLD.
WK_EXPORT int webProcessServiceMain(int argc, char** argv);

// This entry point is used for the WebProcessServiceForWebKitDevelopment
// which needs to be re-exec, and can't link directly to WebKit2 requiring
// some DYLD fiddling.
WK_EXPORT void initializeWebProcessForWebProcessServiceForWebKitDevelopment(const char* clientIdentifier, xpc_connection_t, mach_port_t serverPort, const char* uiProcessName);

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // HAVE(XPC)

#endif // WebProcessServiceEntryPoints_h
