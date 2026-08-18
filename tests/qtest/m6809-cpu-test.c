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
static QTestState *qts_hd6309;
static QTestState *qts_turbo9;
static char *rom_path;
static bool ran;
static bool ran_hd6309;
static bool ran_turbo9;

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

/* Same 6809 programs on -cpu hd6309 (emulation mode, no 6309 opcodes). */
static void test_isa_case_hd6309(const void *data)
{
    QTestState *saved = qts;
    bool saved_ran = ran;

    qts = qts_hd6309;
    ran = ran_hd6309;
    isa_run_case(data);
    ran_hd6309 = ran;
    qts = saved;
    ran = saved_ran;
}

/* Same 6809 programs on -cpu turbo9 (including 8→16 TFR fill of $FF). */
static void test_isa_case_turbo9(const void *data)
{
    QTestState *saved = qts;
    bool saved_ran = ran;

    qts = qts_turbo9;
    ran = ran_turbo9;
    isa_run_case(data);
    ran_turbo9 = ran;
    qts = saved;
    ran = saved_ran;
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

static void isa_run_on_cpu(const char *cpu, const IsaCase *c)
{
    QTestState *saved = qts;
    bool saved_ran = ran;

    qts = isa_vm_cpu(cpu);
    ran = false;
    isa_run_case(c);
    qtest_quit(qts);
    qts = saved;
    ran = saved_ran;
}

static void hd6309_isa_run(const IsaCase *c)
{
    isa_run_on_cpu("hd6309", c);
}

static void turbo9_isa_run(const IsaCase *c)
{
    isa_run_on_cpu("turbo9", c);
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

/* ldx #$1234; tfr x,v; tfr v,y */
static void test_hd6309_tfr_v(void)
{
    static const uint8_t code[] = {
        0x8e, 0x12, 0x34,       /* ldx #$1234 */
        0x1f, 0x17,             /* tfr x,v */
        0x1f, 0x72,             /* tfr v,y */
    };
    const IsaCase c = {
        .name = "hd6309_tfr_v",
        .disas = "ldx #$1234; tfr x,v; tfr v,y",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "X=1234", "V=1234", "Y=1234", NULL },
    };

    hd6309_isa_run(&c);
}

/* lda #$ab; tfr a,e; lda #$cd; tfr a,f */
static void test_hd6309_tfr_e_f(void)
{
    static const uint8_t code[] = {
        0x86, 0xab,             /* lda #$ab */
        0x1f, 0x8e,             /* tfr a,e */
        0x86, 0xcd,             /* lda #$cd */
        0x1f, 0x8f,             /* tfr a,f */
    };
    const IsaCase c = {
        .name = "hd6309_tfr_e_f",
        .disas = "lda #$ab; tfr a,e; lda #$cd; tfr a,f",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "A=cd", "E=ab", "F=cd", "W=abcd", NULL
        },
    };

    hd6309_isa_run(&c);
}

/* ldd #$1234; ldw #$abcd; exg d,w */
static void test_hd6309_exg_d_w(void)
{
    static const uint8_t code[] = {
        0xcc, 0x12, 0x34,       /* ldd #$1234 */
        0x10, 0x86, 0xab, 0xcd, /* ldw #$abcd */
        0x1e, 0x06,             /* exg d,w */
    };
    const IsaCase c = {
        .name = "hd6309_exg_d_w",
        .disas = "ldd #$1234; ldw #$abcd; exg d,w",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "D=abcd", "W=1234", NULL },
    };

    hd6309_isa_run(&c);
}

/* 6309 8→16 TFR duplicates the byte (6809 fills $FF; see tfr_a_x). */
static void test_hd6309_tfr_a_x(void)
{
    static const uint8_t code[] = {
        0x86, 0x5a,             /* lda #$5a */
        0x1f, 0x81,             /* tfr a,x */
    };
    const IsaCase c = {
        .name = "hd6309_tfr_a_x",
        .disas = "lda #$5a; tfr a,x",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "A=5a", "X=5a5a", NULL },
    };

    hd6309_isa_run(&c);
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

/* tfr a,w is 6309-only; a 6809 must halt on register code 6. */
static void test_m6809_tfr_w_halts(void)
{
    static const uint8_t code[] = {
        0x86, 0x5a,             /* lda #$5a */
        0x1f, 0x86,             /* tfr a,w (illegal on 6809) */
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("m6809");
    isa_load(s, &local_ran, code, sizeof(code), NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8002", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8002"));
    g_assert_nonnull(strstr(regs, "A=5a"));
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

/* lda ,w is 6309-only; a 6809 must halt on postbyte $8F. */
static void test_m6809_index_w_halts(void)
{
    static const uint8_t code[] = {
        0x86, 0x11,             /* lda #$11 */
        0xa6, 0x8f,             /* lda ,w (illegal on 6809) */
        0x86, 0x22,
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("m6809");
    isa_load(s, &local_ran, code, sizeof(code), NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8002", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8002"));
    g_assert_nonnull(strstr(regs, "A=11"));
    qtest_quit(s);
}

/* lds #$4000; $15 (illegal) — trap to $FFF0, MD.IL set, 12-byte frame. */
static void test_hd6309_illegal_trap(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x15,                   /* still illegal on 6309 */
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

static void test_hd6309_ldmd_bitmd(void)
{
    static const uint8_t code[] = {
        0x11, 0x3d, 0x01,       /* ldmd #$01 */
        0x11, 0x3c, 0xc0,       /* bitmd #$c0 — IL/DZ clear, so Z */
    };
    const IsaCase c = {
        .name = "hd6309_ldmd_bitmd",
        .disas = "ldmd #$01; bitmd #$c0",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "MD=01", "NM=1", NULL },
    };

    hd6309_isa_run(&c);
}

/*
 * Illegal sets MD.IL. BITMD #$80 must leave IL (DZ was clear → Z).
 * BITMD #$40 then sees IL (Z clear) and clears it.
 */
static void test_hd6309_bitmd_clears(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x15,                   /* illegal */
    };
    static const uint8_t handler[] = {
        0x11, 0x3c, 0x80,       /* bitmd #$80 */
        0x11, 0x3c, 0x40,       /* bitmd #$40 */
        0x20, 0xfe,             /* bra * */
    };
    static const IsaVec vec[] = {
        { 0xfff0, ROM_BASE + OPERAND_OFFSET },
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("hd6309");
    isa_load(s, &local_ran, code, sizeof(code),
             handler, sizeof(handler), NULL, 0, vec, ARRAY_SIZE(vec), false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=80f6", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=80f6"));
    g_assert_nonnull(strstr(regs, "MD=00"));
    g_assert_nonnull(strstr(regs, "CC=d0"));
    qtest_quit(s);
}

static void test_hd6309_aim(void)
{
    static const uint8_t code[] = {
        0x02, 0x0f, 0x20,       /* aim #$0f,<$20 */
    };
    static const uint8_t mem[] = { 0x0f };
    const IsaCase c = {
        .name = "hd6309_aim",
        .disas = "aim #$0f,<$20",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ NULL },
        .load = (const IsaLoad[]){ { 0x0020, 0xff } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x0020, mem, 1 } },
        .n_mem = 1,
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_oim(void)
{
    static const uint8_t code[] = {
        0x01, 0xf0, 0x20,       /* oim #$f0,<$20 */
    };
    static const uint8_t mem[] = { 0xff };
    const IsaCase c = {
        .name = "hd6309_oim",
        .disas = "oim #$f0,<$20",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ NULL },
        .load = (const IsaLoad[]){ { 0x0020, 0x0f } },
        .n_load = 1,
        .mem = (const IsaMem[]){ { 0x0020, mem, 1 } },
        .n_mem = 1,
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_ldq(void)
{
    static const uint8_t code[] = {
        0xcd, 0x11, 0x22, 0x33, 0x44, /* ldq #$11223344 */
    };
    const IsaCase c = {
        .name = "hd6309_ldq",
        .disas = "ldq #$11223344",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "D=1122", "W=3344", "E=33", "F=44", NULL
        },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_lde(void)
{
    static const uint8_t code[] = {
        0x11, 0x86, 0xab,       /* lde #$ab */
    };
    const IsaCase c = {
        .name = "hd6309_lde",
        .disas = "lde #$ab",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "E=ab", "F=00", "W=ab00", NULL },
    };

    hd6309_isa_run(&c);
}

/* lde #$50; cmpe #$50 — Z set, E unchanged. Reset CC is I|F. */
static void test_hd6309_cmpe(void)
{
    static const uint8_t code[] = {
        0x11, 0x86, 0x50,       /* lde #$50 */
        0x11, 0x81, 0x50,       /* cmpe #$50 */
    };
    const IsaCase c = {
        .name = "hd6309_cmpe",
        .disas = "lde #$50; cmpe #$50",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "E=50", "CC=54", NULL },
    };

    hd6309_isa_run(&c);
}

/* Krn uses CMPE indexed ($11A1). */
static void test_hd6309_cmpe_idx(void)
{
    static const uint8_t code[] = {
        0x11, 0x86, 0x99,       /* lde #$99 */
        0x8e, 0x80, 0xf0,       /* ldx #$80f0 */
        0x11, 0xa1, 0x84,       /* cmpe ,x */
    };
    static const uint8_t operand[] = { 0x99 };
    const IsaCase c = {
        .name = "hd6309_cmpe_idx",
        .disas = "lde #$99; ldx #$80f0; cmpe ,x",
        .code = code,
        .code_len = sizeof(code),
        .operand = operand,
        .operand_len = sizeof(operand),
        .regs = (const char *const[]){ "E=99", "X=80f0", "CC=54", NULL },
    };

    hd6309_isa_run(&c);
}

/* ldd #$ffff; andd #$0f0f */
static void test_hd6309_andd(void)
{
    static const uint8_t code[] = {
        0xcc, 0xff, 0xff,       /* ldd #$ffff */
        0x10, 0x84, 0x0f, 0x0f, /* andd #$0f0f */
    };
    const IsaCase c = {
        .name = "hd6309_andd",
        .disas = "ldd #$ffff; andd #$0f0f",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "D=0f0f", NULL },
    };

    hd6309_isa_run(&c);
}

/* ldw #$0001; addw #$0002 */
static void test_hd6309_addw(void)
{
    static const uint8_t code[] = {
        0x10, 0x86, 0x00, 0x01, /* ldw #$0001 */
        0x10, 0x8b, 0x00, 0x02, /* addw #$0002 */
    };
    const IsaCase c = {
        .name = "hd6309_addw",
        .disas = "ldw #$0001; addw #$0002",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "W=0003", "E=00", "F=03", NULL },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_sexw(void)
{
    static const uint8_t code[] = {
        0x10, 0x86, 0x80, 0x00, /* ldw #$8000 */
        0x14,                   /* sexw */
    };
    const IsaCase c = {
        .name = "hd6309_sexw",
        .disas = "ldw #$8000; sexw",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "D=ffff", "W=8000", NULL },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_clrd(void)
{
    static const uint8_t code[] = {
        0xcc, 0x12, 0x34,       /* ldd #$1234 */
        0x10, 0x4f,             /* clrd */
    };
    const IsaCase c = {
        .name = "hd6309_clrd",
        .disas = "ldd #$1234; clrd",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "D=0000", "A=00", "B=00", NULL },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_addr(void)
{
    static const uint8_t code[] = {
        0xcc, 0x00, 0x01,       /* ldd #$0001 */
        0x8e, 0x00, 0x02,       /* ldx #$0002 */
        0x10, 0x30, 0x01,       /* addr d,x */
    };
    const IsaCase c = {
        .name = "hd6309_addr",
        .disas = "ldd #$0001; ldx #$0002; addr d,x",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "X=0003", "D=0001", NULL },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_muld_divd(void)
{
    static const uint8_t code[] = {
        0xcc, 0x00, 0x03,       /* ldd #$0003 */
        0x11, 0x8f, 0x00, 0x04, /* muld #$0004  Q=12 */
        0xcc, 0x00, 0x0a,       /* ldd #$000a */
        0x11, 0x8d, 0x02,       /* divd #$02    B=5 A=0 */
    };
    const IsaCase c = {
        .name = "hd6309_muld_divd",
        .disas = "ldd #3; muld #4; ldd #10; divd #2",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "A=00", "B=05", NULL },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_divq(void)
{
    static const uint8_t code[] = {
        0xcd, 0x00, 0x00, 0x00, 0x0a, /* ldq #$0000000a */
        0x11, 0x8e, 0x00, 0x02,       /* divq #$0002 */
    };
    const IsaCase c = {
        .name = "hd6309_divq",
        .disas = "ldq #10; divq #2",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "D=0000", "W=0005", NULL },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_pshsw(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x10, 0x86, 0xaa, 0xbb, /* ldw #$aabb */
        0x10, 0x38,             /* pshsw */
    };
    static const uint8_t frame[] = { 0xaa, 0xbb };
    const IsaCase c = {
        .name = "hd6309_pshsw",
        .disas = "lds #$4000; ldw #$aabb; pshsw",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "S=3ffe", "W=aabb", NULL },
        .mem = (const IsaMem[]){ { 0x3ffe, frame, sizeof(frame) } },
        .n_mem = 1,
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_pulsw(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x10, 0x86, 0xaa, 0xbb, /* ldw #$aabb */
        0x10, 0x38,             /* pshsw */
        0x10, 0x86, 0x00, 0x00, /* ldw #$0000 */
        0x10, 0x39,             /* pulsw */
    };
    const IsaCase c = {
        .name = "hd6309_pulsw",
        .disas = "lds #$4000; ldw #$aabb; pshsw; ldw #0; pulsw",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "S=4000", "W=aabb", NULL },
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_tfm(void)
{
    static const uint8_t code[] = {
        0x10, 0x86, 0x00, 0x02, /* ldw #$0002 */
        0x8e, 0x20, 0x00,       /* ldx #$2000 */
        0x10, 0x8e, 0x20, 0x10, /* ldy #$2010 */
        0x11, 0x38, 0x12,       /* tfm x+,y+ */
    };
    static const uint8_t dest[] = { 0x11, 0x22 };
    const IsaCase c = {
        .name = "hd6309_tfm",
        .disas = "ldw #2; ldx #$2000; ldy #$2010; tfm x+,y+",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "W=0000", "X=2002", "Y=2012", NULL
        },
        .load = (const IsaLoad[]){
            { 0x2000, 0x11 }, { 0x2001, 0x22 }
        },
        .n_load = 2,
        .mem = (const IsaMem[]){ { 0x2010, dest, sizeof(dest) } },
        .n_mem = 1,
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_band(void)
{
    /* BAND A,5,1,$40 — A bit1 AND mem$40 bit5. A=$0F, mem=$C6 -> A=$0D */
    static const uint8_t code[] = {
        0x86, 0x0f,             /* lda #$0f */
        0x11, 0x30, 0x69, 0x40, /* band a,5,1,$40 */
    };
    const IsaCase c = {
        .name = "hd6309_band",
        .disas = "lda #$0f; band a,5,1,$40",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "A=0d", NULL },
        .load = (const IsaLoad[]){ { 0x0040, 0xc6 } },
        .n_load = 1,
    };

    hd6309_isa_run(&c);
}

static void test_hd6309_div0(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0xcc, 0x00, 0x0a,       /* ldd #$000a */
        0x11, 0x8d, 0x00,       /* divd #0 */
    };
    static const IsaLoad load[] = {
        { 0x8100, 0x20 },
        { 0x8101, 0xfe },
    };
    static const IsaVec vec[] = {
        { 0xffee, 0x8100 },
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
    g_assert_nonnull(strstr(regs, "MD=80"));
    qtest_quit(s);
}

/* $14 is SEXW on a 6309; a 6809 must still halt. */
static void test_m6809_sexw_halts(void)
{
    static const uint8_t code[] = {
        0x14,
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("m6809");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8000", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8000"));
    qtest_quit(s);
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

/* CPU12 EMUL: D=$FA34, Y=$012B → Y:D=$01243ABC. */
static void test_turbo9_emul(void)
{
    static const uint8_t code[] = {
        0xcc, 0xfa, 0x34,       /* ldd #$fa34 */
        0x10, 0x8e, 0x01, 0x2b, /* ldy #$012b */
        0x14,                   /* emul */
    };
    const IsaCase c = {
        .name = "turbo9_emul",
        .disas = "ldd #$fa34; ldy #$012b; emul",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "Y=0124", "D=3abc", NULL },
    };

    turbo9_isa_run(&c);
}

/* Signed: D=$FA34 (-1484), Y=$012B (299) → Y:D=$FFF93ABC. */
static void test_turbo9_emuls(void)
{
    static const uint8_t code[] = {
        0xcc, 0xfa, 0x34,       /* ldd #$fa34 */
        0x10, 0x8e, 0x01, 0x2b, /* ldy #$012b */
        0x15,                   /* emuls */
    };
    const IsaCase c = {
        .name = "turbo9_emuls",
        .disas = "ldd #$fa34; ldy #$012b; emuls",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "Y=fff9", "D=3abc", NULL },
    };

    turbo9_isa_run(&c);
}

static void test_turbo9_idiv(void)
{
    static const uint8_t code[] = {
        0xcc, 0x00, 0x0a,       /* ldd #10 */
        0x8e, 0x00, 0x03,       /* ldx #3 */
        0x18,                   /* idiv */
    };
    const IsaCase c = {
        .name = "turbo9_idiv",
        .disas = "ldd #10; ldx #3; idiv",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "X=0003", "D=0001", NULL },
    };

    turbo9_isa_run(&c);
}

static void test_turbo9_idivs(void)
{
    static const uint8_t code[] = {
        0xcc, 0x00, 0x0c,       /* ldd #12 */
        0x8e, 0x00, 0x05,       /* ldx #5 */
        0x10, 0x18,             /* idivs */
    };
    const IsaCase c = {
        .name = "turbo9_idivs",
        .disas = "ldd #12; ldx #5; idivs",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "X=0002", "D=0002", NULL },
    };

    turbo9_isa_run(&c);
}

static void test_turbo9_ediv(void)
{
    static const uint8_t code[] = {
        0x10, 0x8e, 0x00, 0x00, /* ldy #0 */
        0xcc, 0x00, 0x64,       /* ldd #100 */
        0x8e, 0x00, 0x05,       /* ldx #5 */
        0x10, 0x14,             /* ediv */
    };
    const IsaCase c = {
        .name = "turbo9_ediv",
        .disas = "ldy #0; ldd #100; ldx #5; ediv",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "Y=0014", "D=0000", NULL },
    };

    turbo9_isa_run(&c);
}

/* CPU12 EDIVS: Y:D=$FFF502EA, X=$0653 → Y=$FE43, D=$0131. */
static void test_turbo9_edivs(void)
{
    static const uint8_t code[] = {
        0x10, 0x8e, 0xff, 0xf5, /* ldy #$fff5 */
        0xcc, 0x02, 0xea,       /* ldd #$02ea */
        0x8e, 0x06, 0x53,       /* ldx #$0653 */
        0x10, 0x15,             /* edivs */
    };
    const IsaCase c = {
        .name = "turbo9_edivs",
        .disas = "ldy #$fff5; ldd #$02ea; ldx #$0653; edivs",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "Y=fe43", "D=0131", NULL },
    };

    turbo9_isa_run(&c);
}

/* FDIV 1/2 → 0.5 = $8000, rem 0. */
static void test_turbo9_fdiv(void)
{
    static const uint8_t code[] = {
        0xcc, 0x00, 0x01,       /* ldd #1 */
        0x8e, 0x00, 0x02,       /* ldx #2 */
        0x10, 0x19,             /* fdiv */
    };
    const IsaCase c = {
        .name = "turbo9_fdiv",
        .disas = "ldd #1; ldx #2; fdiv",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "X=8000", "D=0000", NULL },
    };

    turbo9_isa_run(&c);
}

/*
 * IDIV by 0: C set, X=$FFFF, D unchanged, PC advances. $FFEE is poisoned so a
 * 6309-style trap would miss the spin PC.
 */
static void test_turbo9_idiv_div0(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0xcc, 0x00, 0x0a,       /* ldd #10 */
        0x8e, 0x00, 0x00,       /* ldx #0 */
        0x18,                   /* idiv */
    };
    const IsaCase c = {
        .name = "turbo9_idiv_div0",
        .disas = "lds #$4000; ldd #10; ldx #0; idiv",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){
            "X=ffff", "D=000a", "S=4000", "CC=51", NULL
        },
        .vec = (const IsaVec[]){ { 0xffee, 0x8100 } },
        .n_vec = 1,
    };

    turbo9_isa_run(&c);
}

static void test_turbo9_ediv_overflow(void)
{
    static const uint8_t code[] = {
        0x10, 0x8e, 0xff, 0xff, /* ldy #$ffff */
        0xcc, 0xff, 0xff,       /* ldd #$ffff */
        0x8e, 0x00, 0x01,       /* ldx #1 */
        0x10, 0x14,             /* ediv */
    };
    const IsaCase c = {
        .name = "turbo9_ediv_overflow",
        .disas = "ldy #$ffff; ldd #$ffff; ldx #1; ediv",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "Y=ffff", "D=ffff", "CC=52", NULL },
    };

    turbo9_isa_run(&c);
}

static void test_turbo9_edivs_overflow(void)
{
    static const uint8_t code[] = {
        0x10, 0x8e, 0x7f, 0xff, /* ldy #$7fff */
        0xcc, 0xff, 0xff,       /* ldd #$ffff */
        0x8e, 0x00, 0x01,       /* ldx #1 */
        0x10, 0x15,             /* edivs */
    };
    const IsaCase c = {
        .name = "turbo9_edivs_overflow",
        .disas = "ldy #$7fff; ldd #$ffff; ldx #1; edivs",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "Y=7fff", "D=ffff", "CC=52", NULL },
    };

    turbo9_isa_run(&c);
}

/* $8000 / $FFFF does not fit signed 16; X and D unchanged. */
static void test_turbo9_idivs_overflow(void)
{
    static const uint8_t code[] = {
        0xcc, 0x80, 0x00,       /* ldd #$8000 */
        0x8e, 0xff, 0xff,       /* ldx #$ffff */
        0x10, 0x18,             /* idivs */
    };
    const IsaCase c = {
        .name = "turbo9_idivs_overflow",
        .disas = "ldd #$8000; ldx #$ffff; idivs",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "X=ffff", "D=8000", "CC=52", NULL },
    };

    turbo9_isa_run(&c);
}

/* FDIV overflow when D >= X: X=$FFFF, D unchanged, V set. */
static void test_turbo9_fdiv_overflow(void)
{
    static const uint8_t code[] = {
        0xcc, 0x00, 0x05,       /* ldd #5 */
        0x8e, 0x00, 0x03,       /* ldx #3 */
        0x10, 0x19,             /* fdiv */
    };
    const IsaCase c = {
        .name = "turbo9_fdiv_overflow",
        .disas = "ldd #5; ldx #3; fdiv",
        .code = code,
        .code_len = sizeof(code),
        .regs = (const char *const[]){ "X=ffff", "D=0005", "CC=52", NULL },
    };

    turbo9_isa_run(&c);
}

/* $15 is EMULS on Turbo9; a 6809 must still halt. */
static void test_m6809_emuls_halts(void)
{
    static const uint8_t code[] = {
        0x15,
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("m6809");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8000", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8000"));
    qtest_quit(s);
}

/*
 * 6309 ldw #$8000; sexw on turbo9: ldw is illegal, so halt at $8000.
 * Must not produce the 6309 SEXW result D=$FFFF.
 */
static void test_turbo9_ldw_sexw_not_6309(void)
{
    static const uint8_t code[] = {
        0x10, 0x86, 0x80, 0x00, /* ldw #$8000 */
        0x14,                   /* sexw on 6309 / emul on turbo9 */
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("turbo9");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8000", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8000"));
    g_assert_nonnull(strstr(regs, "D=0000"));
    g_assert_null(strstr(regs, "D=ffff"));
    g_assert_null(strstr(regs, "W="));
    qtest_quit(s);
}

/* $113D is LDMD on a 6309; Turbo9 must halt, not trap. Dump stays 6809-shaped. */
static void test_turbo9_ldmd_halts(void)
{
    static const uint8_t code[] = {
        0x10, 0xce, 0x40, 0x00, /* lds #$4000 */
        0x11, 0x3d, 0x01,       /* ldmd #$01 (illegal on turbo9) */
        0x20, 0xfe,
    };
    QTestState *s;
    g_autofree char *regs = NULL;
    bool local_ran = false;

    s = isa_vm_cpu("turbo9");
    isa_load(s, &local_ran, code, sizeof(code),
             NULL, 0, NULL, 0, NULL, 0, false);
    qtest_qmp_assert_success(s, "{'execute': 'cont'}");
    regs = wait_registers(s, "PC=8004", CASE_TIMEOUT_MS);
    g_assert_nonnull(strstr(regs, "PC=8004"));
    g_assert_nonnull(strstr(regs, "S=4000"));
    g_assert_null(strstr(regs, "MD="));
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
    qts_hd6309 = isa_vm_cpu("hd6309");
    qts_turbo9 = isa_vm_cpu("turbo9");
    ran = false;
    ran_hd6309 = false;
    ran_turbo9 = false;

    for (i = 0; i < ARRAY_SIZE(isa_cases); i++) {
        char *path = g_strdup_printf("/isa/%s", isa_cases[i].name);
        qtest_add_data_func(path, &isa_cases[i], test_isa_case);
        g_free(path);
    }
    for (i = 0; i < ARRAY_SIZE(isa_cases); i++) {
        char *path;

        /* 6309 8→16 TFR duplicates the byte; 6809 fills $FF. */
        if (strcmp(isa_cases[i].name, "tfr_a_x") == 0) {
            continue;
        }
        path = g_strdup_printf("/isa/hd6309_compat/%s", isa_cases[i].name);
        qtest_add_data_func(path, &isa_cases[i], test_isa_case_hd6309);
        g_free(path);
    }
    for (i = 0; i < ARRAY_SIZE(isa_cases); i++) {
        char *path = g_strdup_printf("/isa/turbo9_compat/%s", isa_cases[i].name);

        qtest_add_data_func(path, &isa_cases[i], test_isa_case_turbo9);
        g_free(path);
    }
    qtest_add_func("/isa/invalid_indexed_87", test_invalid_indexed);
    qtest_add_func("/isa/hd6309_tfr_a_w", test_hd6309_tfr_a_w);
    qtest_add_func("/isa/hd6309_tfr_a_x", test_hd6309_tfr_a_x);
    qtest_add_func("/isa/hd6309_tfr_v", test_hd6309_tfr_v);
    qtest_add_func("/isa/hd6309_tfr_e_f", test_hd6309_tfr_e_f);
    qtest_add_func("/isa/hd6309_exg_d_w", test_hd6309_exg_d_w);
    qtest_add_func("/isa/hd6309_tfr_zero_x", test_hd6309_tfr_zero_x);
    qtest_add_func("/isa/m6809_tfr_zero_halts", test_m6809_tfr_zero_halts);
    qtest_add_func("/isa/m6809_tfr_w_halts", test_m6809_tfr_w_halts);
    qtest_add_func("/isa/hd6309_index_e_x", test_hd6309_index_e_x);
    qtest_add_func("/isa/hd6309_index_w", test_hd6309_index_w);
    qtest_add_func("/isa/m6809_index_w_halts", test_m6809_index_w_halts);
    qtest_add_func("/isa/hd6309_illegal_trap", test_hd6309_illegal_trap);
    qtest_add_func("/isa/hd6309_swi_emu", test_hd6309_swi_emu);
    qtest_add_func("/isa/hd6309_swi_native", test_hd6309_swi_native);
    qtest_add_func("/isa/hd6309_swi_rti_native", test_hd6309_swi_rti_native);
    qtest_add_func("/isa/hd6309_cwai_irq_native", test_hd6309_cwai_irq_native);
    qtest_add_func("/isa/hd6309_firq_native_short", test_hd6309_firq_native_short);
    qtest_add_func("/isa/hd6309_firq_fm_emu", test_hd6309_firq_fm_emu);
    qtest_add_func("/isa/hd6309_firq_fm_native", test_hd6309_firq_fm_native);
    qtest_add_func("/isa/hd6309_ldmd_bitmd", test_hd6309_ldmd_bitmd);
    qtest_add_func("/isa/hd6309_bitmd_clears", test_hd6309_bitmd_clears);
    qtest_add_func("/isa/hd6309_aim", test_hd6309_aim);
    qtest_add_func("/isa/hd6309_oim", test_hd6309_oim);
    qtest_add_func("/isa/hd6309_ldq", test_hd6309_ldq);
    qtest_add_func("/isa/hd6309_lde", test_hd6309_lde);
    qtest_add_func("/isa/hd6309_cmpe", test_hd6309_cmpe);
    qtest_add_func("/isa/hd6309_cmpe_idx", test_hd6309_cmpe_idx);
    qtest_add_func("/isa/hd6309_andd", test_hd6309_andd);
    qtest_add_func("/isa/hd6309_addw", test_hd6309_addw);
    qtest_add_func("/isa/hd6309_sexw", test_hd6309_sexw);
    qtest_add_func("/isa/hd6309_clrd", test_hd6309_clrd);
    qtest_add_func("/isa/hd6309_addr", test_hd6309_addr);
    qtest_add_func("/isa/hd6309_muld_divd", test_hd6309_muld_divd);
    qtest_add_func("/isa/hd6309_divq", test_hd6309_divq);
    qtest_add_func("/isa/hd6309_pshsw", test_hd6309_pshsw);
    qtest_add_func("/isa/hd6309_pulsw", test_hd6309_pulsw);
    qtest_add_func("/isa/hd6309_tfm", test_hd6309_tfm);
    qtest_add_func("/isa/hd6309_band", test_hd6309_band);
    qtest_add_func("/isa/hd6309_div0", test_hd6309_div0);
    qtest_add_func("/isa/m6809_sexw_halts", test_m6809_sexw_halts);
    qtest_add_func("/isa/m6809_ldmd_halts", test_m6809_ldmd_halts);
    qtest_add_func("/isa/m6809_emuls_halts", test_m6809_emuls_halts);
    qtest_add_func("/isa/turbo9_emul", test_turbo9_emul);
    qtest_add_func("/isa/turbo9_emuls", test_turbo9_emuls);
    qtest_add_func("/isa/turbo9_idiv", test_turbo9_idiv);
    qtest_add_func("/isa/turbo9_idivs", test_turbo9_idivs);
    qtest_add_func("/isa/turbo9_ediv", test_turbo9_ediv);
    qtest_add_func("/isa/turbo9_edivs", test_turbo9_edivs);
    qtest_add_func("/isa/turbo9_fdiv", test_turbo9_fdiv);
    qtest_add_func("/isa/turbo9_idiv_div0", test_turbo9_idiv_div0);
    qtest_add_func("/isa/turbo9_ediv_overflow", test_turbo9_ediv_overflow);
    qtest_add_func("/isa/turbo9_edivs_overflow", test_turbo9_edivs_overflow);
    qtest_add_func("/isa/turbo9_idivs_overflow", test_turbo9_idivs_overflow);
    qtest_add_func("/isa/turbo9_fdiv_overflow", test_turbo9_fdiv_overflow);
    qtest_add_func("/isa/turbo9_ldw_sexw_not_6309", test_turbo9_ldw_sexw_not_6309);
    qtest_add_func("/isa/turbo9_ldmd_halts", test_turbo9_ldmd_halts);
    qtest_add_func("/isa/cwai_irq", test_cwai_irq);
    qtest_add_func("/isa/firq_short_frame", test_firq_short_frame);
    qtest_add_func("/isa/sync_masked_falls_through", test_sync_masked);
    qtest_add_func("/isa/irq_after_andcc", test_irq_after_andcc);

    ret = g_test_run();

    qtest_quit(qts);
    qtest_quit(qts_hd6309);
    qtest_quit(qts_turbo9);
    unlink(rom_path);
    g_free(rom_path);
    return ret;
}
