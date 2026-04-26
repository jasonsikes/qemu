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

/* -kernel load address, size, and entry. */
#define OS9_BOOTTRACK_ADDR   0x2600
#define OS9_BOOTTRACK_SIZE   0x1200
#define OS9_BOOTTRACK_ENTRY  0x2602
#define COCO3_VEC_TABLE      0xfff0

bool m6809_load_firmware(MemoryRegion *mr, const char *firmware);
bool m6809_load_boottrack(M6809CPU *cpu, MemoryRegion *ram, hwaddr ram_offset,
                          const char *filename);

#endif /* HW_M6809_BOOT_H */
