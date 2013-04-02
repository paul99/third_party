
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTileGrid.h"

SkTileGrid::SkTileGrid(int tileWidth, int tileHeight, int xTileCount, int yTileCount,
    int borderPixels, SkTileGridNextDatumFunctionPtr nextDatumFunction)
{
    fTileWidth = tileWidth;
    fTileHeight = tileHeight;
    fXTileCount = xTileCount;
    fYTileCount = yTileCount;
    // Border padding is offset by 1 as a provision for AA and
    // to cancel-out the outset applied by getClipDeviceBounds.
    fBorderPixels = borderPixels + 1;
    fTileCount = fXTileCount * fYTileCount;
    fInsertionCount = 0;
    fGridBounds = SkIRect::MakeXYWH(0, 0, fTileWidth * fXTileCount, fTileHeight * fYTileCount);
    fNextDatumFunction = nextDatumFunction;
    fTileData = SkNEW_ARRAY(SkTDArray<void *>, fTileCount);
}

SkTileGrid::~SkTileGrid() {
    SkDELETE_ARRAY(fTileData);
}

SkTDArray<void *>& SkTileGrid::tile(int x, int y) {
    return fTileData[y * fXTileCount + x];
}

void SkTileGrid::insert(void* data, const SkIRect& bounds, bool) {
    SkASSERT(!bounds.isEmpty());
    SkIRect dilatedBounds = bounds;
    dilatedBounds.outset(fBorderPixels, fBorderPixels);

    if (!SkIRect::Intersects(dilatedBounds, fGridBounds)) {
        return;
    }

    int minTileX = SkMax32(SkMin32(dilatedBounds.left() / fTileWidth, fXTileCount - 1), 0);
    int maxTileX = SkMax32(SkMin32(dilatedBounds.right() / fTileWidth, fXTileCount - 1), 0);
    int minTileY = SkMax32(SkMin32(dilatedBounds.top() / fTileHeight, fYTileCount -1), 0);
    int maxTileY = SkMax32(SkMin32(dilatedBounds.bottom() / fTileHeight, fYTileCount -1), 0);

    for (int x = minTileX; x <= maxTileX; x++) {
        for (int y = minTileY; y <= maxTileY; y++) {
            this->tile(x, y).push(data);
        }
    }
    fInsertionCount++;
}

void SkTileGrid::search(const SkIRect& query, SkTDArray<void*>* results) {
    // Convert the query rectangle from device coordinates to tile coordinates
    // by rounding outwards to the nearest tile boundary so that the resulting tile
    // region includes the query rectangle. (using truncating division to "floor")
    int tileStartX = (query.left() + fBorderPixels) / fTileWidth;
    int tileEndX = (query.right() + fTileWidth - fBorderPixels) / fTileWidth;
    int tileStartY = (query.top() + fBorderPixels) / fTileHeight;
    int tileEndY = (query.bottom() + fTileHeight - fBorderPixels) / fTileHeight;
    if (tileStartX >= fXTileCount || tileStartY >= fYTileCount || tileEndX <= 0 || tileEndY <= 0) {
        return; // query does not intersect the grid
    }
    // clamp to grid
    if (tileStartX < 0) tileStartX = 0;
    if (tileStartY < 0) tileStartY = 0;
    if (tileEndX > fXTileCount) tileEndX = fXTileCount;
    if (tileEndY > fYTileCount) tileEndY = fYTileCount;

    int queryTileCount = (tileEndX - tileStartX) * (tileEndY - tileStartY);
    if (queryTileCount == 1) {
        *results = this->tile(tileStartX, tileStartY);
    } else {
        results->reset();
        SkTDArray<int> curPositions;
        curPositions.setCount(queryTileCount);
        // Note: Reserving space for 1024 tile pointers on the stack. If the
        // malloc becomes a bottleneck, we may consider increasing that number.
        // Typical large web page, say 2k x 16k, would require 512 tiles of
        // size 256 x 256 pixels.
        SkAutoSTArray<1024, SkTDArray<void *>*> storage(queryTileCount);
        SkTDArray<void *>** tileRange = storage.get();
        int tile = 0;
        for (int x = tileStartX; x < tileEndX; ++x) {
            for (int y = tileStartY; y < tileEndY; ++y) {
                tileRange[tile] = &this->tile(x, y);
                curPositions[tile] = tileRange[tile]->count() ? 0 : kTileFinished;
                ++tile;
            }
        }
        void *nextElement;
        while(NULL != (nextElement = fNextDatumFunction(tileRange, curPositions))) {
            results->push(nextElement);
        }
    }
}

void SkTileGrid::clear() {
    for (int i = 0; i < fTileCount; i++) {
        fTileData[i].reset();
    }
}

int SkTileGrid::getCount() const {
    return fInsertionCount;
}
