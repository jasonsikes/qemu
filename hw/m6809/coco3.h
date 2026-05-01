/*
 * Color Computer 3 system: 6809, GIME MMU, 512 KiB RAM, and ROM.
 *
 * Copyright (c) 2025 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_COCO3_H
#define HW_M6809_COCO3_H

#include "target/m6809/cpu.h"
#include "qom/object.h"
#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "mc6821.h"
#include "virt_disk.h"
#include "virt_cons.h"

#define TYPE_COCO3 "coco3"

typedef struct Coco3State Coco3State;
DECLARE_INSTANCE_CHECKER(Coco3State, COCO3, TYPE_COCO3)

#define COCO3_RAM_SIZE          (512 * KiB)
#define GIME_PAGE_SIZE          0x2000
#define GIME_PAGE_COUNT         8
#define GIME_MMU_REGS           16
#define GIME_MMU_BLOCK_MASK     0x3f
#define GIME_RESET_BASE_BLOCK   56
#define GIME_ROM_BLOCK          0x3c
#define GIME_VEC_SIZE           0x20

#define GIME_INIT0_ROMSEL       0x03
#define GIME_INIT0_MC3          0x08
#define GIME_INIT0_FEN          0x10
#define GIME_INIT0_IEN          0x20
#define GIME_INIT0_MMUEN        0x40
#define GIME_INIT0_COCO         0x80 /* 1 = VDG/SAM video, 0 = GIME */
#define GIME_INIT1_TR           0x01
#define GIME_IRQ_VBORD          0x08

#define GIME_VMODE_LPR          0x07
#define GIME_VMODE_H50          0x08
#define GIME_VMODE_MOCH         0x10
#define GIME_VMODE_BPI          0x20
#define GIME_VMODE_BP           0x80 /* 1 = graphics, 0 = text */
#define GIME_VRES_CRES          0x03
#define GIME_VRES_HRES          0x1c
#define GIME_VRES_LPF           0x60
#define GIME_HOFF_HVEN          0x80
#define GIME_PALETTE_COUNT      16
#define GIME_COLOR_MASK         0x3f

/* Offsets within the $FF90 GIME window. */
#define GIME_R_INIT0            0x00
#define GIME_R_INIT1            0x01
#define GIME_R_IRQEN            0x02
#define GIME_R_FIREN            0x03
#define GIME_R_VMODE            0x08
#define GIME_R_VRES             0x09
#define GIME_R_BORDER           0x0a
#define GIME_R_VBANK            0x0b
#define GIME_R_VSCROLL          0x0c
#define GIME_R_VOFF_MSB         0x0d
#define GIME_R_VOFF_LSB         0x0e
#define GIME_R_HOFF             0x0f
#define GIME_R_MMU              0x10
#define GIME_R_PALETTE          0x20
#define GIME_R_SAM              0x30
#define GIME_R_SAM_TY           0x4e

struct Coco3State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    M6809CPU cpu;
    MemoryRegion *ram;
    MemoryRegion rom;

    MemoryRegion ram_page[GIME_PAGE_COUNT];
    MemoryRegion rom_page[GIME_PAGE_COUNT];
    MemoryRegion fexx;          /* $FE00-$FEFF; MC3 pins this to block $3F */
    MemoryRegion vectors;
    uint8_t vec[GIME_VEC_SIZE];
    bool vec_custom;
    MemoryRegion gime_io;

    /* PIA0/GIME IRQ ORed; PIA1/GIME FIRQ ORed. */
    MC6821State pia0;
    MC6821State pia1;
    qemu_irq cpu_irq;
    qemu_irq cpu_firq;
    qemu_irq *irq_src;
    qemu_irq *firq_src;
    uint8_t irq_level[2];
    uint8_t firq_level[2];
    Coco3VirtDiskState vdisk;
    Coco3VirtConsState vcons;

    uint8_t init0;
    uint8_t init1;
    uint8_t gime_irqen;
    uint8_t gime_firen;
    uint8_t gime_pending;
    uint8_t mmu[GIME_MMU_REGS];
    bool sam_ty;                /* SAM TY: RAM at $3C-$3F */
    bool os9_kernel;            /* -kernel: keep INIT0.MMUEN across reset */

    QEMUTimer *frame_timer;
    qemu_irq cart;
    bool fake_cart_firq;

    QemuConsole *con;
    bool video_dirty;
    bool video_unimp_logged;

    uint8_t vmode;
    uint8_t vres;
    uint8_t border;
    uint8_t vbank;
    uint8_t vscroll;
    uint8_t voff_msb;
    uint8_t voff_lsb;
    uint8_t hoff;
    uint8_t palette[GIME_PALETTE_COUNT];
    uint32_t palette_rgb[GIME_PALETTE_COUNT];
};

#endif /* HW_M6809_COCO3_H */
