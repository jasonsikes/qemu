/*
 * Western Digital WD1773 floppy disk controller
 *
 * Instant command completion except for Type II, which presents a
 * 256-byte sector through the Data register with DRQ. INTRQ stays
 * high until Status is read. Not a cycle-accurate MFM model.
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
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "system/block-backend.h"
#include "wd1773.h"

#define WD1773_R_CMD        0
#define WD1773_R_TRACK      1
#define WD1773_R_SECTOR     2
#define WD1773_R_DATA       3

#define WD1773_ST_BUSY      0x01
#define WD1773_ST_INDEX     0x02
#define WD1773_ST_DRQ       0x02
#define WD1773_ST_TR00      0x04
#define WD1773_ST_LOST      0x04
#define WD1773_ST_CRC       0x08
#define WD1773_ST_SEEKERR   0x10
#define WD1773_ST_RNF       0x10
#define WD1773_ST_RECTYPE   0x20
#define WD1773_ST_WP        0x40
#define WD1773_ST_NRDY      0x80

#define WD1773_FI_IMM       0x08

static bool wd1773_ready(const WD1773State *s)
{
    return s->blk != NULL && s->ready;
}

void wd1773_set_ready(WD1773State *s, bool ready)
{
    s->ready = ready;
}

static void wd1773_set_intrq(WD1773State *s, bool level)
{
    s->intrq = level;
    qemu_set_irq(s->irq_intrq, level);
}

static void wd1773_set_drq(WD1773State *s, bool level)
{
    s->drq = level;
    qemu_set_irq(s->irq_drq, level);
}

static void wd1773_complete(WD1773State *s, bool type1)
{
    s->busy = false;
    s->xfer_n = 0;
    s->status_type_1 = type1;
    wd1773_set_drq(s, false);
    wd1773_set_intrq(s, true);
}

static unsigned wd1773_sides(const WD1773State *s)
{
    int64_t len;

    if (!s->blk) {
        return 1;
    }
    len = blk_getlength(s->blk);
    if (len >= (int64_t)WD1773_SPT * WD1773_SECSIZE * 35 * 2) {
        return 2;
    }
    return 1;
}

static bool wd1773_offset(const WD1773State *s, unsigned side, int64_t *off)
{
    unsigned sides = wd1773_sides(s);
    int64_t len, o;

    if (!s->blk || side >= sides || s->sector == 0 || s->sector > WD1773_SPT) {
        return false;
    }
    len = blk_getlength(s->blk);
    if (len < 0) {
        return false;
    }
    o = ((int64_t)s->track * sides + side) * WD1773_SPT * WD1773_SECSIZE +
        (int64_t)(s->sector - 1) * WD1773_SECSIZE;
    if (o < 0 || o > len - WD1773_SECSIZE) {
        return false;
    }
    *off = o;
    return true;
}

static void wd1773_not_ready(WD1773State *s, bool type1)
{
    s->rnf = false;
    wd1773_complete(s, type1);
}

static void wd1773_restore(WD1773State *s)
{
    if (!wd1773_ready(s)) {
        wd1773_not_ready(s, true);
        return;
    }
    s->head_track = 0;
    s->track = 0;
    s->step_dir = -1;
    wd1773_complete(s, true);
}

static void wd1773_seek(WD1773State *s)
{
    if (!wd1773_ready(s)) {
        wd1773_not_ready(s, true);
        return;
    }
    s->step_dir = (s->data > s->head_track) ? 1 : -1;
    s->head_track = s->data;
    s->track = s->data;
    wd1773_complete(s, true);
}

static void wd1773_step(WD1773State *s, int dir, bool update)
{
    if (!wd1773_ready(s)) {
        wd1773_not_ready(s, true);
        return;
    }
    if (dir) {
        s->step_dir = dir;
    }
    if (s->step_dir > 0) {
        if (s->head_track < 255) {
            s->head_track++;
        }
        if (update && s->track < 255) {
            s->track++;
        }
    } else {
        if (s->head_track) {
            s->head_track--;
        }
        if (update && s->track) {
            s->track--;
        }
    }
    wd1773_complete(s, true);
}

static void wd1773_read_sector(WD1773State *s)
{
    int64_t off;

    if (!wd1773_ready(s)) {
        wd1773_not_ready(s, false);
        return;
    }
    if (!wd1773_offset(s, s->side, &off)) {
        s->rnf = true;
        wd1773_complete(s, false);
        return;
    }
    if (blk_pread(s->blk, off, WD1773_SECSIZE, s->xfer, 0) < 0) {
        s->rnf = true;
        wd1773_complete(s, false);
        return;
    }
    s->xfer_i = 0;
    s->xfer_n = WD1773_SECSIZE;
    s->xfer_write = false;
    s->data = s->xfer[0];
    s->status_type_1 = false;
    wd1773_set_drq(s, true);
}

void wd1773_set_side(WD1773State *s, uint8_t side)
{
    uint8_t next = side ? 1 : 0;

    if (s->side == next) {
        return;
    }
    s->side = next;
    /* Head select is the cart latch. Apply it before any byte is taken. */
    if (s->busy && !s->xfer_write && s->xfer_i == 0 &&
        s->xfer_n == WD1773_SECSIZE) {
        wd1773_read_sector(s);
    }
}

static void wd1773_write_sector(WD1773State *s)
{
    int64_t off;

    if (!wd1773_ready(s)) {
        wd1773_not_ready(s, false);
        return;
    }
    if (!blk_is_writable(s->blk)) {
        s->wr_protect = true;
        wd1773_complete(s, false);
        return;
    }
    if (!wd1773_offset(s, s->side, &off)) {
        s->rnf = true;
        wd1773_complete(s, false);
        return;
    }
    s->xfer_i = 0;
    s->xfer_n = WD1773_SECSIZE;
    s->xfer_write = true;
    s->wr_protect = false;
    s->status_type_1 = false;
    wd1773_set_drq(s, true);
}

static void wd1773_commit_write(WD1773State *s)
{
    int64_t off;

    if (!wd1773_offset(s, s->side, &off) ||
        blk_pwrite(s->blk, off, WD1773_SECSIZE, s->xfer, 0) < 0) {
        s->rnf = true;
    }
    wd1773_complete(s, false);
}

static void wd1773_force_interrupt(WD1773State *s, uint8_t cmd)
{
    s->xfer_n = 0;
    s->busy = false;
    s->status_type_1 = true;
    wd1773_set_drq(s, false);
    if (cmd & WD1773_FI_IMM) {
        wd1773_set_intrq(s, true);
    }
}

static void wd1773_command(WD1773State *s, uint8_t val)
{
    s->cmd = val;
    if ((val & 0xf0) == 0xd0) {
        wd1773_force_interrupt(s, val);
        return;
    }
    if (s->busy) {
        return;
    }

    wd1773_set_intrq(s, false);
    s->busy = true;
    s->seek_err = false;
    s->rnf = false;
    s->crc_err = false;
    s->lost_data = false;
    s->record_type = false;
    s->wr_protect = false;

    if ((val & 0xf0) == 0x00) {
        wd1773_restore(s);
    } else if ((val & 0xf0) == 0x10) {
        wd1773_seek(s);
    } else if ((val & 0xe0) == 0x20) {
        wd1773_step(s, 0, val & 0x10);
    } else if ((val & 0xe0) == 0x40) {
        wd1773_step(s, 1, val & 0x10);
    } else if ((val & 0xe0) == 0x60) {
        wd1773_step(s, -1, val & 0x10);
    } else if ((val & 0xe0) == 0x80) {
        wd1773_read_sector(s);
    } else if ((val & 0xe0) == 0xa0) {
        wd1773_write_sector(s);
    } else {
        qemu_log_mask(LOG_UNIMP, "wd1773: command 0x%02x unimplemented\n", val);
        s->rnf = true;
        wd1773_complete(s, false);
    }
}

static uint8_t wd1773_status(WD1773State *s)
{
    uint8_t st = 0;

    if (s->busy) {
        st |= WD1773_ST_BUSY;
    }
    if (!wd1773_ready(s)) {
        st |= WD1773_ST_NRDY;
    }
    if (s->status_type_1) {
        if (s->head_track == 0) {
            st |= WD1773_ST_TR00;
        }
        if (s->seek_err) {
            st |= WD1773_ST_SEEKERR;
        }
        if (s->crc_err) {
            st |= WD1773_ST_CRC;
        }
        if (s->blk && !blk_is_writable(s->blk)) {
            st |= WD1773_ST_WP;
        }
    } else {
        if (s->drq) {
            st |= WD1773_ST_DRQ;
        }
        if (s->lost_data) {
            st |= WD1773_ST_LOST;
        }
        if (s->crc_err) {
            st |= WD1773_ST_CRC;
        }
        if (s->rnf) {
            st |= WD1773_ST_RNF;
        }
        if (s->record_type) {
            st |= WD1773_ST_RECTYPE;
        }
        if (s->wr_protect) {
            st |= WD1773_ST_WP;
        }
    }
    return st;
}

static uint8_t wd1773_read_data(WD1773State *s)
{
    uint8_t val = s->data;

    if (!s->xfer_n || s->xfer_write) {
        return val;
    }
    s->xfer_i++;
    if (s->xfer_i < s->xfer_n) {
        s->data = s->xfer[s->xfer_i];
        wd1773_set_drq(s, true);
    } else {
        wd1773_complete(s, false);
    }
    return val;
}

static void wd1773_write_data(WD1773State *s, uint8_t val)
{
    s->data = val;
    if (!s->xfer_n || !s->xfer_write) {
        return;
    }
    s->xfer[s->xfer_i++] = val;
    if (s->xfer_i < s->xfer_n) {
        wd1773_set_drq(s, true);
    } else {
        wd1773_commit_write(s);
    }
}

static uint64_t wd1773_read(void *opaque, hwaddr addr, unsigned size)
{
    WD1773State *s = opaque;

    switch (addr & 3) {
    case WD1773_R_CMD:
        wd1773_set_intrq(s, false);
        return wd1773_status(s);
    case WD1773_R_TRACK:
        return s->track;
    case WD1773_R_SECTOR:
        return s->sector;
    case WD1773_R_DATA:
        /* Spare read after the sector. The last real byte still has xfer_n. */
        if (!s->xfer_n && !s->busy && s->intrq && s->idle_read) {
            s->idle_read(s->idle_opaque);
        }
        return wd1773_read_data(s);
    default:
        g_assert_not_reached();
    }
}

static void wd1773_write(void *opaque, hwaddr addr, uint64_t val,
                         unsigned size)
{
    WD1773State *s = opaque;
    uint8_t data = val;

    switch (addr & 3) {
    case WD1773_R_CMD:
        wd1773_command(s, data);
        break;
    case WD1773_R_TRACK:
        if (!s->busy) {
            s->track = data;
        }
        break;
    case WD1773_R_SECTOR:
        if (!s->busy) {
            s->sector = data;
        }
        break;
    case WD1773_R_DATA:
        wd1773_write_data(s, data);
        break;
    default:
        g_assert_not_reached();
    }
}

static const MemoryRegionOps wd1773_ops = {
    .read = wd1773_read,
    .write = wd1773_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void wd1773_reset_hold(Object *obj, ResetType type)
{
    WD1773State *s = WD1773(obj);

    s->track = 0;
    s->sector = 1;
    s->data = 0;
    s->cmd = 0;
    s->head_track = 0;
    s->step_dir = -1;
    s->busy = false;
    s->status_type_1 = true;
    s->seek_err = false;
    s->rnf = false;
    s->crc_err = false;
    s->lost_data = false;
    s->record_type = false;
    s->wr_protect = false;
    s->xfer_n = 0;
    s->ready = false;
    s->side = 0;
    wd1773_set_drq(s, false);
    wd1773_set_intrq(s, false);
}

static void wd1773_realize(DeviceState *dev, Error **errp)
{
    WD1773State *s = WD1773(dev);
    uint64_t perm;

    if (!s->blk) {
        return;
    }
    perm = BLK_PERM_CONSISTENT_READ |
           (blk_supports_write_perm(s->blk) ? BLK_PERM_WRITE : 0);
    blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp);
}

static void wd1773_init(Object *obj)
{
    WD1773State *s = WD1773(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &wd1773_ops, s, TYPE_WD1773,
                          WD1773_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq_intrq);
    sysbus_init_irq(sbd, &s->irq_drq);
}

static const VMStateDescription vmstate_wd1773 = {
    .name = TYPE_WD1773,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(track, WD1773State),
        VMSTATE_UINT8(sector, WD1773State),
        VMSTATE_UINT8(data, WD1773State),
        VMSTATE_UINT8(cmd, WD1773State),
        VMSTATE_UINT8(head_track, WD1773State),
        VMSTATE_INT8(step_dir, WD1773State),
        VMSTATE_BOOL(busy, WD1773State),
        VMSTATE_BOOL(status_type_1, WD1773State),
        VMSTATE_BOOL(seek_err, WD1773State),
        VMSTATE_BOOL(rnf, WD1773State),
        VMSTATE_BOOL(crc_err, WD1773State),
        VMSTATE_BOOL(lost_data, WD1773State),
        VMSTATE_BOOL(record_type, WD1773State),
        VMSTATE_BOOL(wr_protect, WD1773State),
        VMSTATE_BOOL(intrq, WD1773State),
        VMSTATE_BOOL(drq, WD1773State),
        VMSTATE_BOOL(ready, WD1773State),
        VMSTATE_UINT8_ARRAY(xfer, WD1773State, WD1773_SECSIZE),
        VMSTATE_UINT32(xfer_i, WD1773State),
        VMSTATE_UINT32(xfer_n, WD1773State),
        VMSTATE_BOOL(xfer_write, WD1773State),
        VMSTATE_END_OF_LIST()
    }
};

static const Property wd1773_properties[] = {
    DEFINE_PROP_DRIVE("drive", WD1773State, blk),
};

static void wd1773_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "Western Digital WD1773 FDC";
    dc->realize = wd1773_realize;
    dc->user_creatable = false;
    dc->vmsd = &vmstate_wd1773;
    device_class_set_props(dc, wd1773_properties);
    rc->phases.hold = wd1773_reset_hold;
}

static const TypeInfo wd1773_types[] = {
    {
        .name = TYPE_WD1773,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(WD1773State),
        .instance_init = wd1773_init,
        .class_init = wd1773_class_init,
    }
};

DEFINE_TYPES(wd1773_types)
