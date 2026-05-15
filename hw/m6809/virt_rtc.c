/*
 * CoCo 3 virt RTC (guest ABI: os9/qemu.d). Host time via qemu_get_timedate().
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "system/memory.h"
#include "system/rtc.h"
#include "virt_rtc.h"

#define VRTC_VER        1

#define VRTC_R_MAGIC    0
#define VRTC_R_VER      1
#define VRTC_R_YEAR     2
#define VRTC_R_SEC      7

static void coco3_virt_rtc_latch(Coco3VirtRtcState *s)
{
    struct tm tm;
    int year;

    qemu_get_timedate(&tm, 0);
    year = tm.tm_year;
    if (year < 0) {
        year = 0;
    } else if (year > 255) {
        year = 255;
    }
    s->packet[0] = year;
    s->packet[1] = tm.tm_mon + 1;
    s->packet[2] = tm.tm_mday;
    s->packet[3] = tm.tm_hour;
    s->packet[4] = tm.tm_min;
    s->packet[5] = tm.tm_sec;
    s->latched = true;
}

static uint64_t coco3_virt_rtc_read(void *opaque, hwaddr addr, unsigned size)
{
    Coco3VirtRtcState *s = opaque;

    switch (addr) {
    case VRTC_R_MAGIC:
        coco3_virt_rtc_latch(s);
        return 'Q';
    case VRTC_R_VER:
        if (!s->latched) {
            coco3_virt_rtc_latch(s);
        }
        return VRTC_VER;
    default:
        if (addr >= VRTC_R_YEAR && addr <= VRTC_R_SEC) {
            if (!s->latched) {
                coco3_virt_rtc_latch(s);
            }
            return s->packet[addr - VRTC_R_YEAR];
        }
        return 0;
    }
}

static void coco3_virt_rtc_write(void *opaque G_GNUC_UNUSED,
                                 hwaddr addr G_GNUC_UNUSED,
                                 uint64_t val G_GNUC_UNUSED,
                                 unsigned size G_GNUC_UNUSED)
{
}

static const MemoryRegionOps coco3_virt_rtc_ops = {
    .read = coco3_virt_rtc_read,
    .write = coco3_virt_rtc_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void coco3_virt_rtc_reset_hold(Object *obj, ResetType type)
{
    Coco3VirtRtcState *s = COCO3_VIRT_RTC(obj);

    memset(s->packet, 0, sizeof(s->packet));
    s->latched = false;
}

static void coco3_virt_rtc_init(Object *obj)
{
    Coco3VirtRtcState *s = COCO3_VIRT_RTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &coco3_virt_rtc_ops, s,
                          TYPE_COCO3_VIRT_RTC, COCO3_VRTC_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void coco3_virt_rtc_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "CoCo 3 virt RTC";
    dc->user_creatable = false;
    rc->phases.hold = coco3_virt_rtc_reset_hold;
}

static const TypeInfo coco3_virt_rtc_types[] = {
    {
        .name = TYPE_COCO3_VIRT_RTC,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Coco3VirtRtcState),
        .instance_init = coco3_virt_rtc_init,
        .class_init = coco3_virt_rtc_class_init,
    }
};

DEFINE_TYPES(coco3_virt_rtc_types)
