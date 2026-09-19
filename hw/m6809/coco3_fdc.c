/*
 * Tandy FD-502 Color Computer disk controller
 *
 * $FF40 is the write latch (drive select, motor, density, HALT enable).
 * $FF48 is the WD1773. Density gates INTRQ onto CPU NMI. INTRQ also
 * clears the HALT-enable bit. The 6809 HALT pin is not modeled; DRQ
 * stays asserted for the whole sector so Disk BASIC's LDA $FF4B loop
 * can run without stalling.
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
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "coco3_fdc.h"

static void coco3_fdc_update_ready(Coco3FdcState *s)
{
    bool sel0 = s->latch & FDC_LATCH_DS0;
    bool motor = s->latch & FDC_LATCH_MOTOR;

    wd1773_set_ready(&s->wd1773, sel0 && motor);
}

static void coco3_fdc_update_irqs(Coco3FdcState *s)
{
    bool nmi = s->intrq && (s->latch & FDC_LATCH_DENSITY);
    bool halt = (s->latch & FDC_LATCH_HALT) && !s->drq && !s->intrq;

    qemu_set_irq(s->irq_nmi, nmi);
    qemu_set_irq(s->irq_halt, halt);
}

static void coco3_fdc_intrq(void *opaque, int n, int level)
{
    Coco3FdcState *s = opaque;

    if (level) {
        s->latch &= ~FDC_LATCH_HALT;
    }
    s->intrq = level;
    coco3_fdc_update_irqs(s);
}

static void coco3_fdc_drq(void *opaque, int n, int level)
{
    Coco3FdcState *s = opaque;

    s->drq = level;
    coco3_fdc_update_irqs(s);
}

static uint64_t coco3_fdc_latch_read(void *opaque, hwaddr addr, unsigned size)
{
    Coco3FdcState *s = opaque;

    return s->latch;
}

static void coco3_fdc_latch_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    Coco3FdcState *s = opaque;

    s->latch = val;
    coco3_fdc_update_ready(s);
    coco3_fdc_update_irqs(s);
}

static const MemoryRegionOps coco3_fdc_latch_ops = {
    .read = coco3_fdc_latch_read,
    .write = coco3_fdc_latch_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void coco3_fdc_reset_hold(Object *obj, ResetType type)
{
    Coco3FdcState *s = COCO3_FDC(obj);

    s->latch = 0;
    s->intrq = false;
    s->drq = false;
    coco3_fdc_update_ready(s);
    coco3_fdc_update_irqs(s);
}

static void coco3_fdc_realize(DeviceState *dev, Error **errp)
{
    Coco3FdcState *s = COCO3_FDC(dev);

    if (s->blk) {
        /* Same backend; already attached to this device by the drive prop. */
        s->wd1773.blk = s->blk;
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->wd1773), errp)) {
        return;
    }
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->wd1773), WD1773_IRQ_INTRQ,
                       qdev_get_gpio_in_named(dev, "intrq", 0));
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->wd1773), WD1773_IRQ_DRQ,
                       qdev_get_gpio_in_named(dev, "drq", 0));
    coco3_fdc_update_ready(s);
}

static void coco3_fdc_init(Object *obj)
{
    Coco3FdcState *s = COCO3_FDC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    object_initialize_child(obj, "wd1773", &s->wd1773, TYPE_WD1773);

    memory_region_init(&s->iomem, obj, TYPE_COCO3_FDC, COCO3_FDC_IO_SIZE);
    memory_region_init_io(&s->latch_io, obj, &coco3_fdc_latch_ops, s,
                          "coco3-fdc-latch", 8);
    memory_region_add_subregion(&s->iomem, 0, &s->latch_io);
    memory_region_add_subregion(&s->iomem, 8, &s->wd1773.iomem);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_nmi);
    sysbus_init_irq(sbd, &s->irq_halt);

    qdev_init_gpio_in_named(DEVICE(obj), coco3_fdc_intrq, "intrq", 1);
    qdev_init_gpio_in_named(DEVICE(obj), coco3_fdc_drq, "drq", 1);
}

static int coco3_fdc_post_load(void *opaque, int version_id)
{
    Coco3FdcState *s = opaque;

    coco3_fdc_update_ready(s);
    coco3_fdc_update_irqs(s);
    return 0;
}

static const VMStateDescription vmstate_coco3_fdc = {
    .name = TYPE_COCO3_FDC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = coco3_fdc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(latch, Coco3FdcState),
        VMSTATE_BOOL(intrq, Coco3FdcState),
        VMSTATE_BOOL(drq, Coco3FdcState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property coco3_fdc_properties[] = {
    DEFINE_PROP_DRIVE("drive", Coco3FdcState, blk),
};

static void coco3_fdc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "CoCo FD-502 floppy disk controller";
    dc->realize = coco3_fdc_realize;
    dc->user_creatable = false;
    dc->vmsd = &vmstate_coco3_fdc;
    device_class_set_props(dc, coco3_fdc_properties);
    rc->phases.hold = coco3_fdc_reset_hold;
}

static const TypeInfo coco3_fdc_types[] = {
    {
        .name = TYPE_COCO3_FDC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Coco3FdcState),
        .instance_init = coco3_fdc_init,
        .class_init = coco3_fdc_class_init,
    }
};

DEFINE_TYPES(coco3_fdc_types)
