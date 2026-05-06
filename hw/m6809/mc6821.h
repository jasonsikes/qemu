/*
 * Motorola MC6821 Peripheral Interface Adapter
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_MC6821_H
#define HW_M6809_MC6821_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_MC6821 "mc6821"

typedef struct MC6821State MC6821State;
DECLARE_INSTANCE_CHECKER(MC6821State, MC6821, TYPE_MC6821)

/* Four registers, mirrored through a 16-byte board window. */
#define MC6821_IO_SIZE 0x10

#define MC6821_GPIO_PA      "PA"
#define MC6821_GPIO_PB      "PB"
#define MC6821_GPIO_PA_IN   "PA-in"
#define MC6821_GPIO_PB_IN   "PB-in"

typedef struct MC6821Port {
    uint8_t data;
    uint8_t ddr;
    uint8_t cr;
    uint8_t in;
    bool c1;
} MC6821Port;

struct MC6821State {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    qemu_irq irq;
    MC6821Port a;
    MC6821Port b;
    qemu_irq a_out[8];
    qemu_irq b_out[8];
};

void mc6821_set_port_in(MC6821State *s, bool port_b, uint8_t value);

#endif /* HW_M6809_MC6821_H */
