/*
 * Color Computer 3 board tests (PIA0 keyboard matrix, virt RTC).
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

#define VRTC_MAGIC 0xff50
#define VRTC_VER   0xff51
#define VRTC_YEAR  0xff52
#define VRTC_MONTH 0xff53
#define VRTC_DAY   0xff54
#define VRTC_HOUR  0xff55
#define VRTC_MIN   0xff56
#define VRTC_SEC   0xff57

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

static QTestState *coco3_vm_new(void)
{
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

static void test_keyboard_a(void)
{
    QTestState *s = coco3_vm_new();

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

static void test_keyboard_modifiers(void)
{
    QTestState *s = coco3_vm_new();

    pia0_kbd_init(s);

    send_key(s, "ret", true);
    g_assert_cmphex(scan_col(s, 0), ==, 0xbf); /* Enter: PA6 */
    send_key(s, "ret", false);

    send_key(s, "shift", true);
    g_assert_cmphex(scan_col(s, 7), ==, 0xbf); /* Shift: PA6 */
    send_key(s, "shift", false);

    send_key(s, "esc", true);
    g_assert_cmphex(scan_col(s, 2), ==, 0xbf); /* Break: PA6 */
    send_key(s, "esc", false);

    send_key(s, "f12", true);
    g_assert_cmphex(scan_col(s, 1), ==, 0xbf); /* Clear: PA6 */
    send_key(s, "f12", false);

    qtest_quit(s);
}

static void test_keyboard_glyphs(void)
{
    QTestState *s = coco3_vm_new();

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

static QTestState *coco3_vm_new_rtc(void)
{
    return qtest_initf("-M coco3 -bios %s -rtc base=2026-08-21T21:00:00,clock=vm",
                       rom_path);
}

static void test_virt_rtc(void)
{
    QTestState *s = coco3_vm_new_rtc();
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

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    rom_path = write_stub_rom();
    qtest_add_func("/coco3/keyboard/a", test_keyboard_a);
    qtest_add_func("/coco3/keyboard/modifiers", test_keyboard_modifiers);
    qtest_add_func("/coco3/keyboard/glyphs", test_keyboard_glyphs);
    qtest_add_func("/coco3/virt-rtc", test_virt_rtc);

    ret = g_test_run();

    unlink(rom_path);
    g_free(rom_path);
    return ret;
}
