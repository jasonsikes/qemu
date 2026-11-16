/*
 * Western Digital WD1773 floppy disk controller
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_WD1773_H
#define HW_M6809_WD1773_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_WD1773 "wd1773"

typedef struct WD1773State WD1773State;
DECLARE_INSTANCE_CHECKER(WD1773State, WD1773, TYPE_WD1773)

/* Command/Status, Track, Sector, Data; mirrored in an 8-byte window. */
#define WD1773_IO_SIZE      8
#define WD1773_SECSIZE      256
#define WD1773_SPT          18

/* sysbus-irq 0 = INTRQ, 1 = DRQ */
#define WD1773_IRQ_INTRQ    0
#define WD1773_IRQ_DRQ      1

typedef struct BlockBackend BlockBackend;

struct WD1773State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    BlockBackend *blk;
    qemu_irq irq_intrq;
    qemu_irq irq_drq;

    uint8_t track;
    uint8_t sector;
    uint8_t data;
    uint8_t cmd;
    uint8_t head_track;
    int8_t step_dir;

    bool busy;
    bool status_type_1;
    bool seek_err;
    bool rnf;
    bool crc_err;
    bool lost_data;
    bool record_type;
    bool wr_protect;
    bool intrq;
    bool drq;
    bool ready;                 /* READY pin, driven by the disk-cart latch */
    uint8_t side;               /* head, from the cart side-select */

    /* Data-register read after the sector transfer has finished. */
    void (*idle_read)(void *opaque);
    void *idle_opaque;

    uint8_t xfer[WD1773_SECSIZE];
    uint32_t xfer_i;
    uint32_t xfer_n;
    bool xfer_write;
};

void wd1773_set_ready(WD1773State *s, bool ready);
void wd1773_set_side(WD1773State *s, uint8_t side);
#endif /* HW_M6809_WD1773_H */
