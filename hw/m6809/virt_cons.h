/*
 * CoCo 3 virt console
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_VIRT_CONS_H
#define HW_M6809_VIRT_CONS_H

#include "hw/core/sysbus.h"
#include "chardev/char-fe.h"
#include "qom/object.h"

#define TYPE_COCO3_VIRT_CONSOLE "coco3-virt-console"

typedef struct Coco3VirtConsState Coco3VirtConsState;
DECLARE_INSTANCE_CHECKER(Coco3VirtConsState, COCO3_VIRT_CONSOLE,
                         TYPE_COCO3_VIRT_CONSOLE)

#define COCO3_VCONS_IO_SIZE 0x10

struct Coco3VirtConsState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    CharFrontend chr;
    uint8_t rx_data;
    bool rx_full;
};

#endif /* HW_M6809_VIRT_CONS_H */
