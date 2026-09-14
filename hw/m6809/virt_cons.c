/*
 * CoCo 3 virt console
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "chardev/char-fe.h"
#include "qom/object.h"
#include "virt_cons.h"

#define VCONS_VER       1

#define VCONS_R_MAGIC   0
#define VCONS_R_VER     1
#define VCONS_R_DATA    2
#define VCONS_R_STAT    3

#define VCONS_STAT_RX   0x01

static int coco3_virt_cons_can_receive(void *opaque)
{
    Coco3VirtConsState *s = opaque;

    return !s->rx_full;
}

static void coco3_virt_cons_receive(void *opaque, const uint8_t *buf, int size)
{
    Coco3VirtConsState *s = opaque;
    uint8_t ch;

    if (size < 1 || s->rx_full) {
        return;
    }
    ch = buf[0];
    if (ch == '\n') {
        ch = '\r';
    }
    s->rx_data = ch;
    s->rx_full = true;
}

static uint64_t coco3_virt_cons_read(void *opaque, hwaddr addr, unsigned size)
{
    Coco3VirtConsState *s = opaque;
    uint8_t data;

    switch (addr) {
    case VCONS_R_MAGIC:
        return 'Q';
    case VCONS_R_VER:
        return VCONS_VER;
    case VCONS_R_DATA:
        if (!s->rx_full) {
            return 0;
        }
        data = s->rx_data;
        s->rx_full = false;
        qemu_chr_fe_accept_input(&s->chr);
        return data;
    case VCONS_R_STAT:
        return s->rx_full ? VCONS_STAT_RX : 0;
    default:
        return 0;
    }
}

static void coco3_virt_cons_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    Coco3VirtConsState *s = opaque;
    uint8_t ch = val;

    if (addr != VCONS_R_DATA) {
        return;
    }
    qemu_chr_fe_write_all(&s->chr, &ch, 1);
}

static const MemoryRegionOps coco3_virt_cons_ops = {
    .read = coco3_virt_cons_read,
    .write = coco3_virt_cons_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void coco3_virt_cons_reset_hold(Object *obj, ResetType type)
{
    Coco3VirtConsState *s = COCO3_VIRT_CONSOLE(obj);

    s->rx_data = 0;
    s->rx_full = false;
}

static void coco3_virt_cons_realize(DeviceState *dev, Error **errp)
{
    Coco3VirtConsState *s = COCO3_VIRT_CONSOLE(dev);

    qemu_chr_fe_set_handlers(&s->chr, coco3_virt_cons_can_receive,
                             coco3_virt_cons_receive, NULL, NULL, s, NULL,
                             true);
}

static void coco3_virt_cons_init(Object *obj)
{
    Coco3VirtConsState *s = COCO3_VIRT_CONSOLE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &coco3_virt_cons_ops, s,
                          TYPE_COCO3_VIRT_CONSOLE, COCO3_VCONS_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_coco3_virt_cons = {
    .name = TYPE_COCO3_VIRT_CONSOLE,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(rx_data, Coco3VirtConsState),
        VMSTATE_BOOL(rx_full, Coco3VirtConsState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property coco3_virt_cons_properties[] = {
    DEFINE_PROP_CHR("chardev", Coco3VirtConsState, chr),
};

static void coco3_virt_cons_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "CoCo 3 virt console";
    dc->realize = coco3_virt_cons_realize;
    dc->user_creatable = false;
    dc->vmsd = &vmstate_coco3_virt_cons;
    device_class_set_props(dc, coco3_virt_cons_properties);
    rc->phases.hold = coco3_virt_cons_reset_hold;
}

static const TypeInfo coco3_virt_cons_types[] = {
    {
        .name = TYPE_COCO3_VIRT_CONSOLE,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Coco3VirtConsState),
        .instance_init = coco3_virt_cons_init,
        .class_init = coco3_virt_cons_class_init,
    }
};

DEFINE_TYPES(coco3_virt_cons_types)
