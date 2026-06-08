/*
 * 6809 ISA tests on coco3 via libqtest (SAM TY maps $8000 as RAM).
 *
 * Copyright (c) 2025 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define ROM_SIZE         (32 * 1024)
#define ROM_BASE         0x8000
#define OPERAND_OFFSET   0xf0
#define SAM_TY_SET       0xffdf
#define CASE_TIMEOUT_MS  2000

typedef struct {
    uint16_t addr;
    uint8_t val;
} IsaLoad;

typedef struct {
    uint16_t addr;
    const uint8_t *bytes;
    size_t len;
} IsaMem;

typedef struct {
    uint16_t addr;
    uint16_t dest;
} IsaVec;

typedef struct {
    const char *name;
    const char *disas;
    const uint8_t *code;
    size_t code_len;
    const char *const *regs;
    const uint8_t *operand;
    size_t operand_len;
    const IsaLoad *load;
    int n_load;
    const IsaMem *mem;
    int n_mem;
    const IsaVec *vec;
    int n_vec;
} IsaCase;

static const IsaCase isa_cases[] = {
#include "m6809-cpu-test-cases.c.inc"
};

static QTestState *qts;
static char *rom_path;
static bool ran;

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

    fd = g_file_open_tmp("m6809-isa-XXXXXX", &path, &err);
    g_assert_no_error(err);
    g_assert_cmpint(write(fd, rom, sizeof(rom)), ==, sizeof(rom));
    close(fd);
    return path;
}

static QTestState *isa_vm_new(const char *machine, const char *accel)
{
    return qtest_initf("-M %s -bios %s -accel %s -S", machine, rom_path, accel);
}

static void isa_load(QTestState *s, bool *did_run,
                     const uint8_t *code, size_t len,
                     const uint8_t *operand, size_t operand_len,
                     const IsaLoad *load, int n_load,
                     const IsaVec *vec, int n_vec,
                     bool append_bra)
{
    uint8_t prog[256];
    int i;

    g_assert_cmpuint(len + 2, <=, sizeof(prog));

    /* $FE00-$FFFF is outside the RAM memset; repair RESET before reset. */
    qtest_writeb(s, 0xfffe, ROM_BASE >> 8);
    qtest_writeb(s, 0xffff, ROM_BASE & 0xff);
    if (*did_run) {
        qtest_qmp_assert_success(s, "{'execute': 'stop'}");
        qtest_system_reset(s);
    }
    *did_run = true;

    /* SAM TY: odd $FFDF sets all-RAM, so $8000 is writable. */
    qtest_writeb(s, SAM_TY_SET, 0);
    qtest_memset(s, 0, 0, 0xfe00);

    memcpy(prog, code, len);
    if (append_bra) {
        prog[len] = 0x20;
        prog[len + 1] = 0xfe;
        len += 2;
    }
    qtest_memwrite(s, ROM_BASE, prog, len);

    if (operand_len) {
        qtest_memwrite(s, ROM_BASE + OPERAND_OFFSET, operand, operand_len);
    }
    for (i = 0; i < n_load; i++) {
        qtest_writeb(s, load[i].addr, load[i].val);
    }
    for (i = 0; i < n_vec; i++) {
        qtest_writeb(s, vec[i].addr, vec[i].dest >> 8);
        qtest_writeb(s, vec[i].addr + 1, vec[i].dest & 0xff);
    }
}

static char *wait_registers(QTestState *s, const char *needle, int timeout_ms)
{
    gint64 deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
    char *regs = NULL;

    do {
        g_free(regs);
        regs = qtest_hmp(s, "info registers");
        if (strstr(regs, needle)) {
            return regs;
        }
        g_usleep(10000);
    } while (g_get_monotonic_time() < deadline);

    return regs;
}

static void isa_check(const IsaCase *c, const char *regs)
{
    int i;

    for (i = 0; c->regs[i]; i++) {
        if (!strstr(regs, c->regs[i])) {
            g_test_message("%s (%s): missing %s\n%s",
                           c->name, c->disas, c->regs[i], regs);
        }
        g_assert_nonnull(strstr(regs, c->regs[i]));
    }
    for (i = 0; i < c->n_mem; i++) {
        uint8_t got[64];

        g_assert_cmpuint(c->mem[i].len, <=, sizeof(got));
        qtest_memread(qts, c->mem[i].addr, got, c->mem[i].len);
        if (memcmp(got, c->mem[i].bytes, c->mem[i].len) != 0) {
            g_test_message("%s (%s): memory at 0x%04x mismatch",
                           c->name, c->disas, c->mem[i].addr);
        }
        g_assert_cmpint(memcmp(got, c->mem[i].bytes, c->mem[i].len), ==, 0);
    }
}

static void isa_run_case(const IsaCase *c)
{
    char needle[16];
    g_autofree char *regs = NULL;
    uint16_t spin = ROM_BASE + c->code_len;

    isa_load(qts, &ran, c->code, c->code_len,
             c->operand, c->operand_len,
             c->load, c->n_load, c->vec, c->n_vec, true);
    qtest_qmp_assert_success(qts, "{'execute': 'cont'}");
    snprintf(needle, sizeof(needle), "PC=%04x", spin);
    regs = wait_registers(qts, needle, CASE_TIMEOUT_MS);
    if (!strstr(regs, needle)) {
        g_test_message("%s (%s): timed out waiting for %s\n%s",
                       c->name, c->disas, needle, regs);
    }
    g_assert_nonnull(strstr(regs, needle));
    isa_check(c, regs);
}

static void test_isa_case(const void *data)
{
    isa_run_case(data);
}

static void test_invalid_indexed(void)
{
    static const uint8_t code[] = {
        0x86, 0x11,             /* lda #$11 */
        0xa6, 0x87,             /* lda (invalid postbyte $87) */
        0x86, 0x22,
        0x20, 0xfe,             /* bra * */
    };
    QTestState *saved = qts;
    bool saved_ran = ran;
    bool local_ran = false;
    g_autofree char *regs = NULL;

    qts = isa_vm_new("coco3", "tcg,one-insn-per-tb=on");
    isa_load(qts, &local_ran, code, sizeof(code),
             NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(qts, "{'execute': 'cont'}");
    regs = wait_registers(qts, "PC=8002", CASE_TIMEOUT_MS);
    if (!strstr(regs, "PC=8002") || !strstr(regs, "A=11")) {
        g_test_message("invalid_indexed_87: did not trap\n%s", regs);
    }
    g_assert_nonnull(strstr(regs, "PC=8002"));
    g_assert_nonnull(strstr(regs, "A=11"));
    qtest_quit(qts);
    qts = saved;
    ran = saved_ran;
}

static QTestState *isa_vm_cpu(const char *cpu)
{
    return qtest_initf("-M coco3 -cpu %s -bios %s -accel tcg,one-insn-per-tb=on -S",
                       cpu, rom_path);
}

static void hd6309_isa_run(const IsaCase *c)
{
    QTestState *saved = qts;
    bool saved_ran = ran;

    qts = isa_vm_cpu("hd6309");
    ran = false;
    isa_run_case(c);
    qtest_quit(qts);
    qts = saved;
    ran = saved_ran;
}

static void hd6309_firq_run(const IsaCase *c)
{
    QTestState *saved = qts;
    bool saved_ran = ran;

    qts = qtest_initf(
        "-M coco3,fake-cart-firq=on -cpu hd6309 -bios %s "
        "-accel tcg,one-insn-per-tb=on -S", rom_path);
    ran = false;
    isa_run_case(c);
    qtest_quit(qts);
    qts = saved;
    ran = saved_ran;
}

/* lda #$5a; tfr a,w — 6309 duplicates A into both halves of W. */
static void test_hd6309_tfr_a_w(void)
{
    static const uint8_t code[] = {
        0x86, 0x5a,             /* lda #$5a */
        0x1f, 0x86,             /* tfr a,w */
        0x20, 0xfe,             /* bra * */
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code), NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8004", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8004"));
    g_assert_nonnull(strstr(regs, "E=5a"));
    g_assert_nonnull(strstr(regs, "F=5a"));
    g_assert_nonnull(strstr(regs, "W=5a5a"));
    qtest_quit(s);
}

/* ldx #$ffff; tfr 0,x — Zero reads 0, so X is cleared. */
static void test_hd6309_tfr_zero_x(void)
{
    static const uint8_t code[] = {
        0x8e, 0xff, 0xff,       /* ldx #$ffff */
        0x1f, 0xc1,             /* tfr 0,x */
        0x20, 0xfe,             /* bra * */
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code), NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8005", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8005"));
    g_assert_nonnull(strstr(regs, "X=0000"));
    qtest_quit(s);
}

/* Same TFR 0,X encoding on a 6809 must halt (undefined register code). */
static void test_m6809_tfr_zero_halts(void)
{
    static const uint8_t code[] = {
        0x8e, 0xff, 0xff,       /* ldx #$ffff */
        0x1f, 0xc1,             /* tfr 0,x (illegal on 6809) */
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("m6809");
    isa_load(s, &local_ran, code, sizeof(code), NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8003", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8003"));
    g_assert_nonnull(strstr(regs, "X=ffff"));
    qtest_quit(s);
}

/* lda #$02; tfr a,e; ldx #$80f0; lda e,x */
static void test_hd6309_index_e_x(void)
{
    static const uint8_t code[] = {
        0x86, 0x02,             /* lda #$02 */
        0x1f, 0x8e,             /* tfr a,e */
        0x8e, 0x80, 0xf0,       /* ldx #$80f0 */
        0xa6, 0x87,             /* lda e,x */
        0x20, 0xfe,
    };
    static const uint8_t operand[] = { 0x00, 0x00, 0x99 };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code),
             operand, sizeof(operand), NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8009", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8009"));
    g_assert_nonnull(strstr(regs, "A=99"));
    qtest_quit(s);
}

/* ldx #$80f0; tfr x,w; lda ,w */
static void test_hd6309_index_w(void)
{
    static const uint8_t code[] = {
        0x8e, 0x80, 0xf0,       /* ldx #$80f0 */
        0x1f, 0x16,             /* tfr x,w */
        0xa6, 0x8f,             /* lda ,w */
        0x20, 0xfe,
    };
    static const uint8_t operand[] = { 0x42 };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code),
             operand, sizeof(operand), NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8007", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8007"));
    g_assert_nonnull(strstr(regs, "A=42"));
    g_assert_nonnull(strstr(regs, "W=80f0"));
    qtest_quit(s);
}

/* lds #$4000; $01 (illegal) — trap to $FFF0, MD.IL set, 12-byte frame. */
static void test_hd6309_illegal_trap(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x01,                   /* illegal */
    };
    static const IsaLoad load[] = {
        { 0x8100, 0x20 },       /* bra * */
        { 0x8101, 0xfe },
    };
    static const IsaVec vec[] = {
        { 0xfff0, 0x8100 },
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, load, ARRAY_SIZE(load), vec, ARRAY_SIZE(vec), false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8100", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8100"));
    g_assert_nonnull(strstr(regs, "MD=40"));
    g_assert_nonnull(strstr(regs, "S=3ff4"));
    qtest_quit(s);
}

/* lds #$4000; swi — 6309 emulation still uses a 12-byte frame. */
static void test_hd6309_swi_emu(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x3f,                   /* swi */
    };
    static const IsaVec vec[] = { { 0xfffa, 0x8100 } };
    static const IsaLoad load[] = {
        { 0x8100, 0x20 },       /* bra * */
        { 0x8101, 0xfe },
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, load, ARRAY_SIZE(load), vec, ARRAY_SIZE(vec), false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8100", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8100"));
    g_assert_nonnull(strstr(regs, "S=3ff4"));
    g_assert_nonnull(strstr(regs, "NM=0"));
    qtest_quit(s);
}

/*
 * Native SWI: 14-byte frame PC,U,Y,X,DP,F,E,B,A,CC so memory is
 * CC,A,B,E,F,DP,X,Y,U,PC.
 */
static void test_hd6309_swi_native(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x11, 0x3d, 0x01,       /* ldmd #$01 */
        0x86, 0xaa,             /* lda #$aa */
        0x1f, 0x8e,             /* tfr a,e */
        0x86, 0xbb,             /* lda #$bb */
        0x1f, 0x8f,             /* tfr a,f */
        0x86, 0x12,             /* lda #$12 */
        0x1f, 0x8b,             /* tfr a,dp */
        0xc6, 0x34,             /* ldb #$34 */
        0x8e, 0x11, 0x11,       /* ldx #$1111 */
        0x10, 0x8e, 0x22, 0x22, /* ldy #$2222 */
        0xce, 0x33, 0x33,       /* ldu #$3333 */
        0x3f,                   /* swi at $801f; stacked PC = $8020 */
    };
    static const IsaLoad load[] = {
        { 0x8100, 0x20 },       /* bra * */
        { 0x8101, 0xfe },
    };
    static const IsaVec vec[] = { { 0xfffa, 0x8100 } };
    static const uint8_t frame[] = {
        0xd0, 0x12, 0x34, 0xaa, 0xbb, 0x12,
        0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x80, 0x20,
    };
    uint8_t got[sizeof(frame)];
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, load, ARRAY_SIZE(load), vec, ARRAY_SIZE(vec), false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8100", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8100"));
    g_assert_nonnull(strstr(regs, "S=3ff2"));
    g_assert_nonnull(strstr(regs, "E=aa"));
    g_assert_nonnull(strstr(regs, "F=bb"));
    g_assert_nonnull(strstr(regs, "MD=01"));
    g_assert_nonnull(strstr(regs, "NM=1"));
    qtest_memread(s, 0x3ff2, got, sizeof(got));
    g_assert_cmpint(memcmp(got, frame, sizeof(frame)), ==, 0);
    qtest_quit(s);
}

/* Native SWI then RTI restores W and S. */
static void test_hd6309_swi_rti_native(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x11, 0x3d, 0x01,       /* ldmd #$01 */
        0x86, 0xaa,             /* lda #$aa */
        0x1f, 0x8e,             /* tfr a,e */
        0x86, 0xbb,             /* lda #$bb */
        0x1f, 0x8f,             /* tfr a,f */
        0x86, 0x5a,             /* lda #$5a */
        0x8e, 0x11, 0x11,       /* ldx #$1111 */
        0x3f,                   /* swi */
    };
    static const uint8_t handler[] = {
        0x4c, 0xb7, 0x20, 0x00, 0x3b, /* inca; sta $2000; rti */
    };
    const IsaCase c = {
        .name = "hd6309_swi_rti_native",
        .disas = "ldmd #$01; lda/tfr e,f; lda #$5a; ldx #$1111; swi",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "A=5a", "X=1111", "S=4000", "E=aa", "F=bb", "MD=01", NULL
        },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfffa, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x2000, (const uint8_t[]){ 0x5b }, 1 } },
        .n_mem = 1,
    };

    hd6309_isa_run(&c);
}

/* Native CWAI + IRQ: 14-byte frame, then RTI + incb. */
static void test_hd6309_cwai_irq_native(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,             /* lds #$4000 */
        0x11, 0x3d, 0x01,                   /* ldmd #$01 */
        0x86, 0xaa, 0x1f, 0x8e,             /* lda #$aa; tfr a,e */
        0x86, 0xbb, 0x1f, 0x8f,             /* lda #$bb; tfr a,f */
        0x86, 0x20, 0xb7, 0xff, 0x90,       /* lda #$20; sta $ff90 */
        0x86, 0x08, 0xb7, 0xff, 0x92,       /* lda #$08; sta $ff92 */
        0x86, 0x12, 0x1f, 0x8b,             /* lda #$12; tfr a,dp */
        0xcc, 0x12, 0x34,                   /* ldd #$1234 */
        0x8e, 0x11, 0x11,                   /* ldx #$1111 */
        0x10, 0x8e, 0x22, 0x22,             /* ldy #$2222 */
        0xce, 0x33, 0x33,                   /* ldu #$3333 */
        0x3c, 0xaf,                         /* cwai #$af */
        0x5c,                               /* incb */
    };
    static const uint8_t handler[] = {
        0x7f, 0xff, 0x92, 0xb6, 0xff, 0x92, 0x3b,
    };
    static const uint8_t frame[] = {
        0x80, 0x12, 0x34, 0xaa, 0xbb, 0x12,
        0x11, 0x11, 0x22, 0x22, 0x33, 0x33, 0x80, 0x2c,
    };
    const IsaCase c = {
        .name = "hd6309_cwai_irq_native",
        .disas = "ldmd #$01; set e/f; enable irq; cwai #$af; incb",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "A=12", "B=35", "DP=12", "X=1111", "Y=2222",
            "U=3333", "S=4000", "E=aa", "F=bb", "CC=80", "MD=01", NULL
        },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfff8, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x3ff2, frame, sizeof(frame) } },
        .n_mem = 1,
    };

    hd6309_isa_run(&c);
}

/* Native mode, MD.FM=0: FIRQ still stacks only PC, CC. */
static void test_hd6309_firq_native_short(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,             /* lds #$4000 */
        0x11, 0x3d, 0x01,                   /* ldmd #$01 */
        0x86, 0x05, 0xb7, 0xff, 0x23,       /* lda #$05; sta $ff23 */
        0x86, 0x5a, 0x1c, 0xaf, 0x13,       /* lda #$5a; andcc #$af; sync */
        0x4c,                               /* inca */
    };
    static const uint8_t handler[] = {
        0x86, 0x04, 0xb7, 0xff, 0x23, 0xb6, 0xff, 0x22, 0x3b,
    };
    static const uint8_t frame[] = { 0x00, 0x80, 0x11 };
    const IsaCase c = {
        .name = "hd6309_firq_native_short",
        .disas = "ldmd #$01; enable firq; lda #$5a; andcc #$af; sync; inca",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "A=01", "S=4000", "CC=00", "MD=01", NULL },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfff6, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x3ffd, frame, sizeof(frame) } },
        .n_mem = 1,
    };

    hd6309_firq_run(&c);
}

/* MD.FM=1, emulation: FIRQ uses the 12-byte entire frame. */
static void test_hd6309_firq_fm_emu(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,             /* lds #$4000 */
        0x11, 0x3d, 0x02,                   /* ldmd #$02 */
        0x86, 0x05, 0xb7, 0xff, 0x23,       /* lda #$05; sta $ff23 */
        0x86, 0x5a, 0x1c, 0xaf, 0x13,       /* lda #$5a; andcc #$af; sync */
        0x4c,                               /* inca */
    };
    static const uint8_t handler[] = {
        0x86, 0x04, 0xb7, 0xff, 0x23, 0xb6, 0xff, 0x22, 0x3b,
    };
    static const uint8_t frame[] = {
        0x80, 0x5a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x80, 0x11,
    };
    const IsaCase c = {
        .name = "hd6309_firq_fm_emu",
        .disas = "ldmd #$02; enable firq; lda #$5a; andcc #$af; sync; inca",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "A=5b", "S=4000", "CC=80", "MD=02", NULL },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfff6, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x3ff4, frame, sizeof(frame) } },
        .n_mem = 1,
    };

    hd6309_firq_run(&c);
}

/* Native + MD.FM=1: FIRQ uses the 14-byte entire frame. */
static void test_hd6309_firq_fm_native(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,             /* lds #$4000 */
        0x11, 0x3d, 0x03,                   /* ldmd #$03 */
        0x86, 0xaa, 0x1f, 0x8e,             /* lda #$aa; tfr a,e */
        0x86, 0xbb, 0x1f, 0x8f,             /* lda #$bb; tfr a,f */
        0x86, 0x05, 0xb7, 0xff, 0x23,       /* lda #$05; sta $ff23 */
        0x86, 0x5a, 0x1c, 0xaf, 0x13,       /* lda #$5a; andcc #$af; sync */
        0x4c,                               /* inca */
    };
    static const uint8_t handler[] = {
        0x86, 0x04, 0xb7, 0xff, 0x23, 0xb6, 0xff, 0x22, 0x3b,
    };
    static const uint8_t frame[] = {
        0x80, 0x5a, 0x00, 0xaa, 0xbb, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x19,
    };
    const IsaCase c = {
        .name = "hd6309_firq_fm_native",
        .disas = "ldmd #$03; set e/f; enable firq; sync; inca",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "A=5b", "S=4000", "E=aa", "F=bb", "CC=80", "MD=03", NULL
        },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfff6, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x3ff2, frame, sizeof(frame) } },
        .n_mem = 1,
    };

    hd6309_firq_run(&c);
}

/* $113D is LDMD on a 6309; a 6809 must still halt. */
static void test_m6809_ldmd_halts(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x11, 0x3d, 0x01,       /* ldmd #$01 (illegal on 6809) */
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("m6809");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8004", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8004"));
    g_assert_nonnull(strstr(regs, "S=4000"));
    qtest_quit(s);
}

static void test_cwai_irq(void)
{
    /* lds #$4000; enable GIME IRQ; load regs; cwai #$af; incb */
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,
        0x86, 0x20, 0xb7, 0xff, 0x90,
        0x86, 0x08, 0xb7, 0xff, 0x92,
        0x86, 0x12, 0x1f, 0x8b,
        0xcc, 0x12, 0x34,
        0x8e, 0x11, 0x11,
        0x10, 0x8e, 0x22, 0x22,
        0xce, 0x33, 0x33,
        0x3c, 0xaf,
        0x5c,
    };
    static const uint8_t handler[] = {
        0x7f, 0xff, 0x92, 0xb6, 0xff, 0x92, 0x3b, /* clr $ff92; lda $ff92; rti */
    };
    static const uint8_t frame[] = {
        0x80, 0x12, 0x34, 0x12, 0x11, 0x11, 0x22, 0x22, 0x33, 0x33,
        0x80, 0x21,
    };
    const IsaCase c = {
        .name = "cwai_irq",
        .disas = "lds #$4000; enable irq; cwai #$af; incb",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "A=12", "B=35", "DP=12", "X=1111", "Y=2222",
            "U=3333", "S=4000", "CC=80", NULL
        },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfff8, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x3ff4, frame, sizeof(frame) } },
        .n_mem = 1,
    };

    isa_run_case(&c);
}

static void test_firq_short_frame(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,
        0x86, 0x05, 0xb7, 0xff, 0x23, /* lda #$05; sta $ff23  PIA1 CRB */
        0x86, 0x5a, 0x1c, 0xaf, 0x13, /* lda #$5a; andcc #$af; sync */
        0x4c,                         /* inca after FIRQ return */
    };
    static const uint8_t handler[] = {
        0x86, 0x04, 0xb7, 0xff, 0x23, 0xb6, 0xff, 0x22, 0x3b,
    };
    static const uint8_t frame[] = { 0x00, 0x80, 0x0e };
    QTestState *saved = qts;
    bool saved_ran = ran;
    const IsaCase c = {
        .name = "firq_short_frame",
        .disas = "lds #$4000; enable firq; lda #$5a; andcc #$af; sync; inca",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "A=01", "S=4000", "CC=00", NULL },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfff6, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x3ffd, frame, sizeof(frame) } },
        .n_mem = 1,
    };

    /* DEFINE_PROP_BOOL is frozen after realize; set it on -M instead. */
    qts = isa_vm_new("coco3,fake-cart-firq=on", "tcg,one-insn-per-tb=on");
    ran = false;
    isa_run_case(&c);
    qtest_quit(qts);
    qts = saved;
    ran = saved_ran;
}

static void test_sync_masked(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,
        0x86, 0x20, 0xb7, 0xff, 0x90,
        0x86, 0x08, 0xb7, 0xff, 0x92,
        0x86, 0x22, 0x13, 0x5c, /* lda #$22; sync; incb (I still set) */
    };
    const IsaCase c = {
        .name = "sync_masked_falls_through",
        .disas = "lds #$4000; enable irq; lda #$22; sync; incb",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "A=22", "B=01", "S=4000", "CC=50", NULL
        },
    };

    isa_run_case(&c);
}

static void test_irq_after_andcc(void)
{
    /* No one-insn-per-tb: ANDCC must exit the TB to take the latched IRQ. */
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00,
        0x5f,                             /* clrb */
        0x86, 0x20, 0xb7, 0xff, 0x90,
        0x86, 0x08, 0xb7, 0xff, 0x92,
        0x13, 0x1c, 0xaf,                 /* sync; andcc #$af */
        0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
        0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
        0x5c, 0x5c, 0x5c, 0x5c, 0x5c,
        0x5c, 0x5c, 0x5c, 0x5c, 0x5c,     /* incb * 20 */
    };
    static const uint8_t handler[] = {
        0xf7, 0x20, 0x00,                 /* stb $2000 */
        0x7f, 0xff, 0x92, 0xb6, 0xff, 0x92, 0x3b,
    };
    static const uint8_t mem0[] = { 0x00 };
    QTestState *saved = qts;
    bool saved_ran = ran;
    const IsaCase c = {
        .name = "irq_after_andcc",
        .disas = "lds #$4000; clrb; enable irq; sync; andcc #$af; incb *20",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "B=14", "CC=80", NULL },
        .operand = handler,
        .operand_len = sizeof(handler),
        .vec = (const IsaVec[]){ { 0xfff8, ROM_BASE + OPERAND_OFFSET } },
        .n_vec = 1,
        .mem = (const IsaMem[]){ { 0x2000, mem0, 1 } },
        .n_mem = 1,
    };

    qts = isa_vm_new("coco3", "tcg");
    ran = false;
    isa_run_case(&c);
    qtest_quit(qts);
    qts = saved;
    ran = saved_ran;
}

int main(int argc, char **argv)
{
    int i, ret;

    g_test_init(&argc, &argv, NULL);

    rom_path = write_stub_rom();
    qts = isa_vm_new("coco3", "tcg,one-insn-per-tb=on");
    ran = false;

    for (i = 0; i < ARRAY_SIZE(isa_cases); i++) {
        char *path = g_strdup_printf("/isa/%s", isa_cases[i].name);
        qtest_add_data_func(path, &isa_cases[i], test_isa_case);
        g_free(path);
    }
    qtest_add_func("/isa/invalid_indexed_87", test_invalid_indexed);
    qtest_add_func("/isa/hd6309_tfr_a_w", test_hd6309_tfr_a_w);
    qtest_add_func("/isa/hd6309_tfr_zero_x", test_hd6309_tfr_zero_x);
    qtest_add_func("/isa/m6809_tfr_zero_halts", test_m6809_tfr_zero_halts);
    qtest_add_func("/isa/hd6309_index_e_x", test_hd6309_index_e_x);
    qtest_add_func("/isa/hd6309_index_w", test_hd6309_index_w);
    qtest_add_func("/isa/hd6309_illegal_trap", test_hd6309_illegal_trap);
    qtest_add_func("/isa/hd6309_swi_emu", test_hd6309_swi_emu);
    qtest_add_func("/isa/hd6309_swi_native", test_hd6309_swi_native);
    qtest_add_func("/isa/hd6309_swi_rti_native", test_hd6309_swi_rti_native);
    qtest_add_func("/isa/hd6309_cwai_irq_native", test_hd6309_cwai_irq_native);
    qtest_add_func("/isa/hd6309_firq_native_short", test_hd6309_firq_native_short);
    qtest_add_func("/isa/hd6309_firq_fm_emu", test_hd6309_firq_fm_emu);
    qtest_add_func("/isa/hd6309_firq_fm_native", test_hd6309_firq_fm_native);
    qtest_add_func("/isa/m6809_ldmd_halts", test_m6809_ldmd_halts);
    qtest_add_func("/isa/cwai_irq", test_cwai_irq);
    qtest_add_func("/isa/firq_short_frame", test_firq_short_frame);
    qtest_add_func("/isa/sync_masked_falls_through", test_sync_masked);
    qtest_add_func("/isa/irq_after_andcc", test_irq_after_andcc);

    ret = g_test_run();

    qtest_quit(qts);
    unlink(rom_path);
    g_free(rom_path);
    return ret;
}
