/*
 * Copyright 2009 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "SkBlitRow.h"
#include "SkBlitMask.h"
#include "SkColorPriv.h"
#include "SkDither.h"
#include "SkUtilsArm.h"

// The following functions are declared/defined with explicit hidden
// visibility. This is more efficient than -fvisbility=hidden.
#pragma GCC visibility push(hidden)

// Set USE_NEON_CODE and USE_ARM_CODE as boolean (0 or 1) values
// indicating whether we need NEON and normal ARM code paths.
//
// We use the NEON code for both ALWAYS and DYNAMIC modes
// We use the ARM code for NONE and DYNAMIC modes
//
#define  USE_NEON_CODE  (defined(SK_CPU_LENDIAN) && !SK_ARM_NEON_IS_NONE)
#define  USE_ARM_CODE   (defined(SK_CPU_LENDIAN) && !SK_ARM_NEON_IS_ALWAYS)

#if USE_NEON_CODE

extern void S32A_D565_Opaque_neon(uint16_t* SK_RESTRICT dst,
                                  const SkPMColor* SK_RESTRICT src, int count,
                                  U8CPU alpha, int /*x*/, int /*y*/);

extern void S32A_D565_Blend_neon(uint16_t* SK_RESTRICT dst,
                                 const SkPMColor* SK_RESTRICT src, int count,
                                 U8CPU alpha, int /*x*/, int /*y*/);

extern void S32_D565_Blend_Dither_neon(uint16_t *dst, const SkPMColor *src,
                                       int count, U8CPU alpha, int x, int y);

/* Don't have a special version that assumes each src is opaque, but our S32A
   is still faster than the default, so use it here
 */
#define S32_D565_Opaque_neon    S32A_D565_Opaque_neon
#define S32_D565_Blend_neon     S32A_D565_Blend_neon

#endif

#if USE_ARM_CODE

#define S32A_D565_Opaque_arm       NULL
#define S32A_D565_Blend_arm        NULL
#define S32_D565_Blend_Dither_arm  NULL

/* Don't have a special version that assumes each src is opaque, but our S32A
   is still faster than the default, so use it here
 */
#define S32_D565_Opaque_arm    S32A_D565_Opaque_arm
#define S32_D565_Blend_arm     S32A_D565_Blend_arm

#endif

///////////////////////////////////////////////////////////////////////////////

#if USE_NEON_CODE

extern void S32A_Opaque_BlitRow32_neon(SkPMColor* SK_RESTRICT dst,
                                  const SkPMColor* SK_RESTRICT src,
                                  int count, U8CPU alpha);

#endif

#if USE_ARM_CODE

#ifdef TEST_SRC_ALPHA
#error The ARM asm version of S32A_Opaque_BlitRow32 does not support TEST_SRC_ALPHA
#endif

static void S32A_Opaque_BlitRow32_arm(SkPMColor* SK_RESTRICT dst,
                                  const SkPMColor* SK_RESTRICT src,
                                  int count, U8CPU alpha) {

    SkASSERT(255 == alpha);

    /* Does not support the TEST_SRC_ALPHA case */
    asm volatile (
                  "cmp    %[count], #0               \n\t" /* comparing count with 0 */
                  "beq    3f                         \n\t" /* if zero exit */

                  "mov    ip, #0xff                  \n\t" /* load the 0xff mask in ip */
                  "orr    ip, ip, ip, lsl #16        \n\t" /* convert it to 0xff00ff in ip */

                  "cmp    %[count], #2               \n\t" /* compare count with 2 */
                  "blt    2f                         \n\t" /* if less than 2 -> single loop */

                  /* Double Loop */
                  "1:                                \n\t" /* <double loop> */
                  "ldm    %[src]!, {r5,r6}           \n\t" /* load the src(s) at r5-r6 */
                  "ldm    %[dst], {r7,r8}            \n\t" /* loading dst(s) into r7-r8 */
                  "lsr    r4, r5, #24                \n\t" /* extracting the alpha from source and storing it to r4 */

                  /* ----------- */
                  "and    r9, ip, r7                 \n\t" /* r9 = br masked by ip */
                  "rsb    r4, r4, #256               \n\t" /* subtracting the alpha from 256 -> r4=scale */
                  "and    r10, ip, r7, lsr #8        \n\t" /* r10 = ag masked by ip */

                  "mul    r9, r9, r4                 \n\t" /* br = br * scale */
                  "mul    r10, r10, r4               \n\t" /* ag = ag * scale */
                  "and    r9, ip, r9, lsr #8         \n\t" /* lsr br by 8 and mask it */

                  "and    r10, r10, ip, lsl #8       \n\t" /* mask ag with reverse mask */
                  "lsr    r4, r6, #24                \n\t" /* extracting the alpha from source and storing it to r4 */
                  "orr    r7, r9, r10                \n\t" /* br | ag*/

                  "add    r7, r5, r7                 \n\t" /* dst = src + calc dest(r7) */
                  "rsb    r4, r4, #256               \n\t" /* subtracting the alpha from 255 -> r4=scale */

                  /* ----------- */
                  "and    r9, ip, r8                 \n\t" /* r9 = br masked by ip */

                  "and    r10, ip, r8, lsr #8        \n\t" /* r10 = ag masked by ip */
                  "mul    r9, r9, r4                 \n\t" /* br = br * scale */
                  "sub    %[count], %[count], #2     \n\t"
                  "mul    r10, r10, r4               \n\t" /* ag = ag * scale */

                  "and    r9, ip, r9, lsr #8         \n\t" /* lsr br by 8 and mask it */
                  "and    r10, r10, ip, lsl #8       \n\t" /* mask ag with reverse mask */
                  "cmp    %[count], #1               \n\t" /* comparing count with 1 */
                  "orr    r8, r9, r10                \n\t" /* br | ag */

                  "add    r8, r6, r8                 \n\t" /* dst = src + calc dest(r8) */

                  /* ----------------- */
                  "stm    %[dst]!, {r7,r8}           \n\t" /* *dst = r7, increment dst by two (each times 4) */
                  /* ----------------- */

                  "bgt    1b                         \n\t" /* if greater than 1 -> reloop */
                  "blt    3f                         \n\t" /* if less than 1 -> exit */

                  /* Single Loop */
                  "2:                                \n\t" /* <single loop> */
                  "ldr    r5, [%[src]], #4           \n\t" /* load the src pointer into r5 r5=src */
                  "ldr    r7, [%[dst]]               \n\t" /* loading dst into r7 */
                  "lsr    r4, r5, #24                \n\t" /* extracting the alpha from source and storing it to r4 */

                  /* ----------- */
                  "and    r9, ip, r7                 \n\t" /* r9 = br masked by ip */
                  "rsb    r4, r4, #256               \n\t" /* subtracting the alpha from 256 -> r4=scale */

                  "and    r10, ip, r7, lsr #8        \n\t" /* r10 = ag masked by ip */
                  "mul    r9, r9, r4                 \n\t" /* br = br * scale */
                  "mul    r10, r10, r4               \n\t" /* ag = ag * scale */
                  "and    r9, ip, r9, lsr #8         \n\t" /* lsr br by 8 and mask it */

                  "and    r10, r10, ip, lsl #8       \n\t" /* mask ag */
                  "orr    r7, r9, r10                \n\t" /* br | ag */

                  "add    r7, r5, r7                 \n\t" /* *dst = src + calc dest(r7) */

                  /* ----------------- */
                  "str    r7, [%[dst]], #4           \n\t" /* *dst = r7, increment dst by one (times 4) */
                  /* ----------------- */

                  "3:                                \n\t" /* <exit> */
                  : [dst] "+r" (dst), [src] "+r" (src), [count] "+r" (count)
                  :
                  : "cc", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "ip", "memory"
                  );
}

#endif

/*
 * ARM asm version of S32A_Blend_BlitRow32
 */
#if USE_NEON_CODE || USE_ARM_CODE

static void S32A_Blend_BlitRow32_arm(SkPMColor* SK_RESTRICT dst,
                                 const SkPMColor* SK_RESTRICT src,
                                 int count, U8CPU alpha) {
    asm volatile (
                  "cmp    %[count], #0               \n\t" /* comparing count with 0 */
                  "beq    3f                         \n\t" /* if zero exit */

                  "mov    r12, #0xff                 \n\t" /* load the 0xff mask in r12 */
                  "orr    r12, r12, r12, lsl #16     \n\t" /* convert it to 0xff00ff in r12 */

                  /* src1,2_scale */
                  "add    %[alpha], %[alpha], #1     \n\t" /* loading %[alpha]=src_scale=alpha+1 */

                  "cmp    %[count], #2               \n\t" /* comparing count with 2 */
                  "blt    2f                         \n\t" /* if less than 2 -> single loop */

                  /* Double Loop */
                  "1:                                \n\t" /* <double loop> */
                  "ldm    %[src]!, {r5, r6}          \n\t" /* loading src pointers into r5 and r6 */
                  "ldm    %[dst], {r7, r8}           \n\t" /* loading dst pointers into r7 and r8 */

                  /* dst1_scale and dst2_scale*/
                  "lsr    r9, r5, #24                \n\t" /* src >> 24 */
                  "lsr    r10, r6, #24               \n\t" /* src >> 24 */
                  "smulbb r9, r9, %[alpha]           \n\t" /* r9 = SkMulS16 r9 with src_scale */
                  "smulbb r10, r10, %[alpha]         \n\t" /* r10 = SkMulS16 r10 with src_scale */
                  "lsr    r9, r9, #8                 \n\t" /* r9 >> 8 */
                  "lsr    r10, r10, #8               \n\t" /* r10 >> 8 */
                  "rsb    r9, r9, #256               \n\t" /* dst1_scale = r9 = 255 - r9 + 1 */
                  "rsb    r10, r10, #256             \n\t" /* dst2_scale = r10 = 255 - r10 + 1 */

                  /* ---------------------- */

                  /* src1, src1_scale */
                  "and    r11, r12, r5, lsr #8       \n\t" /* ag = r11 = r5 masked by r12 lsr by #8 */
                  "and    r4, r12, r5                \n\t" /* rb = r4 = r5 masked by r12 */
                  "mul    r11, r11, %[alpha]         \n\t" /* ag = r11 times src_scale */
                  "mul    r4, r4, %[alpha]           \n\t" /* rb = r4 times src_scale */
                  "and    r11, r11, r12, lsl #8      \n\t" /* ag masked by reverse mask (r12) */
                  "and    r4, r12, r4, lsr #8        \n\t" /* rb masked by mask (r12) */
                  "orr    r5, r11, r4                \n\t" /* r5 = (src1, src_scale) */

                  /* dst1, dst1_scale */
                  "and    r11, r12, r7, lsr #8       \n\t" /* ag = r11 = r7 masked by r12 lsr by #8 */
                  "and    r4, r12, r7                \n\t" /* rb = r4 = r7 masked by r12 */
                  "mul    r11, r11, r9               \n\t" /* ag = r11 times dst_scale (r9) */
                  "mul    r4, r4, r9                 \n\t" /* rb = r4 times dst_scale (r9) */
                  "and    r11, r11, r12, lsl #8      \n\t" /* ag masked by reverse mask (r12) */
                  "and    r4, r12, r4, lsr #8        \n\t" /* rb masked by mask (r12) */
                  "orr    r9, r11, r4                \n\t" /* r9 = (dst1, dst_scale) */

                  /* ---------------------- */
                  "add    r9, r5, r9                 \n\t" /* *dst = src plus dst both scaled */
                  /* ---------------------- */

                  /* ====================== */

                  /* src2, src2_scale */
                  "and    r11, r12, r6, lsr #8       \n\t" /* ag = r11 = r6 masked by r12 lsr by #8 */
                  "and    r4, r12, r6                \n\t" /* rb = r4 = r6 masked by r12 */
                  "mul    r11, r11, %[alpha]         \n\t" /* ag = r11 times src_scale */
                  "mul    r4, r4, %[alpha]           \n\t" /* rb = r4 times src_scale */
                  "and    r11, r11, r12, lsl #8      \n\t" /* ag masked by reverse mask (r12) */
                  "and    r4, r12, r4, lsr #8        \n\t" /* rb masked by mask (r12) */
                  "orr    r6, r11, r4                \n\t" /* r6 = (src2, src_scale) */

                  /* dst2, dst2_scale */
                  "and    r11, r12, r8, lsr #8       \n\t" /* ag = r11 = r8 masked by r12 lsr by #8 */
                  "and    r4, r12, r8                \n\t" /* rb = r4 = r8 masked by r12 */
                  "mul    r11, r11, r10              \n\t" /* ag = r11 times dst_scale (r10) */
                  "mul    r4, r4, r10                \n\t" /* rb = r4 times dst_scale (r6) */
                  "and    r11, r11, r12, lsl #8      \n\t" /* ag masked by reverse mask (r12) */
                  "and    r4, r12, r4, lsr #8        \n\t" /* rb masked by mask (r12) */
                  "orr    r10, r11, r4               \n\t" /* r10 = (dst2, dst_scale) */

                  "sub    %[count], %[count], #2     \n\t" /* decrease count by 2 */
                  /* ---------------------- */
                  "add    r10, r6, r10               \n\t" /* *dst = src plus dst both scaled */
                  /* ---------------------- */
                  "cmp    %[count], #1               \n\t" /* compare count with 1 */
                  /* ----------------- */
                  "stm    %[dst]!, {r9, r10}         \n\t" /* copy r9 and r10 to r7 and r8 respectively */
                  /* ----------------- */

                  "bgt    1b                         \n\t" /* if %[count] greater than 1 reloop */
                  "blt    3f                         \n\t" /* if %[count] less than 1 exit */
                                                           /* else get into the single loop */
                  /* Single Loop */
                  "2:                                \n\t" /* <single loop> */
                  "ldr    r5, [%[src]], #4           \n\t" /* loading src pointer into r5: r5=src */
                  "ldr    r7, [%[dst]]               \n\t" /* loading dst pointer into r7: r7=dst */

                  "lsr    r6, r5, #24                \n\t" /* src >> 24 */
                  "and    r8, r12, r5, lsr #8        \n\t" /* ag = r8 = r5 masked by r12 lsr by #8 */
                  "smulbb r6, r6, %[alpha]           \n\t" /* r6 = SkMulS16 with src_scale */
                  "and    r9, r12, r5                \n\t" /* rb = r9 = r5 masked by r12 */
                  "lsr    r6, r6, #8                 \n\t" /* r6 >> 8 */
                  "mul    r8, r8, %[alpha]           \n\t" /* ag = r8 times scale */
                  "rsb    r6, r6, #256               \n\t" /* r6 = 255 - r6 + 1 */

                  /* src, src_scale */
                  "mul    r9, r9, %[alpha]           \n\t" /* rb = r9 times scale */
                  "and    r8, r8, r12, lsl #8        \n\t" /* ag masked by reverse mask (r12) */
                  "and    r9, r12, r9, lsr #8        \n\t" /* rb masked by mask (r12) */
                  "orr    r10, r8, r9                \n\t" /* r10 = (scr, src_scale) */

                  /* dst, dst_scale */
                  "and    r8, r12, r7, lsr #8        \n\t" /* ag = r8 = r7 masked by r12 lsr by #8 */
                  "and    r9, r12, r7                \n\t" /* rb = r9 = r7 masked by r12 */
                  "mul    r8, r8, r6                 \n\t" /* ag = r8 times scale (r6) */
                  "mul    r9, r9, r6                 \n\t" /* rb = r9 times scale (r6) */
                  "and    r8, r8, r12, lsl #8        \n\t" /* ag masked by reverse mask (r12) */
                  "and    r9, r12, r9, lsr #8        \n\t" /* rb masked by mask (r12) */
                  "orr    r7, r8, r9                 \n\t" /* r7 = (dst, dst_scale) */

                  "add    r10, r7, r10               \n\t" /* *dst = src plus dst both scaled */

                  /* ----------------- */
                  "str    r10, [%[dst]], #4          \n\t" /* *dst = r10, postincrement dst by one (times 4) */
                  /* ----------------- */

                  "3:                                \n\t" /* <exit> */
                  : [dst] "+r" (dst), [src] "+r" (src), [count] "+r" (count), [alpha] "+r" (alpha)
                  :
                  : "cc", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r11", "r12", "memory"
                  );

}
#endif

/* Neon version of S32_Blend_BlitRow32()
 * portable version is in src/core/SkBlitRow_D32.cpp
 */
#if USE_NEON_CODE
extern void S32_Blend_BlitRow32_neon(SkPMColor* SK_RESTRICT dst,
                                const SkPMColor* SK_RESTRICT src,
                                int count, U8CPU alpha);

#endif

#if USE_ARM_CODE
#define	S32_Blend_BlitRow32_arm	NULL
#endif

///////////////////////////////////////////////////////////////////////////////

#if USE_NEON_CODE

extern void S32A_D565_Opaque_Dither_neon (uint16_t * SK_RESTRICT dst,
                                      const SkPMColor* SK_RESTRICT src,
                                      int count, U8CPU alpha, int x, int y);

#endif

#if USE_ARM_CODE
#define	S32A_D565_Opaque_Dither_arm NULL
#endif

///////////////////////////////////////////////////////////////////////////////

#if USE_NEON_CODE
extern void S32_D565_Opaque_Dither_neon(uint16_t* SK_RESTRICT dst,
                                     const SkPMColor* SK_RESTRICT src,
                                     int count, U8CPU alpha, int x, int y);
#endif

#if USE_ARM_CODE
#define	S32_D565_Opaque_Dither_arm NULL
#endif

#pragma GCC visibility pop

///////////////////////////////////////////////////////////////////////////////

#if USE_NEON_CODE
static const SkBlitRow::Proc platform_565_procs_neon[] = {
    // no dither
    S32_D565_Opaque_neon,
    S32_D565_Blend_neon,
    S32A_D565_Opaque_neon,
    S32A_D565_Blend_neon,
    
    // dither
    S32_D565_Opaque_Dither_neon,
    S32_D565_Blend_Dither_neon,
    S32A_D565_Opaque_Dither_neon,
    NULL,   // S32A_D565_Blend_Dither
};
#endif

#if USE_ARM_CODE
static const SkBlitRow::Proc platform_565_procs_arm[] = {
    // no dither
    S32_D565_Opaque_arm,
    S32_D565_Blend_arm,
    S32A_D565_Opaque_arm,
    S32A_D565_Blend_arm,
    
    // dither
    S32_D565_Opaque_Dither_arm,
    S32_D565_Blend_Dither_arm,
    S32A_D565_Opaque_Dither_arm,
    NULL,   // S32A_D565_Blend_Dither
};
#endif

SkBlitRow::Proc SkBlitRow::PlatformProcs565(unsigned flags) {
#if USE_NEON_CODE && USE_ARM_CODE
    if (sk_cpu_arm_has_neon()) {
        return platform_565_procs_neon[flags];
    } else {
        return platform_565_procs_arm[flags];
    }
#elif USE_NEON_CODE
    return platform_565_procs_neon[flags];
#elif USE_ARM_CODE
    return platform_565_procs_arm[flags];
#else
    return NULL;
#endif
}

static const SkBlitRow::Proc platform_4444_procs[] = {
    // no dither
    NULL,   // S32_D4444_Opaque,
    NULL,   // S32_D4444_Blend,
    NULL,   // S32A_D4444_Opaque,
    NULL,   // S32A_D4444_Blend,
    
    // dither
    NULL,   // S32_D4444_Opaque_Dither,
    NULL,   // S32_D4444_Blend_Dither,
    NULL,   // S32A_D4444_Opaque_Dither,
    NULL,   // S32A_D4444_Blend_Dither
};

SkBlitRow::Proc SkBlitRow::PlatformProcs4444(unsigned flags) {
    return platform_4444_procs[flags];
}


#if USE_NEON_CODE
static const SkBlitRow::Proc32 platform_32_procs_neon[] = {
    NULL,   // S32_Opaque,
    S32_Blend_BlitRow32_neon,		// S32_Blend,
    S32A_Opaque_BlitRow32_neon,		// S32A_Opaque,
    S32A_Blend_BlitRow32_arm		// S32A_Blend
};
#endif

#if USE_ARM_CODE
static const SkBlitRow::Proc32 platform_32_procs_arm[] = {
    NULL,   // S32_Opaque,
    S32_Blend_BlitRow32_arm,           // S32_Blend,
    S32A_Opaque_BlitRow32_arm,         // S32A_Opaque,
    S32A_Blend_BlitRow32_arm           // S32A_Blend
};
#endif

SkBlitRow::Proc32 SkBlitRow::PlatformProcs32(unsigned flags) {
#if USE_NEON_CODE && USE_ARM_CODE
    if (sk_cpu_arm_has_neon()) {
        return platform_32_procs_neon[flags];
    } else {
        return platform_32_procs_arm[flags];
    }
#elif USE_NEON_CODE
    return platform_32_procs_neon[flags];
#elif USE_ARM_CODE
    return platform_32_procs_arm[flags];
#else
    return NULL;
#endif
}

SkBlitRow::ColorProc SkBlitRow::PlatformColorProc() {
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////

SkBlitMask::ColorProc SkBlitMask::PlatformColorProcs(SkBitmap::Config dstConfig,
                                                     SkMask::Format maskFormat,
                                                     SkColor color) {
    return NULL;
}

SkBlitMask::RowProc SkBlitMask::PlatformRowProcs(SkBitmap::Config dstConfig,
                                                 SkMask::Format maskFormat,
                                                 RowFlags flags) {
    return NULL;
}
