/*
 * Motorola MC6821 Peripheral Interface Adapter
 *
 * Unimplemented: Cx2 handshake outputs and strobe modes. Simple Cx2
 * output (control bits 5 and 4 set, pin follows bit 3) is supported.
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "mc6821.h"

/* Control register bits. Bits 6 and 7 are the read-only interrupt flags. */
#define CR_C1_ENABLE    0x01 /* interrupt on the selected Cx1 edge */
#define CR_C1_RISING    0x02 /* 0 = high-to-low edge, 1 = low-to-high */
#define CR_DDR_SELECT   0x04 /* 0 = DDR at the data address, 1 = data */
#define CR_C2_ENABLE    0x08 /* input: IRQ2 enable; simple output: pin level */
#define CR_C2_DIRECT    0x10 /* with OUTPUT: C2 follows CR_C2_ENABLE */
#define CR_C2_OUTPUT    0x20
#define CR_IRQ2         0x40
#define CR_IRQ1         0x80
#define CR_WRITE_MASK   0x3f

int mc6821_c2_level(const MC6821Port *port)
{
    if ((port->cr & (CR_C2_OUTPUT | CR_C2_DIRECT)) ==
        (CR_C2_OUTPUT | CR_C2_DIRECT)) {
        return !!(port->cr & CR_C2_ENABLE);
    }
    return 0;
}

/* A flag only reaches the interrupt output when its enable bit is also set. */
static bool mc6821_port_irq(const MC6821Port *port)
{
    return ((port->cr & CR_IRQ1) && (port->cr & CR_C1_ENABLE))
        || ((port->cr & CR_IRQ2) && (port->cr & CR_C2_ENABLE)
            && !(port->cr & CR_C2_OUTPUT));
}

static void mc6821_update_irq(MC6821State *s)
{
    qemu_set_irq(s->irq, mc6821_port_irq(&s->a) || mc6821_port_irq(&s->b));
}

/* Pins configured as outputs read back the output register, inputs the pins. */
static uint8_t mc6821_port_level(const MC6821Port *port)
{
    return (port->data & port->ddr) | (port->in & ~port->ddr);
}

static void mc6821_port_set_out(qemu_irq *out, const MC6821Port *port)
{
    uint8_t level = mc6821_port_level(port);
    int i;

    for (i = 0; i < 8; i++) {
        qemu_set_irq(out[i], (level >> i) & 1);
    }
}

static void mc6821_update_c2(MC6821State *s)
{
    qemu_set_irq(s->ca2, mc6821_c2_level(&s->a));
    qemu_set_irq(s->cb2, mc6821_c2_level(&s->b));
}

static void mc6821_update_out(MC6821State *s)
{
    mc6821_port_set_out(s->a_out, &s->a);
    mc6821_port_set_out(s->b_out, &s->b);
    mc6821_update_c2(s);
}

void mc6821_set_port_in(MC6821State *s, bool port_b, uint8_t value)
{
    MC6821Port *port = port_b ? &s->b : &s->a;

    port->in = value;
}

static void mc6821_pa_in(void *opaque, int n, int level)
{
    MC6821State *s = opaque;

    if (level) {
        s->a.in |= 1u << n;
    } else {
        s->a.in &= ~(1u << n);
    }
}

static void mc6821_pb_in(void *opaque, int n, int level)
{
    MC6821State *s = opaque;

    if (level) {
        s->b.in |= 1u << n;
    } else {
        s->b.in &= ~(1u << n);
    }
}

static uint64_t mc6821_read(void *opaque, hwaddr addr, unsigned size)
{
    MC6821State *s = opaque;
    MC6821Port *port = (addr & 2) ? &s->b : &s->a;

    if (addr & 1) {
        return port->cr;
    }

    if (!(port->cr & CR_DDR_SELECT)) {
        return port->ddr;
    }

    /* Reading the data register acknowledges both interrupts for this half. */
    port->cr &= ~(CR_IRQ1 | CR_IRQ2);
    mc6821_update_irq(s);
    return mc6821_port_level(port);
}

static void mc6821_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
{
    MC6821State *s = opaque;
    MC6821Port *port = (addr & 2) ? &s->b : &s->a;

    if (addr & 1) {
        uint8_t cr = val & CR_WRITE_MASK;

        /* The interrupt flags are set by hardware only, so preserve them. */
        port->cr = (port->cr & ~CR_WRITE_MASK) | cr;
        if ((cr & CR_C2_OUTPUT) && !(cr & CR_C2_DIRECT)) {
            qemu_log_mask(LOG_UNIMP, "mc6821: Cx2 handshake/strobe\n");
        }
        mc6821_update_irq(s);
        mc6821_update_c2(s);
        return;
    }

    if (port->cr & CR_DDR_SELECT) {
        port->data = val;
    } else {
        port->ddr = val;
    }
    mc6821_update_out(s);
}

static const MemoryRegionOps mc6821_ops = {
    .read = mc6821_read,
    .write = mc6821_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

/*
 * Cx1 is edge-sensitive. Latch IRQ1 on the selected edge; it stays set
 * until the guest reads the data register.
 */
static void mc6821_set_c1(MC6821State *s, MC6821Port *port, int level)
{
    if (!!level == port->c1) {
        return;
    }
    port->c1 = level;

    if (!!level == !!(port->cr & CR_C1_RISING)) {
        port->cr |= CR_IRQ1;
        mc6821_update_irq(s);
    }
}

static void mc6821_ca1(void *opaque, int n, int level)
{
    MC6821State *s = opaque;

    mc6821_set_c1(s, &s->a, level);
}

static void mc6821_cb1(void *opaque, int n, int level)
{
    MC6821State *s = opaque;

    mc6821_set_c1(s, &s->b, level);
}

static void mc6821_reset_hold(Object *obj, ResetType type)
{
    MC6821State *s = MC6821(obj);

    s->a = (MC6821Port){ };
    s->b = (MC6821Port){ };
    qemu_set_irq(s->irq, false);
    mc6821_update_out(s);
}

static void mc6821_init(Object *obj)
{
    MC6821State *s = MC6821(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &mc6821_ops, s, TYPE_MC6821,
                          MC6821_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);

    qdev_init_gpio_in_named(DEVICE(obj), mc6821_ca1, "CA1", 1);
    qdev_init_gpio_in_named(DEVICE(obj), mc6821_cb1, "CB1", 1);
    qdev_init_gpio_in_named(DEVICE(obj), mc6821_pa_in, MC6821_GPIO_PA_IN, 8);
    qdev_init_gpio_in_named(DEVICE(obj), mc6821_pb_in, MC6821_GPIO_PB_IN, 8);
    qdev_init_gpio_out_named(DEVICE(obj), s->a_out, MC6821_GPIO_PA, 8);
    qdev_init_gpio_out_named(DEVICE(obj), s->b_out, MC6821_GPIO_PB, 8);
    qdev_init_gpio_out_named(DEVICE(obj), &s->ca2, MC6821_GPIO_CA2, 1);
    qdev_init_gpio_out_named(DEVICE(obj), &s->cb2, MC6821_GPIO_CB2, 1);
}

static const VMStateDescription vmstate_mc6821_port = {
    .name = "mc6821-port",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(data, MC6821Port),
        VMSTATE_UINT8(ddr, MC6821Port),
        VMSTATE_UINT8(cr, MC6821Port),
        VMSTATE_UINT8(in, MC6821Port),
        VMSTATE_BOOL(c1, MC6821Port),
        VMSTATE_END_OF_LIST()
    }
};

static int mc6821_post_load(void *opaque, int version_id)
{
    mc6821_update_out(opaque);
    return 0;
}

static const VMStateDescription vmstate_mc6821 = {
    .name = TYPE_MC6821,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = mc6821_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(a, MC6821State, 1, vmstate_mc6821_port, MC6821Port),
        VMSTATE_STRUCT(b, MC6821State, 1, vmstate_mc6821_port, MC6821Port),
        VMSTATE_END_OF_LIST()
    }
};

static void mc6821_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "Motorola MC6821 PIA";
    dc->vmsd = &vmstate_mc6821;
    rc->phases.hold = mc6821_reset_hold;
}

static const TypeInfo mc6821_types[] = {
    {
        .name = TYPE_MC6821,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(MC6821State),
        .instance_init = mc6821_init,
        .class_init = mc6821_class_init,
    }
};

DEFINE_TYPES(mc6821_types)
