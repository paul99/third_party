/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
// This header can be included multiple times from SkBitmapProcState.cpp
// It is used to select the best bitmap processing routine depending
// on the current state.

#ifndef PROC_TYPE
#error PROC_TYPE must be defined before including this file
#endif

    if (SkShader::kClamp_TileMode == fTileModeX &&
        SkShader::kClamp_TileMode == fTileModeY)
    {
        // clamp gets special version of filterOne
        fFilterOneX = SK_Fixed1;
        fFilterOneY = SK_Fixed1;
        return MAKENAME1(ClampX_ClampY_Procs)[index];
    }
    
    // all remaining procs use this form for filterOne
    fFilterOneX = SK_Fixed1 / fBitmap->width();
    fFilterOneY = SK_Fixed1 / fBitmap->height();
    
    if (SkShader::kRepeat_TileMode == fTileModeX &&
        SkShader::kRepeat_TileMode == fTileModeY)
    {
        return MAKENAME1(RepeatX_RepeatY_Procs)[index];
    }

    fTileProcX = choose_tile_proc(fTileModeX);
    fTileProcY = choose_tile_proc(fTileModeY);
    return GeneralXY_Procs[index];

#undef PROC_TYPE
