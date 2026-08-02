/*
 * Color Computer 3 cartridge slot and ROM cartridge
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_COCO3_CART_H
#define HW_M6809_COCO3_CART_H

#include "hw/core/qdev.h"
#include "qom/object.h"
#include "system/memory.h"
#include "qemu/units.h"

#define TYPE_COCO3_CART_BUS "coco3-cart-bus"
#define TYPE_COCO3_CART     "coco3-cart"

typedef struct Coco3CartBus Coco3CartBus;
typedef struct Coco3CartState Coco3CartState;
DECLARE_INSTANCE_CHECKER(Coco3CartBus, COCO3_CART_BUS, TYPE_COCO3_CART_BUS)
DECLARE_INSTANCE_CHECKER(Coco3CartState, COCO3_CART, TYPE_COCO3_CART)

#define COCO3_CART_ROM_SIZE  (16 * KiB)

struct Coco3CartBus {
    /*< private >*/
    BusState parent_obj;

    /*< public >*/
    MemoryRegion mr;            /* 16K window GIME aliases into */
    bool present;
};

struct Coco3CartState {
    /*< private >*/
    DeviceState parent_obj;

    /*< public >*/
    char *romfile;
    MemoryRegion rom;
    bool mapped;
};

#endif /* HW_M6809_COCO3_CART_H */
