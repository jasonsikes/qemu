/*
 * CoCo 3 virt disk
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
#include "hw/core/qdev-properties.h"
#include "hw/core/qdev-properties-system.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "system/block-backend.h"
#include "exec/memattrs.h"
#include "virt_disk.h"

#define VDISK_SECTOR    256
#define VDISK_VER       1

#define VDISK_R_MAGIC   0
#define VDISK_R_VER     1
#define VDISK_R_CMD     2
#define VDISK_R_STAT    3
#define VDISK_R_DRV     4
#define VDISK_R_LSN     5
#define VDISK_R_BUF     8

#define VDISK_CMD_READ  1
#define VDISK_CMD_WRITE 2

/* OS-9 error codes returned in Q.STAT. */
#define OS9_E_UNIT      241
#define OS9_E_WP        242
#define OS9_E_READ      244
#define OS9_E_WRITE     245
#define OS9_E_NOTRDY    246
#define OS9_E_SECT      247

/* Copy 256 bytes through CPU space, wrapping at 64 KiB. */
static bool coco3_virt_disk_cpu_rw(uint16_t addr, void *buf, bool is_write)
{
    uint32_t first = 0x10000u - addr;
    uint8_t *p = buf;

    if (first > VDISK_SECTOR) {
        first = VDISK_SECTOR;
    }
    if (address_space_rw(&address_space_memory, addr, MEMTXATTRS_UNSPECIFIED,
                         p, first, is_write) != MEMTX_OK) {
        return false;
    }
    if (first < VDISK_SECTOR) {
        if (address_space_rw(&address_space_memory, 0, MEMTXATTRS_UNSPECIFIED,
                             p + first, VDISK_SECTOR - first,
                             is_write) != MEMTX_OK) {
            return false;
        }
    }
    return true;
}

static void coco3_virt_disk_run(Coco3VirtDiskState *s)
{
    uint8_t sector[VDISK_SECTOR];
    uint32_t lsn;
    uint16_t buf;
    int64_t offset, len;
    bool writing;

    s->stat = 0;

    if (!s->blk) {
        s->stat = OS9_E_NOTRDY;
        return;
    }
    if (s->drv != 0) {
        s->stat = OS9_E_UNIT;
        return;
    }
    if (s->cmd != VDISK_CMD_READ && s->cmd != VDISK_CMD_WRITE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "coco3-virt-disk: unknown command 0x%02x\n", s->cmd);
        s->stat = OS9_E_NOTRDY;
        return;
    }

    writing = s->cmd == VDISK_CMD_WRITE;
    if (writing && !blk_is_writable(s->blk)) {
        s->stat = OS9_E_WP;
        return;
    }

    lsn = ((uint32_t)s->lsn[0] << 16) | ((uint32_t)s->lsn[1] << 8) | s->lsn[2];
    buf = ((uint16_t)s->buf[0] << 8) | s->buf[1];
    offset = (int64_t)lsn * VDISK_SECTOR;
    len = blk_getlength(s->blk);
    if (len < 0) {
        s->stat = OS9_E_NOTRDY;
        return;
    }
    if (offset >= len || len - offset < VDISK_SECTOR) {
        s->stat = OS9_E_SECT;
        return;
    }

    if (writing) {
        if (!coco3_virt_disk_cpu_rw(buf, sector, false)) {
            s->stat = OS9_E_WRITE;
            return;
        }
        if (blk_pwrite(s->blk, offset, VDISK_SECTOR, sector, 0) < 0) {
            s->stat = OS9_E_WRITE;
        }
    } else {
        if (blk_pread(s->blk, offset, VDISK_SECTOR, sector, 0) < 0) {
            s->stat = OS9_E_READ;
            return;
        }
        if (!coco3_virt_disk_cpu_rw(buf, sector, true)) {
            s->stat = OS9_E_READ;
        }
    }
}

static uint64_t coco3_virt_disk_read(void *opaque, hwaddr addr, unsigned size)
{
    Coco3VirtDiskState *s = opaque;

    switch (addr) {
    case VDISK_R_MAGIC:
        return 'Q';
    case VDISK_R_VER:
        return VDISK_VER;
    case VDISK_R_CMD:
        return s->cmd;
    case VDISK_R_STAT:
        return s->stat;
    case VDISK_R_DRV:
        return s->drv;
    case VDISK_R_LSN:
    case VDISK_R_LSN + 1:
    case VDISK_R_LSN + 2:
        return s->lsn[addr - VDISK_R_LSN];
    case VDISK_R_BUF:
    case VDISK_R_BUF + 1:
        return s->buf[addr - VDISK_R_BUF];
    default:
        return 0;
    }
}

static void coco3_virt_disk_write(void *opaque, hwaddr addr, uint64_t val,
                                  unsigned size)
{
    Coco3VirtDiskState *s = opaque;
    uint8_t data = val;

    switch (addr) {
    case VDISK_R_CMD:
        s->cmd = data;
        coco3_virt_disk_run(s);
        break;
    case VDISK_R_DRV:
        s->drv = data;
        break;
    case VDISK_R_LSN:
    case VDISK_R_LSN + 1:
    case VDISK_R_LSN + 2:
        s->lsn[addr - VDISK_R_LSN] = data;
        break;
    case VDISK_R_BUF:
    case VDISK_R_BUF + 1:
        s->buf[addr - VDISK_R_BUF] = data;
        break;
    default:
        break;
    }
}

static const MemoryRegionOps coco3_virt_disk_ops = {
    .read = coco3_virt_disk_read,
    .write = coco3_virt_disk_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void coco3_virt_disk_reset_hold(Object *obj, ResetType type)
{
    Coco3VirtDiskState *s = COCO3_VIRT_DISK(obj);

    s->cmd = 0;
    s->stat = 0;
    s->drv = 0;
    memset(s->lsn, 0, sizeof(s->lsn));
    memset(s->buf, 0, sizeof(s->buf));
}

static void coco3_virt_disk_realize(DeviceState *dev, Error **errp)
{
    Coco3VirtDiskState *s = COCO3_VIRT_DISK(dev);
    uint64_t perm;

    if (!s->blk) {
        return;
    }

    perm = BLK_PERM_CONSISTENT_READ |
           (blk_supports_write_perm(s->blk) ? BLK_PERM_WRITE : 0);
    if (blk_set_perm(s->blk, perm, BLK_PERM_ALL, errp) < 0) {
        return;
    }
}

static void coco3_virt_disk_init(Object *obj)
{
    Coco3VirtDiskState *s = COCO3_VIRT_DISK(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &coco3_virt_disk_ops, s,
                          TYPE_COCO3_VIRT_DISK, COCO3_VDISK_IO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const VMStateDescription vmstate_coco3_virt_disk = {
    .name = TYPE_COCO3_VIRT_DISK,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(cmd, Coco3VirtDiskState),
        VMSTATE_UINT8(stat, Coco3VirtDiskState),
        VMSTATE_UINT8(drv, Coco3VirtDiskState),
        VMSTATE_UINT8_ARRAY(lsn, Coco3VirtDiskState, 3),
        VMSTATE_UINT8_ARRAY(buf, Coco3VirtDiskState, 2),
        VMSTATE_END_OF_LIST()
    }
};

static const Property coco3_virt_disk_properties[] = {
    DEFINE_PROP_DRIVE("drive", Coco3VirtDiskState, blk),
};

static void coco3_virt_disk_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->desc = "CoCo 3 virt disk";
    dc->realize = coco3_virt_disk_realize;
    dc->user_creatable = false;
    dc->vmsd = &vmstate_coco3_virt_disk;
    device_class_set_props(dc, coco3_virt_disk_properties);
    rc->phases.hold = coco3_virt_disk_reset_hold;
}

static const TypeInfo coco3_virt_disk_types[] = {
    {
        .name = TYPE_COCO3_VIRT_DISK,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Coco3VirtDiskState),
        .instance_init = coco3_virt_disk_init,
        .class_init = coco3_virt_disk_class_init,
    }
};

DEFINE_TYPES(coco3_virt_disk_types)
