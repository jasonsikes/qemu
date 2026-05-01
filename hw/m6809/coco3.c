/*
 * Color Computer 3 system (CPU + GIME MMU + ROM)
 *
 * Copyright (c) 2025 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/units.h"
#include "qemu/bswap.h"
#include "qapi/error.h"
#include "migration/vmstate.h"
#include "system/memory.h"
#include "system/address-spaces.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "hw/core/cpu.h"
#include "hw/core/qdev-properties.h"
#include "qom/object.h"
#include "system/blockdev.h"
#include "system/system.h"
#include "coco3.h"
#include "coco3_video.h"
#include "boot.h"

/* CoCo ROM typically occupies the upper 32 KiB of the 64 KiB address space. */
#define COCO3_ROM_SIZE  (32 * KiB)

/* Overlays on the 8 KiB MMU windows; higher numbers win. */
#define COCO3_ROM_PRIORITY  1
#define COCO3_FEXX_PRIORITY 2
#define COCO3_IO_PRIORITY   3
#define COCO3_VEC_PRIORITY  4

#define COCO3_PIA0_BASE  0xff00
#define COCO3_VCONS_BASE 0xff10 /* virt console */
#define COCO3_PIA1_BASE  0xff20
#define COCO3_VDISK_BASE 0xff30 /* virt disk */

#define GIME_IO_BASE    0xff90
#define GIME_IO_SIZE    0x50
#define GIME_FEXX_BASE  0xfe00
#define GIME_FEXX_SIZE  0x100
#define GIME_FEXX_OFF   0x1e00
/* Page 7 alias is $E000-$FDFF; $FE00-$FEFF is the "fexx" window. */
#define GIME_PAGE7_RAM_SIZE  GIME_FEXX_OFF
#define GIME_FEXX_BLOCK 0x3f
#define GIME_VEC_BASE   0xffe0
#define GIME_CART_BLOCK 0x3e

/* 60 Hz field rate, as on an NTSC CoCo. */
#define COCO3_FRAME_NS  (NANOSECONDS_PER_SECOND / 60)

static uint32_t coco3_mmu_block(const Coco3State *s, int page)
{
    if (s->init0 & GIME_INIT0_MMUEN) {
        int task = (s->init1 & GIME_INIT1_TR) ? GIME_PAGE_COUNT : 0;

        return s->mmu[task + page] & GIME_MMU_BLOCK_MASK;
    }

    /*
     * MMU off: the eight CPU pages are hard-wired to the top 64 KiB of
     * physical RAM (blocks 56-63). ROM then overlays blocks $3C-$3D.
     */
    return GIME_RESET_BASE_BLOCK + page;
}

/*
 * Blocks $3C-$3F show internal ROM unless SAM TY maps all RAM, or the ROM
 * select bits would have chosen cartridge ROM. There is no cartridge, so
 * those pages stay RAM.
 */
static bool coco3_page_is_rom(const Coco3State *s, uint32_t block)
{
    uint8_t rom_mode;

    if (s->sam_ty || block < GIME_ROM_BLOCK) {
        return false;
    }

    rom_mode = s->init0 & GIME_INIT0_ROMSEL;
    if (rom_mode == 3 || (rom_mode < 2 && block >= GIME_CART_BLOCK)) {
        return false;
    }

    return true;
}

static void coco3_mmu_update_page(Coco3State *s, int page)
{
    uint32_t block = coco3_mmu_block(s, page);

    memory_region_set_alias_offset(&s->ram_page[page],
                                   block * GIME_PAGE_SIZE);
    memory_region_set_enabled(&s->rom_page[page],
                              coco3_page_is_rom(s, block));
}

static void coco3_mmu_update_all(Coco3State *s)
{
    int page;

    memory_region_transaction_begin();
    for (page = 0; page < GIME_PAGE_COUNT; page++) {
        coco3_mmu_update_page(s, page);
    }
    memory_region_transaction_commit();
}

static uint8_t *coco3_fexx_ptr(Coco3State *s, hwaddr addr)
{
    uint32_t block = (s->init0 & GIME_INIT0_MC3)
        ? GIME_FEXX_BLOCK
        : coco3_mmu_block(s, 7);

    return memory_region_get_ram_ptr(s->ram) +
           block * GIME_PAGE_SIZE + GIME_FEXX_OFF + (addr & 0xff);
}

static uint64_t coco3_fexx_read(void *opaque, hwaddr addr, unsigned size)
{
    return *coco3_fexx_ptr(opaque, addr);
}

static void coco3_fexx_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    *coco3_fexx_ptr(opaque, addr) = val;
}

static const MemoryRegionOps coco3_fexx_ops = {
    .read = coco3_fexx_read,
    .write = coco3_fexx_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

static uint64_t coco3_vec_read(void *opaque, hwaddr addr, unsigned size)
{
    Coco3State *s = opaque;
    uint8_t *rom;

    if (s->vec_custom) {
        return s->vec[addr];
    }

    rom = memory_region_get_ram_ptr(&s->rom);
    return rom[memory_region_size(&s->rom) - GIME_VEC_SIZE + addr];
}

static void coco3_vec_write(void *opaque, hwaddr addr, uint64_t val,
                            unsigned size)
{
    Coco3State *s = opaque;

    s->vec[addr] = val;
    s->vec_custom = true;
}

static const MemoryRegionOps coco3_vec_ops = {
    .read = coco3_vec_read,
    .write = coco3_vec_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

static void coco3_gime_update_irqs(Coco3State *s)
{
    if (!s->irq_src) {
        return;
    }
    /* INIT0.IEN/FEN are ignored; $FF92/$FF93 alone drive the CPU lines. */
    qemu_set_irq(s->irq_src[1], s->gime_pending & s->gime_irqen);
    qemu_set_irq(s->firq_src[1], s->gime_pending & s->gime_firen);
}

static void coco3_irq_src(void *opaque, int n, int level)
{
    Coco3State *s = opaque;

    s->irq_level[n] = level;
    qemu_set_irq(s->cpu_irq, s->irq_level[0] | s->irq_level[1]);
}

static void coco3_firq_src(void *opaque, int n, int level)
{
    Coco3State *s = opaque;

    s->firq_level[n] = level;
    qemu_set_irq(s->cpu_firq, s->firq_level[0] | s->firq_level[1]);
}

static void coco3_gime_reset(Coco3State *s)
{
    int i;

    s->init0 = s->os9_kernel ? GIME_INIT0_MMUEN : 0;
    s->init1 = 0;
    s->gime_irqen = 0;
    s->gime_firen = 0;
    s->gime_pending = 0;
    s->sam_ty = false;
    s->vmode = 0;
    s->vres = 0;
    s->border = 0;
    s->vbank = 0;
    s->vscroll = 0;
    s->voff_msb = 0;
    s->voff_lsb = 0;
    s->hoff = 0;
    memset(s->palette, 0, sizeof(s->palette));
    for (i = 0; i < GIME_PAGE_COUNT; i++) {
        s->mmu[i] = s->mmu[i + GIME_PAGE_COUNT] = GIME_RESET_BASE_BLOCK + i;
    }
    coco3_mmu_update_all(s);
    coco3_gime_update_irqs(s);
    coco3_video_reset(s);
}

static uint8_t coco3_gime_ack(Coco3State *s, uint8_t enable)
{
    uint8_t v = s->gime_pending & enable;

    s->gime_pending &= ~v;
    coco3_gime_update_irqs(s);
    return v;
}

static uint64_t coco3_gime_read(void *opaque, hwaddr addr, unsigned size)
{
    Coco3State *s = opaque;

    /* $FFA0-$FFAF: MMU registers. Only the low six bits are defined. */
    if (addr >= GIME_R_MMU && addr < GIME_R_MMU + GIME_MMU_REGS) {
        return s->mmu[addr - GIME_R_MMU] & GIME_MMU_BLOCK_MASK;
    }

    /* $FFB0-$FFBF: palette. Upper two bits are undefined. */
    if (addr >= GIME_R_PALETTE &&
        addr < GIME_R_PALETTE + GIME_PALETTE_COUNT) {
        return s->palette[addr - GIME_R_PALETTE] & GIME_COLOR_MASK;
    }

    switch (addr) {
    case GIME_R_INIT0:
        return s->init0;
    case GIME_R_INIT1:
        return s->init1;
    case GIME_R_IRQEN:
        /* Enable is write-only; a read returns and acknowledges pending IRQs. */
        return coco3_gime_ack(s, s->gime_irqen);
    case GIME_R_FIREN:
        return coco3_gime_ack(s, s->gime_firen);
    case GIME_R_VMODE:
        return s->vmode;
    case GIME_R_VRES:
        return s->vres;
    case GIME_R_BORDER:
        return s->border & GIME_COLOR_MASK;
    case GIME_R_VBANK:
        return s->vbank;
    case GIME_R_VSCROLL:
        return s->vscroll;
    case GIME_R_VOFF_MSB:
        return s->voff_msb;
    case GIME_R_VOFF_LSB:
        return s->voff_lsb;
    case GIME_R_HOFF:
        return s->hoff;
    default:
        return 0;
    }
}

static void coco3_gime_write(void *opaque, hwaddr addr, uint64_t val,
                             unsigned size)
{
    Coco3State *s = opaque;
    uint8_t data = val;

    if (addr >= GIME_R_SAM) {
        /* SAM: even address clears the bit, odd sets it. Only TY matters. */
        if ((addr & ~1) == GIME_R_SAM_TY) {
            bool ty = addr & 1;

            if (s->sam_ty != ty) {
                s->sam_ty = ty;
                coco3_mmu_update_all(s);
            }
        }
        return;
    }

    if (addr >= GIME_R_PALETTE &&
        addr < GIME_R_PALETTE + GIME_PALETTE_COUNT) {
        coco3_video_set_palette(s, addr - GIME_R_PALETTE, data);
        return;
    }

    if (addr >= GIME_R_MMU && addr < GIME_R_MMU + GIME_MMU_REGS) {
        int slot = addr - GIME_R_MMU;
        uint8_t block = data & GIME_MMU_BLOCK_MASK;

        if (s->mmu[slot] != block) {
            s->mmu[slot] = block;
            coco3_mmu_update_page(s, slot & 7);
        }
        return;
    }

    switch (addr) {
    case GIME_R_INIT0:
        if (s->init0 != data) {
            uint8_t old = s->init0;

            s->init0 = data;
            if ((old ^ data) & ~(GIME_INIT0_IEN | GIME_INIT0_FEN |
                                 GIME_INIT0_COCO)) {
                coco3_mmu_update_all(s);
            }
            coco3_gime_update_irqs(s);
            if ((old ^ data) & GIME_INIT0_COCO) {
                coco3_video_invalidate(s);
            }
        }
        break;
    case GIME_R_INIT1:
        if ((s->init1 ^ data) & GIME_INIT1_TR) {
            s->init1 = data;
            coco3_mmu_update_all(s);
        } else {
            s->init1 = data;
        }
        break;
    case GIME_R_IRQEN:
        s->gime_irqen = data;
        coco3_gime_update_irqs(s);
        break;
    case GIME_R_FIREN:
        s->gime_firen = data;
        coco3_gime_update_irqs(s);
        break;
    case GIME_R_VMODE:
        s->vmode = data;
        coco3_video_invalidate(s);
        break;
    case GIME_R_VRES:
        s->vres = data;
        coco3_video_invalidate(s);
        break;
    case GIME_R_BORDER:
        s->border = data & GIME_COLOR_MASK;
        coco3_video_invalidate(s);
        break;
    case GIME_R_VBANK:
        s->vbank = data;
        break;
    case GIME_R_VSCROLL:
        s->vscroll = data;
        coco3_video_invalidate(s);
        break;
    case GIME_R_VOFF_MSB:
        s->voff_msb = data;
        coco3_video_invalidate(s);
        break;
    case GIME_R_VOFF_LSB:
        s->voff_lsb = data;
        coco3_video_invalidate(s);
        break;
    case GIME_R_HOFF:
        s->hoff = data;
        coco3_video_invalidate(s);
        break;
    default:
        break;
    }
}

static const MemoryRegionOps coco3_gime_ops = {
    .read = coco3_gime_read,
    .write = coco3_gime_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 1,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_BIG_ENDIAN,
};

/* 60 Hz VBORD. */
static void coco3_frame_tick(void *opaque)
{
    Coco3State *s = opaque;

    s->gime_pending |= GIME_IRQ_VBORD;
    coco3_gime_update_irqs(s);
    coco3_video_invalidate(s);
    if (s->fake_cart_firq) {
        qemu_irq_raise(s->cart);
        qemu_irq_lower(s->cart);
    }

    timer_mod(s->frame_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + COCO3_FRAME_NS);
}

static void coco3_realize(DeviceState *dev, Error **errp)
{
    Coco3State *s = COCO3(dev);
    MemoryRegion *sysmem = get_system_memory();
    DeviceState *cpu;
    int i;

    if (!s->ram) {
        error_setg(errp, "coco3: missing ram memory region");
        return;
    }
    if (memory_region_size(s->ram) != COCO3_RAM_SIZE) {
        error_setg(errp, "coco3: RAM size must be 512 KiB");
        return;
    }

    object_initialize_child(OBJECT(dev), "cpu", &s->cpu,
                            M6809_CPU_TYPE_NAME("m6809"));
    object_property_set_bool(OBJECT(&s->cpu), "realized", true, &error_abort);
    cpu = DEVICE(&s->cpu);

    memory_region_init_rom(&s->rom, OBJECT(dev), "coco3.rom",
                           COCO3_ROM_SIZE, &error_fatal);

    for (i = 0; i < GIME_PAGE_COUNT; i++) {
        char name[32];
        uint32_t ram_size = (i == 7) ? GIME_PAGE7_RAM_SIZE : GIME_PAGE_SIZE;

        snprintf(name, sizeof(name), "coco3.ram.page%d", i);
        memory_region_init_alias(&s->ram_page[i], OBJECT(dev), name, s->ram,
                                 i * GIME_PAGE_SIZE, ram_size);
        memory_region_add_subregion(sysmem, i * GIME_PAGE_SIZE,
                                    &s->ram_page[i]);

        snprintf(name, sizeof(name), "coco3.rom.page%d", i);
        memory_region_init_alias(&s->rom_page[i], OBJECT(dev), name, &s->rom,
                                 (i & 3) * GIME_PAGE_SIZE, GIME_PAGE_SIZE);
        memory_region_set_enabled(&s->rom_page[i], false);
        memory_region_add_subregion_overlap(sysmem, i * GIME_PAGE_SIZE,
                                            &s->rom_page[i],
                                            COCO3_ROM_PRIORITY);
    }

    memory_region_init_io(&s->fexx, OBJECT(dev), &coco3_fexx_ops, s,
                          "coco3.fexx", GIME_FEXX_SIZE);
    memory_region_add_subregion_overlap(sysmem, GIME_FEXX_BASE, &s->fexx,
                                        COCO3_FEXX_PRIORITY);

    memory_region_init_io(&s->vectors, OBJECT(dev), &coco3_vec_ops, s,
                          "coco3.vectors", GIME_VEC_SIZE);
    memory_region_add_subregion_overlap(sysmem, GIME_VEC_BASE, &s->vectors,
                                        COCO3_VEC_PRIORITY);

    memory_region_init_io(&s->gime_io, OBJECT(dev), &coco3_gime_ops, s,
                          "coco3.gime", GIME_IO_SIZE);
    memory_region_add_subregion_overlap(sysmem, GIME_IO_BASE, &s->gime_io,
                                        COCO3_IO_PRIORITY);

    s->cpu_irq = qdev_get_gpio_in(cpu, M6809_CPU_IRQ);
    s->cpu_firq = qdev_get_gpio_in(cpu, M6809_CPU_FIRQ);
    s->irq_src = qemu_allocate_irqs(coco3_irq_src, s, 2);
    s->firq_src = qemu_allocate_irqs(coco3_firq_src, s, 2);

    object_initialize_child(OBJECT(dev), "pia0", &s->pia0, TYPE_MC6821);
    sysbus_realize(SYS_BUS_DEVICE(&s->pia0), &error_abort);
    memory_region_add_subregion_overlap(sysmem, COCO3_PIA0_BASE,
                                        sysbus_mmio_get_region(
                                            SYS_BUS_DEVICE(&s->pia0), 0),
                                        COCO3_IO_PRIORITY);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pia0), 0, s->irq_src[0]);

    object_initialize_child(OBJECT(dev), "pia1", &s->pia1, TYPE_MC6821);
    sysbus_realize(SYS_BUS_DEVICE(&s->pia1), &error_abort);
    memory_region_add_subregion_overlap(sysmem, COCO3_PIA1_BASE,
                                        sysbus_mmio_get_region(
                                            SYS_BUS_DEVICE(&s->pia1), 0),
                                        COCO3_IO_PRIORITY);
    sysbus_connect_irq(SYS_BUS_DEVICE(&s->pia1), 0, s->firq_src[0]);

    object_initialize_child(OBJECT(dev), "virt-console", &s->vcons,
                            TYPE_COCO3_VIRT_CONSOLE);
    qdev_prop_set_chr(DEVICE(&s->vcons), "chardev", serial_hd(0));
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->vcons), errp)) {
        return;
    }
    memory_region_add_subregion_overlap(sysmem, COCO3_VCONS_BASE,
                                        sysbus_mmio_get_region(
                                            SYS_BUS_DEVICE(&s->vcons), 0),
                                        COCO3_IO_PRIORITY);

    object_initialize_child(OBJECT(dev), "virt-disk", &s->vdisk,
                            TYPE_COCO3_VIRT_DISK);
    {
        DriveInfo *dinfo = drive_get(IF_NONE, 0, 0);

        if (dinfo) {
            qdev_prop_set_drive(DEVICE(&s->vdisk), "drive",
                                blk_by_legacy_dinfo(dinfo));
        }
    }
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->vdisk), errp)) {
        return;
    }
    memory_region_add_subregion_overlap(sysmem, COCO3_VDISK_BASE,
                                        sysbus_mmio_get_region(
                                            SYS_BUS_DEVICE(&s->vdisk), 0),
                                        COCO3_IO_PRIORITY);

    coco3_video_init(s);

    s->cart = qdev_get_gpio_in_named(DEVICE(&s->pia1), "CB1", 0);
    s->frame_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, coco3_frame_tick, s);
    timer_mod(s->frame_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + COCO3_FRAME_NS);

    coco3_gime_reset(s);
}

static void coco3_reset_hold(Object *obj, ResetType type)
{
    Coco3State *s = COCO3(obj);
    uint8_t *rom;
    size_t sz;
    uint16_t rst;

    coco3_gime_reset(s);
    cpu_reset(CPU(&s->cpu));

    /* -kernel: boot-track entry. -bios: RESET vector in the ROM. */
    if (s->os9_kernel) {
        cpu_set_pc(CPU(&s->cpu), OS9_BOOTTRACK_ENTRY);
        return;
    }
    rom = memory_region_get_ram_ptr(&s->rom);
    sz = memory_region_size(&s->rom);
    rst = lduw_be_p(rom + sz - 2);
    if (rst) {
        cpu_set_pc(CPU(&s->cpu), rst);
    }
}

static int coco3_post_load(void *opaque, int version_id)
{
    Coco3State *s = opaque;

    coco3_mmu_update_all(s);
    coco3_gime_update_irqs(s);
    coco3_video_reset(s);
    qemu_set_irq(s->cpu_irq, s->irq_level[0] | s->irq_level[1]);
    qemu_set_irq(s->cpu_firq, s->firq_level[0] | s->firq_level[1]);
    return 0;
}

static const VMStateDescription coco3_vmstate = {
    .name = TYPE_COCO3,
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = coco3_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(init0, Coco3State),
        VMSTATE_UINT8(init1, Coco3State),
        VMSTATE_UINT8_ARRAY(mmu, Coco3State, GIME_MMU_REGS),
        VMSTATE_BOOL(sam_ty, Coco3State),
        VMSTATE_BOOL(fake_cart_firq, Coco3State),
        VMSTATE_UINT8(gime_irqen, Coco3State),
        VMSTATE_UINT8(gime_firen, Coco3State),
        VMSTATE_UINT8(gime_pending, Coco3State),
        VMSTATE_UINT8_ARRAY(vec, Coco3State, GIME_VEC_SIZE),
        VMSTATE_BOOL(vec_custom, Coco3State),
        VMSTATE_UINT8_ARRAY(irq_level, Coco3State, 2),
        VMSTATE_UINT8_ARRAY(firq_level, Coco3State, 2),
        VMSTATE_TIMER_PTR(frame_timer, Coco3State),
        VMSTATE_UINT8(vmode, Coco3State),
        VMSTATE_UINT8(vres, Coco3State),
        VMSTATE_UINT8(border, Coco3State),
        VMSTATE_UINT8(vbank, Coco3State),
        VMSTATE_UINT8(vscroll, Coco3State),
        VMSTATE_UINT8(voff_msb, Coco3State),
        VMSTATE_UINT8(voff_lsb, Coco3State),
        VMSTATE_UINT8(hoff, Coco3State),
        VMSTATE_UINT8_ARRAY(palette, Coco3State, GIME_PALETTE_COUNT),
        VMSTATE_END_OF_LIST()
    }
};

static const Property coco3_properties[] = {
    DEFINE_PROP_BOOL("fake-cart-firq", Coco3State, fake_cart_firq, false),
    DEFINE_PROP_BOOL("os9-kernel", Coco3State, os9_kernel, false),
    DEFINE_PROP_LINK("ram", Coco3State, ram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void coco3_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = coco3_realize;
    dc->user_creatable = false;
    dc->vmsd = &coco3_vmstate;
    device_class_set_props(dc, coco3_properties);
    rc->phases.hold = coco3_reset_hold;
}

static const TypeInfo coco3_types[] = {
    {
        .name = TYPE_COCO3,
        .parent = TYPE_SYS_BUS_DEVICE,
        .instance_size = sizeof(Coco3State),
        .class_init = coco3_class_init,
    }
};

DEFINE_TYPES(coco3_types)
