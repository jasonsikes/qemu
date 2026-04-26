/*
 * Tandy/Radio Shack Color Computer 3 machine
 *
 * Copyright (c) 2025 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "coco3.h"
#include "boot.h"
#include "qom/object.h"
#include "hw/core/boards.h"
#include "hw/core/sysbus.h"

struct Coco3MachineState {
    MachineState parent_obj;

    Coco3State sys;
    bool fake_cart_firq;
};
typedef struct Coco3MachineState Coco3MachineState;

#define TYPE_COCO3_MACHINE MACHINE_TYPE_NAME("coco3")
DECLARE_INSTANCE_CHECKER(Coco3MachineState, COCO3_MACHINE, TYPE_COCO3_MACHINE)

static void coco3_machine_init(MachineState *machine)
{
    Coco3MachineState *s = COCO3_MACHINE(machine);

    if (machine->ram_size != COCO3_RAM_SIZE) {
        error_report("coco3: RAM size must be 512 KiB");
        exit(EXIT_FAILURE);
    }

    object_initialize_child(OBJECT(machine), "sys", &s->sys,
                            TYPE_COCO3);
    object_property_set_link(OBJECT(&s->sys), "ram",
                             OBJECT(machine->ram), &error_abort);
    object_property_set_bool(OBJECT(&s->sys), "fake-cart-firq",
                             s->fake_cart_firq, &error_abort);
    /* REL needs MMUEN on at reset; set before realize. */
    if (machine->kernel_filename) {
        object_property_set_bool(OBJECT(&s->sys), "os9-kernel",
                                 true, &error_abort);
    }
    sysbus_realize(SYS_BUS_DEVICE(&s->sys), &error_abort);

    if (machine->firmware && machine->kernel_filename) {
        error_report("coco3: -bios and -kernel cannot be used together");
        exit(EXIT_FAILURE);
    }

    if (machine->firmware) {
        if (!m6809_load_firmware(&s->sys.rom, machine->firmware)) {
            exit(1);
        }
    }

    if (machine->kernel_filename) {
        hwaddr ram_off;

        ram_off = (hwaddr)GIME_RESET_BASE_BLOCK * GIME_PAGE_SIZE +
                  OS9_BOOTTRACK_ADDR;
        if (!m6809_load_boottrack(&s->sys.cpu, machine->ram, ram_off,
                                  machine->kernel_filename)) {
            exit(1);
        }
    }
}

static bool coco3_get_fake_cart_firq(Object *obj, Error **errp)
{
    return COCO3_MACHINE(obj)->fake_cart_firq;
}

static void coco3_set_fake_cart_firq(Object *obj, bool value, Error **errp)
{
    COCO3_MACHINE(obj)->fake_cart_firq = value;
}

static void coco3_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    object_class_property_add_bool(oc, "fake-cart-firq",
                                   coco3_get_fake_cart_firq,
                                   coco3_set_fake_cart_firq);
    object_class_property_set_description(oc, "fake-cart-firq",
                                          "Pulse PIA1 CB1 (cartridge FIRQ) once per frame");

    mc->desc = "Tandy/Radio Shack Color Computer 3";
    mc->init = coco3_machine_init;
    mc->default_cpus = 1;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->default_ram_size = COCO3_RAM_SIZE;
    mc->default_ram_id = "coco3.ram";
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
    /* First -drive (if=none, or default) is the virt disk at $FF30. */
    mc->block_default_type = IF_NONE;
}

static const TypeInfo coco3_machine_types[] = {
    {
        .name = TYPE_COCO3_MACHINE,
        .parent = TYPE_MACHINE,
        .instance_size = sizeof(Coco3MachineState),
        .class_init = coco3_machine_class_init,
    }
};

DEFINE_TYPES(coco3_machine_types)
