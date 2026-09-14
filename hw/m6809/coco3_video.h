/*
 * Color Computer 3 GIME scanout (same SoC as coco3.c).
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_COCO3_VIDEO_H
#define HW_M6809_COCO3_VIDEO_H

#include "coco3.h"

#define COCO3_DISPLAY_WIDTH   640
#define COCO3_RASTER_HEIGHT   240
#define COCO3_YSCALE          2
#define COCO3_DISPLAY_HEIGHT  (COCO3_RASTER_HEIGHT * COCO3_YSCALE)

void coco3_video_init(Coco3State *s);
void coco3_video_reset(Coco3State *s);
void coco3_video_invalidate(Coco3State *s);
void coco3_video_set_palette(Coco3State *s, unsigned idx, uint8_t val);

#endif /* HW_M6809_COCO3_VIDEO_H */
