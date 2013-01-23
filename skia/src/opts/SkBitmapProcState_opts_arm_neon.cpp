/*
 * Copyright 2012 Google Inc.
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

// The default procedures must have an empty PROC_TYPE
#define PROC_TYPE  _neon
#include "SkBitmapProcState_filter_neon.h"
#include "SkBitmapProcState_procs.h"
