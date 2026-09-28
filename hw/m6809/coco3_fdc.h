/*
 * Tandy FD-502 Color Computer disk controller (WD1773 + latch)
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_COCO3_FDC_H
#define HW_M6809_COCO3_FDC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "wd1773.h"

#define TYPE_COCO3_FDC "coco3-fdc"

typedef struct Coco3FdcState Coco3FdcState;
DECLARE_INSTANCE_CHECKER(Coco3FdcState, COCO3_FDC, TYPE_COCO3_FDC)

/* $FF40 latch (8 bytes mirrored), $FF48 WD1773 (8 bytes mirrored). */
#define COCO3_FDC_IO_SIZE       0x10

/* sysbus-irq 0 = gated NMI, 1 = HALT */
#define COCO3_FDC_IRQ_NMI       0
#define COCO3_FDC_IRQ_HALT      1

#define FDC_LATCH_DS0           0x01
#define FDC_LATCH_DS1           0x02
#define FDC_LATCH_DS2           0x04
#define FDC_LATCH_MOTOR         0x08
#define FDC_LATCH_PRECOMP       0x10
#define FDC_LATCH_DENSITY       0x20 /* 1 = double; gates INTRQ onto NMI */
#define FDC_LATCH_DS3           0x40
#define FDC_LATCH_HALT          0x80

typedef struct BlockBackend BlockBackend;

struct Coco3FdcState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    WD1773State wd1773;
    MemoryRegion iomem;
    MemoryRegion latch_io;
    BlockBackend *blk;
    qemu_irq irq_nmi;
    qemu_irq irq_halt;
    uint8_t latch;
    bool intrq;
    bool drq;
    bool saw_drq;               /* this command transferred data (Type II) */
    bool nmi_defer;             /* hold CPU NMI until the next $FF40 write */
};

#endif /* HW_M6809_COCO3_FDC_H */
