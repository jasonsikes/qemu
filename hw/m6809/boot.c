/*
 * M6809 / Color Computer firmware loader helpers
 *
 * Copyright (c) 2025 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/datadir.h"
#include "system/memory.h"
#include "system/physmem.h"
#include "system/reset.h"
#include "hw/core/loader.h"
#include "boot.h"
#include "qemu/error-report.h"

static void m6809_boottrack_reset(void *opaque)
{
    cpu_set_pc(CPU(opaque), OS9_BOOTTRACK_ENTRY);
}

bool m6809_load_firmware(MemoryRegion *program_mr, const char *firmware)
{
    g_autofree char *filename = NULL;
    int bytes_loaded;

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (filename == NULL) {
        error_report("Cannot find firmware image '%s'", firmware);
        return false;
    }

    bytes_loaded = load_image_mr(filename, program_mr);
    if (bytes_loaded < 0) {
        error_report("Unable to load firmware image %s as raw binary",
                     firmware);
        return false;
    }

    return true;
}

bool m6809_load_boottrack(M6809CPU *cpu, MemoryRegion *ram, hwaddr ram_offset,
                          const char *filename)
{
    g_autofree char *data = NULL;
    gsize len, i;
    g_autoptr(GError) gerr = NULL;
    uint8_t *ram_ptr;
    static const uint8_t coco3_vectors[] = {
        0x00, 0x00, /* $FFF0 reserved */
        0xfe, 0xee, /* $FFF2 SWI3 */
        0xfe, 0xf1, /* $FFF4 SWI2 */
        0xfe, 0xf4, /* $FFF6 FIRQ */
        0xfe, 0xf7, /* $FFF8 IRQ  */
        0xfe, 0xfa, /* $FFFA SWI  */
        0xfe, 0xfd, /* $FFFC NMI  */
        OS9_BOOTTRACK_ENTRY >> 8,
        OS9_BOOTTRACK_ENTRY & 0xff, /* $FFFE RESET */
    };

    if (!g_file_get_contents(filename, &data, &len, &gerr)) {
        error_report("coco3: could not read boot track '%s': %s",
                     filename, gerr->message);
        return false;
    }
    if (len == 0) {
        error_report("coco3: boot track '%s' is empty", filename);
        return false;
    }
    if (len > OS9_BOOTTRACK_SIZE) {
        error_report("coco3: '%s' is %zu bytes; max is %d",
                     filename, (size_t)len, OS9_BOOTTRACK_SIZE);
        return false;
    }
    if (len != OS9_BOOTTRACK_SIZE) {
        warn_report("coco3: '%s' is %zu bytes; expected %d",
                    filename, (size_t)len, OS9_BOOTTRACK_SIZE);
    }
    if (len >= 2 && (data[0] != 'O' || data[1] != 'S')) {
        warn_report("coco3: boot track does not start with 'OS'");
    }
    if (ram_offset + len > memory_region_size(ram)) {
        error_report("coco3: boot track does not fit in RAM");
        return false;
    }

    ram_ptr = memory_region_get_ram_ptr(ram);
    memcpy(ram_ptr + ram_offset, data, len);

    for (i = 0; i < sizeof(coco3_vectors); i++) {
        cpu_physical_memory_write(COCO3_VEC_TABLE + i, &coco3_vectors[i], 1);
    }

    cpu_set_pc(CPU(cpu), OS9_BOOTTRACK_ENTRY);
    qemu_register_reset(m6809_boottrack_reset, cpu);
    return true;
}
