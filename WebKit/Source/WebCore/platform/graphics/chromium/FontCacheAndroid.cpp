/*
 * Copyright (c) 2011 Google Inc. All rights reserved.
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
#include "FontCache.h"

#include "Font.h"
#include "FontDescription.h"
#include "FontPlatformData.h"
#include "NotImplemented.h"
#include "PlatformSupport.h"
#include "SimpleFontData.h"

#include "SkPaint.h"
#include "SkTypeface_android.h"
#include "SkUtils.h"

#include <unicode/locid.h>
#include <wtf/Assertions.h>
#include <wtf/text/AtomicString.h>
#include <wtf/text/CString.h>

namespace WebCore {

// Load custom fonts from file for layout tests, as we need to match
// font of chromium/linux.
static CString getCustomFontPath(const char* name, bool bold, bool italic) {
    static const char* kDeviceFontDirForTest = "/data/drt/fonts/";

    static const struct AliasToFontName {
        const char* alias;
        const char* font_name;
    } kAliasToFontName[] = {
        // The following mapping roughly equals to fonts.conf used by
        // TestShellGtk.cpp.
        { "Times", "Times New Roman" },
        { "sans", "Arial" },
        { "sans serif", "Arial" },
        { "Helvetica", "Arial" },
        { "sans-serif", "Arial" },
        { "serif", "Times New Roman" },
        { "mono", "Courier New" },
        { "monospace", "Courier New" },
        { "Courier", "Courier New" },
        { "cursive", "Comic Sans MS" },
        { "fantasy", "Impact" },
        { "Monaco", "Times New Roman" },
    };

    static const struct FontNameToFont {
        const char* font_name;
        const char* font_files[4];  // 0: normal; 1: bold; 2: italic; 3: bold italic
    } kFontNameToFont[] = {
        { "Times New Roman",
            { "Times_New_Roman.ttf", "Times_New_Roman_Bold.ttf",
              "Times_New_Roman_Italic.ttf", "Times_New_Roman_Bold_Italic.ttf" }
        },
        { "Arial",
            { "Arial.ttf", "Arial_Bold.ttf",
              "Arial_Italic.ttf", "Arial_Bold_Italic.ttf" }
        },
        { "Courier New",
            { "Courier_New.ttf", "Courier_New_Bold.ttf",
              "Courier_New_Italic.ttf", "Courier_New_Bold_Italic.ttf" }
        },
        { "Comic Sans MS",
            { "Comic_Sans_MS.ttf", "Comic_Sans_MS_Bold.ttf",
              "Comic_Sans_MS.ttf", "Comic_Sans_MS_Bold.ttf" }
        },
        { "Impact",
            { "Impact.ttf", "Impact.ttf", "Impact.ttf", "Impact.ttf" }
        },
        { "Georgia",
            { "Georgia.ttf", "Georgia_Bold.ttf",
              "Georgia_Italic.ttf", "Georgia_Bold_Italic.ttf" }
        },
        { "Trebuchet MS",
            { "Trebuchet_MS.ttf", "Trebuchet_MS_Bold.ttf",
              "Trebuchet_MS_Italic.ttf", "Trebuchet_MS_Bold_Italic.ttf" }
        },
        { "Verdana",
            { "Verdana.ttf", "Verdana_Bold.ttf",
              "Verdana_Italic.ttf", "Verdana_Bold_Italic.ttf" }
        },
        { "Ahem",
            { "AHEM____.TTF", "AHEM____.TTF", "AHEM____.TTF", "AHEM____.TTF" }
        },
    };

    for (size_t i = 0; i < sizeof(kAliasToFontName) / sizeof(kAliasToFontName[0]); ++i) {
        if (!strcasecmp(name, kAliasToFontName[i].alias)) {
            name = kAliasToFontName[i].font_name;
            break;
        }
    }

    for (size_t i = 0; i < sizeof(kFontNameToFont) / sizeof(kFontNameToFont[0]); ++i) {
        if (!strcasecmp(name, kFontNameToFont[i].font_name)) {
            size_t style_index = bold ? (italic ? 3 : 1) : (italic ? 2 : 0);
            std::string font_path(kDeviceFontDirForTest);
            font_path += kFontNameToFont[i].font_files[style_index];
            return CString(font_path.c_str());
        }
    }
    return CString();
}

static const char* getFallbackFontName(const FontDescription& fontDescription)
{
    switch (fontDescription.genericFamily()) {
    case FontDescription::StandardFamily:
    case FontDescription::SerifFamily:
        return "serif";
    case FontDescription::SansSerifFamily:
        return "sans-serif";
    case FontDescription::MonospaceFamily:
        return "monospace";
    case FontDescription::CursiveFamily:
        return "cursive";
    case FontDescription::FantasyFamily:
        return "fantasy";
    case FontDescription::NoFamily:
    default:
        return "";
    }
}

static bool isFallbackFamily(String family)
{
    return family.startsWith("-webkit-")
        || equalIgnoringCase(family, "serif")
        || equalIgnoringCase(family, "sans-serif")
        || equalIgnoringCase(family, "sans")
        || equalIgnoringCase(family, "monospace")
        || equalIgnoringCase(family, "cursive")
        || equalIgnoringCase(family, "fantasy")
        || equalIgnoringCase(family, "times") // Skia aliases for serif
        || equalIgnoringCase(family, "times new roman")
        || equalIgnoringCase(family, "palatino")
        || equalIgnoringCase(family, "georgia")
        || equalIgnoringCase(family, "baskerville")
        || equalIgnoringCase(family, "goudy")
        || equalIgnoringCase(family, "ITC Stone Serif")
        || equalIgnoringCase(family, "arial") // Skia aliases for sans-serif
        || equalIgnoringCase(family, "helvetica")
        || equalIgnoringCase(family, "tahoma")
        || equalIgnoringCase(family, "verdana")
        || equalIgnoringCase(family, "courier") // Skia aliases for monospace
        || equalIgnoringCase(family, "courier new")
        || equalIgnoringCase(family, "monaco");
}

void FontCache::platformInit()
{
}

const SimpleFontData* FontCache::getFontDataForCharacters(const Font& font, const UChar* characters, int length)
{
    icu::Locale locale = icu::Locale::getDefault();
    PlatformSupport::FontFamily family;
    PlatformSupport::getFontFamilyForCharacters(characters, length, locale.getLanguage(), &family);
    if (family.name.isEmpty())
        return 0;

    AtomicString atomicFamily(family.name);
    return getCachedFontData(getCachedFontPlatformData(font.fontDescription(), atomicFamily, DoNotRetain), DoNotRetain);
}

SimpleFontData* FontCache::getSimilarFontPlatformData(const Font& font)
{
    return 0;
}

SimpleFontData* FontCache::getLastResortFallbackFont(const FontDescription& description, ShouldRetain shouldRetain)
{
    DEFINE_STATIC_LOCAL(const AtomicString, serif, ("Serif"));
    DEFINE_STATIC_LOCAL(const AtomicString, monospace, ("Monospace"));
    DEFINE_STATIC_LOCAL(const AtomicString, sans, ("Sans"));

    FontPlatformData* fontPlatformData = 0;
    switch (description.genericFamily()) {
    case FontDescription::SerifFamily:
        fontPlatformData = getCachedFontPlatformData(description, serif);
        break;
    case FontDescription::MonospaceFamily:
        fontPlatformData = getCachedFontPlatformData(description, monospace);
        break;
    case FontDescription::SansSerifFamily:
    default:
        fontPlatformData = getCachedFontPlatformData(description, sans);
        break;
    }

    ASSERT(fontPlatformData);
    return getCachedFontData(fontPlatformData, shouldRetain);
}

void FontCache::getTraitsInFamily(const AtomicString& familyName, Vector<unsigned>& traitsMasks)
{
    notImplemented();
}

FontPlatformData* FontCache::getCachedFallbackScriptFontPlatformData(const FontDescription& fontDescription, const AtomicString& family)
{
    return getCachedFontPlatformData(fontDescription, family, true);
}

FontPlatformData* FontCache::createFontPlatformData(const FontDescription& fontDescription, const AtomicString& family)
{
    const char* name = 0;
    CString nameString; // Keeps name valid within scope of this function in case that name is from a family.

    // If a fallback font is being created (e.g. "-webkit-monospace"), convert
    // it in to the fallback name (e.g. "monospace").
    if (!family.length() || family.startsWith("-webkit-"))
        name = getFallbackFontName(fontDescription);
    else {
        nameString = family.string().utf8();
        name = nameString.data();
    }

    int style = SkTypeface::kNormal;
    if (fontDescription.weight() >= FontWeightBold)
        style |= SkTypeface::kBold;
    if (fontDescription.italic())
        style |= SkTypeface::kItalic;

    SkTypeface* typeface = 0;
    FontPlatformData* result = 0;
    if (PlatformSupport::layoutTestMode()) {
        CString customFontPath = getCustomFontPath(name,
                                                   style & SkTypeface::kBold,
                                                   style & SkTypeface::kItalic);
        if (customFontPath.length()) {
            typeface = SkTypeface::CreateFromFile(customFontPath.data());
            result = new FontPlatformData(typeface, name, fontDescription.computedSize(),
                                          (style & SkTypeface::kBold) && !typeface->isBold(),
                                          (style & SkTypeface::kItalic) && !typeface->isItalic(),
                                          fontDescription.orientation(),
                                          fontDescription.textOrientation());
        }
    }
    if (!typeface) {
        FallbackScripts fallbackScript = SkGetFallbackScriptFromID(name);
        if (SkTypeface_ValidScript(fallbackScript)) {
            // Do not use fallback fonts in layout test.
            if (PlatformSupport::layoutTestMode())
                return NULL;
            typeface = SkCreateTypefaceForScript(fallbackScript);
            if (typeface)
                result = new FontPlatformData(typeface, name, fontDescription.computedSize(),
                                              (style & SkTypeface::kBold) && !typeface->isBold(),
                                              (style & SkTypeface::kItalic) && !typeface->isItalic(),
                                              fontDescription.orientation(),
                                              fontDescription.textOrientation());
        } else {
            typeface = SkTypeface::CreateFromName(name, SkTypeface::kNormal);

            // CreateFromName always returns a typeface, falling back to a default font
            // if the one requested could not be found. Calling Equal() with a null
            // pointer will compare the returned font against the default, with the
            // caveat that the default is always of normal style. When that happens,
            // ignore the default font and allow WebCore to provide the next font on the
            // CSS fallback list. The only exception to this occurs when the family name
            // is a commonly used generic family, which is the case when called by
            // getSimilarFontPlatformData() or getLastResortFallbackFont(). In that case
            // the default font is an acceptable result.

            if (!SkTypeface::Equal(typeface, 0) || isFallbackFamily(family.string())) {
                // We had to use normal styling to see if this was a default font. If
                // we need bold or italic, replace with the corrected typeface.
                if (style != SkTypeface::kNormal) {
                    typeface->unref();
                    typeface = SkTypeface::CreateFromName(name, static_cast<SkTypeface::Style>(style));
                }
                result = new FontPlatformData(typeface, name, fontDescription.computedSize(),
                                              (style & SkTypeface::kBold) && !typeface->isBold(),
                                              (style & SkTypeface::kItalic) && !typeface->isItalic(),
                                              fontDescription.orientation(),
                                              fontDescription.textOrientation());
            }
        }
    }

    SkSafeUnref(typeface);
    return result;

}

}  // namespace WebCore
