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
#include "qemu/log.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "coco3_video.h"

/* 320×192×16: HRES=111 (160 bytes/row), CRES=10 (4 bits/pixel). */
#define GIME_MODE_WIDTH   320
#define GIME_MODE_HEIGHT  192
#define GIME_BPL_320_16   160

static uint32_t gime_rgb_to_pixel(uint8_t color)
{
    unsigned r = (((color >> 4) & 2) | ((color >> 2) & 1)) * 0x55;
    unsigned g = (((color >> 3) & 2) | ((color >> 1) & 1)) * 0x55;
    unsigned b = (((color >> 2) & 2) | ((color >> 0) & 1)) * 0x55;

    return rgb_to_pixel32(r, g, b);
}

static uint32_t coco3_video_base(const Coco3State *s)
{
    uint32_t base;

    /* $FF9D:$FF9E are bits 18–3 of the physical address (8-byte steps). */
    base = ((uint32_t)s->voff_msb << 11) | ((uint32_t)s->voff_lsb << 3);
    /* $FF9F bits 6–0 are a 2-byte horizontal offset. */
    base += (s->hoff & 0x7f) * 2;
    return base & (COCO3_RAM_SIZE - 1);
}

static bool coco3_video_is_320x192x16(const Coco3State *s)
{
    if (s->init0 & GIME_INIT0_COCO) {
        return false;
    }
    if (!(s->vmode & GIME_VMODE_BP)) {
        return false;
    }
    if (s->hoff & GIME_HOFF_HVEN) {
        return false;
    }
    /* LPF=00 (192), HRES=111 (160 bytes), CRES=10 (16 color). */
    return (s->vres & (GIME_VRES_LPF | GIME_VRES_HRES | GIME_VRES_CRES))
           == ((7 << 2) | 2);
}

static void coco3_video_log_unimp(Coco3State *s)
{
    if (s->video_unimp_logged) {
        return;
    }

    s->video_unimp_logged = true;
    qemu_log_mask(LOG_UNIMP,
                  "coco3: GIME video INIT0=%02x VMODE=%02x VRES=%02x "
                  "HOFF=%02x unimplemented\n",
                  s->init0, s->vmode, s->vres, s->hoff);
}

static void coco3_fill_border(Coco3State *s, DisplaySurface *surface)
{
    uint8_t *d = surface_data(surface);
    uint32_t pixel = gime_rgb_to_pixel(s->border & GIME_COLOR_MASK);
    int x, y, width, height, stride;

    width = surface_width(surface);
    height = surface_height(surface);
    stride = surface_stride(surface);

    for (y = 0; y < height; y++) {
        uint32_t *p = (uint32_t *)d;

        for (x = 0; x < width; x++) {
            p[x] = pixel;
        }
        d += stride;
    }
}

static void coco3_draw_320x192x16(Coco3State *s, DisplaySurface *surface)
{
    uint8_t *ram = memory_region_get_ram_ptr(s->ram);
    uint8_t *d = surface_data(surface);
    uint32_t base = coco3_video_base(s);
    int stride = surface_stride(surface);
    int x0 = (COCO3_DISPLAY_WIDTH - GIME_MODE_WIDTH) / 2;
    int y0 = (COCO3_DISPLAY_HEIGHT - GIME_MODE_HEIGHT) / 2;
    int x, y;

    coco3_fill_border(s, surface);
    d += (size_t)y0 * stride + (size_t)x0 * 4;

    for (y = 0; y < GIME_MODE_HEIGHT; y++) {
        uint32_t *p = (uint32_t *)d;
        uint32_t row = base + (uint32_t)y * GIME_BPL_320_16;

        for (x = 0; x < GIME_MODE_WIDTH; x += 2) {
            uint8_t b = ram[(row + (x >> 1)) & (COCO3_RAM_SIZE - 1)];

            p[x] = s->palette_rgb[b >> 4];
            p[x + 1] = s->palette_rgb[b & 0x0f];
        }
        d += stride;
    }
}

static void coco3_gfx_invalidate(void *opaque)
{
    Coco3State *s = opaque;

    s->video_dirty = true;
}

static void coco3_gfx_update(void *opaque)
{
    Coco3State *s = opaque;
    DisplaySurface *surface;
    bool bitmap = coco3_video_is_320x192x16(s);

    if (!bitmap && !s->video_dirty) {
        return;
    }
    s->video_dirty = false;

    surface = qemu_console_surface(s->con);
    if (!surface || surface_bits_per_pixel(surface) != 32) {
        return;
    }

    if (bitmap) {
        s->video_unimp_logged = false;
        coco3_draw_320x192x16(s, surface);
    } else {
        coco3_video_log_unimp(s);
        coco3_fill_border(s, surface);
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

void coco3_video_reset(Coco3State *s)
{
    int i;

    for (i = 0; i < GIME_PALETTE_COUNT; i++) {
        s->palette_rgb[i] = gime_rgb_to_pixel(s->palette[i]);
    }
    s->video_unimp_logged = false;
    coco3_video_invalidate(s);
}

void coco3_video_set_palette(Coco3State *s, unsigned idx, uint8_t val)
{
    val &= GIME_COLOR_MASK;
    s->palette[idx] = val;
    s->palette_rgb[idx] = gime_rgb_to_pixel(val);
    coco3_video_invalidate(s);
}
