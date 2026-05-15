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
#include "ui/input.h"
#include "standard-headers/linux/input-event-codes.h"
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

    /*
     * -bios: VDG-compat like silicon so Color BASIC can paint $0400.
     * -kernel: MMUEN; NitrOS-9 reprograms the GIME itself.
     */
    s->init0 = s->os9_kernel ? GIME_INIT0_MMUEN : GIME_INIT0_COCO;
    s->init1 = 0;
    s->gime_irqen = 0;
    s->gime_firen = 0;
    s->gime_pending = 0;
    s->sam_ty = false;
    s->sam_v = 0;
    s->sam_f = 0;
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
        /* SAM: even address clears the bit, odd sets it. */
        unsigned bit = (addr - GIME_R_SAM) >> 1;
        bool set = addr & 1;

        if (bit <= 2) {
            uint8_t mask = 1u << bit;
            uint8_t v = set ? (s->sam_v | mask) : (s->sam_v & ~mask);

            if (s->sam_v != v) {
                s->sam_v = v;
                coco3_video_invalidate(s);
            }
        } else if (bit >= 3 && bit <= 9) {
            uint8_t mask = 1u << (bit - 3);
            uint8_t f = set ? (s->sam_f | mask) : (s->sam_f & ~mask);

            if (s->sam_f != f) {
                s->sam_f = f;
                coco3_video_invalidate(s);
            }
        } else if ((addr & ~1) == GIME_R_SAM_TY) {
            if (s->sam_ty != set) {
                s->sam_ty = set;
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
        coco3_video_invalidate(s);
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

/*
 * CoCo 3 keyboard. PIA0 PB bits are column strobes (outputs, active low);
 * PA0–PA6 are row inputs (active low). PA7 is the DAC comparator and stays
 * pulled up until a joystick is modelled.
 *
 *        PB0 PB1 PB2 PB3 PB4 PB5 PB6 PB7
 *  PA6:  Ent Clr Brk Alt Ctr F1  F2  Shift
 *  PA5:  8   9   :   ;   ,   -   .   /
 *  PA4:  0   1   2   3   4   5   6   7
 *  PA3:  X   Y   Z   Up  Dwn Lft Rgt Space
 *  PA2:  P   Q   R   S   T   U   V   W
 *  PA1:  H   I   J   K   L   M   N   O
 *  PA0:  @   A   B   C   D   E   F   G
 *
 * Host keys are mapped by US-QWERTY glyph, not by CoCo keycap position, so
 * Shift-2 is @ rather than CoCo's ". Shift/Ctrl/Alt are applied after that
 * translation; a remapped unshifted glyph (e.g. @) does not hold CoCo Shift.
 * There is no CoCo key for ^ _ [ ] { } \\ | ` ~.
 */
#define COCO3_KB_NONE  0xff

typedef struct Coco3KeyMap {
    unsigned int lnx;
    uint8_t col, row, sh;     /* host key, no Shift */
    uint8_t scol, srow, ssh;  /* host key, Shift: matrix + CoCo Shift? */
} Coco3KeyMap;

/* Same CoCo key; host Shift also presses CoCo Shift (letters, Shift-Clear). */
#define K_POS(lnx, c, r) { (lnx), (c), (r), 0, (c), (r), 1 }

static const Coco3KeyMap coco3_keymap[] = {
    K_POS(KEY_A, 1, 0), K_POS(KEY_B, 2, 0), K_POS(KEY_C, 3, 0),
    K_POS(KEY_D, 4, 0), K_POS(KEY_E, 5, 0), K_POS(KEY_F, 6, 0),
    K_POS(KEY_G, 7, 0),
    K_POS(KEY_H, 0, 1), K_POS(KEY_I, 1, 1), K_POS(KEY_J, 2, 1),
    K_POS(KEY_K, 3, 1), K_POS(KEY_L, 4, 1), K_POS(KEY_M, 5, 1),
    K_POS(KEY_N, 6, 1), K_POS(KEY_O, 7, 1),
    K_POS(KEY_P, 0, 2), K_POS(KEY_Q, 1, 2), K_POS(KEY_R, 2, 2),
    K_POS(KEY_S, 3, 2), K_POS(KEY_T, 4, 2), K_POS(KEY_U, 5, 2),
    K_POS(KEY_V, 6, 2), K_POS(KEY_W, 7, 2),
    K_POS(KEY_X, 0, 3), K_POS(KEY_Y, 1, 3), K_POS(KEY_Z, 2, 3),
    K_POS(KEY_UP, 3, 3), K_POS(KEY_DOWN, 4, 3),
    K_POS(KEY_LEFT, 5, 3), K_POS(KEY_BACKSPACE, 5, 3),
    K_POS(KEY_RIGHT, 6, 3), K_POS(KEY_SPACE, 7, 3),
    /* 0 )  1 !  2 @  3 #  4 $  5 %  6 ^  7 &  8 *  9 ( */
    { KEY_0, 0, 4, 0, 1, 5, 1 },
    K_POS(KEY_1, 1, 4),
    { KEY_2, 2, 4, 0, 0, 0, 0 },
    K_POS(KEY_3, 3, 4), K_POS(KEY_4, 4, 4), K_POS(KEY_5, 5, 4),
    { KEY_6, 6, 4, 0, COCO3_KB_NONE, 0, 0 },
    { KEY_7, 7, 4, 0, 6, 4, 1 },
    { KEY_8, 0, 5, 0, 2, 5, 1 },
    { KEY_9, 1, 5, 0, 0, 5, 1 },
    /* - _   = +   [ none] */
    { KEY_MINUS, 5, 5, 0, COCO3_KB_NONE, 0, 0 },
    { KEY_EQUAL, 5, 5, 1, 3, 5, 1 },
    { KEY_SEMICOLON, 3, 5, 0, 2, 5, 0 },
    { KEY_APOSTROPHE, 7, 4, 1, 2, 4, 1 },
    K_POS(KEY_COMMA, 4, 5), K_POS(KEY_DOT, 6, 5), K_POS(KEY_SLASH, 7, 5),
    K_POS(KEY_ENTER, 0, 6),
    K_POS(KEY_F12, 1, 6), K_POS(KEY_HOME, 1, 6), /* Clear */
    K_POS(KEY_ESC, 2, 6), K_POS(KEY_END, 2, 6),  /* Break */
    K_POS(KEY_F1, 5, 6), K_POS(KEY_F2, 6, 6),
};

static bool coco3_host_key_down(const Coco3State *s, unsigned int lnx)
{
    return lnx < 128 &&
           (s->kb_pressed[lnx / 64] & (1ULL << (lnx % 64))) != 0;
}

static void coco3_keyboard_rebuild(Coco3State *s)
{
    size_t i;
    bool host_shift = coco3_host_key_down(s, KEY_LEFTSHIFT) ||
                      coco3_host_key_down(s, KEY_RIGHTSHIFT);
    bool any = false;
    bool need_shift = false;

    memset(s->kb_matrix, 0, sizeof(s->kb_matrix));
    for (i = 0; i < ARRAY_SIZE(coco3_keymap); i++) {
        const Coco3KeyMap *k = &coco3_keymap[i];
        uint8_t col, row, sh;
        bool with_shift;

        if (!coco3_host_key_down(s, k->lnx)) {
            continue;
        }
        with_shift =
            (s->kb_shifted[k->lnx / 64] & (1ULL << (k->lnx % 64))) != 0;
        if (with_shift) {
            col = k->scol;
            row = k->srow;
            sh = k->ssh;
        } else {
            col = k->col;
            row = k->row;
            sh = k->sh;
        }
        if (col == COCO3_KB_NONE) {
            continue;
        }
        s->kb_matrix[col] |= 1u << row;
        any = true;
        if (sh) {
            need_shift = true;
        }
    }
    if (need_shift || (host_shift && !any)) {
        s->kb_matrix[7] |= 1u << 6;
    }
    if (coco3_host_key_down(s, KEY_LEFTCTRL) ||
        coco3_host_key_down(s, KEY_RIGHTCTRL)) {
        s->kb_matrix[4] |= 1u << 6;
    }
    if (coco3_host_key_down(s, KEY_LEFTALT) ||
        coco3_host_key_down(s, KEY_RIGHTALT)) {
        s->kb_matrix[3] |= 1u << 6;
    }
}

static void coco3_keyboard_scan(Coco3State *s)
{
    /* Undriven PB pins are pulled up, so they do not select a column. */
    uint8_t cols = s->pia0.b.data | (uint8_t)~s->pia0.b.ddr;
    uint8_t rows = 0x7f;
    int col;

    for (col = 0; col < 8; col++) {
        if (!(cols & (1u << col))) {
            rows &= ~s->kb_matrix[col];
        }
    }
    mc6821_set_port_in(&s->pia0, false, rows | 0x80);
}

static void coco3_keyboard_column(void *opaque, int n G_GNUC_UNUSED,
                                  int level G_GNUC_UNUSED)
{
    coco3_keyboard_scan(opaque);
}

/* PIA1 PB is $FF22 (VDG A/G GM CSS). A write must dirty scanout. */
static void coco3_vdg_mode(void *opaque, int n G_GNUC_UNUSED,
                           int level G_GNUC_UNUSED)
{
    coco3_video_invalidate(opaque);
}

/*
 * Color BASIC KEYIN and NitrOS-9 K$RdKey sample a live matrix with no
 * FIFO. Hold each host press for two 60 Hz ticks so guest has a chance
 * to scan the key before it's released.
 */
#define COCO3_KB_HOLD_FRAMES  2

static void coco3_keyboard_event(DeviceState *dev,
                                 QemuConsole *src G_GNUC_UNUSED,
                                 InputEvent *evt)
{
    Coco3State *s = COCO3(dev);
    InputKeyEvent *key = evt->u.key.data;
    int qcode = qemu_input_key_value_to_qcode(key->key);
    unsigned int lnx;
    unsigned int word;
    uint64_t bit;

    if (qcode >= qemu_input_map_qcode_to_linux_len) {
        return;
    }
    lnx = qemu_input_map_qcode_to_linux[qcode];
    if (lnx >= 128) {
        return;
    }
    word = lnx / 64;
    bit = 1ULL << (lnx % 64);
    if (key->down) {
        bool host_shift = coco3_host_key_down(s, KEY_LEFTSHIFT) ||
                          coco3_host_key_down(s, KEY_RIGHTSHIFT) ||
                          lnx == KEY_LEFTSHIFT || lnx == KEY_RIGHTSHIFT;

        s->kb_pressed[word] |= bit;
        s->kb_release[word] &= ~bit;
        s->kb_hold[lnx] = COCO3_KB_HOLD_FRAMES;
        if (host_shift) {
            s->kb_shifted[word] |= bit;
        } else {
            s->kb_shifted[word] &= ~bit;
        }
    } else if (s->kb_hold[lnx]) {
        s->kb_release[word] |= bit;
        return;
    } else {
        s->kb_pressed[word] &= ~bit;
        s->kb_shifted[word] &= ~bit;
    }
    coco3_keyboard_rebuild(s);
    coco3_keyboard_scan(s);
}

static void coco3_keyboard_hold_tick(Coco3State *s)
{
    unsigned int i;
    bool changed = false;

    for (i = 0; i < 128; i++) {
        unsigned int word;
        uint64_t bit;

        if (!s->kb_hold[i]) {
            continue;
        }
        s->kb_hold[i]--;
        if (s->kb_hold[i]) {
            continue;
        }
        word = i / 64;
        bit = 1ULL << (i % 64);
        if (!(s->kb_release[word] & bit)) {
            continue;
        }
        s->kb_release[word] &= ~bit;
        s->kb_pressed[word] &= ~bit;
        s->kb_shifted[word] &= ~bit;
        changed = true;
    }
    if (changed) {
        coco3_keyboard_rebuild(s);
        coco3_keyboard_scan(s);
    }
}

static const QemuInputHandler coco3_keyboard_handler = {
    .name = "coco3-keyboard",
    .mask = INPUT_EVENT_MASK_KEY,
    .event = coco3_keyboard_event,
};

static void coco3_keyboard_reset(Coco3State *s)
{
    memset(s->kb_pressed, 0, sizeof(s->kb_pressed));
    memset(s->kb_release, 0, sizeof(s->kb_release));
    memset(s->kb_shifted, 0, sizeof(s->kb_shifted));
    memset(s->kb_hold, 0, sizeof(s->kb_hold));
    memset(s->kb_matrix, 0, sizeof(s->kb_matrix));
    coco3_keyboard_scan(s);
}

/* 60 Hz VBORD. */
static void coco3_frame_tick(void *opaque)
{
    Coco3State *s = opaque;

    s->gime_pending |= GIME_IRQ_VBORD;
    coco3_gime_update_irqs(s);
    coco3_keyboard_hold_tick(s);
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
    for (i = 0; i < 8; i++) {
        qdev_connect_gpio_out_named(DEVICE(&s->pia0), MC6821_GPIO_PB, i,
                                    qdev_get_gpio_in_named(DEVICE(s),
                                                           "kb-col", i));
    }

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
    for (i = 0; i < 8; i++) {
        qdev_connect_gpio_out_named(DEVICE(&s->pia1), MC6821_GPIO_PB, i,
                                    qdev_get_gpio_in_named(DEVICE(s),
                                                           "vdg-mode", i));
    }

    s->kbd_hs = qemu_input_handler_register(dev, &coco3_keyboard_handler);

    s->cart = qdev_get_gpio_in_named(DEVICE(&s->pia1), "CB1", 0);
    s->frame_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, coco3_frame_tick, s);
    timer_mod(s->frame_timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + COCO3_FRAME_NS);

    coco3_gime_reset(s);
    coco3_keyboard_reset(s);
}

static void coco3_reset_hold(Object *obj, ResetType type)
{
    Coco3State *s = COCO3(obj);
    uint8_t *rom;
    size_t sz;
    uint16_t rst;

    coco3_gime_reset(s);
    coco3_keyboard_reset(s);
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
    memset(s->kb_pressed, 0, sizeof(s->kb_pressed));
    memset(s->kb_release, 0, sizeof(s->kb_release));
    memset(s->kb_shifted, 0, sizeof(s->kb_shifted));
    memset(s->kb_hold, 0, sizeof(s->kb_hold));
    coco3_keyboard_scan(s);
    qemu_set_irq(s->cpu_irq, s->irq_level[0] | s->irq_level[1]);
    qemu_set_irq(s->cpu_firq, s->firq_level[0] | s->firq_level[1]);
    return 0;
}

static const VMStateDescription coco3_vmstate = {
    .name = TYPE_COCO3,
    .version_id = 6,
    .minimum_version_id = 6,
    .post_load = coco3_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(init0, Coco3State),
        VMSTATE_UINT8(init1, Coco3State),
        VMSTATE_UINT8_ARRAY(mmu, Coco3State, GIME_MMU_REGS),
        VMSTATE_BOOL(sam_ty, Coco3State),
        VMSTATE_UINT8(sam_v, Coco3State),
        VMSTATE_UINT8(sam_f, Coco3State),
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
        VMSTATE_UINT8_ARRAY(kb_matrix, Coco3State, 8),
        VMSTATE_END_OF_LIST()
    }
};

static const Property coco3_properties[] = {
    DEFINE_PROP_BOOL("fake-cart-firq", Coco3State, fake_cart_firq, false),
    DEFINE_PROP_BOOL("os9-kernel", Coco3State, os9_kernel, false),
    DEFINE_PROP_LINK("ram", Coco3State, ram, TYPE_MEMORY_REGION,
                     MemoryRegion *),
};

static void coco3_unrealize(DeviceState *dev)
{
    Coco3State *s = COCO3(dev);

    g_clear_pointer(&s->kbd_hs, qemu_input_handler_unregister);
}

static void coco3_instance_init(Object *obj)
{
    qdev_init_gpio_in_named(DEVICE(obj), coco3_keyboard_column, "kb-col", 8);
    qdev_init_gpio_in_named(DEVICE(obj), coco3_vdg_mode, "vdg-mode", 8);
}

static void coco3_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    dc->realize = coco3_realize;
    dc->unrealize = coco3_unrealize;
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
        .instance_init = coco3_instance_init,
        .class_init = coco3_class_init,
    }
};

DEFINE_TYPES(coco3_types)
