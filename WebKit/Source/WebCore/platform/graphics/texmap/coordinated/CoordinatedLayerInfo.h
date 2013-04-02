/*
 Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies)

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Library General Public
 License as published by the Free Software Foundation; either
 version 2 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Library General Public License for more details.

 You should have received a copy of the GNU Library General Public License
 along with this library; see the file COPYING.LIB.  If not, write to
 the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 Boston, MA 02110-1301, USA.
 */

#ifndef CoordinatedLayerInfo_h
#define CoordinatedLayerInfo_h

#if USE(COORDINATED_GRAPHICS)

#include "Color.h"
#include "FloatRect.h"
#include "FloatSize.h"
#include "IntRect.h"
#include "TransformationMatrix.h"

namespace WebCore {

typedef uint32_t CoordinatedLayerID;
enum { InvalidCoordinatedLayerID = 0 };

typedef uint64_t CoordinatedImageBackingID;
enum { InvalidCoordinatedImageBackingID = 0 };

// NOTE: CoordinatedLayerInfo should only use POD types, as to make serialization faster.
struct CoordinatedLayerInfo {
    CoordinatedLayerInfo()
        : replica(InvalidCoordinatedLayerID)
        , mask(InvalidCoordinatedLayerID)
        , imageID(InvalidCoordinatedImageBackingID)
        , opacity(0)
        , debugBorderWidth(0)
        , flags(0) { }

    CoordinatedLayerID replica;
    CoordinatedLayerID mask;
    CoordinatedImageBackingID imageID;

    FloatPoint pos;
    FloatPoint3D anchorPoint;
    FloatSize size;
    TransformationMatrix transform;
    TransformationMatrix childrenTransform;
    IntRect contentsRect;
    float opacity;
    Color solidColor;
    Color debugBorderColor;
    float debugBorderWidth;

    union {
        struct {
            bool contentsOpaque : 1;
            bool drawsContent : 1;
            bool contentsVisible : 1;
            bool backfaceVisible : 1;
            bool masksToBounds : 1;
            bool preserves3D : 1;
            bool isRootLayer: 1;
            bool fixedToViewport : 1;
            bool showDebugBorders : 1;
            bool showRepaintCounter : 1;
        };
        unsigned flags;
    };
};

} // namespace WebCore

#endif // USE(COORDINATED_GRAPHICS)

#endif // CoordinatedLayerInfo_h
