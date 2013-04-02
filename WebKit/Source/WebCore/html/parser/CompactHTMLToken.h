/*
 * Copyright (C) 2013 Google, Inc. All Rights Reserved.
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
 * THIS SOFTWARE IS PROVIDED BY GOOGLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL GOOGLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#ifndef CompactHTMLToken_h
#define CompactHTMLToken_h

#if ENABLE(THREADED_HTML_PARSER)

#include "HTMLTokenTypes.h"
#include <wtf/OwnPtr.h>
#include <wtf/PassOwnPtr.h>
#include <wtf/RefCounted.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>
#include <wtf/text/TextPosition.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

class HTMLToken;
class XSSInfo;

class CompactAttribute {
public:
    CompactAttribute(const String& name, const String& value)
        : m_name(name)
        , m_value(value)
    {
    }

    const String& name() const { return m_name; }
    const String& value() const { return m_value; }

private:
    String m_name;
    String m_value;
};

class CompactHTMLToken {
public:
    CompactHTMLToken(const HTMLToken*, const TextPosition&);
    CompactHTMLToken(const CompactHTMLToken&);

    bool isSafeToSendToAnotherThread() const;

    HTMLTokenTypes::Type type() const { return static_cast<HTMLTokenTypes::Type>(m_type); }
    const String& data() const { return m_data; }
    bool selfClosing() const { return m_selfClosing; }
    bool isAll8BitData() const { return m_isAll8BitData; }
    const Vector<CompactAttribute>& attributes() const { return m_attributes; }
    const TextPosition& textPosition() const { return m_textPosition; }

    // There is only 1 DOCTYPE token per document, so to avoid increasing the
    // size of CompactHTMLToken, we just use the m_attributes vector.
    const String& publicIdentifier() const { return m_attributes[0].name(); }
    const String& systemIdentifier() const { return m_attributes[0].value(); }
    bool doctypeForcesQuirks() const { return m_doctypeForcesQuirks; }
    XSSInfo* xssInfo() const;
    void setXSSInfo(PassOwnPtr<XSSInfo>);

private:
    unsigned m_type : 4;
    unsigned m_selfClosing : 1;
    unsigned m_isAll8BitData : 1;
    unsigned m_doctypeForcesQuirks: 1;

    String m_data; // "name", "characters", or "data" depending on m_type
    Vector<CompactAttribute> m_attributes;
    TextPosition m_textPosition;
    OwnPtr<XSSInfo> m_xssInfo;
};

typedef Vector<CompactHTMLToken> CompactHTMLTokenStream;

}

namespace WTF {
// This is required for a struct with OwnPtr. We know CompactHTMLToken is simple enough that
// initializing to 0 and moving with memcpy (and then not destructing the original) will work.
template<> struct VectorTraits<WebCore::CompactHTMLToken> : SimpleClassVectorTraits { };
}

#endif // ENABLE(THREADED_HTML_PARSER)

#endif
