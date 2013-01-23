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

    fSampleProc32 = MAKENAME1(SkBitmapProcState_gSample32)[index];
    index >>= 1;    // shift away any opaque/alpha distinction
    fSampleProc16 = MAKENAME1(SkBitmapProcState_gSample16)[index];

    // our special-case shaderprocs
    if (MAKENAME1(S16_D16_filter_DX) == fSampleProc16) {
        if (clamp_clamp) {
            fShaderProc16 = MAKENAME1(Clamp_S16_D16_filter_DX_shaderproc);
        } else if (SkShader::kRepeat_TileMode == fTileModeX &&
                   SkShader::kRepeat_TileMode == fTileModeY) {
            fShaderProc16 = MAKENAME1(Repeat_S16_D16_filter_DX_shaderproc);
        }
    } else if (MAKENAME1(SI8_opaque_D32_filter_DX) == fSampleProc32 && clamp_clamp) {
        fShaderProc32 = MAKENAME1(Clamp_SI8_opaque_D32_filter_DX_shaderproc);
    }

#undef PROC_TYPE
