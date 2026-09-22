/*
 * Color Computer 3 board tests (PIA0 keyboard, analog joystick,
 * 65C52 Microsoft mouse, virt RTC).
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define ROM_SIZE  (32 * 1024)
#define ROM_BASE  0x8000

#define PIA0_DA   0xff00
#define PIA0_CA   0xff01
#define PIA0_DB   0xff02
#define PIA0_CB   0xff03
#define PIA1_DA   0xff20
#define PIA1_CA   0xff21

#define VRTC_MAGIC 0xff50
#define VRTC_VER   0xff51
#define VRTC_YEAR  0xff52
#define VRTC_MONTH 0xff53
#define VRTC_DAY   0xff54
#define VRTC_HOUR  0xff55
#define VRTC_MIN   0xff56
#define VRTC_SEC   0xff57

#define MOUSE_IS   0xff64
#define MOUSE_CF   0xff65
#define MOUSE_TB   0xff66
#define MOUSE_DATA 0xff67
#define GIME_IRQEN 0xff92

#define MOS65C52_ISE_RXF  0x01
#define MOS65C52_ISE_IRQ  0x80
#define GIME_IRQ_CART     0x01

static char *rom_path;

static char *write_stub_rom(void)
{
    uint8_t rom[ROM_SIZE];
    char *path = NULL;
    GError *err = NULL;
    int fd;

    memset(rom, 0, sizeof(rom));
    rom[0] = 0x20;
    rom[1] = 0xfe;
    rom[ROM_SIZE - 2] = ROM_BASE >> 8;
    rom[ROM_SIZE - 1] = ROM_BASE & 0xff;

    fd = g_file_open_tmp("coco3-kbd-XXXXXX", &path, &err);
    g_assert_no_error(err);
    g_assert_cmpint(write(fd, rom, sizeof(rom)), ==, sizeof(rom));
    close(fd);
    return path;
}

#define RESET_ENTRY  0x8c1b

static char *write_reset_rom(void)
{
    uint8_t rom[ROM_SIZE];
    char *path = NULL;
    GError *err = NULL;
    int fd;

    memset(rom, 0, sizeof(rom));
    rom[0] = 0x45; /* illegal if fetched from $8000 */
    rom[RESET_ENTRY - ROM_BASE] = 0x20;
    rom[RESET_ENTRY - ROM_BASE + 1] = 0xfe;
    rom[ROM_SIZE - 2] = RESET_ENTRY >> 8;
    rom[ROM_SIZE - 1] = RESET_ENTRY & 0xff;

    fd = g_file_open_tmp("coco3-reset-XXXXXX", &path, &err);
    g_assert_no_error(err);
    g_assert_cmpint(write(fd, rom, sizeof(rom)), ==, sizeof(rom));
    close(fd);
    return path;
}

static void test_bios_reset_vector(void)
{
    char *path = write_reset_rom();
    QTestState *s = qtest_initf("-M coco3 -bios %s", path);
    char *regs = qtest_hmp(s, "info registers");

    g_assert_nonnull(strstr(regs, "PC=8c1b"));
    g_free(regs);
    qtest_quit(s);
    unlink(path);
    g_free(path);
}

static QTestState *coco3_vm_new(const char *cpu)
{
    if (cpu) {
        return qtest_initf("-M coco3 -cpu %s -bios %s", cpu, rom_path);
    }
    return qtest_initf("-M coco3 -bios %s", rom_path);
}

/* PIA0: PA inputs, PB outputs, data registers selected. */
static void pia0_kbd_init(QTestState *s)
{
    qtest_writeb(s, PIA0_CA, 0);
    qtest_writeb(s, PIA0_DA, 0);
    qtest_writeb(s, PIA0_CA, 0x04);
    qtest_writeb(s, PIA0_CB, 0);
    qtest_writeb(s, PIA0_DB, 0xff);
    qtest_writeb(s, PIA0_CB, 0x04);
}

static void send_key(QTestState *s, const char *qcode, bool down)
{
    if (down) {
        qtest_qmp_assert_success(s,
            "{'execute': 'input-send-event', 'arguments': {"
            " 'events': [{'type': 'key', 'data': {'down': true,"
            "  'key': {'type': 'qcode', 'data': %s}}}]}}",
            qcode);
    } else {
        qtest_qmp_assert_success(s,
            "{'execute': 'input-send-event', 'arguments': {"
            " 'events': [{'type': 'key', 'data': {'down': false,"
            "  'key': {'type': 'qcode', 'data': %s}}}]}}",
            qcode);
    }
}

static uint8_t scan_col(QTestState *s, int col)
{
    qtest_writeb(s, PIA0_DB, (uint8_t)~(1u << col));
    return qtest_readb(s, PIA0_DA);
}

static void test_keyboard_a(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    pia0_kbd_init(s);

    /* No keys: every column reads pulled-up rows plus DAC. */
    g_assert_cmphex(scan_col(s, 1), ==, 0xff);

    send_key(s, "a", true);
    /* A is PB1 / PA0. */
    g_assert_cmphex(scan_col(s, 1), ==, 0xfe);
    g_assert_cmphex(scan_col(s, 0), ==, 0xff);

    send_key(s, "a", false);
    /* Min hold: still down until two 60 Hz ticks. */
    g_assert_cmphex(scan_col(s, 1), ==, 0xfe);
    qtest_clock_step(s, (1000000000LL / 60) * 2);
    g_assert_cmphex(scan_col(s, 1), ==, 0xff);

    qtest_quit(s);
}

static void test_keyboard_modifiers(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    pia0_kbd_init(s);

    send_key(s, "ret", true);
    g_assert_cmphex(scan_col(s, 0), ==, 0xbf); /* Enter: PA6 */
    send_key(s, "ret", false);
    qtest_clock_step(s, (1000000000LL / 60) * 2);

    send_key(s, "shift", true);
    g_assert_cmphex(scan_col(s, 7), ==, 0xbf); /* Shift: PA6 */
    send_key(s, "shift", false);
    qtest_clock_step(s, (1000000000LL / 60) * 2);

    send_key(s, "esc", true);
    g_assert_cmphex(scan_col(s, 2), ==, 0xbf); /* Break: PA6 */
    send_key(s, "esc", false);
    qtest_clock_step(s, (1000000000LL / 60) * 2);

    send_key(s, "f12", true);
    g_assert_cmphex(scan_col(s, 1), ==, 0xbf); /* Clear: PA6 */
    send_key(s, "f12", false);

    qtest_quit(s);
}

static void test_keyboard_glyphs(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    pia0_kbd_init(s);

    send_key(s, "2", true);
    g_assert_cmphex(scan_col(s, 2), ==, 0xef); /* 2: PA4 */
    send_key(s, "2", false);
    qtest_clock_step(s, (1000000000LL / 60) * 2);

    /* Shift-2 is @, not CoCo's ". CoCo Shift is not held. */
    send_key(s, "shift", true);
    send_key(s, "2", true);
    g_assert_cmphex(scan_col(s, 0), ==, 0xfe); /* @: PA0 */
    g_assert_cmphex(scan_col(s, 7), ==, 0xff);
    send_key(s, "2", false);
    send_key(s, "shift", false);
    qtest_clock_step(s, (1000000000LL / 60) * 2);

    /* Letters still use CoCo Shift. */
    send_key(s, "shift", true);
    send_key(s, "a", true);
    g_assert_cmphex(scan_col(s, 1), ==, 0xfe);
    g_assert_cmphex(scan_col(s, 7), ==, 0xbf);
    send_key(s, "a", false);
    send_key(s, "shift", false);
    qtest_clock_step(s, (1000000000LL / 60) * 2);

    /* = is CoCo Shift-minus; Shift-; is CoCo colon (no Shift). */
    send_key(s, "equal", true);
    g_assert_cmphex(scan_col(s, 5), ==, 0xdf); /* minus: PA5 */
    g_assert_cmphex(scan_col(s, 7), ==, 0xbf);
    send_key(s, "equal", false);
    qtest_clock_step(s, (1000000000LL / 60) * 2);

    send_key(s, "shift", true);
    send_key(s, "semicolon", true);
    g_assert_cmphex(scan_col(s, 2), ==, 0xdf); /* colon: PA5 */
    g_assert_cmphex(scan_col(s, 7), ==, 0xff);
    send_key(s, "semicolon", false);
    send_key(s, "shift", false);

    qtest_quit(s);
}

/* PIA1 PA2–PA7 are DAC outputs. */
static void pia1_dac_init(QTestState *s)
{
    qtest_writeb(s, PIA1_CA, 0);
    qtest_writeb(s, PIA1_DA, 0xfc);
    qtest_writeb(s, PIA1_CA, 0x04);
}

static void set_dac(QTestState *s, uint8_t val)
{
    qtest_writeb(s, PIA1_DA, (val & 0x3f) << 2);
}

/* PIA0 CA2 = SEL1 (LSB), CB2 = SEL2 (MSB). Keep data registers selected. */
static void set_mux(QTestState *s, unsigned sel)
{
    qtest_writeb(s, PIA0_CA, 0x34 | ((sel & 1) ? 0x08 : 0));
    qtest_writeb(s, PIA0_CB, 0x34 | ((sel & 2) ? 0x08 : 0));
}

static uint8_t read_pa(QTestState *s)
{
    qtest_writeb(s, PIA0_DB, 0xff);
    return qtest_readb(s, PIA0_DA);
}

static void send_abs(QTestState *s, int x, int y)
{
    qtest_qmp_assert_success(s,
        "{'execute': 'input-send-event', 'arguments': {"
        " 'events': ["
        "  {'type': 'abs', 'data': {'axis': 'x', 'value': %d}},"
        "  {'type': 'abs', 'data': {'axis': 'y', 'value': %d}}"
        "]}}", x, y);
}

static void send_btn(QTestState *s, const char *button, bool down)
{
    if (down) {
        qtest_qmp_assert_success(s,
            "{'execute': 'input-send-event', 'arguments': {"
            " 'events': [{'type': 'btn', 'data': {'down': true,"
            "  'button': %s}}]}}",
            button);
    } else {
        qtest_qmp_assert_success(s,
            "{'execute': 'input-send-event', 'arguments': {"
            " 'events': [{'type': 'btn', 'data': {'down': false,"
            "  'button': %s}}]}}",
            button);
    }
}

static void test_joystick_dac(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    pia0_kbd_init(s);
    pia1_dac_init(s);
    set_mux(s, 0);

    /* Centered right X is 32. */
    set_dac(s, 32);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0x80);
    set_dac(s, 33);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0);

    send_abs(s, 0, 0);
    set_dac(s, 0);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0x80);
    set_dac(s, 1);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0);

    send_abs(s, 0x7fff, 0x7fff);
    set_mux(s, 1); /* right Y */
    set_dac(s, 62);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0x80);
    set_dac(s, 63);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0x80);

    /* Left stick stays centered. */
    set_mux(s, 2);
    set_dac(s, 32);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0x80);
    set_dac(s, 33);
    g_assert_cmphex(read_pa(s) & 0x80, ==, 0);

    qtest_quit(s);
}

static void test_joystick_buttons(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    pia0_kbd_init(s);

    g_assert_cmphex(read_pa(s), ==, 0xff);

    send_btn(s, "left", true);
    g_assert_cmphex(read_pa(s), ==, 0xfe); /* PA0 */
    send_btn(s, "right", true);
    g_assert_cmphex(read_pa(s), ==, 0xfa); /* PA0 and PA2 */
    send_btn(s, "left", false);
    send_btn(s, "right", false);
    g_assert_cmphex(read_pa(s), ==, 0xff);

    qtest_quit(s);
}

static void mouse_init(QTestState *s)
{
    qtest_writeb(s, MOUSE_CF, 0x46);
    qtest_writeb(s, MOUSE_CF, 0xc0);
    qtest_writeb(s, MOUSE_TB, 0);
    qtest_writeb(s, MOUSE_IS, MOS65C52_ISE_IRQ | 0x07);
    qtest_writeb(s, GIME_IRQEN, GIME_IRQ_CART);
}

static void test_mouse_packet(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    mouse_init(s);
    g_assert_cmphex(qtest_readb(s, MOUSE_IS) & 0x07, ==, 0);

    qtest_qmp_assert_success(s,
        "{'execute': 'input-send-event', 'arguments': {"
        " 'events': [{'type': 'rel', 'data': {'axis': 'x', 'value': 10}}]}}");

    g_assert_cmphex(qtest_readb(s, MOUSE_IS) & 0x81, ==, 0x81);
    g_assert_cmphex(qtest_readb(s, GIME_IRQEN) & GIME_IRQ_CART, ==,
                    GIME_IRQ_CART);
    /* Microsoft: sync $40, dx=10, dy=0. */
    g_assert_cmphex(qtest_readb(s, MOUSE_DATA), ==, 0x40);
    g_assert_cmphex(qtest_readb(s, MOUSE_DATA), ==, 0x0a);
    g_assert_cmphex(qtest_readb(s, MOUSE_DATA), ==, 0x00);
    g_assert_cmphex(qtest_readb(s, MOUSE_IS) & 0x07, ==, 0);

    qtest_quit(s);
}

static QTestState *coco3_vm_new_rtc(const char *cpu)
{
    if (cpu) {
        return qtest_initf(
            "-M coco3 -cpu %s -bios %s -rtc base=2026-08-21T21:00:00,clock=vm",
            cpu, rom_path);
    }
    return qtest_initf("-M coco3 -bios %s -rtc base=2026-08-21T21:00:00,clock=vm",
                       rom_path);
}

static void test_virt_rtc(const void *data)
{
    QTestState *s = coco3_vm_new_rtc(data);
    uint8_t sec0, sec1;

    g_assert_cmphex(qtest_readb(s, VRTC_MAGIC), ==, 'Q');
    g_assert_cmphex(qtest_readb(s, VRTC_VER), ==, 0x01);
    g_assert_cmpint(qtest_readb(s, VRTC_YEAR), ==, 126); /* 2026 */
    g_assert_cmpint(qtest_readb(s, VRTC_MONTH), ==, 8);
    g_assert_cmpint(qtest_readb(s, VRTC_DAY), ==, 21);
    g_assert_cmpint(qtest_readb(s, VRTC_HOUR), ==, 21);
    g_assert_cmpint(qtest_readb(s, VRTC_MIN), ==, 0);
    sec0 = qtest_readb(s, VRTC_SEC);

    qtest_clock_step(s, 2 * 1000000000LL);
    g_assert_cmpint(qtest_readb(s, VRTC_SEC), ==, sec0);

    qtest_readb(s, VRTC_MAGIC);
    sec1 = qtest_readb(s, VRTC_SEC);
    g_assert_cmpint(sec1, ==, (sec0 + 2) % 60);

    qtest_quit(s);
}

/* Color BASIC TIMER increments from PIA0 CB1 (FS) on each 60 Hz field. */
static void test_pia0_fsync(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    /* CRB: data register, CB1 IRQ enable, falling edge. */
    qtest_writeb(s, PIA0_CB, 0x05);
    g_assert_cmphex(qtest_readb(s, PIA0_CB) & 0x80, ==, 0);

    qtest_clock_step(s, 1000000000LL / 60);
    g_assert_cmphex(qtest_readb(s, PIA0_CB) & 0x80, ==, 0x80);

    qtest_readb(s, PIA0_DB);
    g_assert_cmphex(qtest_readb(s, PIA0_CB) & 0x80, ==, 0);

    qtest_quit(s);
}

/*
 * $FFE0-$FFFF is vector RAM, filled from ROM at reset. Writing one byte
 * must not make the rest of the table read as zero.
 */
static void test_vector_ram(const void *data)
{
    QTestState *s = coco3_vm_new(data);

    g_assert_cmphex(qtest_readb(s, 0xfffe), ==, ROM_BASE >> 8);
    g_assert_cmphex(qtest_readb(s, 0xffff), ==, ROM_BASE & 0xff);

    qtest_writeb(s, 0xffe0, 0xaa);
    g_assert_cmphex(qtest_readb(s, 0xffe0), ==, 0xaa);
    g_assert_cmphex(qtest_readb(s, 0xfffe), ==, ROM_BASE >> 8);
    g_assert_cmphex(qtest_readb(s, 0xffff), ==, ROM_BASE & 0xff);

    qtest_quit(s);
}

#define CART_ROM_SIZE  (16 * 1024)

static char *write_cart_rom(void)
{
    uint8_t rom[CART_ROM_SIZE];
    char *path = NULL;
    GError *err = NULL;
    int fd;

    memset(rom, 0xff, sizeof(rom));
    rom[0] = 'D';
    rom[1] = 'K';

    fd = g_file_open_tmp("coco3-cart-XXXXXX", &path, &err);
    g_assert_no_error(err);
    g_assert_cmpint(write(fd, rom, sizeof(rom)), ==, sizeof(rom));
    close(fd);
    return path;
}

/* ROMSEL 16K+16K maps cartridge ROM at $C000. */
static void test_cart_rom(void)
{
    char *cart = write_cart_rom();
    QTestState *s = qtest_initf("-M coco3 -device coco3-cart,romfile=%s -bios %s",
                                cart, rom_path);

    g_assert_cmphex(qtest_readb(s, 0xc000), ==, 'D');
    g_assert_cmphex(qtest_readb(s, 0xc001), ==, 'K');
    /* Second 8K of the 16K cart aliases at $E000 when ROMSEL is 16K+16K. */
    g_assert_cmphex(qtest_readb(s, 0xe000), ==, 0xff);

    qtest_quit(s);
    unlink(cart);
    g_free(cart);
}

#define GIME_INIT0     0xff90
#define GIME_INIT0_SCS 0x04

#define FDC_LATCH      0xff40
#define WD1773_CMD     0xff48
#define WD1773_TRACK   0xff49
#define WD1773_SECTOR  0xff4a
#define WD1773_DATA    0xff4b
#define WD1773_QOM     "/machine/sys/fdc/wd1773"
#define FDC_QOM        "/machine/sys/fdc"
#define CPU_QOM        "/machine/sys/cpu"

#define WD1773_ST_BUSY 0x01
#define WD1773_ST_DRQ  0x02
#define WD1773_ST_TR00 0x04
#define WD1773_ST_RNF  0x10
#define WD1773_ST_NRDY 0x80
#define WD1773_IRQ_INTRQ 0
#define WD1773_IRQ_DRQ   1
#define FDC_IRQ_NMI      0
#define FDC_IRQ_HALT     1
#define CPU_IRQ_NMI      2

#define FDC_DS0          0x01
#define FDC_MOTOR        0x08
#define FDC_DENSITY      0x20
#define FDC_HALT         0x80
#define FDC_SELECT       (FDC_DS0 | FDC_MOTOR)

#define WD1773_DSK_SIZE (35 * 18 * 256)

static char *write_blank_dsk(void)
{
    char *path = NULL;
    GError *err = NULL;
    int fd;

    fd = g_file_open_tmp("coco3-wd1773-XXXXXX", &path, &err);
    g_assert_no_error(err);
    g_assert_cmpint(ftruncate(fd, WD1773_DSK_SIZE), ==, 0);
    close(fd);
    return path;
}

static void fdc_enable_scs(QTestState *s)
{
    qtest_writeb(s, GIME_INIT0,
                 qtest_readb(s, GIME_INIT0) | GIME_INIT0_SCS);
}

static QTestState *wd1773_vm(const char *dsk)
{
    QTestState *s;

    if (dsk) {
        s = qtest_initf(
            "-M coco3 -bios %s -drive if=none,index=2,file=%s,format=raw",
            rom_path, dsk);
    } else {
        s = qtest_initf("-M coco3 -bios %s", rom_path);
    }
    fdc_enable_scs(s);
    qtest_irq_intercept_out_named(s, WD1773_QOM, "sysbus-irq");
    if (dsk) {
        qtest_writeb(s, FDC_LATCH, FDC_SELECT);
    }
    return s;
}

static void test_wd1773_restore(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = wd1773_vm(dsk);
    uint8_t st;

    qtest_writeb(s, WD1773_CMD, 0x00);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_DRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    g_assert_cmphex(st & WD1773_ST_BUSY, ==, 0);
    g_assert_cmphex(st & WD1773_ST_TR00, ==, WD1773_ST_TR00);
    g_assert_cmphex(st & WD1773_ST_NRDY, ==, 0);
    g_assert_cmphex(qtest_readb(s, WD1773_TRACK), ==, 0);

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_seek(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = wd1773_vm(dsk);
    uint8_t st;

    qtest_writeb(s, WD1773_DATA, 5);
    qtest_writeb(s, WD1773_CMD, 0x10);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & WD1773_ST_BUSY, ==, 0);
    g_assert_cmphex(st & WD1773_ST_TR00, ==, 0);
    g_assert_cmphex(qtest_readb(s, WD1773_TRACK), ==, 5);

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_read_no_media(void)
{
    QTestState *s = wd1773_vm(NULL);
    uint8_t st;

    qtest_writeb(s, WD1773_SECTOR, 1);
    qtest_writeb(s, WD1773_CMD, 0x80);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & WD1773_ST_NRDY, ==, WD1773_ST_NRDY);
    g_assert_cmphex(st & WD1773_ST_BUSY, ==, 0);
    g_assert_cmphex(st & WD1773_ST_DRQ, ==, 0);

    qtest_quit(s);
}

static void test_wd1773_write_read_sector(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = wd1773_vm(dsk);
    uint8_t st;
    int i;

    qtest_writeb(s, WD1773_TRACK, 0);
    qtest_writeb(s, WD1773_SECTOR, 1);
    qtest_writeb(s, WD1773_CMD, 0xa0);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_DRQ));
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & WD1773_ST_BUSY, ==, WD1773_ST_BUSY);
    g_assert_cmphex(st & WD1773_ST_DRQ, ==, WD1773_ST_DRQ);

    for (i = 0; i < 256; i++) {
        qtest_writeb(s, WD1773_DATA, (uint8_t)(0xa5 ^ i));
    }
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_DRQ));
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & (WD1773_ST_BUSY | WD1773_ST_DRQ | WD1773_ST_RNF),
                    ==, 0);

    qtest_writeb(s, WD1773_CMD, 0x80);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_DRQ));
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    for (i = 0; i < 256; i++) {
        g_assert_cmphex(qtest_readb(s, WD1773_DATA), ==, (uint8_t)(0xa5 ^ i));
    }
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_DRQ));
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & (WD1773_ST_BUSY | WD1773_ST_DRQ | WD1773_ST_RNF),
                    ==, 0);

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_rnf(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = wd1773_vm(dsk);
    uint8_t st;

    qtest_writeb(s, WD1773_SECTOR, 19);
    qtest_writeb(s, WD1773_CMD, 0x80);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_DRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & WD1773_ST_RNF, ==, WD1773_ST_RNF);
    g_assert_cmphex(st & WD1773_ST_BUSY, ==, 0);

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_force_interrupt(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = wd1773_vm(dsk);
    uint8_t st;

    qtest_writeb(s, WD1773_SECTOR, 1);
    qtest_writeb(s, WD1773_CMD, 0xa0);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_DRQ));
    qtest_writeb(s, WD1773_DATA, 0x11);
    qtest_writeb(s, WD1773_DATA, 5);
    qtest_writeb(s, WD1773_CMD, 0x10);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_DRQ));
    g_assert_cmphex(qtest_readb(s, WD1773_TRACK), ==, 0);

    qtest_writeb(s, WD1773_CMD, 0xd0);
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_DRQ));
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & WD1773_ST_BUSY, ==, 0);

    qtest_writeb(s, WD1773_CMD, 0xd8);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    qtest_readb(s, WD1773_CMD);
    g_assert_false(qtest_get_irq(s, WD1773_IRQ_INTRQ));

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_mirror(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = wd1773_vm(dsk);

    qtest_writeb(s, WD1773_TRACK + 4, 7);
    g_assert_cmphex(qtest_readb(s, WD1773_TRACK), ==, 7);
    qtest_writeb(s, WD1773_DATA, 3);
    qtest_writeb(s, WD1773_CMD + 4, 0x10);
    g_assert_true(qtest_get_irq(s, WD1773_IRQ_INTRQ));
    g_assert_cmphex(qtest_readb(s, WD1773_TRACK + 4), ==, 3);

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_latch_ready(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = qtest_initf(
        "-M coco3 -bios %s -drive if=none,index=2,file=%s,format=raw",
        rom_path, dsk);
    uint8_t st;

    fdc_enable_scs(s);
    qtest_irq_intercept_out_named(s, WD1773_QOM, "sysbus-irq");
    qtest_writeb(s, WD1773_CMD, 0x00);
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & WD1773_ST_NRDY, ==, WD1773_ST_NRDY);

    qtest_writeb(s, FDC_LATCH, FDC_SELECT);
    qtest_writeb(s, WD1773_CMD, 0x00);
    st = qtest_readb(s, WD1773_CMD);
    g_assert_cmphex(st & WD1773_ST_NRDY, ==, 0);
    g_assert_cmphex(st & WD1773_ST_TR00, ==, WD1773_ST_TR00);

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_nmi_density_gate(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = qtest_initf(
        "-M coco3 -bios %s -drive if=none,index=2,file=%s,format=raw",
        rom_path, dsk);

    fdc_enable_scs(s);
    qtest_irq_intercept_in(s, CPU_QOM);
    qtest_writeb(s, FDC_LATCH, FDC_SELECT);
    qtest_writeb(s, WD1773_CMD, 0x00);
    g_assert_false(qtest_get_irq(s, CPU_IRQ_NMI));

    qtest_writeb(s, FDC_LATCH, FDC_SELECT | FDC_DENSITY);
    g_assert_true(qtest_get_irq(s, CPU_IRQ_NMI));

    qtest_readb(s, WD1773_CMD);
    g_assert_false(qtest_get_irq(s, CPU_IRQ_NMI));

    qtest_writeb(s, FDC_LATCH, FDC_SELECT | FDC_DENSITY);
    qtest_writeb(s, WD1773_CMD, 0x00);
    g_assert_true(qtest_get_irq(s, CPU_IRQ_NMI));

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_wd1773_intrq_clears_halt(void)
{
    char *dsk = write_blank_dsk();
    QTestState *s = qtest_initf(
        "-M coco3 -bios %s -drive if=none,index=2,file=%s,format=raw",
        rom_path, dsk);

    fdc_enable_scs(s);
    qtest_irq_intercept_out_named(s, FDC_QOM, "sysbus-irq");
    qtest_writeb(s, FDC_LATCH, FDC_SELECT | FDC_HALT);
    g_assert_cmphex(qtest_readb(s, FDC_LATCH) & FDC_HALT, ==, FDC_HALT);
    g_assert_true(qtest_get_irq(s, FDC_IRQ_HALT));
    g_assert_false(qtest_get_irq(s, FDC_IRQ_NMI));

    qtest_writeb(s, WD1773_CMD, 0x00);
    g_assert_cmphex(qtest_readb(s, FDC_LATCH) & FDC_HALT, ==, 0);
    g_assert_false(qtest_get_irq(s, FDC_IRQ_HALT));
    g_assert_false(qtest_get_irq(s, FDC_IRQ_NMI));

    qtest_quit(s);
    unlink(dsk);
    g_free(dsk);
}

static void test_fdc_scs_gate(void)
{
    QTestState *s = qtest_initf("-M coco3 -bios %s", rom_path);

    qtest_writeb(s, FDC_LATCH, FDC_SELECT);
    g_assert_cmphex(qtest_readb(s, FDC_LATCH), ==, 0);

    fdc_enable_scs(s);
    qtest_writeb(s, FDC_LATCH, FDC_SELECT);
    g_assert_cmphex(qtest_readb(s, FDC_LATCH), ==, FDC_SELECT);

    qtest_writeb(s, GIME_INIT0, qtest_readb(s, GIME_INIT0) & ~GIME_INIT0_SCS);
    g_assert_cmphex(qtest_readb(s, FDC_LATCH), ==, 0);

    fdc_enable_scs(s);
    g_assert_cmphex(qtest_readb(s, FDC_LATCH), ==, FDC_SELECT);

    qtest_quit(s);
}

static void add_coco3_tests(const char *prefix, const char *cpu)
{
    char *path;

    path = g_strdup_printf("%s/keyboard/a", prefix);
    qtest_add_data_func(path, cpu, test_keyboard_a);
    g_free(path);
    path = g_strdup_printf("%s/keyboard/modifiers", prefix);
    qtest_add_data_func(path, cpu, test_keyboard_modifiers);
    g_free(path);
    path = g_strdup_printf("%s/keyboard/glyphs", prefix);
    qtest_add_data_func(path, cpu, test_keyboard_glyphs);
    g_free(path);
    path = g_strdup_printf("%s/joystick/dac", prefix);
    qtest_add_data_func(path, cpu, test_joystick_dac);
    g_free(path);
    path = g_strdup_printf("%s/joystick/buttons", prefix);
    qtest_add_data_func(path, cpu, test_joystick_buttons);
    g_free(path);
    path = g_strdup_printf("%s/mouse/packet", prefix);
    qtest_add_data_func(path, cpu, test_mouse_packet);
    g_free(path);
    path = g_strdup_printf("%s/pia0/fsync", prefix);
    qtest_add_data_func(path, cpu, test_pia0_fsync);
    g_free(path);
    path = g_strdup_printf("%s/vectors/ram", prefix);
    qtest_add_data_func(path, cpu, test_vector_ram);
    g_free(path);
    path = g_strdup_printf("%s/virt-rtc", prefix);
    qtest_add_data_func(path, cpu, test_virt_rtc);
    g_free(path);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    rom_path = write_stub_rom();
    qtest_add_func("/coco3/bios/reset-vector", test_bios_reset_vector);
    qtest_add_func("/coco3/cart", test_cart_rom);
    qtest_add_func("/coco3/wd1773/restore", test_wd1773_restore);
    qtest_add_func("/coco3/wd1773/seek", test_wd1773_seek);
    qtest_add_func("/coco3/wd1773/read-no-media", test_wd1773_read_no_media);
    qtest_add_func("/coco3/wd1773/write-read-sector",
                  test_wd1773_write_read_sector);
    qtest_add_func("/coco3/wd1773/rnf", test_wd1773_rnf);
    qtest_add_func("/coco3/wd1773/force-interrupt",
                  test_wd1773_force_interrupt);
    qtest_add_func("/coco3/wd1773/mirror", test_wd1773_mirror);
    qtest_add_func("/coco3/wd1773/latch-ready", test_wd1773_latch_ready);
    qtest_add_func("/coco3/wd1773/nmi-density-gate",
                  test_wd1773_nmi_density_gate);
    qtest_add_func("/coco3/wd1773/intrq-clears-halt",
                  test_wd1773_intrq_clears_halt);
    qtest_add_func("/coco3/wd1773/scs-gate", test_fdc_scs_gate);
    add_coco3_tests("/coco3", NULL);
    add_coco3_tests("/coco3/hd6309", "hd6309");
    add_coco3_tests("/coco3/turbo9", "turbo9");

    ret = g_test_run();

    unlink(rom_path);
    g_free(rom_path);
    return ret;
}
