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

/* VDG-compat (INIT0.COCO): 256×192, doubled to 512×192. */
#define VDG_WIDTH         256
#define VDG_HEIGHT        192
#define VDG_XSCALE        2
#define VDG_BODY_WIDTH    (VDG_WIDTH * VDG_XSCALE)
#define VDG_TEXT_COLS     32
#define VDG_CELL_H        12
#define VDG_PMODE4_BPL    32
#define VDG_BORDER_GREEN  0x12
#define VDG_BORDER_BUFF   0x3f

/*
 * NitrOS-9 SYS/stdfonts Fnt_S8x8 (group 200 buffer 1): 8×8, codes 0x00–0x7f.
 * Bit 7 is the leftmost pixel. GIME text ignores bit 7 of the character
 * code, so 0x80–0xff alias 0x00–0x7f. EOU calls this a GIME CG ROM copy.
 */
static const uint8_t coco3_font_8x8[128][8] = {
    { 0x38, 0x44, 0x40, 0x40, 0x40, 0x44, 0x38, 0x10 }, /* 0x00 */
    { 0x44, 0x00, 0x44, 0x44, 0x44, 0x4c, 0x34, 0x00 }, /* 0x01 */
    { 0x08, 0x10, 0x38, 0x44, 0x7c, 0x40, 0x38, 0x00 }, /* 0x02 */
    { 0x10, 0x28, 0x38, 0x04, 0x3c, 0x44, 0x3c, 0x00 }, /* 0x03 */
    { 0x28, 0x00, 0x38, 0x04, 0x3c, 0x44, 0x3c, 0x00 }, /* 0x04 */
    { 0x20, 0x10, 0x38, 0x04, 0x3c, 0x44, 0x3c, 0x00 }, /* 0x05 */
    { 0x10, 0x00, 0x38, 0x04, 0x3c, 0x44, 0x3c, 0x00 }, /* 0x06 */
    { 0x00, 0x00, 0x38, 0x44, 0x40, 0x44, 0x38, 0x10 }, /* 0x07 */
    { 0x10, 0x28, 0x38, 0x44, 0x7c, 0x40, 0x38, 0x00 }, /* 0x08 */
    { 0x28, 0x00, 0x38, 0x44, 0x7c, 0x40, 0x38, 0x00 }, /* 0x09 */
    { 0x20, 0x10, 0x38, 0x44, 0x7c, 0x40, 0x38, 0x00 }, /* 0x0a */
    { 0x28, 0x00, 0x30, 0x10, 0x10, 0x10, 0x38, 0x00 }, /* 0x0b */
    { 0x10, 0x28, 0x00, 0x30, 0x10, 0x10, 0x38, 0x00 }, /* 0x0c */
    { 0x00, 0x18, 0x24, 0x38, 0x24, 0x24, 0x38, 0x40 }, /* 0x0d */
    { 0x44, 0x10, 0x28, 0x44, 0x7c, 0x44, 0x44, 0x00 }, /* 0x0e */
    { 0x10, 0x10, 0x28, 0x44, 0x7c, 0x44, 0x44, 0x00 }, /* 0x0f */
    { 0x08, 0x10, 0x38, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x10 */
    { 0x00, 0x00, 0x68, 0x14, 0x3c, 0x50, 0x3c, 0x00 }, /* 0x11 */
    { 0x3c, 0x50, 0x50, 0x78, 0x50, 0x50, 0x5c, 0x00 }, /* 0x12 */
    { 0x10, 0x28, 0x38, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x13 */
    { 0x28, 0x00, 0x38, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x14 */
    { 0x00, 0x00, 0x38, 0x4c, 0x54, 0x64, 0x38, 0x00 }, /* 0x15 */
    { 0x10, 0x28, 0x00, 0x44, 0x44, 0x4c, 0x34, 0x00 }, /* 0x16 */
    { 0x20, 0x10, 0x44, 0x44, 0x44, 0x4c, 0x34, 0x00 }, /* 0x17 */
    { 0x38, 0x4c, 0x54, 0x54, 0x54, 0x64, 0x38, 0x00 }, /* 0x18 */
    { 0x44, 0x38, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x19 */
    { 0x28, 0x44, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x1a */
    { 0x38, 0x40, 0x38, 0x44, 0x38, 0x04, 0x38, 0x00 }, /* 0x1b */
    { 0x08, 0x14, 0x10, 0x38, 0x10, 0x50, 0x3c, 0x00 }, /* 0x1c */
    { 0x10, 0x10, 0x7c, 0x10, 0x10, 0x00, 0x7c, 0x00 }, /* 0x1d */
    { 0x10, 0x28, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x1e */
    { 0x08, 0x14, 0x10, 0x38, 0x10, 0x10, 0x20, 0x40 }, /* 0x1f */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x20 space */
    { 0x10, 0x10, 0x10, 0x10, 0x10, 0x00, 0x10, 0x00 }, /* 0x21 ! */
    { 0x28, 0x28, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x22 " */
    { 0x28, 0x28, 0x7c, 0x28, 0x7c, 0x28, 0x28, 0x00 }, /* 0x23 # */
    { 0x10, 0x3c, 0x50, 0x38, 0x14, 0x78, 0x10, 0x00 }, /* 0x24 $ */
    { 0x60, 0x64, 0x08, 0x10, 0x20, 0x4c, 0x0c, 0x00 }, /* 0x25 % */
    { 0x20, 0x50, 0x50, 0x20, 0x54, 0x48, 0x34, 0x00 }, /* 0x26 & */
    { 0x10, 0x10, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x27 ' */
    { 0x08, 0x10, 0x20, 0x20, 0x20, 0x10, 0x08, 0x00 }, /* 0x28 ( */
    { 0x20, 0x10, 0x08, 0x08, 0x08, 0x10, 0x20, 0x00 }, /* 0x29 ) */
    { 0x00, 0x10, 0x54, 0x38, 0x38, 0x54, 0x10, 0x00 }, /* 0x2a * */
    { 0x00, 0x10, 0x10, 0x7c, 0x10, 0x10, 0x00, 0x00 }, /* 0x2b + */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x10, 0x20 }, /* 0x2c , */
    { 0x00, 0x00, 0x00, 0x7c, 0x00, 0x00, 0x00, 0x00 }, /* 0x2d - */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00 }, /* 0x2e . */
    { 0x00, 0x04, 0x08, 0x10, 0x20, 0x40, 0x00, 0x00 }, /* 0x2f / */
    { 0x38, 0x44, 0x4c, 0x54, 0x64, 0x44, 0x38, 0x00 }, /* 0x30 0 */
    { 0x10, 0x30, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00 }, /* 0x31 1 */
    { 0x38, 0x44, 0x04, 0x38, 0x40, 0x40, 0x7c, 0x00 }, /* 0x32 2 */
    { 0x38, 0x44, 0x04, 0x08, 0x04, 0x44, 0x38, 0x00 }, /* 0x33 3 */
    { 0x08, 0x18, 0x28, 0x48, 0x7c, 0x08, 0x08, 0x00 }, /* 0x34 4 */
    { 0x7c, 0x40, 0x78, 0x04, 0x04, 0x44, 0x38, 0x00 }, /* 0x35 5 */
    { 0x38, 0x40, 0x40, 0x78, 0x44, 0x44, 0x38, 0x00 }, /* 0x36 6 */
    { 0x7c, 0x04, 0x08, 0x10, 0x20, 0x40, 0x40, 0x00 }, /* 0x37 7 */
    { 0x38, 0x44, 0x44, 0x38, 0x44, 0x44, 0x38, 0x00 }, /* 0x38 8 */
    { 0x38, 0x44, 0x44, 0x38, 0x04, 0x04, 0x38, 0x00 }, /* 0x39 9 */
    { 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x00, 0x00 }, /* 0x3a : */
    { 0x00, 0x00, 0x10, 0x00, 0x00, 0x10, 0x10, 0x20 }, /* 0x3b ; */
    { 0x08, 0x10, 0x20, 0x40, 0x20, 0x10, 0x08, 0x00 }, /* 0x3c < */
    { 0x00, 0x00, 0x7c, 0x00, 0x7c, 0x00, 0x00, 0x00 }, /* 0x3d = */
    { 0x20, 0x10, 0x08, 0x04, 0x08, 0x10, 0x20, 0x00 }, /* 0x3e > */
    { 0x38, 0x44, 0x04, 0x08, 0x10, 0x00, 0x10, 0x00 }, /* 0x3f ? */
    { 0x38, 0x44, 0x04, 0x34, 0x4c, 0x4c, 0x38, 0x00 }, /* 0x40 @ */
    { 0x10, 0x28, 0x44, 0x44, 0x7c, 0x44, 0x44, 0x00 }, /* 0x41 A */
    { 0x78, 0x24, 0x24, 0x38, 0x24, 0x24, 0x78, 0x00 }, /* 0x42 B */
    { 0x38, 0x44, 0x40, 0x40, 0x40, 0x44, 0x38, 0x00 }, /* 0x43 C */
    { 0x78, 0x24, 0x24, 0x24, 0x24, 0x24, 0x78, 0x00 }, /* 0x44 D */
    { 0x7c, 0x40, 0x40, 0x70, 0x40, 0x40, 0x7c, 0x00 }, /* 0x45 E */
    { 0x7c, 0x40, 0x40, 0x70, 0x40, 0x40, 0x40, 0x00 }, /* 0x46 F */
    { 0x38, 0x44, 0x40, 0x40, 0x4c, 0x44, 0x38, 0x00 }, /* 0x47 G */
    { 0x44, 0x44, 0x44, 0x7c, 0x44, 0x44, 0x44, 0x00 }, /* 0x48 H */
    { 0x38, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00 }, /* 0x49 I */
    { 0x04, 0x04, 0x04, 0x04, 0x04, 0x44, 0x38, 0x00 }, /* 0x4a J */
    { 0x44, 0x48, 0x50, 0x60, 0x50, 0x48, 0x44, 0x00 }, /* 0x4b K */
    { 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x7c, 0x00 }, /* 0x4c L */
    { 0x44, 0x6c, 0x54, 0x54, 0x44, 0x44, 0x44, 0x00 }, /* 0x4d M */
    { 0x44, 0x44, 0x64, 0x54, 0x4c, 0x44, 0x44, 0x00 }, /* 0x4e N */
    { 0x38, 0x44, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x4f O */
    { 0x78, 0x44, 0x44, 0x78, 0x40, 0x40, 0x40, 0x00 }, /* 0x50 P */
    { 0x38, 0x44, 0x44, 0x44, 0x54, 0x48, 0x34, 0x00 }, /* 0x51 Q */
    { 0x78, 0x44, 0x44, 0x78, 0x50, 0x48, 0x44, 0x00 }, /* 0x52 R */
    { 0x38, 0x44, 0x40, 0x38, 0x04, 0x44, 0x38, 0x00 }, /* 0x53 S */
    { 0x7c, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x00 }, /* 0x54 T */
    { 0x44, 0x44, 0x44, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x55 U */
    { 0x44, 0x44, 0x44, 0x28, 0x28, 0x10, 0x10, 0x00 }, /* 0x56 V */
    { 0x44, 0x44, 0x44, 0x44, 0x54, 0x6c, 0x44, 0x00 }, /* 0x57 W */
    { 0x44, 0x44, 0x28, 0x10, 0x28, 0x44, 0x44, 0x00 }, /* 0x58 X */
    { 0x44, 0x44, 0x28, 0x10, 0x10, 0x10, 0x10, 0x00 }, /* 0x59 Y */
    { 0x7c, 0x04, 0x08, 0x10, 0x20, 0x40, 0x7c, 0x00 }, /* 0x5a Z */
    { 0x38, 0x20, 0x20, 0x20, 0x20, 0x20, 0x38, 0x00 }, /* 0x5b [ */
    { 0x00, 0x40, 0x20, 0x10, 0x08, 0x04, 0x00, 0x00 }, /* 0x5c \\ */
    { 0x38, 0x08, 0x08, 0x08, 0x08, 0x08, 0x38, 0x00 }, /* 0x5d ] */
    { 0x10, 0x28, 0x44, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x5e ^ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x00 }, /* 0x5f _ */
    { 0x20, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x60 ` */
    { 0x00, 0x00, 0x38, 0x04, 0x3c, 0x44, 0x3c, 0x00 }, /* 0x61 a */
    { 0x40, 0x40, 0x58, 0x64, 0x44, 0x64, 0x58, 0x00 }, /* 0x62 b */
    { 0x00, 0x00, 0x38, 0x44, 0x40, 0x44, 0x38, 0x00 }, /* 0x63 c */
    { 0x04, 0x04, 0x34, 0x4c, 0x44, 0x4c, 0x34, 0x00 }, /* 0x64 d */
    { 0x00, 0x00, 0x38, 0x44, 0x7c, 0x40, 0x38, 0x00 }, /* 0x65 e */
    { 0x08, 0x14, 0x10, 0x38, 0x10, 0x10, 0x10, 0x00 }, /* 0x66 f */
    { 0x00, 0x00, 0x34, 0x4c, 0x4c, 0x34, 0x04, 0x38 }, /* 0x67 g */
    { 0x40, 0x40, 0x58, 0x64, 0x44, 0x44, 0x44, 0x00 }, /* 0x68 h */
    { 0x00, 0x10, 0x00, 0x30, 0x10, 0x10, 0x38, 0x00 }, /* 0x69 i */
    { 0x00, 0x04, 0x00, 0x04, 0x04, 0x04, 0x44, 0x38 }, /* 0x6a j */
    { 0x40, 0x40, 0x48, 0x50, 0x60, 0x50, 0x48, 0x00 }, /* 0x6b k */
    { 0x30, 0x10, 0x10, 0x10, 0x10, 0x10, 0x38, 0x00 }, /* 0x6c l */
    { 0x00, 0x00, 0x68, 0x54, 0x54, 0x54, 0x54, 0x00 }, /* 0x6d m */
    { 0x00, 0x00, 0x58, 0x64, 0x44, 0x44, 0x44, 0x00 }, /* 0x6e n */
    { 0x00, 0x00, 0x38, 0x44, 0x44, 0x44, 0x38, 0x00 }, /* 0x6f o */
    { 0x00, 0x00, 0x78, 0x44, 0x44, 0x78, 0x40, 0x40 }, /* 0x70 p */
    { 0x00, 0x00, 0x3c, 0x44, 0x44, 0x3c, 0x04, 0x04 }, /* 0x71 q */
    { 0x00, 0x00, 0x58, 0x64, 0x40, 0x40, 0x40, 0x00 }, /* 0x72 r */
    { 0x00, 0x00, 0x3c, 0x40, 0x38, 0x04, 0x78, 0x00 }, /* 0x73 s */
    { 0x20, 0x20, 0x70, 0x20, 0x20, 0x24, 0x18, 0x00 }, /* 0x74 t */
    { 0x00, 0x00, 0x44, 0x44, 0x44, 0x4c, 0x34, 0x00 }, /* 0x75 u */
    { 0x00, 0x00, 0x44, 0x44, 0x44, 0x28, 0x10, 0x00 }, /* 0x76 v */
    { 0x00, 0x00, 0x44, 0x54, 0x54, 0x28, 0x28, 0x00 }, /* 0x77 w */
    { 0x00, 0x00, 0x44, 0x28, 0x10, 0x28, 0x44, 0x00 }, /* 0x78 x */
    { 0x00, 0x00, 0x44, 0x44, 0x44, 0x3c, 0x04, 0x38 }, /* 0x79 y */
    { 0x00, 0x00, 0x7c, 0x08, 0x10, 0x20, 0x7c, 0x00 }, /* 0x7a z */
    { 0x08, 0x10, 0x10, 0x20, 0x10, 0x10, 0x08, 0x00 }, /* 0x7b { */
    { 0x10, 0x10, 0x10, 0x00, 0x10, 0x10, 0x10, 0x00 }, /* 0x7c | */
    { 0x20, 0x10, 0x10, 0x08, 0x10, 0x10, 0x20, 0x00 }, /* 0x7d } */
    { 0x20, 0x54, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 0x7e ~ */
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7c, 0x00 }, /* 0x7f */
};

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

/*
 * VDG-compat base: SAM F0–F6 × 512, plus the high $FF9D bits that select
 * a 64 KiB bank in 512 KiB (MAME gime get_video_base, masks from Kowalski).
 */
static uint32_t coco3_vdg_base(const Coco3State *s)
{
    uint32_t base = (uint32_t)(s->sam_f & SAM_F_MASK) << 9;

    base += (uint32_t)(s->voff_msb & 0xe0) << 11;
    base += (uint32_t)(s->voff_lsb & 0x3f) << 3;
    return base & (COCO3_RAM_SIZE - 1);
}

static uint8_t coco3_vdg_ff22(const Coco3State *s)
{
    return s->pia1.b.data;
}

static int coco3_video_lpr(const Coco3State *s)
{
    switch (s->vmode & GIME_VMODE_LPR) {
    case 0:
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 8;
    case 4:
        return 9;
    case 5:
        return 10;
    case 6:
        return 11;
    default:
        return 0;
    }
}

static int coco3_video_lpf(const Coco3State *s)
{
    switch (s->vres & GIME_VRES_LPF) {
    case 0x00:
        return 192;
    case 0x20:
        return 200;
    case 0x60:
        return 225;
    default:
        return 0;
    }
}

static int coco3_video_underline_line(const Coco3State *s)
{
    switch (s->vmode & GIME_VMODE_LPR) {
    case 3:
        return 7;
    case 4:
    case 5:
        return 8;
    case 6:
        return 9;
    default:
        return -1;
    }
}

static uint8_t coco3_glyph_row(uint8_t ch, int line)
{
    if (line < 0 || line >= 8) {
        return 0;
    }
    return coco3_font_8x8[ch & 0x7f][line];
}

static uint8_t coco3_vdg_glyph_row(uint8_t data, int line)
{
    uint8_t ch = data & 0x3f;
    uint8_t ascii;

    /* 8×12 cell: one blank line, then the 8×8 glyph. */
    if (line < 1 || line > 8) {
        return 0;
    }
    /* VDG 6-bit: 0–31 are @–_, 32–63 are space–?. */
    ascii = (ch < 32) ? ch + 0x40 : ch;
    return coco3_glyph_row(ascii, line - 1);
}

static int coco3_gime_bpl(const Coco3State *s)
{
    static const int bpl[8] = { 16, 20, 32, 40, 64, 80, 128, 160 };

    return bpl[(s->vres & GIME_VRES_HRES) >> 2];
}

static int coco3_gime_bpp(const Coco3State *s)
{
    switch (s->vres & GIME_VRES_CRES) {
    case 0:
        return 1;
    case 1:
        return 2;
    default:
        return 4;
    }
}

static bool coco3_video_is_gime_gfx(const Coco3State *s)
{
    int width;

    if (s->init0 & GIME_INIT0_COCO) {
        return false;
    }
    if (!(s->vmode & GIME_VMODE_BP)) {
        return false;
    }
    if (s->hoff & GIME_HOFF_HVEN) {
        return false;
    }
    if (!coco3_video_lpf(s)) {
        return false;
    }
    /* 1/2/4 bpp at the eight HRES byte widths; keep the raster ≤ 640. */
    width = coco3_gime_bpl(s) * (8 / coco3_gime_bpp(s));
    return width > 0 && width <= COCO3_DISPLAY_WIDTH;
}

static bool coco3_video_is_gime_text(const Coco3State *s, int *cols)
{
    unsigned hres;

    if (s->init0 & GIME_INIT0_COCO) {
        return false;
    }
    if (s->vmode & GIME_VMODE_BP) {
        return false;
    }
    if (s->hoff & GIME_HOFF_HVEN) {
        return false;
    }
    if (!coco3_video_lpr(s) || !coco3_video_lpf(s)) {
        return false;
    }
    /* 0x0=32, 0x1=40, 1x0=64, 1x1=80. */
    hres = s->vres & GIME_VRES_HRES_TEXT;
    if (hres == 0x00) {
        *cols = 32;
        return true;
    }
    if (hres == 0x04) {
        *cols = 40;
        return true;
    }
    if (hres == 0x10) {
        *cols = 64;
        return true;
    }
    if (hres == 0x14) {
        *cols = 80;
        return true;
    }
    return false;
}

static bool coco3_video_is_vdg_text(const Coco3State *s)
{
    if (!(s->init0 & GIME_INIT0_COCO)) {
        return false;
    }
    if (coco3_vdg_ff22(s) & VDG_FF22_AG) {
        return false;
    }
    return (s->sam_v & SAM_V_MASK) == 0;
}

static bool coco3_video_is_pmode4(const Coco3State *s)
{
    if (!(s->init0 & GIME_INIT0_COCO)) {
        return false;
    }
    if ((coco3_vdg_ff22(s) & VDG_FF22_PMODE4) != VDG_FF22_PMODE4) {
        return false;
    }
    return (s->sam_v & SAM_V_MASK) == SAM_V_PMODE4;
}

static void coco3_video_log_unimp(Coco3State *s)
{
    if (s->video_unimp_logged) {
        return;
    }

    s->video_unimp_logged = true;
    qemu_log_mask(LOG_UNIMP,
                  "coco3: video INIT0=%02x VMODE=%02x VRES=%02x HOFF=%02x "
                  "SAM V=%u F=%02x FF22=%02x unimplemented\n",
                  s->init0, s->vmode, s->vres, s->hoff,
                  s->sam_v & SAM_V_MASK, s->sam_f & SAM_F_MASK,
                  coco3_vdg_ff22(s));
}

static void coco3_fill_surface(DisplaySurface *surface, uint32_t pixel)
{
    uint8_t *d = surface_data(surface);
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

static void coco3_fill_border(Coco3State *s, DisplaySurface *surface)
{
    coco3_fill_surface(surface, gime_rgb_to_pixel(s->border & GIME_COLOR_MASK));
}

static uint32_t coco3_vdg_border_pixel(const Coco3State *s)
{
    uint8_t ff22 = coco3_vdg_ff22(s);
    uint8_t color;

    if (ff22 & VDG_FF22_AG) {
        color = (ff22 & VDG_FF22_CSS) ? VDG_BORDER_BUFF : VDG_BORDER_GREEN;
    } else {
        color = 0;
    }
    return gime_rgb_to_pixel(color);
}

static void coco3_draw_gime_gfx(Coco3State *s, DisplaySurface *surface)
{
    uint8_t *ram = memory_region_get_ram_ptr(s->ram);
    uint8_t *d = surface_data(surface);
    uint32_t base = coco3_video_base(s);
    int stride = surface_stride(surface);
    int lpr = coco3_video_lpr(s);
    int lpf = coco3_video_lpf(s);
    int bpl = coco3_gime_bpl(s);
    int bpp = coco3_gime_bpp(s);
    int px_per_byte = 8 / bpp;
    int mask = (1 << bpp) - 1;
    int width = bpl * px_per_byte;
    int x0 = (COCO3_DISPLAY_WIDTH - width) / 2;
    int y0 = (COCO3_DISPLAY_HEIGHT - lpf) / 2;
    int x, y, i;

    if (lpr < 1) {
        lpr = 1;
    }

    coco3_fill_border(s, surface);
    d += (size_t)y0 * stride + (size_t)x0 * 4;

    for (y = 0; y < lpf; y++) {
        uint32_t *p = (uint32_t *)d;
        uint32_t row = base + (uint32_t)(y / lpr) * bpl;
        int px = 0;

        for (x = 0; x < bpl; x++) {
            uint8_t b = ram[(row + x) & (COCO3_RAM_SIZE - 1)];

            for (i = px_per_byte - 1; i >= 0; i--) {
                p[px++] = s->palette_rgb[(b >> (i * bpp)) & mask];
            }
        }
        d += stride;
    }
}

static void coco3_draw_gime_text(Coco3State *s, DisplaySurface *surface,
                                 int cols)
{
    uint8_t *ram = memory_region_get_ram_ptr(s->ram);
    uint8_t *d = surface_data(surface);
    uint32_t base = coco3_video_base(s);
    int stride = surface_stride(surface);
    int lpr = coco3_video_lpr(s);
    int lpf = coco3_video_lpf(s);
    int underline_line = coco3_video_underline_line(s);
    bool has_attr = s->vres & GIME_VRES_CRES_ATTR;
    int bpc = has_attr ? 2 : 1;
    int xscale = (cols <= 40) ? 2 : 1;
    int width = cols * 8 * xscale;
    int x0 = (COCO3_DISPLAY_WIDTH - width) / 2;
    int y0 = (COCO3_DISPLAY_HEIGHT - lpf) / 2;
    int y, col, bit, k;

    coco3_fill_border(s, surface);
    d += (size_t)y0 * stride + (size_t)x0 * 4;

    for (y = 0; y < lpf; y++) {
        uint32_t *p = (uint32_t *)d;
        int line = y % lpr;
        uint32_t row = base + (uint32_t)(y / lpr) * (cols * bpc);
        int px = 0;

        for (col = 0; col < cols; col++) {
            uint32_t addr = (row + col * bpc) & (COCO3_RAM_SIZE - 1);
            uint8_t ch = ram[addr];
            uint8_t attr, bits;
            uint32_t fg, bg;

            if (has_attr) {
                attr = ram[(addr + 1) & (COCO3_RAM_SIZE - 1)];
                bg = s->palette_rgb[attr & GIME_ATTR_BG];
                fg = s->palette_rgb[8 + ((attr & GIME_ATTR_FG) >> 3)];
            } else {
                attr = 0;
                bg = s->palette_rgb[0];
                fg = s->palette_rgb[1];
            }

            bits = coco3_glyph_row(ch, line);
            if (has_attr && (attr & GIME_ATTR_UNDERLINE) &&
                line == underline_line) {
                bits = 0xff;
            }

            for (bit = 7; bit >= 0; bit--) {
                uint32_t pix = (bits & (1u << bit)) ? fg : bg;
                for (k = 0; k < xscale; k++) {
                    p[px++] = pix;
                }
            }
        }
        d += stride;
    }
}

static void coco3_draw_vdg_text(Coco3State *s, DisplaySurface *surface)
{
    uint8_t *ram = memory_region_get_ram_ptr(s->ram);
    uint8_t *d = surface_data(surface);
    uint32_t base = coco3_vdg_base(s);
    uint8_t ff22 = coco3_vdg_ff22(s);
    int stride = surface_stride(surface);
    int pal0 = (ff22 & VDG_FF22_CSS) ? 14 : 12;
    uint32_t bg = s->palette_rgb[pal0];
    uint32_t fg = s->palette_rgb[pal0 + 1];
    int x0 = (COCO3_DISPLAY_WIDTH - VDG_BODY_WIDTH) / 2;
    int y0 = (COCO3_DISPLAY_HEIGHT - VDG_HEIGHT) / 2;
    int y, col, bit, k;

    coco3_fill_surface(surface, coco3_vdg_border_pixel(s));
    d += (size_t)y0 * stride + (size_t)x0 * 4;

    for (y = 0; y < VDG_HEIGHT; y++) {
        uint32_t *p = (uint32_t *)d;
        int line = y % VDG_CELL_H;
        uint32_t row = base + (uint32_t)(y / VDG_CELL_H) * VDG_TEXT_COLS;
        int px = 0;

        for (col = 0; col < VDG_TEXT_COLS; col++) {
            uint8_t ch = ram[(row + col) & (COCO3_RAM_SIZE - 1)];
            uint32_t on, off;
            uint8_t bits;

            if (ch & 0x80) {
                /* SG4: $80–$FF. Bits 3–0 are UL/UR/LL/LR in an 8×12
                 * cell; bits 6–4 select palette 0–7. Off is palette 8. */
                uint8_t nib = (line < 6) ? (ch >> 2) : ch;

                bits = ((nib & 2) ? 0xf0 : 0) | ((nib & 1) ? 0x0f : 0);
                on = s->palette_rgb[(ch >> 4) & 7];
                off = s->palette_rgb[8];
            } else {
                bits = coco3_vdg_glyph_row(ch, line);
                /* Codes $40–$7F are inverse alphanumeric. */
                if ((ch & 0xc0) == 0x40) {
                    on = bg;
                    off = fg;
                } else {
                    on = fg;
                    off = bg;
                }
            }

            for (bit = 7; bit >= 0; bit--) {
                uint32_t pix = (bits & (1u << bit)) ? on : off;
                for (k = 0; k < VDG_XSCALE; k++) {
                    p[px++] = pix;
                }
            }
        }
        d += stride;
    }
}

static void coco3_draw_pmode4(Coco3State *s, DisplaySurface *surface)
{
    uint8_t *ram = memory_region_get_ram_ptr(s->ram);
    uint8_t *d = surface_data(surface);
    uint32_t base = coco3_vdg_base(s);
    uint8_t ff22 = coco3_vdg_ff22(s);
    int stride = surface_stride(surface);
    int pal0 = (ff22 & VDG_FF22_CSS) ? 10 : 8;
    uint32_t c0 = s->palette_rgb[pal0];
    uint32_t c1 = s->palette_rgb[pal0 + 1];
    int x0 = (COCO3_DISPLAY_WIDTH - VDG_BODY_WIDTH) / 2;
    int y0 = (COCO3_DISPLAY_HEIGHT - VDG_HEIGHT) / 2;
    int x, y, k;

    coco3_fill_surface(surface, coco3_vdg_border_pixel(s));
    d += (size_t)y0 * stride + (size_t)x0 * 4;

    for (y = 0; y < VDG_HEIGHT; y++) {
        uint32_t *p = (uint32_t *)d;
        uint32_t row = base + (uint32_t)y * VDG_PMODE4_BPL;
        int px = 0;

        for (x = 0; x < VDG_PMODE4_BPL; x++) {
            uint8_t b = ram[(row + x) & (COCO3_RAM_SIZE - 1)];
            int bit;

            for (bit = 7; bit >= 0; bit--) {
                uint32_t pix = (b & (1u << bit)) ? c1 : c0;

                for (k = 0; k < VDG_XSCALE; k++) {
                    p[px++] = pix;
                }
            }
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
    bool gfx = coco3_video_is_gime_gfx(s);
    int cols = 0;
    bool text = coco3_video_is_gime_text(s, &cols);
    bool vdg_text = coco3_video_is_vdg_text(s);
    bool pmode4 = coco3_video_is_pmode4(s);

    if (!gfx && !text && !vdg_text && !pmode4 && !s->video_dirty) {
        return;
    }
    s->video_dirty = false;

    surface = qemu_console_surface(s->con);
    if (!surface || surface_bits_per_pixel(surface) != 32) {
        return;
    }

    if (gfx) {
        s->video_unimp_logged = false;
        coco3_draw_gime_gfx(s, surface);
    } else if (text) {
        s->video_unimp_logged = false;
        coco3_draw_gime_text(s, surface, cols);
    } else if (vdg_text) {
        s->video_unimp_logged = false;
        coco3_draw_vdg_text(s, surface);
    } else if (pmode4) {
        s->video_unimp_logged = false;
        coco3_draw_pmode4(s, surface);
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
    if (s->con) {
        graphic_hw_invalidate(s->con);
    }
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
