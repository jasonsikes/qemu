/*
 * 65C52 at $FF64 plus a Microsoft serial mouse (NitrOS-9 joydrv_6552M).
 *
 * Registers (l52.defs): IRQ status/enable, control/format, unused, data.
 * CART IRQ is GIME $FF92 bit 0. Packets are 7-bit, three bytes, sync bit 6.
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "system/memory.h"
#include "ui/console.h"
#include "ui/input.h"
#include "coco3_mouse.h"

#define MOS65C52_ISREG      0
#define MOS65C52_CFREG      1
#define MOS65C52_TBREG      2
#define MOS65C52_DATA       3

#define MOS65C52_ISE_RXF    0x01
#define MOS65C52_ISE_PAR    0x02
#define MOS65C52_ISE_FOB    0x04
#define MOS65C52_ISE_MASK   0x07
#define MOS65C52_ISE_IRQ    0x80

#define MOS65C52_CS_TXE     0x40

#define MSMOUSE_LO6(n)      ((n) & 0x3f)
#define MSMOUSE_HI2(n)      (((n) & 0xc0) >> 6)

static uint8_t coco3_mouse_isr(const Coco3State *s)
{
    uint8_t isr = 0;

    if (s->mouse_rx_n) {
        isr |= MOS65C52_ISE_RXF;
    }
    if (isr & s->mouse_iereg & MOS65C52_ISE_MASK) {
        isr |= MOS65C52_ISE_IRQ;
    }
    return isr;
}

static void coco3_mouse_update_irq(Coco3State *s)
{
    bool irq = (s->mouse_iereg & MOS65C52_ISE_IRQ) &&
               (coco3_mouse_isr(s) & MOS65C52_ISE_IRQ);

    if (irq && !s->mouse_irq) {
        coco3_gime_cart_raise(s);
    }
    s->mouse_irq = irq;
}

static void coco3_mouse_push(Coco3State *s, uint8_t b)
{
    unsigned w;

    if (s->mouse_rx_n == COCO3_MOUSE_RX_SIZE) {
        return;
    }
    w = (s->mouse_rx_r + s->mouse_rx_n) % COCO3_MOUSE_RX_SIZE;
    s->mouse_rx[w] = b & 0x7f;
    s->mouse_rx_n++;
    coco3_mouse_update_irq(s);
}

static uint8_t coco3_mouse_pop(Coco3State *s)
{
    uint8_t b = 0;

    if (s->mouse_rx_n) {
        b = s->mouse_rx[s->mouse_rx_r];
        s->mouse_rx_r = (s->mouse_rx_r + 1) % COCO3_MOUSE_RX_SIZE;
        s->mouse_rx_n--;
    }
    coco3_mouse_update_irq(s);
    if (s->mouse_irq) {
        coco3_gime_cart_raise(s);
    }
    return b;
}

static void coco3_mouse_queue_packet(Coco3State *s)
{
    int dx = s->mouse_dx;
    int dy = s->mouse_dy;
    uint8_t pkt[3];

    s->mouse_dx = 0;
    s->mouse_dy = 0;
    if (dx > 127) {
        dx = 127;
    } else if (dx < -128) {
        dx = -128;
    }
    if (dy > 127) {
        dy = 127;
    } else if (dy < -128) {
        dy = -128;
    }

    pkt[0] = 0x40 | (MSMOUSE_HI2(dy) << 2) | MSMOUSE_HI2(dx);
    if (s->mouse_left) {
        pkt[0] |= 0x20;
    }
    if (s->mouse_right) {
        pkt[0] |= 0x10;
    }
    pkt[1] = MSMOUSE_LO6(dx);
    pkt[2] = MSMOUSE_LO6(dy);
    coco3_mouse_push(s, pkt[0]);
    coco3_mouse_push(s, pkt[1]);
    coco3_mouse_push(s, pkt[2]);
}

static void coco3_mouse_event(DeviceState *dev, QemuConsole *src G_GNUC_UNUSED,
                              InputEvent *evt)
{
    Coco3State *s = COCO3(dev);
    InputMoveEvent *move;
    InputBtnEvent *btn;

    switch (evt->type) {
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        if (move->axis == INPUT_AXIS_X) {
            s->mouse_dx += move->value;
        } else if (move->axis == INPUT_AXIS_Y) {
            s->mouse_dy += move->value;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        if (btn->button == INPUT_BUTTON_LEFT) {
            s->mouse_left = btn->down;
        } else if (btn->button == INPUT_BUTTON_RIGHT) {
            s->mouse_right = btn->down;
        } else {
            return;
        }
        s->mouse_btn_sync = true;
        coco3_joy_ms_buttons(s, s->mouse_left, s->mouse_right);
        break;
    default:
        return;
    }
}

static void coco3_mouse_sync(DeviceState *dev)
{
    Coco3State *s = COCO3(dev);

    if (s->mouse_dx || s->mouse_dy || s->mouse_btn_sync) {
        s->mouse_btn_sync = false;
        coco3_mouse_queue_packet(s);
    }
}

static const QemuInputHandler coco3_mouse_handler = {
    .name = "coco3-msmouse",
    .mask = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_REL,
    .event = coco3_mouse_event,
    .sync = coco3_mouse_sync,
};

static uint64_t coco3_mouse_read(void *opaque, hwaddr addr, unsigned size)
{
    Coco3State *s = opaque;

    switch (addr) {
    case MOS65C52_ISREG:
        return coco3_mouse_isr(s);
    case MOS65C52_CFREG:
        return MOS65C52_CS_TXE;
    case MOS65C52_DATA:
        return coco3_mouse_pop(s);
    default:
        return 0;
    }
}

static void coco3_mouse_write(void *opaque, hwaddr addr, uint64_t val,
                              unsigned size)
{
    Coco3State *s = opaque;

    switch (addr) {
    case MOS65C52_ISREG:
        s->mouse_iereg = val;
        coco3_mouse_update_irq(s);
        break;
    case MOS65C52_CFREG:
        s->mouse_cfreg = val;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps coco3_mouse_ops = {
    .read = coco3_mouse_read,
    .write = coco3_mouse_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

void coco3_mouse_reset(Coco3State *s)
{
    s->mouse_iereg = 0;
    s->mouse_cfreg = 0;
    s->mouse_rx_r = 0;
    s->mouse_rx_n = 0;
    s->mouse_dx = 0;
    s->mouse_dy = 0;
    s->mouse_left = false;
    s->mouse_right = false;
    s->mouse_btn_sync = false;
    s->mouse_irq = false;
}

void coco3_mouse_init(Coco3State *s)
{
    memory_region_init_io(&s->mouse_io, OBJECT(s), &coco3_mouse_ops, s,
                          "coco3.mouse", COCO3_MOUSE_SIZE);
    s->mouse_hs = qemu_input_handler_register(DEVICE(s), &coco3_mouse_handler);
    qemu_input_handler_activate(s->mouse_hs);
    coco3_mouse_reset(s);
}
