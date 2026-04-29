/*
 * Color Computer 3 GIME scanout
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "coco3_video.h"

static void coco3_gfx_invalidate(void *opaque)
{
    Coco3State *s = opaque;

    s->video_dirty = true;
}

static void coco3_gfx_update(void *opaque)
{
    Coco3State *s = opaque;
    DisplaySurface *surface;
    uint8_t *d;
    uint32_t pixel;
    int x, y, width, height, stride;

    if (!s->video_dirty) {
        return;
    }
    s->video_dirty = false;

    surface = qemu_console_surface(s->con);
    if (!surface || surface_bits_per_pixel(surface) != 32) {
        return;
    }

    pixel = rgb_to_pixel32(0x00, 0xff, 0x00);
    width = surface_width(surface);
    height = surface_height(surface);
    stride = surface_stride(surface);
    d = surface_data(surface);

    for (y = 0; y < height; y++) {
        uint32_t *p = (uint32_t *)d;

        for (x = 0; x < width; x++) {
            p[x] = pixel;
        }
        d += stride;
    }

    dpy_gfx_update_full(s->con);
}

static const GraphicHwOps coco3_gfx_ops = {
    .invalidate = coco3_gfx_invalidate,
    .gfx_update = coco3_gfx_update,
};

void coco3_video_init(Coco3State *s)
{
    s->con = graphic_console_init(DEVICE(s), 0, &coco3_gfx_ops, s);
    qemu_console_resize(s->con, COCO3_DISPLAY_WIDTH, COCO3_DISPLAY_HEIGHT);
    s->video_dirty = true;
}

void coco3_video_invalidate(Coco3State *s)
{
    graphic_hw_invalidate(s->con);
}
