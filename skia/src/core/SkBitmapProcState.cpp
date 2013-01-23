
/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "SkBitmapProcState.h"
#include "SkColorPriv.h"
#include "SkFilterProc.h"
#include "SkPaint.h"
#include "SkShader.h"   // for tilemodes
#include "SkUtilsArm.h"

#define  GLUE2(x,y)    GLUE2_(x,y)
#define  GLUE2_(x,y)   x ## y

#define  GLUE3(x,y,z)  GLUE3_(x,y,z)
#define  GLUE3_(x,y,z) x ## y ## z

#define MAKENAME1(prefix)            GLUE2(prefix,PROC_TYPE)
#define MAKENAME2(prefix,suffix)     GLUE3(prefix,suffix,PROC_TYPE)

#if !SK_ARM_NEON_IS_NONE
// The following are defined in src/opts/SkBitmapProcState_opts_arm_neon.cpp
// since they must be compiled separately with -mfpu=neon.
extern const SkBitmapProcState::SampleProc32 SkBitmapProcState_gSample32_neon[];
extern const SkBitmapProcState::SampleProc16 SkBitmapProcState_gSample16_neon[];

extern void S16_D16_filter_DX_neon(const SkBitmapProcState&, const uint32_t*, int, uint16_t*);
extern void Clamp_S16_D16_filter_DX_shaderproc_neon(const SkBitmapProcState&, int, int, uint16_t*, int);
extern void Repeat_S16_D16_filter_DX_shaderproc_neon(const SkBitmapProcState&, int, int, uint16_t*, int);
extern void SI8_opaque_D32_filter_DX_neon(const SkBitmapProcState&, const uint32_t*, int, SkPMColor*);
extern void Clamp_SI8_opaque_D32_filter_DX_shaderproc_neon(const SkBitmapProcState&, int, int, SkPMColor*, int);
#endif

// The default procedures must have an empty PROC_TYPE
#if !SK_ARM_NEON_IS_ALWAYS
#define PROC_TYPE
#include "SkBitmapProcState_filter.h"
#include "SkBitmapProcState_procs.h"
#endif

static bool valid_for_filtering(unsigned dimension) {
    // for filtering, width and height must fit in 14bits, since we use steal
    // 2 bits from each to store our 4bit subpixel data
    return (dimension & ~0x3FFF) == 0;
}

bool SkBitmapProcState::chooseProcs(const SkMatrix& inv, const SkPaint& paint) {
    if (fOrigBitmap.width() == 0 || fOrigBitmap.height() == 0) {
        return false;
    }

    const SkMatrix* m;
    bool trivial_matrix = (inv.getType() & ~SkMatrix::kTranslate_Mask) == 0;
    bool clamp_clamp = SkShader::kClamp_TileMode == fTileModeX &&
                       SkShader::kClamp_TileMode == fTileModeY;

    if (clamp_clamp || trivial_matrix) {
        m = &inv;
    } else {
        fUnitInvMatrix = inv;
        fUnitInvMatrix.postIDiv(fOrigBitmap.width(), fOrigBitmap.height());
        m = &fUnitInvMatrix;
    }

    fBitmap = &fOrigBitmap;
    if (fOrigBitmap.hasMipMap()) {
        int shift = fOrigBitmap.extractMipLevel(&fMipBitmap,
                                                SkScalarToFixed(m->getScaleX()),
                                                SkScalarToFixed(m->getSkewY()));
        
        if (shift > 0) {
            if (m != &fUnitInvMatrix) {
                fUnitInvMatrix = *m;
                m = &fUnitInvMatrix;
            }

            SkScalar scale = SkFixedToScalar(SK_Fixed1 >> shift);
            fUnitInvMatrix.postScale(scale, scale);
            
            // now point here instead of fOrigBitmap
            fBitmap = &fMipBitmap;
        }
    }

    fInvMatrix      = m;
    fInvProc        = m->getMapXYProc();
    fInvType        = m->getType();
    fInvSx          = SkScalarToFixed(m->getScaleX());
    fInvKy          = SkScalarToFixed(m->getSkewY());

    fAlphaScale = SkAlpha255To256(paint.getAlpha());

    // pick-up filtering from the paint, but only if the matrix is
    // more complex than identity/translate (i.e. no need to pay the cost
    // of filtering if we're not scaled etc.).
    // note: we explicitly check inv, since m might be scaled due to unitinv
    //       trickery, but we don't want to see that for this test
    fDoFilter = paint.isFilterBitmap() &&
                (inv.getType() > SkMatrix::kTranslate_Mask &&
                 valid_for_filtering(fBitmap->width() | fBitmap->height()));

    fShaderProc32 = NULL;
    fShaderProc16 = NULL;
    fSampleProc32 = NULL;
    fSampleProc16 = NULL;

    fMatrixProc = this->chooseMatrixProc(trivial_matrix);
    if (NULL == fMatrixProc) {
        return false;
    }

    ///////////////////////////////////////////////////////////////////////
    
    int index = 0;
    if (fAlphaScale < 256) {  // note: this distinction is not used for D16
        index |= 1;
    }
    if (fInvType <= (SkMatrix::kTranslate_Mask | SkMatrix::kScale_Mask)) {
        index |= 2;
    }
    if (fDoFilter) {
        index |= 4;
    }
    // bits 3,4,5 encoding the source bitmap format
    switch (fBitmap->config()) {
        case SkBitmap::kARGB_8888_Config:
            index |= 0;
            break;
        case SkBitmap::kRGB_565_Config:
            index |= 8;
            break;
        case SkBitmap::kIndex8_Config:
            index |= 16;
            break;
        case SkBitmap::kARGB_4444_Config:
            index |= 24;
            break;
        case SkBitmap::kA8_Config:
            index |= 32;
            fPaintPMColor = SkPreMultiplyColor(paint.getColor());
            break;
        default:
            return false;
    }

#if SK_ARM_NEON_IS_DYNAMIC
    if (sk_cpu_arm_has_neon()) {
        #define PROC_TYPE _neon
        #include "SkBitmapProcState_select.h"
    } else {
        #define PROC_TYPE
        #include "SkBitmapProcState_select.h"
    }
#elif SK_ARM_NEON_IS_ALWAYS
    #define PROC_TYPE _neon
    #include "SkBitmapProcState_select.h"
#else
    #define PROC_TYPE
    #include "SkBitmapProcState_select.h"
#endif

    // see if our platform has any accelerated overrides
    this->platformProcs();
    return true;
}

///////////////////////////////////////////////////////////////////////////////
/*
    The storage requirements for the different matrix procs are as follows,
    where each X or Y is 2 bytes, and N is the number of pixels/elements:
 
    scale/translate     nofilter      Y(4bytes) + N * X
    affine/perspective  nofilter      N * (X Y)
    scale/translate     filter        Y Y + N * (X X)
    affine/perspective  filter        N * (Y Y X X)
 */
int SkBitmapProcState::maxCountForBufferSize(size_t bufferSize) const {
    int32_t size = static_cast<int32_t>(bufferSize);

    size &= ~3; // only care about 4-byte aligned chunks
    if (fInvType <= (SkMatrix::kTranslate_Mask | SkMatrix::kScale_Mask)) {
        size -= 4;   // the shared Y (or YY) coordinate
        if (size < 0) {
            size = 0;
        }
        size >>= 1;
    } else {
        size >>= 2;
    }

    if (fDoFilter) {
        size >>= 1;
    }

    return size;
}

