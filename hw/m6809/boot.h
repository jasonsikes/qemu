/*
 * M6809 / Color Computer firmware loader helpers
 *
 * Copyright (c) 2025 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_BOOT_H
#define HW_M6809_BOOT_H

#include "system/memory.h"
#include "target/m6809/cpu.h"

typedef struct BlockBackend BlockBackend;

/* -kernel load address, size, and entry. Disk BASIC DOS copies track 34
#define OS9_BOOTTRACK_ADDR   0x2600
#define OS9_BOOTTRACK_SIZE   0x1200
#define OS9_BOOTTRACK_ENTRY  0x2602
#define COCO3_VEC_TABLE      0xffee
#define COCO3_DOS_TRACK      34
#define COCO3_SECS_PER_TRACK 18
#define COCO3_SECTOR_SIZE    256

/* OS-9 identification sector (LSN 0). */
#define OS9_DD_TKS           0x03 /* track size in sectors (8-bit DD.SPT) */
#define OS9_DD_FMT           0x10 /* density / sides */
#define OS9_DD_SPT           0x11 /* sectors per track, 16-bit big-endian */
#define OS9_DD_FMT_SIDES     0x01 /* bit 0 set: double-sided */

bool m6809_load_firmware(MemoryRegion *mr, const char *firmware);
bool m6809_load_boottrack(M6809CPU *cpu, MemoryRegion *ram, hwaddr ram_offset,
                          const char *filename);
bool m6809_load_dos_boottrack(M6809CPU *cpu, MemoryRegion *ram,
                              hwaddr ram_offset, BlockBackend *blk);

#endif /* HW_M6809_BOOT_H */
