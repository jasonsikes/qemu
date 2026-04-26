/*
 * Motorola 6809 indexed-addressing postbyte decoder
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#ifndef TARGET_M6809_INDEXED_H
#define TARGET_M6809_INDEXED_H

#include "qemu/bitops.h"

typedef enum M6809IndexedKind {
    M6809_IDX_OFFSET5,
    M6809_IDX_POSTINC1,
    M6809_IDX_POSTINC2,
    M6809_IDX_PREDEC1,
    M6809_IDX_PREDEC2,
    M6809_IDX_ZERO,
    M6809_IDX_B,
    M6809_IDX_A,
    M6809_IDX_OFFSET8,
    M6809_IDX_OFFSET16,
    M6809_IDX_D,
    M6809_IDX_PCR8,
    M6809_IDX_PCR16,
    M6809_IDX_EXTENDED_INDIRECT,
} M6809IndexedKind;

typedef struct M6809IndexedMode {
    M6809IndexedKind kind;
    int reg;
    int offset;
    bool indirect;
} M6809IndexedMode;

static inline bool m6809_decode_indexed(uint8_t post,
                                        M6809IndexedMode *mode)
{
    int submode;

    mode->reg = extract32(post, 5, 2);
    mode->indirect = false;
    mode->offset = 0;

    if (!(post & 0x80)) {
        mode->kind = M6809_IDX_OFFSET5;
        mode->offset = sextract32(post, 0, 5);
        return true;
    }

    mode->indirect = post & 0x10;
    submode = post & 0xf;
    switch (submode) {
    case 0x0:
        if (mode->indirect) {
            return false;
        }
        mode->kind = M6809_IDX_POSTINC1;
        return true;
    case 0x1:
        mode->kind = M6809_IDX_POSTINC2;
        return true;
    case 0x2:
        if (mode->indirect) {
            return false;
        }
        mode->kind = M6809_IDX_PREDEC1;
        return true;
    case 0x3:
        mode->kind = M6809_IDX_PREDEC2;
        return true;
    case 0x4:
        mode->kind = M6809_IDX_ZERO;
        return true;
    case 0x5:
        mode->kind = M6809_IDX_B;
        return true;
    case 0x6:
        mode->kind = M6809_IDX_A;
        return true;
    case 0x8:
        mode->kind = M6809_IDX_OFFSET8;
        return true;
    case 0x9:
        mode->kind = M6809_IDX_OFFSET16;
        return true;
    case 0xb:
        mode->kind = M6809_IDX_D;
        return true;
    case 0xc:
        mode->kind = M6809_IDX_PCR8;
        return true;
    case 0xd:
        mode->kind = M6809_IDX_PCR16;
        return true;
    case 0xf:
        if (post != 0x9f) {
            return false;
        }
        mode->kind = M6809_IDX_EXTENDED_INDIRECT;
        mode->indirect = true;
        return true;
    default:
        return false;
    }
}

#endif /* TARGET_M6809_INDEXED_H */
