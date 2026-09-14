/*
 * Color Computer 3 65C52 Microsoft mouse (joydrv_6552M).
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_COCO3_MOUSE_H
#define HW_M6809_COCO3_MOUSE_H

#include "coco3.h"

#define COCO3_MOUSE_BASE     0xff64
#define COCO3_MOUSE_SIZE     4
#define COCO3_MOUSE_RX_SIZE  16

void coco3_mouse_init(Coco3State *s);
void coco3_mouse_reset(Coco3State *s);

#endif /* HW_M6809_COCO3_MOUSE_H */
