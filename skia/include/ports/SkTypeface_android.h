/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#ifndef SkTypeface_android_DEFINED
#define SkTypeface_android_DEFINED

#include "SkTypeface.h"

enum FallbackScripts {
    kArabic_FallbackScript,
    kArmenian_FallbackScript,
    kBengali_FallbackScript,
    kDevanagari_FallbackScript,
    kEthiopic_FallbackScript,
    kGeorgian_FallbackScript,
    kHebrewRegular_FallbackScript,
    kHebrewBold_FallbackScript,
    kKannada_FallbackScript,
    kMalayalam_FallbackScript,
    kTamilRegular_FallbackScript,
    kTamilBold_FallbackScript,
    kThai_FallbackScript,
    kTelugu_FallbackScript,
    kFallbackScriptNumber
};

#define SkTypeface_ValidScript(s) (s >= 0 && s < kFallbackScriptNumber)

/**
 *  Return a new typeface for a fallback script. If the script is
 *  not valid, or can not map to a font file, returns null.
 *  @param  script  The script id.
 *  @return reference to the matching typeface. Caller must call
 *          unref() when they are done.
 */
SK_API SkTypeface* SkCreateTypefaceForScript(FallbackScripts script);

/**
 *  Return the file name for the fallback script on Android.
 *  If the script is not valid, returns null.
 *  Note it returns a file name does not necessarily mean the file
 *  exists on the system or the file can be successfully loaded as
 *  a font. It returns all known script font file on Android.
 */
SK_API const char* SkGetFallbackScriptID(FallbackScripts script);

/**
 *  Return the fallback script for the font file name on Android.
 *  If the file name is not valid, or can not map to a fallback
 *  script, returns kFallbackScriptNumber.
 */
SK_API FallbackScripts SkGetFallbackScriptFromID(const char* id);

#endif
