/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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

#ifndef MediaKeys_h
#define MediaKeys_h

#if ENABLE(ENCRYPTED_MEDIA_V2)

#include "EventTarget.h"
#include "ExceptionCode.h"
#include <wtf/OwnPtr.h>
#include <wtf/PassRefPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/Uint8Array.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class CDM;
class MediaKeySession;

class MediaKeys : public RefCounted<MediaKeys> {
public:
    static PassRefPtr<MediaKeys> create(const String& keySystem, ExceptionCode&);
    ~MediaKeys();

    PassRefPtr<MediaKeySession> createSession(ScriptExecutionContext*, const String& mimeType, Uint8Array* initData, ExceptionCode&);

    const String& keySystem() const { return m_keySystem; }
    CDM* cdm() { return m_cdm.get(); }

protected:
    MediaKeys(const String& keySystem, PassOwnPtr<CDM>);

    Vector<RefPtr<MediaKeySession> > m_sessions;

    String m_keySystem;
    OwnPtr<CDM> m_cdm;
};

}

#endif // ENABLE(ENCRYPTED_MEDIA_V2)

#endif // MediaKeys_h
