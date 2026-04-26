/*
 * CoCo 3 virt disk
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_VIRT_DISK_H
#define HW_M6809_VIRT_DISK_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_COCO3_VIRT_DISK "coco3-virt-disk"

typedef struct Coco3VirtDiskState Coco3VirtDiskState;
DECLARE_INSTANCE_CHECKER(Coco3VirtDiskState, COCO3_VIRT_DISK,
                         TYPE_COCO3_VIRT_DISK)

#define COCO3_VDISK_IO_SIZE 0x10

struct Coco3VirtDiskState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    BlockBackend *blk;
    uint8_t cmd;
    uint8_t stat;
    uint8_t drv;
    uint8_t lsn[3];
    uint8_t buf[2];
};

#endif /* HW_M6809_VIRT_DISK_H */
