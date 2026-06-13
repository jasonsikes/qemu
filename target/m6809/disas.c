/*
 * QEMU M6809 disassembler
 *
 * Copyright (c) 2025 Jason G. Sikes
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/dis-asm.h"
#include "indexed.h"

typedef struct DisasContext {
    disassemble_info *info;
    uint32_t addr;
    int len;
} DisasContext;

static uint32_t decode_insn_load_bytes(DisasContext *ctx, uint32_t insn,
                                       int i, int n)
{
    while (++i <= n) {
        uint8_t b;

        if (ctx->info->read_memory_func(ctx->addr, &b, 1, ctx->info) != 0) {
            return insn;
        }
        insn |= (uint32_t)b << (32 - i * 8);
        ctx->addr = (ctx->addr + 1) & 0xffff;
        ctx->len = i;
    }
    return insn;
}

static bool decode_insn(DisasContext *ctx, uint32_t insn);
#include "decode-insn.c.inc"

#define output(mnemonic, format, ...) \
    (ctx->info->fprintf_func(ctx->info->stream, "%-8s " format, \
                             mnemonic, ##__VA_ARGS__))

#define INSN(opcode, mnemonic, format, ...)                             \
static bool trans_##opcode(DisasContext *ctx, arg_##opcode *a)          \
{                                                                       \
    output(mnemonic, format, ##__VA_ARGS__);                            \
    return true;                                                        \
}

static bool indexed_read8(DisasContext *ctx, uint8_t *value)
{
    if (ctx->info->read_memory_func(ctx->addr, value, 1, ctx->info) != 0) {
        return false;
    }
    ctx->addr = (ctx->addr + 1) & 0xffff;
    ctx->len++;
    return true;
}

static bool format_indexed(DisasContext *ctx, char *buffer, size_t size)
{
    static const char * const registers[] = { "x", "y", "u", "s" };
    M6809IndexedMode mode;
    char inner[32];
    uint8_t post;
    uint8_t byte;
    uint16_t word;

    if (!indexed_read8(ctx, &post)) {
        return false;
    }

    if (!m6809_decode_indexed(post, true, &mode)) {
        return false;
    }

    switch (mode.kind) {
    case M6809_IDX_OFFSET5:
        snprintf(inner, sizeof(inner), "%d,%s", mode.offset,
                 registers[mode.reg]);
        break;
    case M6809_IDX_POSTINC1:
        snprintf(inner, sizeof(inner), ",%s+", registers[mode.reg]);
        break;
    case M6809_IDX_POSTINC2:
        snprintf(inner, sizeof(inner), ",%s++", registers[mode.reg]);
        break;
    case M6809_IDX_PREDEC1:
        snprintf(inner, sizeof(inner), ",-%s", registers[mode.reg]);
        break;
    case M6809_IDX_PREDEC2:
        snprintf(inner, sizeof(inner), ",--%s", registers[mode.reg]);
        break;
    case M6809_IDX_ZERO:
        snprintf(inner, sizeof(inner), ",%s", registers[mode.reg]);
        break;
    case M6809_IDX_B:
        snprintf(inner, sizeof(inner), "b,%s", registers[mode.reg]);
        break;
    case M6809_IDX_A:
        snprintf(inner, sizeof(inner), "a,%s", registers[mode.reg]);
        break;
    case M6809_IDX_E:
        snprintf(inner, sizeof(inner), "e,%s", registers[mode.reg]);
        break;
    case M6809_IDX_F:
        snprintf(inner, sizeof(inner), "f,%s", registers[mode.reg]);
        break;
    case M6809_IDX_W:
        snprintf(inner, sizeof(inner), ",w");
        break;
    case M6809_IDX_W_POSTINC2:
        snprintf(inner, sizeof(inner), ",w++");
        break;
    case M6809_IDX_W_PREDEC2:
        snprintf(inner, sizeof(inner), ",--w");
        break;
    case M6809_IDX_OFFSET8:
    case M6809_IDX_PCR8:
        if (!indexed_read8(ctx, &byte)) {
            return false;
        }
        snprintf(inner, sizeof(inner), "%d,%s", (int8_t)byte,
                 mode.kind == M6809_IDX_PCR8 ? "pcr" :
                 registers[mode.reg]);
        break;
    case M6809_IDX_OFFSET16:
    case M6809_IDX_PCR16:
    case M6809_IDX_EXTENDED_INDIRECT:
    case M6809_IDX_W_OFFSET16:
        if (!indexed_read8(ctx, &byte)) {
            return false;
        }
        word = (uint16_t)byte << 8;
        if (!indexed_read8(ctx, &byte)) {
            return false;
        }
        word |= byte;
        if (mode.kind == M6809_IDX_EXTENDED_INDIRECT) {
            snprintf(inner, sizeof(inner), "$%04x", word);
        } else {
            snprintf(inner, sizeof(inner), "%d,%s",
                     sextract32(word, 0, 16),
                     mode.kind == M6809_IDX_PCR16 ? "pcr" :
                     mode.kind == M6809_IDX_W_OFFSET16 ? "w" :
                     registers[mode.reg]);
        }
        break;
    case M6809_IDX_D:
        snprintf(inner, sizeof(inner), "d,%s", registers[mode.reg]);
        break;
    case M6809_IDX_W_OFF:
        snprintf(inner, sizeof(inner), "w,%s", registers[mode.reg]);
        break;
    default:
        g_assert_not_reached();
    }

    if (mode.indirect) {
        snprintf(buffer, size, "[%s]", inner);
    } else {
        snprintf(buffer, size, "%s", inner);
    }
    return true;
}

#define INDEXED_INSN(opcode, mnemonic)                                  \
static bool trans_##opcode(DisasContext *ctx, arg_##opcode *a)          \
{                                                                       \
    char operand[40];                                                   \
                                                                        \
    if (!format_indexed(ctx, operand, sizeof(operand))) {               \
        return false;                                                   \
    }                                                                   \
    output(mnemonic, "%s", operand);                                    \
    return true;                                                        \
}

#define INSN_MEM(name, mnemonic)                                        \
INSN(name##_dir, mnemonic, "<$%02x", a->addr)                           \
INDEXED_INSN(name##_idx, mnemonic)                                      \
INSN(name##_ext, mnemonic, "$%04x", a->addr)

#define INSN8(name, mnemonic)                                           \
INSN(name##_imm, mnemonic, "#$%02x", a->imm)                            \
INSN_MEM(name, mnemonic)

#define INSN16(name, mnemonic)                                          \
INSN(name##_imm, mnemonic, "#$%04x", a->imm)                            \
INSN_MEM(name, mnemonic)

#define INSN_RMW(name, mnemonic)                                        \
INSN(name##A, mnemonic "a", "")                                         \
INSN(name##B, mnemonic "b", "")                                         \
INSN_MEM(name, mnemonic)

INSN(NOP,      "nop",  "")
INSN(DAA,      "daa",  "")
INSN(ABX,      "abx",  "")
INDEXED_INSN(LEAX,     "leax")
INDEXED_INSN(LEAY,     "leay")
INDEXED_INSN(LEAS,     "leas")
INDEXED_INSN(LEAU,     "leau")
INSN(MUL,      "mul",  "")
INSN(ANDCC,    "andcc", "#$%02x", a->imm)
INSN(ORCC,     "orcc", "#$%02x", a->imm)

INSN_RMW(NEG, "neg")
INSN_RMW(COM, "com")
INSN_RMW(LSR, "lsr")
INSN_RMW(ROR, "ror")
INSN_RMW(ASR, "asr")
INSN_RMW(ASL, "asl")
INSN_RMW(ROL, "rol")
INSN_RMW(DEC, "dec")
INSN_RMW(INC, "inc")
INSN_RMW(CLR, "clr")
INSN_RMW(TST, "tst")

static void format_stack_mask(char *buffer, size_t size, uint8_t post,
                              bool pshu)
{
    static const char * const names_s[] = {
        "cc", "a", "b", "dp", "x", "y", "u", "pc"
    };
    static const char * const names_u[] = {
        "cc", "a", "b", "dp", "x", "y", "s", "pc"
    };
    const char * const *names = pshu ? names_u : names_s;
    size_t len = 0;
    int i;

    buffer[0] = '\0';
    for (i = 0; i < 8; i++) {
        if (post & (1 << i)) {
            if (len != 0) {
                len += snprintf(buffer + len, size - len, ",");
            }
            len += snprintf(buffer + len, size - len, "%s", names[i]);
        }
    }
}

#define STACK_INSN(opcode, mnemonic, pshu)                              \
static bool trans_##opcode(DisasContext *ctx, arg_##opcode *a)          \
{                                                                       \
    char regs[32];                                                      \
                                                                        \
    format_stack_mask(regs, sizeof(regs), a->post, pshu);               \
    output(mnemonic, "%s", regs);                                       \
    return true;                                                        \
}

STACK_INSN(PSHS, "pshs", false)
STACK_INSN(PULS, "puls", false)
STACK_INSN(PSHU, "pshu", true)
STACK_INSN(PULU, "pulu", true)

INSN(RTS,      "rts",  "")

INSN(CWAI,     "cwai", "#$%02x", a->imm)
INSN(SYNC,     "sync", "")
INSN(RTI,      "rti",  "")
INSN(SWI,      "swi",  "")
INSN(SWI2,     "swi2", "")
INSN(SWI3,     "swi3", "")

static bool trans_BSR(DisasContext *ctx, arg_BSR *a)
{
    uint32_t dest = (ctx->addr + a->disp) & 0xffff;

    output("bsr", "$%04x", dest);
    return true;
}

static bool trans_LBSR(DisasContext *ctx, arg_LBSR *a)
{
    uint32_t dest = (ctx->addr + a->disp) & 0xffff;

    output("lbsr", "$%04x", dest);
    return true;
}

INSN_MEM(JSR, "jsr")
INSN_MEM(JMP, "jmp")

static bool trans_BRANCH(DisasContext *ctx, arg_BRANCH *a)
{
    static const char * const mnemonics[16] = {
        "bra", "brn", "bhi", "bls", "bcc", "bcs", "bne", "beq",
        "bvc", "bvs", "bpl", "bmi", "bge", "blt", "bgt", "ble",
    };
    uint32_t dest = (ctx->addr + a->disp) & 0xffff;

    output(mnemonics[a->cond], "$%04x", dest);
    return true;
}

static bool trans_LBRA(DisasContext *ctx, arg_LBRA *a)
{
    uint32_t dest = (ctx->addr + a->disp) & 0xffff;

    output("lbra", "$%04x", dest);
    return true;
}

static bool trans_LBRANCH(DisasContext *ctx, arg_LBRANCH *a)
{
    static const char * const mnemonics[16] = {
        "lbra", "lbrn", "lbhi", "lbls", "lbcc", "lbcs", "lbne", "lbeq",
        "lbvc", "lbvs", "lbpl", "lbmi", "lbge", "lblt", "lbgt", "lble",
    };
    uint32_t dest = (ctx->addr + a->disp) & 0xffff;

    if (a->cond == 0) {
        return false;
    }
    output(mnemonics[a->cond], "$%04x", dest);
    return true;
}

INSN8(ADDA, "adda")
INSN8(ADDB, "addb")
INSN16(ADDD, "addd")

INSN8(ANDA, "anda")
INSN8(ANDB, "andb")
INSN8(BITA, "bita")
INSN8(BITB, "bitb")
INSN8(EORA, "eora")
INSN8(EORB, "eorb")
INSN8(ORA, "ora")
INSN8(ORB, "orb")

INSN8(SUBA, "suba")
INSN8(SUBB, "subb")
INSN8(SBCA, "sbca")
INSN8(SBCB, "sbcb")
INSN16(SUBD, "subd")
INSN8(ADCA, "adca")
INSN8(ADCB, "adcb")

INSN8(CMPA, "cmpa")
INSN8(CMPB, "cmpb")
INSN16(CMPX, "cmpx")
INSN16(CMPD, "cmpd")
INSN16(CMPY, "cmpy")
INSN16(CMPU, "cmpu")
INSN16(CMPS, "cmps")

INSN8(LDA, "lda")
INSN8(LDB, "ldb")
INSN16(LDD, "ldd")
INSN_MEM(STA, "sta")
INSN_MEM(STB, "stb")
INSN_MEM(STD, "std")

INSN16(LDX, "ldx")
INSN_MEM(STX, "stx")
INSN16(LDU, "ldu")
INSN_MEM(STU, "stu")
INSN16(LDY, "ldy")
INSN_MEM(STY, "sty")
INSN16(LDS, "lds")
INSN_MEM(STS, "sts")

INSN(SEX,      "sex",  "")
INSN(EXG,      "exg",  "$%02x", a->post)
INSN(TFR,      "tfr",  "$%02x", a->post)

INSN(INH14,    "sexw", "")
INSN(LDMD,     "ldmd", "#$%02x", a->imm)
INSN(BITMD,    "bitmd", "#$%02x", a->imm)
INSN(PSHSW,    "pshsw", "")
INSN(PULSW,    "pulsw", "")
INSN(PSHUW,    "pshuw", "")
INSN(PULUW,    "puluw", "")
INSN(NEGD,     "negd", "")
INSN(COMD,     "comd", "")
INSN(LSRD,     "lsrd", "")
INSN(RORD,     "rord", "")
INSN(ASRD,     "asrd", "")
INSN(ASLD,     "asld", "")
INSN(ROLD,     "rold", "")
INSN(DECD,     "decd", "")
INSN(INCD,     "incd", "")
INSN(TSTD,     "tstd", "")
INSN(CLRD,     "clrd", "")
INSN(COMW,     "comw", "")
INSN(LSRW,     "lsrw", "")
INSN(RORW,     "rorw", "")
INSN(ROLW,     "rolw", "")
INSN(DECW,     "decw", "")
INSN(INCW,     "incw", "")
INSN(TSTW,     "tstw", "")
INSN(CLRW,     "clrw", "")
INSN(COME,     "come", "")
INSN(DECE,     "dece", "")
INSN(INCE,     "ince", "")
INSN(TSTE,     "tste", "")
INSN(CLRE,     "clre", "")
INSN(COMF,     "comf", "")
INSN(DECF,     "decf", "")
INSN(INCF,     "incf", "")
INSN(TSTF,     "tstf", "")
INSN(CLRF,     "clrf", "")
INSN(ADDR,     "addr", "$%02x", a->post)
INSN(ADCR,     "adcr", "$%02x", a->post)
INSN(SUBR,     "subr", "$%02x", a->post)
INSN(SBCR,     "sbcr", "$%02x", a->post)
INSN(ANDR,     "andr", "$%02x", a->post)
INSN(ORR,      "orr",  "$%02x", a->post)
INSN(EORR,     "eorr", "$%02x", a->post)
INSN(CMPR,     "cmpr", "$%02x", a->post)

INSN8(LDE, "lde")
INSN_MEM(STE, "ste")
INSN8(LDF, "ldf")
INSN_MEM(STF, "stf")
INSN16(LDW, "ldw")
INSN_MEM(STW, "stw")
INSN_MEM(LDQ, "ldq")
INSN_MEM(STQ, "stq")

INSN8(DIVD, "divd")
INSN16(DIVQ, "divq")
INSN16(MULD, "muld")

static bool trans_LDQ_imm(DisasContext *ctx, arg_LDQ_imm *a)
{
    uint8_t b[4];
    int i;

    for (i = 0; i < 4; i++) {
        if (!indexed_read8(ctx, &b[i])) {
            return false;
        }
    }
    output("ldq", "#$%02x%02x%02x%02x", b[0], b[1], b[2], b[3]);
    return true;
}

#define IM_MEM_INSN(name, mnemonic)                                     \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    output(mnemonic, "#$%02x,<$%02x", a->imm, a->addr);                 \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    char operand[40];                                                   \
                                                                        \
    if (!format_indexed(ctx, operand, sizeof(operand))) {               \
        return false;                                                   \
    }                                                                   \
    output(mnemonic, "#$%02x,%s", a->imm, operand);                     \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    output(mnemonic, "#$%02x,$%04x", a->imm, a->addr);                  \
    return true;                                                        \
}

IM_MEM_INSN(AIM, "aim")
IM_MEM_INSN(OIM, "oim")
IM_MEM_INSN(EIM, "eim")
IM_MEM_INSN(TIM, "tim")

#define BIT_INSN(name, mnemonic)                                        \
static bool trans_##name(DisasContext *ctx, arg_##name *a)              \
{                                                                       \
    output(mnemonic, "$%02x,<$%02x", a->post, a->addr);                 \
    return true;                                                        \
}

BIT_INSN(BAND, "band")
BIT_INSN(BIAND, "biand")
BIT_INSN(BOR, "bor")
BIT_INSN(BIOR, "bior")
BIT_INSN(BEOR, "beor")
BIT_INSN(BIEOR, "bieor")
BIT_INSN(LDBT, "ldbt")
BIT_INSN(STBT, "stbt")

INSN(TFM_pp, "tfm", "$%02x+", a->post)
INSN(TFM_mm, "tfm", "$%02x-", a->post)
INSN(TFM_p0, "tfm", "$%02x+0", a->post)
INSN(TFM_0p, "tfm", "$%02x0+", a->post)

int m6809_print_insn(bfd_vma addr, disassemble_info *info)
{
    DisasContext ctx = {
        .info = info,
        .addr = addr,
        .len = 0,
    };
    uint32_t insn;

    insn = decode_insn_load(&ctx);
    if (!decode_insn(&ctx, insn)) {
        uint8_t b;

        if (info->read_memory_func(addr, &b, 1, info) != 0) {
            info->memory_error_func(-1, addr, info);
            return -1;
        }
        info->fprintf_func(info->stream, ".byte\t0x%02x", b);
        return 1;
    }
    return ctx.len;
}
