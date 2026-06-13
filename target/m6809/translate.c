/*
 * QEMU M6809 instruction translation
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
#include "tcg/tcg-op.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/translator.h"
#include "exec/translation-block.h"
#include "exec/log.h"
#include "disas/disas.h"
#include "tcg/debug-assert.h"
#include "indexed.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#define DISAS_JUMP  DISAS_TARGET_0
#define DISAS_CHAIN DISAS_TARGET_1
/* Return to the main loop, so a pending interrupt is re-tested. */
#define DISAS_EXIT        DISAS_TARGET_2 /* PC already written by the insn */
#define DISAS_UPDATE_EXIT DISAS_TARGET_3 /* PC still needs pc_next */

typedef struct DisasContext {
    DisasContextBase base;
    CPUM6809State *env;
    bool native; /* TB_FLAGS_NATIVE: 6309 MD.NM */
} DisasContext;

static TCGv_i32 cpu_a;
static TCGv_i32 cpu_b;
static TCGv_i32 cpu_x;
static TCGv_i32 cpu_y;
static TCGv_i32 cpu_u;
static TCGv_i32 cpu_s;
static TCGv_i32 cpu_pc;
static TCGv_i32 cpu_dp;
static TCGv_i32 cpu_cc;
static TCGv_i32 cpu_e;
static TCGv_i32 cpu_f;
static TCGv_i32 cpu_v;
static TCGv_i32 cpu_md;

void m6809_tcg_init(void)
{
    cpu_a = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, a), "A");
    cpu_b = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, b), "B");
    cpu_x = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, x), "X");
    cpu_y = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, y), "Y");
    cpu_u = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, u), "U");
    cpu_s = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, s), "S");
    cpu_pc = tcg_global_mem_new_i32(tcg_env,
                                    offsetof(CPUM6809State, pc), "PC");
    cpu_dp = tcg_global_mem_new_i32(tcg_env,
                                    offsetof(CPUM6809State, dp), "DP");
    cpu_cc = tcg_global_mem_new_i32(tcg_env,
                                    offsetof(CPUM6809State, cc), "CC");
    cpu_e = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, e), "E");
    cpu_f = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, f), "F");
    cpu_v = tcg_global_mem_new_i32(tcg_env,
                                   offsetof(CPUM6809State, v), "V");
    cpu_md = tcg_global_mem_new_i32(tcg_env,
                                    offsetof(CPUM6809State, md), "MD");
}

static uint32_t decode_insn_load_bytes(DisasContext *ctx, uint32_t insn,
                                       int i, int n)
{
    while (++i <= n) {
        uint8_t b = translator_ldub(ctx->env, &ctx->base, ctx->base.pc_next);
        ctx->base.pc_next = (ctx->base.pc_next + 1) & 0xffff;
        insn |= (uint32_t)b << (32 - i * 8);
    }
    return insn;
}

static bool decode_insn(DisasContext *ctx, uint32_t insn);
#include "decode-insn.c.inc"

static void gen_goto_tb(DisasContext *dc, unsigned tb_slot_idx, vaddr dest)
{
    dest &= 0xffff;
    if (translator_use_goto_tb(&dc->base, dest)) {
        tcg_gen_goto_tb(tb_slot_idx);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb(dc->base.tb, tb_slot_idx);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
}

static void gen_get_d(TCGv_i32 d)
{
    tcg_gen_deposit_i32(d, cpu_b, cpu_a, 8, 8);
}

static void gen_set_d(TCGv_i32 d)
{
    tcg_gen_ext8u_i32(cpu_b, d);
    tcg_gen_shri_i32(cpu_a, d, 8);
    tcg_gen_ext8u_i32(cpu_a, cpu_a);
}

static void gen_get_w(TCGv_i32 w)
{
    tcg_gen_deposit_i32(w, cpu_f, cpu_e, 8, 8);
}

static void gen_set_w(TCGv_i32 w)
{
    tcg_gen_ext8u_i32(cpu_f, w);
    tcg_gen_shri_i32(cpu_e, w, 8);
    tcg_gen_ext8u_i32(cpu_e, cpu_e);
}

/* Set N and Z from an 8-bit result. Caller has already cleared those bits. */
static void gen_set_nz8(TCGv_i32 r)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_setcondi_i32(TCG_COND_EQ, t, r, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_shri_i32(t, r, 7);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
}

static void gen_set_nz16(TCGv_i32 r)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_setcondi_i32(TCG_COND_EQ, t, r, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_shri_i32(t, r, 15);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
}

static void gen_logic_cc8(TCGv_i32 r)
{
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V));
    gen_set_nz8(r);
}

static void gen_logic_cc16(TCGv_i32 r)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V));

    tcg_gen_setcondi_i32(TCG_COND_EQ, t, r, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_shri_i32(t, r, 15);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
}

static void gen_add8_common(TCGv_i32 dest, TCGv_i32 src, bool with_carry)
{
    TCGv_i32 a = tcg_temp_new_i32();
    TCGv_i32 b = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();
    TCGv_i32 u = tcg_temp_new_i32();

    tcg_gen_andi_i32(a, dest, 0xff);
    tcg_gen_andi_i32(b, src, 0xff);
    tcg_gen_add_i32(r, a, b);
    if (with_carry) {
        tcg_gen_andi_i32(t, cpu_cc, CC_C);
        tcg_gen_add_i32(r, r, t);
    }

    tcg_gen_andi_i32(cpu_cc, cpu_cc,
                     (uint32_t)~(CC_H | CC_N | CC_Z | CC_V | CC_C));

    tcg_gen_setcondi_i32(TCG_COND_GTU, t, r, 0xff);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_andi_i32(r, r, 0xff);

    tcg_gen_setcondi_i32(TCG_COND_EQ, t, r, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_shri_i32(t, r, 7);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_xor_i32(t, a, r);
    tcg_gen_xor_i32(u, b, r);
    tcg_gen_and_i32(t, t, u);
    tcg_gen_andi_i32(t, t, 0x80);
    tcg_gen_setcondi_i32(TCG_COND_NE, t, t, 0);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_xor_i32(t, a, b);
    tcg_gen_xor_i32(t, t, r);
    tcg_gen_andi_i32(t, t, 0x10);
    tcg_gen_setcondi_i32(TCG_COND_NE, t, t, 0);
    tcg_gen_shli_i32(t, t, 5);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_mov_i32(dest, r);
}

static void gen_add16_common(TCGv_i32 dest, TCGv_i32 src, bool with_carry)
{
    TCGv_i32 a = tcg_temp_new_i32();
    TCGv_i32 b = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();
    TCGv_i32 u = tcg_temp_new_i32();

    tcg_gen_andi_i32(a, dest, 0xffff);
    tcg_gen_andi_i32(b, src, 0xffff);
    tcg_gen_add_i32(r, a, b);
    if (with_carry) {
        tcg_gen_andi_i32(t, cpu_cc, CC_C);
        tcg_gen_add_i32(r, r, t);
    }

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));

    tcg_gen_setcondi_i32(TCG_COND_GTU, t, r, 0xffff);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_andi_i32(r, r, 0xffff);

    tcg_gen_setcondi_i32(TCG_COND_EQ, t, r, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_shri_i32(t, r, 15);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_xor_i32(t, a, r);
    tcg_gen_xor_i32(u, b, r);
    tcg_gen_and_i32(t, t, u);
    tcg_gen_andi_i32(t, t, 0x8000);
    tcg_gen_setcondi_i32(TCG_COND_NE, t, t, 0);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_mov_i32(dest, r);
}

static void gen_add16(TCGv_i32 dest, TCGv_i32 src)
{
    gen_add16_common(dest, src, false);
}

static void gen_sub8_common(TCGv_i32 dest, TCGv_i32 src,
                            bool with_carry, bool writeback)
{
    TCGv_i32 a = tcg_temp_new_i32();
    TCGv_i32 b = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(a, dest, 0xff);
    tcg_gen_andi_i32(b, src, 0xff);
    tcg_gen_sub_i32(r, a, b);
    if (with_carry) {
        tcg_gen_andi_i32(t, cpu_cc, CC_C);
        tcg_gen_sub_i32(r, r, t);
    }

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));

    tcg_gen_setcondi_i32(TCG_COND_LT, t, r, 0);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_andi_i32(r, r, 0xff);

    tcg_gen_setcondi_i32(TCG_COND_EQ, t, r, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_shri_i32(t, r, 7);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_xor_i32(t, a, b);
    tcg_gen_xor_i32(b, a, r);
    tcg_gen_and_i32(t, t, b);
    tcg_gen_andi_i32(t, t, 0x80);
    tcg_gen_setcondi_i32(TCG_COND_NE, t, t, 0);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    if (writeback) {
        tcg_gen_mov_i32(dest, r);
    }
}

static void gen_sub8(TCGv_i32 dest, TCGv_i32 src)
{
    gen_sub8_common(dest, src, false, true);
}

static void gen_sbc8(TCGv_i32 dest, TCGv_i32 src)
{
    gen_sub8_common(dest, src, true, true);
}

static void gen_cmp8(TCGv_i32 dest, TCGv_i32 src)
{
    gen_sub8_common(dest, src, false, false);
}

static void gen_sub16_common(TCGv_i32 dest, TCGv_i32 src,
                             bool with_carry, bool writeback)
{
    TCGv_i32 a = tcg_temp_new_i32();
    TCGv_i32 b = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(a, dest, 0xffff);
    tcg_gen_andi_i32(b, src, 0xffff);
    tcg_gen_sub_i32(r, a, b);
    if (with_carry) {
        tcg_gen_andi_i32(t, cpu_cc, CC_C);
        tcg_gen_sub_i32(r, r, t);
    }

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));

    tcg_gen_setcondi_i32(TCG_COND_LT, t, r, 0);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_andi_i32(r, r, 0xffff);

    tcg_gen_setcondi_i32(TCG_COND_EQ, t, r, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_shri_i32(t, r, 15);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    tcg_gen_xor_i32(t, a, b);
    tcg_gen_xor_i32(b, a, r);
    tcg_gen_and_i32(t, t, b);
    tcg_gen_andi_i32(t, t, 0x8000);
    tcg_gen_setcondi_i32(TCG_COND_NE, t, t, 0);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    if (writeback) {
        tcg_gen_mov_i32(dest, r);
    }
}

static void gen_sub16(TCGv_i32 dest, TCGv_i32 src, bool writeback)
{
    gen_sub16_common(dest, src, false, writeback);
}

static void gen_cmp16(TCGv_i32 dest, TCGv_i32 src)
{
    gen_sub16(dest, src, false);
}

typedef enum {
    M6809_LOGIC_AND,
    M6809_LOGIC_OR,
    M6809_LOGIC_EOR,
} M6809LogicOp;

static void gen_logic8(TCGv_i32 dest, TCGv_i32 src, M6809LogicOp op,
                       bool writeback)
{
    TCGv_i32 r = tcg_temp_new_i32();

    switch (op) {
    case M6809_LOGIC_AND:
        tcg_gen_and_i32(r, dest, src);
        break;
    case M6809_LOGIC_OR:
        tcg_gen_or_i32(r, dest, src);
        break;
    case M6809_LOGIC_EOR:
        tcg_gen_xor_i32(r, dest, src);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_gen_andi_i32(r, r, 0xff);
    gen_logic_cc8(r);
    if (writeback) {
        tcg_gen_mov_i32(dest, r);
    }
}

/* ASL/ROL overflow: bit 7 XOR bit 6 of the unshifted operand. */
static void gen_shift_v8(TCGv_i32 src)
{
    TCGv_i32 t = tcg_temp_new_i32();
    TCGv_i32 u = tcg_temp_new_i32();

    tcg_gen_shri_i32(t, src, 6);
    tcg_gen_shri_i32(u, src, 7);
    tcg_gen_xor_i32(t, t, u);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
}

/* RMW: H unchanged. V unchanged for LSR, ASR, ROR. */
static void gen_neg8(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xff);
    tcg_gen_neg_i32(val, src);
    tcg_gen_andi_i32(val, val, 0xff);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));

    /* C if the operand was nonzero; V if it was $80. */
    tcg_gen_setcondi_i32(TCG_COND_NE, t, src, 0);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, src, 0x80);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    gen_set_nz8(val);
}

static void gen_com8(TCGv_i32 val)
{
    tcg_gen_not_i32(val, val);
    tcg_gen_andi_i32(val, val, 0xff);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_ori_i32(cpu_cc, cpu_cc, CC_C);
    gen_set_nz8(val);
}

static void gen_lsr8(TCGv_i32 val)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(val, val, 0xff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_C));

    tcg_gen_andi_i32(t, val, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_shri_i32(val, val, 1);
    gen_set_nz8(val);
}

static void gen_asr8(TCGv_i32 val)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(val, val, 0xff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_C));

    tcg_gen_andi_i32(t, val, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_ext8s_i32(val, val);
    tcg_gen_sari_i32(val, val, 1);
    tcg_gen_andi_i32(val, val, 0xff);
    gen_set_nz8(val);
}

static void gen_asl8(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));

    tcg_gen_shri_i32(t, src, 7);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_shift_v8(src);

    tcg_gen_shli_i32(val, src, 1);
    tcg_gen_andi_i32(val, val, 0xff);
    gen_set_nz8(val);
}

static void gen_rol8(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xff);
    tcg_gen_andi_i32(t, cpu_cc, CC_C);
    tcg_gen_shli_i32(val, src, 1);
    tcg_gen_or_i32(val, val, t);
    tcg_gen_andi_i32(val, val, 0xff);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_shri_i32(t, src, 7);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_shift_v8(src);
    gen_set_nz8(val);
}

static void gen_ror8(TCGv_i32 val)
{
    TCGv_i32 t = tcg_temp_new_i32();
    TCGv_i32 c = tcg_temp_new_i32();

    tcg_gen_andi_i32(val, val, 0xff);
    tcg_gen_andi_i32(c, cpu_cc, CC_C);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_C));

    tcg_gen_andi_i32(t, val, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_shri_i32(val, val, 1);
    tcg_gen_shli_i32(c, c, 7);
    tcg_gen_or_i32(val, val, c);
    gen_set_nz8(val);
}

static void gen_dec8(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xff);
    tcg_gen_subi_i32(val, src, 1);
    tcg_gen_andi_i32(val, val, 0xff);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V));
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, src, 0x80);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_set_nz8(val);
}

static void gen_inc8(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xff);
    tcg_gen_addi_i32(val, src, 1);
    tcg_gen_andi_i32(val, val, 0xff);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V));
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, src, 0x7f);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_set_nz8(val);
}

static void gen_clr8(TCGv_i32 val)
{
    tcg_gen_movi_i32(val, 0);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_ori_i32(cpu_cc, cpu_cc, CC_Z);
}

static void gen_neg16(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xffff);
    tcg_gen_neg_i32(val, src);
    tcg_gen_andi_i32(val, val, 0xffff);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_setcondi_i32(TCG_COND_NE, t, src, 0);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, src, 0x8000);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_set_nz16(val);
}

static void gen_com16(TCGv_i32 val)
{
    tcg_gen_not_i32(val, val);
    tcg_gen_andi_i32(val, val, 0xffff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_ori_i32(cpu_cc, cpu_cc, CC_C);
    gen_set_nz16(val);
}

static void gen_lsr16(TCGv_i32 val)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(val, val, 0xffff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_C));
    tcg_gen_andi_i32(t, val, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_shri_i32(val, val, 1);
    gen_set_nz16(val);
}

static void gen_asr16(TCGv_i32 val)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(val, val, 0xffff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_C));
    tcg_gen_andi_i32(t, val, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_ext16s_i32(val, val);
    tcg_gen_sari_i32(val, val, 1);
    tcg_gen_andi_i32(val, val, 0xffff);
    gen_set_nz16(val);
}

static void gen_shift_v16(TCGv_i32 src)
{
    TCGv_i32 t = tcg_temp_new_i32();
    TCGv_i32 u = tcg_temp_new_i32();

    tcg_gen_shri_i32(t, src, 14);
    tcg_gen_shri_i32(u, src, 15);
    tcg_gen_xor_i32(t, t, u);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
}

static void gen_asl16(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xffff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_shri_i32(t, src, 15);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_shift_v16(src);
    tcg_gen_shli_i32(val, src, 1);
    tcg_gen_andi_i32(val, val, 0xffff);
    gen_set_nz16(val);
}

static void gen_rol16(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xffff);
    tcg_gen_andi_i32(t, cpu_cc, CC_C);
    tcg_gen_shli_i32(val, src, 1);
    tcg_gen_or_i32(val, val, t);
    tcg_gen_andi_i32(val, val, 0xffff);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_shri_i32(t, src, 15);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_shift_v16(src);
    gen_set_nz16(val);
}

static void gen_ror16(TCGv_i32 val)
{
    TCGv_i32 t = tcg_temp_new_i32();
    TCGv_i32 c = tcg_temp_new_i32();

    tcg_gen_andi_i32(val, val, 0xffff);
    tcg_gen_andi_i32(c, cpu_cc, CC_C);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_C));
    tcg_gen_andi_i32(t, val, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_shri_i32(val, val, 1);
    tcg_gen_shli_i32(c, c, 15);
    tcg_gen_or_i32(val, val, c);
    gen_set_nz16(val);
}

static void gen_dec16(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xffff);
    tcg_gen_subi_i32(val, src, 1);
    tcg_gen_andi_i32(val, val, 0xffff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V));
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, src, 0x8000);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_set_nz16(val);
}

static void gen_inc16(TCGv_i32 val)
{
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(src, val, 0xffff);
    tcg_gen_addi_i32(val, src, 1);
    tcg_gen_andi_i32(val, val, 0xffff);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V));
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, src, 0x7fff);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    gen_set_nz16(val);
}

static void gen_clr16(TCGv_i32 val)
{
    tcg_gen_movi_i32(val, 0);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_ori_i32(cpu_cc, cpu_cc, CC_Z);
}

static void gen_logic16(TCGv_i32 dest, TCGv_i32 src, M6809LogicOp op,
                        bool writeback)
{
    TCGv_i32 r = tcg_temp_new_i32();

    switch (op) {
    case M6809_LOGIC_AND:
        tcg_gen_and_i32(r, dest, src);
        break;
    case M6809_LOGIC_OR:
        tcg_gen_or_i32(r, dest, src);
        break;
    case M6809_LOGIC_EOR:
        tcg_gen_xor_i32(r, dest, src);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_gen_andi_i32(r, r, 0xffff);
    gen_logic_cc16(r);
    if (writeback) {
        tcg_gen_mov_i32(dest, r);
    }
}

static void gen_add8_noh(TCGv_i32 dest, TCGv_i32 src, bool with_carry)
{
    TCGv_i32 a = tcg_temp_new_i32();
    TCGv_i32 b = tcg_temp_new_i32();
    TCGv_i32 r = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();
    TCGv_i32 u = tcg_temp_new_i32();

    tcg_gen_andi_i32(a, dest, 0xff);
    tcg_gen_andi_i32(b, src, 0xff);
    tcg_gen_add_i32(r, a, b);
    if (with_carry) {
        tcg_gen_andi_i32(t, cpu_cc, CC_C);
        tcg_gen_add_i32(r, r, t);
    }

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V | CC_C));
    tcg_gen_setcondi_i32(TCG_COND_GTU, t, r, 0xff);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_andi_i32(r, r, 0xff);
    gen_set_nz8(r);

    tcg_gen_xor_i32(t, a, r);
    tcg_gen_xor_i32(u, b, r);
    tcg_gen_and_i32(t, t, u);
    tcg_gen_andi_i32(t, t, 0x80);
    tcg_gen_setcondi_i32(TCG_COND_NE, t, t, 0);
    tcg_gen_shli_i32(t, t, 1);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_mov_i32(dest, r);
}

static TCGv_i32 gen_ea_direct(int addr)
{
    TCGv_i32 ea = tcg_temp_new_i32();

    tcg_gen_shli_i32(ea, cpu_dp, 8);
    tcg_gen_ori_i32(ea, ea, addr & 0xff);
    return ea;
}

static void gen_ld8(TCGv_i32 dest, TCGv_i32 ea)
{
    tcg_gen_qemu_ld_i32(dest, ea, 0, MO_UB);
    gen_logic_cc8(dest);
}

static void gen_st8(TCGv_i32 ea, TCGv_i32 val)
{
    tcg_gen_qemu_st_i32(val, ea, 0, MO_UB);
    gen_logic_cc8(val);
}

static void gen_illegal(DisasContext *ctx)
{
    if (m6809_feature(ctx->env, M6809_FEATURE_6309)) {
        /* Stacked PC is the following instruction, so RTI skips the trap. */
        tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next & 0xffff);
    }
    gen_helper_raise_illegal_instruction(tcg_env);
    ctx->base.is_jmp = DISAS_NORETURN;
}

static bool require_6309(DisasContext *ctx)
{
    if (!m6809_feature(ctx->env, M6809_FEATURE_6309)) {
        gen_illegal(ctx);
        return false;
    }
    return true;
}

/* Unmasking I/F: exit the TB. */
static void gen_exit_after_cc_write(DisasContext *ctx)
{
    ctx->base.is_jmp = (ctx->base.is_jmp == DISAS_JUMP) ? DISAS_EXIT
                                                       : DISAS_UPDATE_EXIT;
}

static void gen_set_nmi_armed(void)
{
    tcg_gen_st8_i32(tcg_constant_i32(1), tcg_env,
                    offsetof(CPUM6809State, nmi_armed));
}

static uint8_t indexed_fetch8(DisasContext *ctx)
{
    uint8_t value = translator_ldub(ctx->env, &ctx->base,
                                    ctx->base.pc_next);

    ctx->base.pc_next = (ctx->base.pc_next + 1) & 0xffff;
    return value;
}

static uint16_t indexed_fetch16(DisasContext *ctx)
{
    uint16_t value = (uint16_t)indexed_fetch8(ctx) << 8;

    return value | indexed_fetch8(ctx);
}

static TCGv_i32 indexed_reg(int reg)
{
    switch (reg) {
    case 0:
        return cpu_x;
    case 1:
        return cpu_y;
    case 2:
        return cpu_u;
    case 3:
        return cpu_s;
    default:
        g_assert_not_reached();
    }
}

static void gen_ld16_wrapped(TCGv_i32 value, TCGv_i32 ea)
{
    TCGv_i32 next = tcg_temp_new_i32();
    TCGv_i32 low = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(value, ea, 0, MO_UB);
    tcg_gen_addi_i32(next, ea, 1);
    tcg_gen_andi_i32(next, next, 0xffff);
    tcg_gen_qemu_ld_i32(low, next, 0, MO_UB);
    tcg_gen_shli_i32(value, value, 8);
    tcg_gen_or_i32(value, value, low);
}

static void gen_st16_wrapped(TCGv_i32 ea, TCGv_i32 val)
{
    TCGv_i32 high = tcg_temp_new_i32();
    TCGv_i32 low = tcg_temp_new_i32();
    TCGv_i32 next = tcg_temp_new_i32();

    tcg_gen_shri_i32(high, val, 8);
    tcg_gen_andi_i32(high, high, 0xff);
    tcg_gen_andi_i32(low, val, 0xff);
    tcg_gen_qemu_st_i32(high, ea, 0, MO_UB);
    tcg_gen_addi_i32(next, ea, 1);
    tcg_gen_andi_i32(next, next, 0xffff);
    tcg_gen_qemu_st_i32(low, next, 0, MO_UB);
}

static void gen_ld16(TCGv_i32 dest, TCGv_i32 ea)
{
    gen_ld16_wrapped(dest, ea);
    gen_logic_cc16(dest);
}

static void gen_st16(TCGv_i32 ea, TCGv_i32 val)
{
    TCGv_i32 masked = tcg_temp_new_i32();

    tcg_gen_andi_i32(masked, val, 0xffff);
    gen_st16_wrapped(ea, masked);
    gen_logic_cc16(masked);
}

static void gen_push8(TCGv_i32 sp, TCGv_i32 val)
{
    TCGv_i32 b = tcg_temp_new_i32();

    tcg_gen_andi_i32(b, val, 0xff);
    tcg_gen_subi_i32(sp, sp, 1);
    tcg_gen_andi_i32(sp, sp, 0xffff);
    tcg_gen_qemu_st_i32(b, sp, 0, MO_UB);
}

static void gen_pull8(TCGv_i32 sp, TCGv_i32 val)
{
    tcg_gen_qemu_ld_i32(val, sp, 0, MO_UB);
    tcg_gen_addi_i32(sp, sp, 1);
    tcg_gen_andi_i32(sp, sp, 0xffff);
}

static void gen_push16(TCGv_i32 sp, TCGv_i32 val)
{
    TCGv_i32 low = tcg_temp_new_i32();
    TCGv_i32 high = tcg_temp_new_i32();

    tcg_gen_andi_i32(low, val, 0xff);
    tcg_gen_shri_i32(high, val, 8);
    tcg_gen_andi_i32(high, high, 0xff);
    gen_push8(sp, low);
    gen_push8(sp, high);
}

static void gen_pull16(TCGv_i32 sp, TCGv_i32 val)
{
    TCGv_i32 low = tcg_temp_new_i32();

    gen_pull8(sp, val);
    gen_pull8(sp, low);
    tcg_gen_shli_i32(val, val, 8);
    tcg_gen_or_i32(val, val, low);
}

/*
 * PSHS/PSHU postbyte, high bit first: PC, U/S, Y, X, DP, B, A, CC.
 * 16-bit values are stored big-endian (low byte pushed first).
 */
static void gen_psh(DisasContext *ctx, TCGv_i32 sp, int mask, bool pshu)
{
    if (mask & 0x80) {
        gen_push16(sp, tcg_constant_i32(ctx->base.pc_next & 0xffff));
    }
    if (mask & 0x40) {
        gen_push16(sp, pshu ? cpu_s : cpu_u);
    }
    if (mask & 0x20) {
        gen_push16(sp, cpu_y);
    }
    if (mask & 0x10) {
        gen_push16(sp, cpu_x);
    }
    if (mask & 0x08) {
        gen_push8(sp, cpu_dp);
    }
    if (mask & 0x04) {
        gen_push8(sp, cpu_b);
    }
    if (mask & 0x02) {
        gen_push8(sp, cpu_a);
    }
    if (mask & 0x01) {
        gen_push8(sp, cpu_cc);
    }
}

static void gen_pul(DisasContext *ctx, TCGv_i32 sp, int mask, bool pulu)
{
    if (mask & 0x01) {
        gen_pull8(sp, cpu_cc);
    }
    if (mask & 0x02) {
        gen_pull8(sp, cpu_a);
    }
    if (mask & 0x04) {
        gen_pull8(sp, cpu_b);
    }
    if (mask & 0x08) {
        gen_pull8(sp, cpu_dp);
    }
    if (mask & 0x10) {
        gen_pull16(sp, cpu_x);
    }
    if (mask & 0x20) {
        gen_pull16(sp, cpu_y);
    }
    if (mask & 0x40) {
        gen_pull16(sp, pulu ? cpu_s : cpu_u);
        if (pulu) {
            gen_set_nmi_armed();
        }
    }
    if (mask & 0x80) {
        gen_pull16(sp, cpu_pc);
        ctx->base.is_jmp = DISAS_JUMP;
    }
    if (mask & 0x01) {
        gen_exit_after_cc_write(ctx);
    }
}

/*
 * Entire interrupt / SWI / CWAI frame. Native extra bytes are only here,
 * not in PSHS/PULS: PC, U, Y, X, DP, [F, E], B, A, CC.
 */
static void gen_stack_entire(DisasContext *ctx)
{
    gen_push16(cpu_s, tcg_constant_i32(ctx->base.pc_next & 0xffff));
    gen_push16(cpu_s, cpu_u);
    gen_push16(cpu_s, cpu_y);
    gen_push16(cpu_s, cpu_x);
    gen_push8(cpu_s, cpu_dp);
    if (ctx->native) {
        TCGv_i32 w = tcg_temp_new_i32();

        gen_get_w(w);
        gen_push16(cpu_s, w);
    }
    gen_push8(cpu_s, cpu_b);
    gen_push8(cpu_s, cpu_a);
    gen_push8(cpu_s, cpu_cc);
}

static void gen_unstack_entire(DisasContext *ctx)
{
    gen_pull8(cpu_s, cpu_a);
    gen_pull8(cpu_s, cpu_b);
    if (ctx->native) {
        gen_pull8(cpu_s, cpu_e);
        gen_pull8(cpu_s, cpu_f);
    }
    gen_pull8(cpu_s, cpu_dp);
    gen_pull16(cpu_s, cpu_x);
    gen_pull16(cpu_s, cpu_y);
    gen_pull16(cpu_s, cpu_u);
    gen_pull16(cpu_s, cpu_pc);
}

static bool gen_ea_indexed(DisasContext *ctx, TCGv_i32 *result)
{
    M6809IndexedMode mode;
    TCGv_i32 ea = tcg_temp_new_i32();
    TCGv_i32 base;
    TCGv_i32 offset;
    uint8_t post = indexed_fetch8(ctx);
    int displacement;

    translator_io_start(&ctx->base);

    if (!m6809_decode_indexed(post,
                              m6809_feature(ctx->env, M6809_FEATURE_6309),
                              &mode)) {
        gen_illegal(ctx);
        return false;
    }

    switch (mode.kind) {
    case M6809_IDX_EXTENDED_INDIRECT:
        tcg_gen_movi_i32(ea, indexed_fetch16(ctx));
        break;
    case M6809_IDX_PCR8:
        displacement = (int8_t)indexed_fetch8(ctx);
        tcg_gen_movi_i32(ea, (ctx->base.pc_next + displacement) & 0xffff);
        break;
    case M6809_IDX_PCR16:
        displacement = sextract32(indexed_fetch16(ctx), 0, 16);
        tcg_gen_movi_i32(ea, (ctx->base.pc_next + displacement) & 0xffff);
        break;
    case M6809_IDX_W:
        gen_get_w(ea);
        break;
    case M6809_IDX_W_OFFSET16:
        gen_get_w(ea);
        displacement = sextract32(indexed_fetch16(ctx), 0, 16);
        tcg_gen_addi_i32(ea, ea, displacement);
        break;
    case M6809_IDX_W_POSTINC2:
        gen_get_w(ea);
        offset = tcg_temp_new_i32();
        tcg_gen_addi_i32(offset, ea, 2);
        tcg_gen_andi_i32(offset, offset, 0xffff);
        gen_set_w(offset);
        break;
    case M6809_IDX_W_PREDEC2:
        gen_get_w(ea);
        tcg_gen_subi_i32(ea, ea, 2);
        tcg_gen_andi_i32(ea, ea, 0xffff);
        gen_set_w(ea);
        break;
    default:
        base = indexed_reg(mode.reg);
        tcg_gen_mov_i32(ea, base);

        switch (mode.kind) {
        case M6809_IDX_OFFSET5:
            tcg_gen_addi_i32(ea, ea, mode.offset);
            break;
        case M6809_IDX_POSTINC1:
        case M6809_IDX_POSTINC2:
            displacement = mode.kind == M6809_IDX_POSTINC1 ? 1 : 2;
            tcg_gen_addi_i32(base, base, displacement);
            tcg_gen_andi_i32(base, base, 0xffff);
            break;
        case M6809_IDX_PREDEC1:
        case M6809_IDX_PREDEC2:
            displacement = mode.kind == M6809_IDX_PREDEC1 ? -1 : -2;
            tcg_gen_addi_i32(base, base, displacement);
            tcg_gen_andi_i32(base, base, 0xffff);
            tcg_gen_mov_i32(ea, base);
            break;
        case M6809_IDX_ZERO:
            break;
        case M6809_IDX_B:
            offset = tcg_temp_new_i32();
            tcg_gen_ext8s_i32(offset, cpu_b);
            tcg_gen_add_i32(ea, ea, offset);
            break;
        case M6809_IDX_A:
            offset = tcg_temp_new_i32();
            tcg_gen_ext8s_i32(offset, cpu_a);
            tcg_gen_add_i32(ea, ea, offset);
            break;
        case M6809_IDX_E:
            offset = tcg_temp_new_i32();
            tcg_gen_ext8s_i32(offset, cpu_e);
            tcg_gen_add_i32(ea, ea, offset);
            break;
        case M6809_IDX_F:
            offset = tcg_temp_new_i32();
            tcg_gen_ext8s_i32(offset, cpu_f);
            tcg_gen_add_i32(ea, ea, offset);
            break;
        case M6809_IDX_OFFSET8:
            tcg_gen_addi_i32(ea, ea, (int8_t)indexed_fetch8(ctx));
            break;
        case M6809_IDX_OFFSET16:
            displacement = sextract32(indexed_fetch16(ctx), 0, 16);
            tcg_gen_addi_i32(ea, ea, displacement);
            break;
        case M6809_IDX_D:
            offset = tcg_temp_new_i32();
            gen_get_d(offset);
            tcg_gen_ext16s_i32(offset, offset);
            tcg_gen_add_i32(ea, ea, offset);
            break;
        case M6809_IDX_W_OFF:
            offset = tcg_temp_new_i32();
            gen_get_w(offset);
            tcg_gen_ext16s_i32(offset, offset);
            tcg_gen_add_i32(ea, ea, offset);
            break;
        default:
            g_assert_not_reached();
        }
        break;
    }

    tcg_gen_andi_i32(ea, ea, 0xffff);
    if (mode.indirect) {
        TCGv_i32 pointer = tcg_temp_new_i32();

        gen_ld16_wrapped(pointer, ea);
        ea = pointer;
    }
    *result = ea;
    return true;
}

typedef void (*M6809AluFn)(TCGv_i32 dest, TCGv_i32 src);

static bool do_alu8_imm(TCGv_i32 dest, int imm, M6809AluFn fn)
{
    fn(dest, tcg_constant_i32(imm & 0xff));
    return true;
}

static bool do_alu8_dir(TCGv_i32 dest, int addr, M6809AluFn fn)
{
    TCGv_i32 src = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(src, gen_ea_direct(addr), 0, MO_UB);
    fn(dest, src);
    return true;
}

static bool do_alu8_idx(DisasContext *ctx, TCGv_i32 dest, M6809AluFn fn)
{
    TCGv_i32 ea;
    TCGv_i32 src = tcg_temp_new_i32();

    if (gen_ea_indexed(ctx, &ea)) {
        tcg_gen_qemu_ld_i32(src, ea, 0, MO_UB);
        fn(dest, src);
    }
    return true;
}

static bool do_alu8_ext(TCGv_i32 dest, int addr, M6809AluFn fn)
{
    TCGv_i32 src = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(src, tcg_constant_i32(addr & 0xffff), 0, MO_UB);
    fn(dest, src);
    return true;
}

static bool do_alu16_imm(TCGv_i32 dest, int imm, M6809AluFn fn)
{
    fn(dest, tcg_constant_i32(imm & 0xffff));
    return true;
}

static bool do_alu16_dir(TCGv_i32 dest, int addr, M6809AluFn fn)
{
    TCGv_i32 src = tcg_temp_new_i32();

    gen_ld16_wrapped(src, gen_ea_direct(addr));
    fn(dest, src);
    return true;
}

static bool do_alu16_idx(DisasContext *ctx, TCGv_i32 dest, M6809AluFn fn)
{
    TCGv_i32 ea;
    TCGv_i32 src = tcg_temp_new_i32();

    if (gen_ea_indexed(ctx, &ea)) {
        gen_ld16_wrapped(src, ea);
        fn(dest, src);
    }
    return true;
}

static bool do_alu16_ext(TCGv_i32 dest, int addr, M6809AluFn fn)
{
    TCGv_i32 src = tcg_temp_new_i32();

    gen_ld16_wrapped(src, tcg_constant_i32(addr & 0xffff));
    fn(dest, src);
    return true;
}

static bool do_d_sub16_imm(int imm, bool writeback)
{
    TCGv_i32 d = tcg_temp_new_i32();

    gen_get_d(d);
    gen_sub16(d, tcg_constant_i32(imm & 0xffff), writeback);
    if (writeback) {
        gen_set_d(d);
    }
    return true;
}

static bool do_d_sub16_dir(int addr, bool writeback)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 src = tcg_temp_new_i32();

    gen_get_d(d);
    gen_ld16_wrapped(src, gen_ea_direct(addr));
    gen_sub16(d, src, writeback);
    if (writeback) {
        gen_set_d(d);
    }
    return true;
}

static bool do_d_sub16_idx(DisasContext *ctx, bool writeback)
{
    TCGv_i32 ea;
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 src = tcg_temp_new_i32();

    if (gen_ea_indexed(ctx, &ea)) {
        gen_get_d(d);
        gen_ld16_wrapped(src, ea);
        gen_sub16(d, src, writeback);
        if (writeback) {
            gen_set_d(d);
        }
    }
    return true;
}

static bool do_d_sub16_ext(int addr, bool writeback)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 src = tcg_temp_new_i32();

    gen_get_d(d);
    gen_ld16_wrapped(src, tcg_constant_i32(addr & 0xffff));
    gen_sub16(d, src, writeback);
    if (writeback) {
        gen_set_d(d);
    }
    return true;
}

#define TRANS_ALU8(name, dest, fn)                                      \
static bool trans_##name##_imm(DisasContext *ctx, arg_##name##_imm *a)  \
{                                                                       \
    return do_alu8_imm(dest, a->imm, fn);                               \
}                                                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    return do_alu8_dir(dest, a->addr, fn);                              \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    return do_alu8_idx(ctx, dest, fn);                                  \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    return do_alu8_ext(dest, a->addr, fn);                              \
}

#define TRANS_ADD8(name, dest, with_carry)                              \
static bool trans_##name##_imm(DisasContext *ctx, arg_##name##_imm *a)  \
{                                                                       \
    gen_add8_common(dest, tcg_constant_i32(a->imm & 0xff), with_carry); \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    TCGv_i32 src = tcg_temp_new_i32();                                  \
    tcg_gen_qemu_ld_i32(src, gen_ea_direct(a->addr), 0, MO_UB);         \
    gen_add8_common(dest, src, with_carry);                             \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    TCGv_i32 ea;                                                        \
    TCGv_i32 src = tcg_temp_new_i32();                                  \
    if (gen_ea_indexed(ctx, &ea)) {                                     \
        tcg_gen_qemu_ld_i32(src, ea, 0, MO_UB);                         \
        gen_add8_common(dest, src, with_carry);                         \
    }                                                                   \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    TCGv_i32 src = tcg_temp_new_i32();                                  \
    tcg_gen_qemu_ld_i32(src, tcg_constant_i32(a->addr & 0xffff), 0,     \
                        MO_UB);                                         \
    gen_add8_common(dest, src, with_carry);                             \
    return true;                                                        \
}

#define TRANS_LOGIC8(name, dest, op, writeback)                         \
static bool trans_##name##_imm(DisasContext *ctx, arg_##name##_imm *a)  \
{                                                                       \
    gen_logic8(dest, tcg_constant_i32(a->imm & 0xff), op, writeback);   \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    TCGv_i32 src = tcg_temp_new_i32();                                  \
    tcg_gen_qemu_ld_i32(src, gen_ea_direct(a->addr), 0, MO_UB);         \
    gen_logic8(dest, src, op, writeback);                               \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    TCGv_i32 ea;                                                        \
    TCGv_i32 src = tcg_temp_new_i32();                                  \
    if (gen_ea_indexed(ctx, &ea)) {                                     \
        tcg_gen_qemu_ld_i32(src, ea, 0, MO_UB);                         \
        gen_logic8(dest, src, op, writeback);                           \
    }                                                                   \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    TCGv_i32 src = tcg_temp_new_i32();                                  \
    tcg_gen_qemu_ld_i32(src, tcg_constant_i32(a->addr & 0xffff), 0,     \
                        MO_UB);                                         \
    gen_logic8(dest, src, op, writeback);                               \
    return true;                                                        \
}

#define TRANS_LEA(name, dest, update_z, set_nmi_armed)                  \
static bool trans_##name(DisasContext *ctx, arg_##name *a)              \
{                                                                       \
    return do_lea(ctx, dest, update_z, set_nmi_armed);                  \
}

#define TRANS_ALU16(name, dest, fn)                                     \
static bool trans_##name##_imm(DisasContext *ctx, arg_##name##_imm *a)  \
{                                                                       \
    return do_alu16_imm(dest, a->imm, fn);                              \
}                                                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    return do_alu16_dir(dest, a->addr, fn);                             \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    return do_alu16_idx(ctx, dest, fn);                                 \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    return do_alu16_ext(dest, a->addr, fn);                             \
}

#define TRANS_D_SUB16(name, writeback)                                  \
static bool trans_##name##_imm(DisasContext *ctx, arg_##name##_imm *a)  \
{                                                                       \
    return do_d_sub16_imm(a->imm, writeback);                           \
}                                                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    return do_d_sub16_dir(a->addr, writeback);                          \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    return do_d_sub16_idx(ctx, writeback);                              \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    return do_d_sub16_ext(a->addr, writeback);                          \
}

typedef void (*M6809RmwFn)(TCGv_i32 val);

static bool do_rmw8_dir(int addr, M6809RmwFn fn)
{
    TCGv_i32 ea = gen_ea_direct(addr);
    TCGv_i32 val = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(val, ea, 0, MO_UB);
    fn(val);
    tcg_gen_qemu_st_i32(val, ea, 0, MO_UB);
    return true;
}

static bool do_rmw8_idx(DisasContext *ctx, M6809RmwFn fn)
{
    TCGv_i32 ea;
    TCGv_i32 val = tcg_temp_new_i32();

    if (gen_ea_indexed(ctx, &ea)) {
        tcg_gen_qemu_ld_i32(val, ea, 0, MO_UB);
        fn(val);
        tcg_gen_qemu_st_i32(val, ea, 0, MO_UB);
    }
    return true;
}

static bool do_rmw8_ext(int addr, M6809RmwFn fn)
{
    TCGv_i32 ea = tcg_constant_i32(addr & 0xffff);
    TCGv_i32 val = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(val, ea, 0, MO_UB);
    fn(val);
    tcg_gen_qemu_st_i32(val, ea, 0, MO_UB);
    return true;
}

#define TRANS_RMW8(name, fn)                                            \
static bool trans_##name##A(DisasContext *ctx, arg_##name##A *a)        \
{                                                                       \
    fn(cpu_a);                                                          \
    return true;                                                        \
}                                                                       \
static bool trans_##name##B(DisasContext *ctx, arg_##name##B *a)        \
{                                                                       \
    fn(cpu_b);                                                          \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    return do_rmw8_dir(a->addr, fn);                                    \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    return do_rmw8_idx(ctx, fn);                                        \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    return do_rmw8_ext(a->addr, fn);                                    \
}

static void gen_cond_branch(DisasContext *ctx, TCGv_i32 take, vaddr dest)
{
    TCGLabel *not_taken = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_EQ, take, 0, not_taken);
    gen_goto_tb(ctx, 0, dest);
    gen_set_label(not_taken);
    ctx->base.is_jmp = DISAS_CHAIN;
}

static bool gen_branch(DisasContext *ctx, int cond, vaddr dest)
{
    TCGv_i32 take = tcg_temp_new_i32();
    int mask;
    TCGCond test;

    switch (cond) {
    case 0x0: /* BRA / LBRA */
        gen_goto_tb(ctx, 0, dest);
        ctx->base.is_jmp = DISAS_NORETURN;
        return true;
    case 0x1: /* BRN / LBRN */
        return true;
    case 0x2: /* BHI: !(C | Z) */
        mask = CC_C | CC_Z;
        test = TCG_COND_EQ;
        break;
    case 0x3: /* BLS: C | Z */
        mask = CC_C | CC_Z;
        test = TCG_COND_NE;
        break;
    case 0x4: /* BCC/BHS */
        mask = CC_C;
        test = TCG_COND_EQ;
        break;
    case 0x5: /* BCS/BLO */
        mask = CC_C;
        test = TCG_COND_NE;
        break;
    case 0x6: /* BNE */
        mask = CC_Z;
        test = TCG_COND_EQ;
        break;
    case 0x7: /* BEQ */
        mask = CC_Z;
        test = TCG_COND_NE;
        break;
    case 0x8: /* BVC */
        mask = CC_V;
        test = TCG_COND_EQ;
        break;
    case 0x9: /* BVS */
        mask = CC_V;
        test = TCG_COND_NE;
        break;
    case 0xa: /* BPL */
        mask = CC_N;
        test = TCG_COND_EQ;
        break;
    case 0xb: /* BMI */
        mask = CC_N;
        test = TCG_COND_NE;
        break;
    case 0xc: /* BGE: N == V */
    case 0xd: /* BLT: N != V */
    case 0xe: /* BGT: Z == 0 && N == V */
    case 0xf: /* BLE: Z == 1 || N != V */
        /*
         * Move N down to V's bit position and XOR it with V.
         * For BGT/BLE, include Z in the nonzero test as well.
         */
        tcg_gen_shri_i32(take, cpu_cc, 2);
        tcg_gen_xor_i32(take, take, cpu_cc);
        tcg_gen_andi_i32(take, take, CC_V);
        if (cond >= 0xe) {
            TCGv_i32 z = tcg_temp_new_i32();

            tcg_gen_andi_i32(z, cpu_cc, CC_Z);
            tcg_gen_or_i32(take, take, z);
        }
        tcg_gen_setcondi_i32((cond & 1) ? TCG_COND_NE : TCG_COND_EQ,
                             take, take, 0);
        gen_cond_branch(ctx, take, dest);
        return true;
    default:
        g_assert_not_reached();
    }

    tcg_gen_andi_i32(take, cpu_cc, mask);
    tcg_gen_setcondi_i32(test, take, take, 0);
    gen_cond_branch(ctx, take, dest);
    return true;
}

static bool trans_BRANCH(DisasContext *ctx, arg_BRANCH *a)
{
    return gen_branch(ctx, a->cond,
                      (ctx->base.pc_next + a->disp) & 0xffff);
}

static bool trans_LBRA(DisasContext *ctx, arg_LBRA *a)
{
    return gen_branch(ctx, 0, (ctx->base.pc_next + a->disp) & 0xffff);
}

static bool trans_LBRANCH(DisasContext *ctx, arg_LBRANCH *a)
{
    /* $1020 is not a documented MC6809 opcode. */
    if (a->cond == 0) {
        gen_illegal(ctx);
        return true;
    }
    return gen_branch(ctx, a->cond,
                      (ctx->base.pc_next + a->disp) & 0xffff);
}

static bool trans_NOP(DisasContext *ctx, arg_NOP *a)
{
    return true;
}

static bool trans_DAA(DisasContext *ctx, arg_DAA *a)
{
    gen_helper_daa(tcg_env);
    return true;
}

static bool trans_ABX(DisasContext *ctx, arg_ABX *a)
{
    tcg_gen_add_i32(cpu_x, cpu_x, cpu_b);
    tcg_gen_andi_i32(cpu_x, cpu_x, 0xffff);
    return true;
}

/*
 * LEA writes the already-computed EA into dest. Auto-index side effects
 * occur inside gen_ea_indexed() first, so LEAX ,X++ restores the original X.
 * LEAX/LEAY update Z from the EA; LEAS/LEAU leave CC unchanged.
 */
static bool do_lea(DisasContext *ctx, TCGv_i32 dest, bool update_z,
                   bool set_nmi_armed)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        tcg_gen_mov_i32(dest, ea);
        if (set_nmi_armed) {
            gen_set_nmi_armed();
        }
        if (update_z) {
            TCGv_i32 t = tcg_temp_new_i32();

            tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~CC_Z);
            tcg_gen_setcondi_i32(TCG_COND_EQ, t, dest, 0);
            tcg_gen_shli_i32(t, t, 2);
            tcg_gen_or_i32(cpu_cc, cpu_cc, t);
        }
    }
    return true;
}

TRANS_LEA(LEAX, cpu_x, true, false)
TRANS_LEA(LEAY, cpu_y, true, false)
TRANS_LEA(LEAS, cpu_s, false, true)
TRANS_LEA(LEAU, cpu_u, false, false)

static bool trans_MUL(DisasContext *ctx, arg_MUL *a)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(d, cpu_a, 0xff);
    tcg_gen_andi_i32(t, cpu_b, 0xff);
    tcg_gen_mul_i32(d, d, t);
    tcg_gen_andi_i32(d, d, 0xffff);
    gen_set_d(d);

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_Z | CC_C));
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, d, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);

    /* C from bit 7 of B. */
    tcg_gen_andi_i32(t, cpu_b, 0x80);
    tcg_gen_setcondi_i32(TCG_COND_NE, t, t, 0);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    return true;
}

static bool trans_ANDCC(DisasContext *ctx, arg_ANDCC *a)
{
    tcg_gen_andi_i32(cpu_cc, cpu_cc, a->imm & 0xff);
    gen_exit_after_cc_write(ctx);
    return true;
}

static bool trans_ORCC(DisasContext *ctx, arg_ORCC *a)
{
    tcg_gen_ori_i32(cpu_cc, cpu_cc, a->imm & 0xff);
    return true;
}

TRANS_ADD8(ADDA, cpu_a, false)
TRANS_ADD8(ADDB, cpu_b, false)

static bool trans_ADDD_imm(DisasContext *ctx, arg_ADDD_imm *a)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 src = tcg_constant_i32(a->imm & 0xffff);

    gen_get_d(d);
    gen_add16(d, src);
    gen_set_d(d);
    return true;
}

static bool trans_ADDD_dir(DisasContext *ctx, arg_ADDD_dir *a)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 src = tcg_temp_new_i32();

    gen_get_d(d);
    gen_ld16_wrapped(src, gen_ea_direct(a->addr));
    gen_add16(d, src);
    gen_set_d(d);
    return true;
}

static bool trans_ADDD_idx(DisasContext *ctx, arg_ADDD_idx *a)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 ea;
    TCGv_i32 src = tcg_temp_new_i32();

    if (gen_ea_indexed(ctx, &ea)) {
        gen_get_d(d);
        gen_ld16_wrapped(src, ea);
        gen_add16(d, src);
        gen_set_d(d);
    }
    return true;
}

static bool trans_ADDD_ext(DisasContext *ctx, arg_ADDD_ext *a)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 src = tcg_temp_new_i32();
    TCGv_i32 ea = tcg_constant_i32(a->addr & 0xffff);

    gen_get_d(d);
    gen_ld16_wrapped(src, ea);
    gen_add16(d, src);
    gen_set_d(d);
    return true;
}

TRANS_LOGIC8(ANDA, cpu_a, M6809_LOGIC_AND, true)
TRANS_LOGIC8(ANDB, cpu_b, M6809_LOGIC_AND, true)
TRANS_LOGIC8(ORA, cpu_a, M6809_LOGIC_OR, true)
TRANS_LOGIC8(ORB, cpu_b, M6809_LOGIC_OR, true)
TRANS_LOGIC8(EORA, cpu_a, M6809_LOGIC_EOR, true)
TRANS_LOGIC8(EORB, cpu_b, M6809_LOGIC_EOR, true)
TRANS_LOGIC8(BITA, cpu_a, M6809_LOGIC_AND, false)
TRANS_LOGIC8(BITB, cpu_b, M6809_LOGIC_AND, false)
TRANS_ALU8(SUBA, cpu_a, gen_sub8)
TRANS_ALU8(SUBB, cpu_b, gen_sub8)
TRANS_ALU8(SBCA, cpu_a, gen_sbc8)
TRANS_ALU8(SBCB, cpu_b, gen_sbc8)
TRANS_ADD8(ADCA, cpu_a, true)
TRANS_ADD8(ADCB, cpu_b, true)
TRANS_ALU8(CMPA, cpu_a, gen_cmp8)
TRANS_ALU8(CMPB, cpu_b, gen_cmp8)
TRANS_ALU16(CMPX, cpu_x, gen_cmp16)
TRANS_ALU16(CMPY, cpu_y, gen_cmp16)
TRANS_ALU16(CMPU, cpu_u, gen_cmp16)
TRANS_ALU16(CMPS, cpu_s, gen_cmp16)
TRANS_D_SUB16(SUBD, true)
TRANS_D_SUB16(CMPD, false)
TRANS_RMW8(NEG, gen_neg8)
TRANS_RMW8(COM, gen_com8)
TRANS_RMW8(LSR, gen_lsr8)
TRANS_RMW8(ROR, gen_ror8)
TRANS_RMW8(ASR, gen_asr8)
TRANS_RMW8(ASL, gen_asl8)
TRANS_RMW8(ROL, gen_rol8)
TRANS_RMW8(DEC, gen_dec8)
TRANS_RMW8(INC, gen_inc8)
TRANS_RMW8(CLR, gen_clr8)

static bool trans_TSTA(DisasContext *ctx, arg_TSTA *a)
{
    gen_logic_cc8(cpu_a);
    return true;
}

static bool trans_TSTB(DisasContext *ctx, arg_TSTB *a)
{
    gen_logic_cc8(cpu_b);
    return true;
}

static bool trans_TST_dir(DisasContext *ctx, arg_TST_dir *a)
{
    TCGv_i32 val = tcg_temp_new_i32();

    gen_ld8(val, gen_ea_direct(a->addr));
    return true;
}

static bool trans_TST_idx(DisasContext *ctx, arg_TST_idx *a)
{
    TCGv_i32 ea;
    TCGv_i32 val = tcg_temp_new_i32();

    if (gen_ea_indexed(ctx, &ea)) {
        gen_ld8(val, ea);
    }
    return true;
}

static bool trans_TST_ext(DisasContext *ctx, arg_TST_ext *a)
{
    TCGv_i32 val = tcg_temp_new_i32();

    gen_ld8(val, tcg_constant_i32(a->addr & 0xffff));
    return true;
}

static bool trans_PSHS(DisasContext *ctx, arg_PSHS *a)
{
    gen_psh(ctx, cpu_s, a->post, false);
    return true;
}

static bool trans_PULS(DisasContext *ctx, arg_PULS *a)
{
    gen_pul(ctx, cpu_s, a->post, false);
    return true;
}

static bool trans_PSHU(DisasContext *ctx, arg_PSHU *a)
{
    gen_psh(ctx, cpu_u, a->post, true);
    return true;
}

static bool trans_PULU(DisasContext *ctx, arg_PULU *a)
{
    gen_pul(ctx, cpu_u, a->post, true);
    return true;
}

static bool trans_RTS(DisasContext *ctx, arg_RTS *a)
{
    gen_pull16(cpu_s, cpu_pc);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

/*
 * SWI/SWI2/SWI3 set E, push the entire machine state, then fetch the
 * service vector. Only SWI sets I and F, and it does so after stacking
 * so the saved CC preserves the previous mask bits. Native mode adds W
 * (E:F) between DP and D; PSHS/PULS postbytes are unchanged.
 */
static bool do_swi(DisasContext *ctx, uint32_t vector, bool mask_if)
{
    tcg_gen_ori_i32(cpu_cc, cpu_cc, CC_E);
    gen_stack_entire(ctx);
    if (mask_if) {
        tcg_gen_ori_i32(cpu_cc, cpu_cc, CC_I | CC_F);
    }
    gen_ld16_wrapped(cpu_pc, tcg_constant_i32(vector));
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_SWI(DisasContext *ctx, arg_SWI *a)
{
    return do_swi(ctx, M6809_VEC_SWI, true);
}

static bool trans_SWI2(DisasContext *ctx, arg_SWI2 *a)
{
    return do_swi(ctx, M6809_VEC_SWI2, false);
}

static bool trans_SWI3(DisasContext *ctx, arg_SWI3 *a)
{
    return do_swi(ctx, M6809_VEC_SWI3, false);
}

static bool trans_RTI(DisasContext *ctx, arg_RTI *a)
{
    TCGv_i32 e = tcg_temp_new_i32();
    TCGLabel *entire = gen_new_label();
    TCGLabel *done = gen_new_label();

    gen_pull8(cpu_s, cpu_cc);
    tcg_gen_andi_i32(e, cpu_cc, CC_E);
    tcg_gen_brcondi_i32(TCG_COND_NE, e, 0, entire);

    gen_pull16(cpu_s, cpu_pc);
    tcg_gen_br(done);

    gen_set_label(entire);
    /* CC was already pulled; restore the rest of the entire state. */
    gen_unstack_entire(ctx);

    gen_set_label(done);
    /* The restored CC may have cleared I or F, so re-test interrupts. */
    ctx->base.is_jmp = DISAS_EXIT;
    return true;
}

static bool trans_BSR(DisasContext *ctx, arg_BSR *a)
{
    vaddr dest = (ctx->base.pc_next + a->disp) & 0xffff;

    gen_push16(cpu_s, tcg_constant_i32(ctx->base.pc_next & 0xffff));
    gen_goto_tb(ctx, 0, dest);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_LBSR(DisasContext *ctx, arg_LBSR *a)
{
    vaddr dest = (ctx->base.pc_next + a->disp) & 0xffff;

    gen_push16(cpu_s, tcg_constant_i32(ctx->base.pc_next & 0xffff));
    gen_goto_tb(ctx, 0, dest);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_JSR_dir(DisasContext *ctx, arg_JSR_dir *a)
{
    TCGv_i32 ea = gen_ea_direct(a->addr);

    gen_push16(cpu_s, tcg_constant_i32(ctx->base.pc_next & 0xffff));
    tcg_gen_mov_i32(cpu_pc, ea);
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_JSR_idx(DisasContext *ctx, arg_JSR_idx *a)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_push16(cpu_s, tcg_constant_i32(ctx->base.pc_next & 0xffff));
        tcg_gen_mov_i32(cpu_pc, ea);
        ctx->base.is_jmp = DISAS_JUMP;
    }
    return true;
}

static bool trans_JSR_ext(DisasContext *ctx, arg_JSR_ext *a)
{
    gen_push16(cpu_s, tcg_constant_i32(ctx->base.pc_next & 0xffff));
    gen_goto_tb(ctx, 0, a->addr & 0xffff);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_JMP_dir(DisasContext *ctx, arg_JMP_dir *a)
{
    tcg_gen_mov_i32(cpu_pc, gen_ea_direct(a->addr));
    ctx->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_JMP_idx(DisasContext *ctx, arg_JMP_idx *a)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        tcg_gen_mov_i32(cpu_pc, ea);
        ctx->base.is_jmp = DISAS_JUMP;
    }
    return true;
}

static bool trans_JMP_ext(DisasContext *ctx, arg_JMP_ext *a)
{
    gen_goto_tb(ctx, 0, a->addr & 0xffff);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_LDA_imm(DisasContext *ctx, arg_LDA_imm *a)
{
    tcg_gen_movi_i32(cpu_a, a->imm & 0xff);
    gen_logic_cc8(cpu_a);
    return true;
}

static bool trans_LDA_dir(DisasContext *ctx, arg_LDA_dir *a)
{
    gen_ld8(cpu_a, gen_ea_direct(a->addr));
    return true;
}

static bool trans_LDA_idx(DisasContext *ctx, arg_LDA_idx *a)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_ld8(cpu_a, ea);
    }
    return true;
}

static bool trans_LDA_ext(DisasContext *ctx, arg_LDA_ext *a)
{
    gen_ld8(cpu_a, tcg_constant_i32(a->addr & 0xffff));
    return true;
}

static bool trans_LDB_imm(DisasContext *ctx, arg_LDB_imm *a)
{
    tcg_gen_movi_i32(cpu_b, a->imm & 0xff);
    gen_logic_cc8(cpu_b);
    return true;
}

static bool trans_LDB_dir(DisasContext *ctx, arg_LDB_dir *a)
{
    gen_ld8(cpu_b, gen_ea_direct(a->addr));
    return true;
}

static bool trans_LDB_idx(DisasContext *ctx, arg_LDB_idx *a)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_ld8(cpu_b, ea);
    }
    return true;
}

static bool trans_LDB_ext(DisasContext *ctx, arg_LDB_ext *a)
{
    gen_ld8(cpu_b, tcg_constant_i32(a->addr & 0xffff));
    return true;
}

static bool trans_LDD_imm(DisasContext *ctx, arg_LDD_imm *a)
{
    TCGv_i32 d = tcg_constant_i32(a->imm & 0xffff);

    gen_set_d(d);
    gen_logic_cc16(d);
    return true;
}

static bool trans_LDD_dir(DisasContext *ctx, arg_LDD_dir *a)
{
    TCGv_i32 d = tcg_temp_new_i32();

    gen_ld16(d, gen_ea_direct(a->addr));
    gen_set_d(d);
    return true;
}

static bool trans_LDD_idx(DisasContext *ctx, arg_LDD_idx *a)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_ld16(d, ea);
        gen_set_d(d);
    }
    return true;
}

static bool trans_LDD_ext(DisasContext *ctx, arg_LDD_ext *a)
{
    TCGv_i32 d = tcg_temp_new_i32();

    gen_ld16(d, tcg_constant_i32(a->addr & 0xffff));
    gen_set_d(d);
    return true;
}

static bool do_ld16_imm(TCGv_i32 dest, int imm)
{
    tcg_gen_movi_i32(dest, imm & 0xffff);
    gen_logic_cc16(dest);
    return true;
}

static bool do_ld16_dir(TCGv_i32 dest, int addr)
{
    gen_ld16(dest, gen_ea_direct(addr));
    return true;
}

static bool do_ld16_idx(DisasContext *ctx, TCGv_i32 dest)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_ld16(dest, ea);
    }
    return true;
}

static bool do_ld16_ext(TCGv_i32 dest, int addr)
{
    gen_ld16(dest, tcg_constant_i32(addr & 0xffff));
    return true;
}

static bool do_st8_dir(TCGv_i32 src, int addr)
{
    gen_st8(gen_ea_direct(addr), src);
    return true;
}

static bool do_st8_idx(DisasContext *ctx, TCGv_i32 src)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_st8(ea, src);
    }
    return true;
}

static bool do_st8_ext(TCGv_i32 src, int addr)
{
    gen_st8(tcg_constant_i32(addr & 0xffff), src);
    return true;
}

static bool do_st16_dir(TCGv_i32 src, int addr)
{
    gen_st16(gen_ea_direct(addr), src);
    return true;
}

static bool do_st16_idx(DisasContext *ctx, TCGv_i32 src)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_st16(ea, src);
    }
    return true;
}

static bool do_st16_ext(TCGv_i32 src, int addr)
{
    gen_st16(tcg_constant_i32(addr & 0xffff), src);
    return true;
}

static bool do_std(TCGv_i32 ea)
{
    TCGv_i32 d = tcg_temp_new_i32();

    gen_get_d(d);
    gen_st16(ea, d);
    return true;
}

static bool trans_STA_dir(DisasContext *ctx, arg_STA_dir *a)
{
    return do_st8_dir(cpu_a, a->addr);
}

static bool trans_STA_idx(DisasContext *ctx, arg_STA_idx *a)
{
    return do_st8_idx(ctx, cpu_a);
}

static bool trans_STA_ext(DisasContext *ctx, arg_STA_ext *a)
{
    return do_st8_ext(cpu_a, a->addr);
}

static bool trans_STB_dir(DisasContext *ctx, arg_STB_dir *a)
{
    return do_st8_dir(cpu_b, a->addr);
}

static bool trans_STB_idx(DisasContext *ctx, arg_STB_idx *a)
{
    return do_st8_idx(ctx, cpu_b);
}

static bool trans_STB_ext(DisasContext *ctx, arg_STB_ext *a)
{
    return do_st8_ext(cpu_b, a->addr);
}

static bool trans_STD_dir(DisasContext *ctx, arg_STD_dir *a)
{
    return do_std(gen_ea_direct(a->addr));
}

static bool trans_STD_idx(DisasContext *ctx, arg_STD_idx *a)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        do_std(ea);
    }
    return true;
}

static bool trans_STD_ext(DisasContext *ctx, arg_STD_ext *a)
{
    return do_std(tcg_constant_i32(a->addr & 0xffff));
}

static bool trans_LDX_imm(DisasContext *ctx, arg_LDX_imm *a)
{
    return do_ld16_imm(cpu_x, a->imm);
}

static bool trans_LDX_dir(DisasContext *ctx, arg_LDX_dir *a)
{
    return do_ld16_dir(cpu_x, a->addr);
}

static bool trans_LDX_idx(DisasContext *ctx, arg_LDX_idx *a)
{
    return do_ld16_idx(ctx, cpu_x);
}

static bool trans_LDX_ext(DisasContext *ctx, arg_LDX_ext *a)
{
    return do_ld16_ext(cpu_x, a->addr);
}

static bool trans_STX_dir(DisasContext *ctx, arg_STX_dir *a)
{
    return do_st16_dir(cpu_x, a->addr);
}

static bool trans_STX_idx(DisasContext *ctx, arg_STX_idx *a)
{
    return do_st16_idx(ctx, cpu_x);
}

static bool trans_STX_ext(DisasContext *ctx, arg_STX_ext *a)
{
    return do_st16_ext(cpu_x, a->addr);
}

static bool trans_LDU_imm(DisasContext *ctx, arg_LDU_imm *a)
{
    return do_ld16_imm(cpu_u, a->imm);
}

static bool trans_LDU_dir(DisasContext *ctx, arg_LDU_dir *a)
{
    return do_ld16_dir(cpu_u, a->addr);
}

static bool trans_LDU_idx(DisasContext *ctx, arg_LDU_idx *a)
{
    return do_ld16_idx(ctx, cpu_u);
}

static bool trans_LDU_ext(DisasContext *ctx, arg_LDU_ext *a)
{
    return do_ld16_ext(cpu_u, a->addr);
}

static bool trans_STU_dir(DisasContext *ctx, arg_STU_dir *a)
{
    return do_st16_dir(cpu_u, a->addr);
}

static bool trans_STU_idx(DisasContext *ctx, arg_STU_idx *a)
{
    return do_st16_idx(ctx, cpu_u);
}

static bool trans_STU_ext(DisasContext *ctx, arg_STU_ext *a)
{
    return do_st16_ext(cpu_u, a->addr);
}

static bool trans_LDY_imm(DisasContext *ctx, arg_LDY_imm *a)
{
    return do_ld16_imm(cpu_y, a->imm);
}

static bool trans_LDY_dir(DisasContext *ctx, arg_LDY_dir *a)
{
    return do_ld16_dir(cpu_y, a->addr);
}

static bool trans_LDY_idx(DisasContext *ctx, arg_LDY_idx *a)
{
    return do_ld16_idx(ctx, cpu_y);
}

static bool trans_LDY_ext(DisasContext *ctx, arg_LDY_ext *a)
{
    return do_ld16_ext(cpu_y, a->addr);
}

static bool trans_STY_dir(DisasContext *ctx, arg_STY_dir *a)
{
    return do_st16_dir(cpu_y, a->addr);
}

static bool trans_STY_idx(DisasContext *ctx, arg_STY_idx *a)
{
    return do_st16_idx(ctx, cpu_y);
}

static bool trans_STY_ext(DisasContext *ctx, arg_STY_ext *a)
{
    return do_st16_ext(cpu_y, a->addr);
}

static bool trans_LDS_imm(DisasContext *ctx, arg_LDS_imm *a)
{
    gen_set_nmi_armed();
    return do_ld16_imm(cpu_s, a->imm);
}

static bool trans_LDS_dir(DisasContext *ctx, arg_LDS_dir *a)
{
    gen_set_nmi_armed();
    return do_ld16_dir(cpu_s, a->addr);
}

static bool trans_LDS_idx(DisasContext *ctx, arg_LDS_idx *a)
{
    TCGv_i32 ea;

    if (gen_ea_indexed(ctx, &ea)) {
        gen_ld16(cpu_s, ea);
        gen_set_nmi_armed();
    }
    return true;
}

static bool trans_LDS_ext(DisasContext *ctx, arg_LDS_ext *a)
{
    gen_set_nmi_armed();
    return do_ld16_ext(cpu_s, a->addr);
}

static bool trans_STS_dir(DisasContext *ctx, arg_STS_dir *a)
{
    return do_st16_dir(cpu_s, a->addr);
}

static bool trans_STS_idx(DisasContext *ctx, arg_STS_idx *a)
{
    return do_st16_idx(ctx, cpu_s);
}

static bool trans_STS_ext(DisasContext *ctx, arg_STS_ext *a)
{
    return do_st16_ext(cpu_s, a->addr);
}

/*
 * TFR/EXG postbyte: source in bits 7:4, destination in bits 3:0.
 * 6809: 0=D 1=X 2=Y 3=U 4=S 5=PC  8=A 9=B A=CC B=DP
 * 6309 adds: 6=W 7=V  C/D=Zero  E=E F=F
 *
 * 6809 8→16 fills the high byte with $FF. 6309 (MAME / Atkinson) reads
 * 8-bit registers as a duplicated byte and writes A/E/DP from the high
 * byte, B/F/CC from the low byte. Zero reads 0 and discards writes.
 */
static bool tfr_is_16(int r)
{
    return r <= 5;
}

static bool tfr_has_6309(DisasContext *ctx)
{
    return m6809_feature(ctx->env, M6809_FEATURE_6309);
}

static void tfr_dup8(TCGv_i32 tmp, TCGv_i32 r)
{
    tcg_gen_deposit_i32(tmp, r, r, 8, 8);
}

static void tfr_write_hi(TCGv_i32 dest, TCGv_i32 val)
{
    tcg_gen_shri_i32(dest, val, 8);
    tcg_gen_ext8u_i32(dest, dest);
}

static TCGv_i32 tfr_read(DisasContext *ctx, int r, TCGv_i32 tmp)
{
    bool hd6309 = tfr_has_6309(ctx);

    switch (r) {
    case 0:
        gen_get_d(tmp);
        return tmp;
    case 1:
        return cpu_x;
    case 2:
        return cpu_y;
    case 3:
        return cpu_u;
    case 4:
        return cpu_s;
    case 5:
        tcg_gen_movi_i32(tmp, ctx->base.pc_next);
        return tmp;
    case 6:
        if (!hd6309) {
            return NULL;
        }
        gen_get_w(tmp);
        return tmp;
    case 7:
        if (!hd6309) {
            return NULL;
        }
        return cpu_v;
    case 8:
        if (hd6309) {
            tfr_dup8(tmp, cpu_a);
            return tmp;
        }
        return cpu_a;
    case 9:
        if (hd6309) {
            tfr_dup8(tmp, cpu_b);
            return tmp;
        }
        return cpu_b;
    case 10:
        if (hd6309) {
            tfr_dup8(tmp, cpu_cc);
            return tmp;
        }
        return cpu_cc;
    case 11:
        if (hd6309) {
            tfr_dup8(tmp, cpu_dp);
            return tmp;
        }
        return cpu_dp;
    case 12:
    case 13:
        if (!hd6309) {
            return NULL;
        }
        tcg_gen_movi_i32(tmp, 0);
        return tmp;
    case 14:
        if (!hd6309) {
            return NULL;
        }
        tfr_dup8(tmp, cpu_e);
        return tmp;
    case 15:
        if (!hd6309) {
            return NULL;
        }
        tfr_dup8(tmp, cpu_f);
        return tmp;
    default:
        return NULL;
    }
}

static void tfr_write(DisasContext *ctx, int r, TCGv_i32 val)
{
    bool hd6309 = tfr_has_6309(ctx);

    switch (r) {
    case 0:
        gen_set_d(val);
        break;
    case 1:
        tcg_gen_andi_i32(cpu_x, val, 0xffff);
        break;
    case 2:
        tcg_gen_andi_i32(cpu_y, val, 0xffff);
        break;
    case 3:
        tcg_gen_andi_i32(cpu_u, val, 0xffff);
        break;
    case 4:
        tcg_gen_andi_i32(cpu_s, val, 0xffff);
        gen_set_nmi_armed();
        break;
    case 5:
        tcg_gen_andi_i32(cpu_pc, val, 0xffff);
        ctx->base.is_jmp = DISAS_JUMP;
        break;
    case 6:
        gen_set_w(val);
        break;
    case 7:
        tcg_gen_andi_i32(cpu_v, val, 0xffff);
        break;
    case 8:
        if (hd6309) {
            tfr_write_hi(cpu_a, val);
        } else {
            tcg_gen_ext8u_i32(cpu_a, val);
        }
        break;
    case 9:
        tcg_gen_ext8u_i32(cpu_b, val);
        break;
    case 10:
        tcg_gen_ext8u_i32(cpu_cc, val);
        break;
    case 11:
        if (hd6309) {
            tfr_write_hi(cpu_dp, val);
        } else {
            tcg_gen_ext8u_i32(cpu_dp, val);
        }
        break;
    case 12:
    case 13:
        break;
    case 14:
        tfr_write_hi(cpu_e, val);
        break;
    case 15:
        tcg_gen_ext8u_i32(cpu_f, val);
        break;
    default:
        break;
    }
}

static bool trans_TFR(DisasContext *ctx, arg_TFR *a)
{
    int src = (a->post >> 4) & 0xf;
    int dst = a->post & 0xf;
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 val;
    bool hd6309 = tfr_has_6309(ctx);

    val = tfr_read(ctx, src, tmp);
    if (!val) {
        gen_illegal(ctx);
        return true;
    }
    if (!hd6309 && !tfr_is_16(dst) && (dst < 8 || dst > 11)) {
        gen_illegal(ctx);
        return true;
    }

    if (!hd6309) {
        if (tfr_is_16(src) && !tfr_is_16(dst)) {
            tcg_gen_ext8u_i32(tmp, val);
            val = tmp;
        } else if (!tfr_is_16(src) && tfr_is_16(dst)) {
            /* 8-bit source to 16-bit dest: high byte is $FF. */
            tcg_gen_ori_i32(tmp, val, 0xff00);
            val = tmp;
        }
    }

    tfr_write(ctx, dst, val);
    if (dst == 10) {
        gen_exit_after_cc_write(ctx);
    }
    return true;
}

static bool trans_EXG(DisasContext *ctx, arg_EXG *a)
{
    int r1 = (a->post >> 4) & 0xf;
    int r2 = a->post & 0xf;
    TCGv_i32 snap1 = tcg_temp_new_i32();
    TCGv_i32 snap2 = tcg_temp_new_i32();
    TCGv_i32 val1;
    TCGv_i32 val2;
    bool hd6309 = tfr_has_6309(ctx);

    val1 = tfr_read(ctx, r1, snap1);
    val2 = tfr_read(ctx, r2, snap2);
    if (!val1 || !val2) {
        gen_illegal(ctx);
        return true;
    }

    tcg_gen_mov_i32(snap1, val1);
    tcg_gen_mov_i32(snap2, val2);

    if (!hd6309) {
        if (tfr_is_16(r1) && !tfr_is_16(r2)) {
            tcg_gen_ori_i32(snap2, snap2, 0xff00);
            tcg_gen_ext8u_i32(snap1, snap1);
        } else if (!tfr_is_16(r1) && tfr_is_16(r2)) {
            tcg_gen_ext8u_i32(snap2, snap2);
            tcg_gen_ori_i32(snap1, snap1, 0xff00);
        }
    }

    tfr_write(ctx, r1, snap2);
    tfr_write(ctx, r2, snap1);
    if (r1 == 10 || r2 == 10) {
        gen_exit_after_cc_write(ctx);
    }
    return true;
}

static bool trans_CWAI(DisasContext *ctx, arg_CWAI *a)
{
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next & 0xffff);
    gen_helper_cwai(tcg_env, tcg_constant_i32(a->imm & 0xff));
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_INH14(DisasContext *ctx, arg_INH14 *a)
{
    TCGv_i32 w;
    TCGv_i32 d;
    TCGv_i32 t;

    if (!require_6309(ctx)) {
        return true;
    }

    /* SEXW: copy W's sign into D. N from W bit 15; Z if Q is 0; V/C unchanged. */
    w = tcg_temp_new_i32();
    d = tcg_temp_new_i32();
    t = tcg_temp_new_i32();
    gen_get_w(w);
    tcg_gen_ext16s_i32(d, w);
    tcg_gen_sari_i32(d, d, 15);
    tcg_gen_andi_i32(d, d, 0xffff);
    gen_set_d(d);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z));
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, w, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_shri_i32(t, w, 15);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    return true;
}

static bool do_im_mem(DisasContext *ctx, TCGv_i32 ea, int imm,
                      M6809LogicOp op, bool writeback)
{
    TCGv_i32 val = tcg_temp_new_i32();

    tcg_gen_qemu_ld_i32(val, ea, 0, MO_UB);
    gen_logic8(val, tcg_constant_i32(imm & 0xff), op, true);
    if (writeback) {
        tcg_gen_qemu_st_i32(val, ea, 0, MO_UB);
    }
    return true;
}

#define TRANS_IM_MEM(name, op, writeback)                               \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    return do_im_mem(ctx, gen_ea_direct(a->addr), a->imm, op, writeback); \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    TCGv_i32 ea;                                                        \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    if (gen_ea_indexed(ctx, &ea)) {                                     \
        do_im_mem(ctx, ea, a->imm, op, writeback);                      \
    }                                                                   \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    return do_im_mem(ctx, tcg_constant_i32(a->addr & 0xffff),           \
                     a->imm, op, writeback);                            \
}

TRANS_IM_MEM(AIM, M6809_LOGIC_AND, true)
TRANS_IM_MEM(OIM, M6809_LOGIC_OR, true)
TRANS_IM_MEM(EIM, M6809_LOGIC_EOR, true)
TRANS_IM_MEM(TIM, M6809_LOGIC_AND, false)

#define TRANS_6309_LD8(name, dest)                                      \
static bool trans_##name##_imm(DisasContext *ctx, arg_##name##_imm *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    tcg_gen_movi_i32(dest, a->imm & 0xff);                              \
    gen_logic_cc8(dest);                                                \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    gen_ld8(dest, gen_ea_direct(a->addr));                              \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    TCGv_i32 ea;                                                        \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    if (gen_ea_indexed(ctx, &ea)) {                                     \
        gen_ld8(dest, ea);                                              \
    }                                                                   \
    return true;                                                        \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    gen_ld8(dest, tcg_constant_i32(a->addr & 0xffff));                  \
    return true;                                                        \
}

#define TRANS_6309_ST8(name, src)                                       \
static bool trans_##name##_dir(DisasContext *ctx, arg_##name##_dir *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    return do_st8_dir(src, a->addr);                                    \
}                                                                       \
static bool trans_##name##_idx(DisasContext *ctx, arg_##name##_idx *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    return do_st8_idx(ctx, src);                                        \
}                                                                       \
static bool trans_##name##_ext(DisasContext *ctx, arg_##name##_ext *a)  \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    return do_st8_ext(src, a->addr);                                    \
}

TRANS_6309_LD8(LDE, cpu_e)
TRANS_6309_ST8(STE, cpu_e)
TRANS_6309_LD8(LDF, cpu_f)
TRANS_6309_ST8(STF, cpu_f)

static bool trans_LDW_imm(DisasContext *ctx, arg_LDW_imm *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    tcg_gen_movi_i32(w, a->imm & 0xffff);
    gen_logic_cc16(w);
    gen_set_w(w);
    return true;
}

static bool trans_LDW_dir(DisasContext *ctx, arg_LDW_dir *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    gen_ld16(w, gen_ea_direct(a->addr));
    gen_set_w(w);
    return true;
}

static bool trans_LDW_idx(DisasContext *ctx, arg_LDW_idx *a)
{
    TCGv_i32 ea;
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    if (gen_ea_indexed(ctx, &ea)) {
        w = tcg_temp_new_i32();
        gen_ld16(w, ea);
        gen_set_w(w);
    }
    return true;
}

static bool trans_LDW_ext(DisasContext *ctx, arg_LDW_ext *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    gen_ld16(w, tcg_constant_i32(a->addr & 0xffff));
    gen_set_w(w);
    return true;
}

static bool do_stw(DisasContext *ctx, TCGv_i32 ea)
{
    TCGv_i32 w = tcg_temp_new_i32();

    gen_get_w(w);
    gen_st16(ea, w);
    return true;
}

static bool trans_STW_dir(DisasContext *ctx, arg_STW_dir *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_stw(ctx, gen_ea_direct(a->addr));
}

static bool trans_STW_idx(DisasContext *ctx, arg_STW_idx *a)
{
    TCGv_i32 ea;

    if (!require_6309(ctx)) {
        return true;
    }
    if (gen_ea_indexed(ctx, &ea)) {
        do_stw(ctx, ea);
    }
    return true;
}

static bool trans_STW_ext(DisasContext *ctx, arg_STW_ext *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_stw(ctx, tcg_constant_i32(a->addr & 0xffff));
}

static void gen_logic_cc32(TCGv_i32 hi, TCGv_i32 lo)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~(CC_N | CC_Z | CC_V));
    tcg_gen_or_i32(t, hi, lo);
    tcg_gen_setcondi_i32(TCG_COND_EQ, t, t, 0);
    tcg_gen_shli_i32(t, t, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
    tcg_gen_shri_i32(t, hi, 15);
    tcg_gen_andi_i32(t, t, 1);
    tcg_gen_shli_i32(t, t, 3);
    tcg_gen_or_i32(cpu_cc, cpu_cc, t);
}

static void gen_ld32(TCGv_i32 hi, TCGv_i32 lo, TCGv_i32 ea)
{
    TCGv_i32 ea2 = tcg_temp_new_i32();

    gen_ld16_wrapped(hi, ea);
    tcg_gen_addi_i32(ea2, ea, 2);
    tcg_gen_andi_i32(ea2, ea2, 0xffff);
    gen_ld16_wrapped(lo, ea2);
    gen_logic_cc32(hi, lo);
}

static void gen_st32(TCGv_i32 ea, TCGv_i32 hi, TCGv_i32 lo)
{
    TCGv_i32 ea2 = tcg_temp_new_i32();

    gen_st16_wrapped(ea, hi);
    tcg_gen_addi_i32(ea2, ea, 2);
    tcg_gen_andi_i32(ea2, ea2, 0xffff);
    gen_st16_wrapped(ea2, lo);
    gen_logic_cc32(hi, lo);
}

static bool trans_LDQ_imm(DisasContext *ctx, arg_LDQ_imm *a)
{
    uint32_t imm;
    TCGv_i32 d;
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    imm = ((uint32_t)indexed_fetch16(ctx) << 16) | indexed_fetch16(ctx);
    d = tcg_constant_i32(imm >> 16);
    w = tcg_constant_i32(imm & 0xffff);
    gen_set_d(d);
    gen_set_w(w);
    gen_logic_cc32(d, w);
    return true;
}

static bool do_ldq(DisasContext *ctx, TCGv_i32 ea)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 w = tcg_temp_new_i32();

    gen_ld32(d, w, ea);
    gen_set_d(d);
    gen_set_w(w);
    return true;
}

static bool trans_LDQ_dir(DisasContext *ctx, arg_LDQ_dir *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_ldq(ctx, gen_ea_direct(a->addr));
}

static bool trans_LDQ_idx(DisasContext *ctx, arg_LDQ_idx *a)
{
    TCGv_i32 ea;

    if (!require_6309(ctx)) {
        return true;
    }
    if (gen_ea_indexed(ctx, &ea)) {
        do_ldq(ctx, ea);
    }
    return true;
}

static bool trans_LDQ_ext(DisasContext *ctx, arg_LDQ_ext *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_ldq(ctx, tcg_constant_i32(a->addr & 0xffff));
}

static bool do_stq(DisasContext *ctx, TCGv_i32 ea)
{
    TCGv_i32 d = tcg_temp_new_i32();
    TCGv_i32 w = tcg_temp_new_i32();

    gen_get_d(d);
    gen_get_w(w);
    gen_st32(ea, d, w);
    return true;
}

static bool trans_STQ_dir(DisasContext *ctx, arg_STQ_dir *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_stq(ctx, gen_ea_direct(a->addr));
}

static bool trans_STQ_idx(DisasContext *ctx, arg_STQ_idx *a)
{
    TCGv_i32 ea;

    if (!require_6309(ctx)) {
        return true;
    }
    if (gen_ea_indexed(ctx, &ea)) {
        do_stq(ctx, ea);
    }
    return true;
}

static bool trans_STQ_ext(DisasContext *ctx, arg_STQ_ext *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_stq(ctx, tcg_constant_i32(a->addr & 0xffff));
}

#define TRANS_6309_RMW16_D(name, fn)                                    \
static bool trans_##name(DisasContext *ctx, arg_##name *a)              \
{                                                                       \
    TCGv_i32 d;                                                         \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    d = tcg_temp_new_i32();                                             \
    gen_get_d(d);                                                       \
    fn(d);                                                              \
    gen_set_d(d);                                                       \
    return true;                                                        \
}

#define TRANS_6309_RMW16_W(name, fn)                                    \
static bool trans_##name(DisasContext *ctx, arg_##name *a)              \
{                                                                       \
    TCGv_i32 w;                                                         \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    w = tcg_temp_new_i32();                                             \
    gen_get_w(w);                                                       \
    fn(w);                                                              \
    gen_set_w(w);                                                       \
    return true;                                                        \
}

#define TRANS_6309_RMW8(name, dest, fn)                                 \
static bool trans_##name(DisasContext *ctx, arg_##name *a)              \
{                                                                       \
    if (!require_6309(ctx)) {                                           \
        return true;                                                    \
    }                                                                   \
    fn(dest);                                                           \
    return true;                                                        \
}

TRANS_6309_RMW16_D(NEGD, gen_neg16)
TRANS_6309_RMW16_D(COMD, gen_com16)
TRANS_6309_RMW16_D(LSRD, gen_lsr16)
TRANS_6309_RMW16_D(RORD, gen_ror16)
TRANS_6309_RMW16_D(ASRD, gen_asr16)
TRANS_6309_RMW16_D(ASLD, gen_asl16)
TRANS_6309_RMW16_D(ROLD, gen_rol16)
TRANS_6309_RMW16_D(DECD, gen_dec16)
TRANS_6309_RMW16_D(INCD, gen_inc16)
TRANS_6309_RMW16_D(CLRD, gen_clr16)

TRANS_6309_RMW16_W(COMW, gen_com16)
TRANS_6309_RMW16_W(LSRW, gen_lsr16)
TRANS_6309_RMW16_W(RORW, gen_ror16)
TRANS_6309_RMW16_W(ROLW, gen_rol16)
TRANS_6309_RMW16_W(DECW, gen_dec16)
TRANS_6309_RMW16_W(INCW, gen_inc16)
TRANS_6309_RMW16_W(CLRW, gen_clr16)

TRANS_6309_RMW8(COME, cpu_e, gen_com8)
TRANS_6309_RMW8(DECE, cpu_e, gen_dec8)
TRANS_6309_RMW8(INCE, cpu_e, gen_inc8)
TRANS_6309_RMW8(CLRE, cpu_e, gen_clr8)
TRANS_6309_RMW8(COMF, cpu_f, gen_com8)
TRANS_6309_RMW8(DECF, cpu_f, gen_dec8)
TRANS_6309_RMW8(INCF, cpu_f, gen_inc8)
TRANS_6309_RMW8(CLRF, cpu_f, gen_clr8)

static bool trans_TSTD(DisasContext *ctx, arg_TSTD *a)
{
    TCGv_i32 d;

    if (!require_6309(ctx)) {
        return true;
    }
    d = tcg_temp_new_i32();
    gen_get_d(d);
    gen_logic_cc16(d);
    return true;
}

static bool trans_TSTW(DisasContext *ctx, arg_TSTW *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    gen_get_w(w);
    gen_logic_cc16(w);
    return true;
}

static bool trans_TSTE(DisasContext *ctx, arg_TSTE *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    gen_logic_cc8(cpu_e);
    return true;
}

static bool trans_TSTF(DisasContext *ctx, arg_TSTF *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    gen_logic_cc8(cpu_f);
    return true;
}

static bool tfr_reg_is_16(int r)
{
    return r <= 7;
}

static void rr_read(DisasContext *ctx, int r, bool dest16, TCGv_i32 tmp)
{
    switch (r) {
    case 0:
        gen_get_d(tmp);
        break;
    case 1:
        tcg_gen_mov_i32(tmp, cpu_x);
        break;
    case 2:
        tcg_gen_mov_i32(tmp, cpu_y);
        break;
    case 3:
        tcg_gen_mov_i32(tmp, cpu_u);
        break;
    case 4:
        tcg_gen_mov_i32(tmp, cpu_s);
        break;
    case 5:
        tcg_gen_movi_i32(tmp, ctx->base.pc_next & 0xffff);
        break;
    case 6:
        gen_get_w(tmp);
        break;
    case 7:
        tcg_gen_mov_i32(tmp, cpu_v);
        break;
    case 8:
        if (dest16) {
            gen_get_d(tmp);
        } else {
            tcg_gen_mov_i32(tmp, cpu_a);
        }
        break;
    case 9:
        if (dest16) {
            gen_get_d(tmp);
        } else {
            tcg_gen_mov_i32(tmp, cpu_b);
        }
        break;
    case 10:
        if (dest16) {
            tcg_gen_mov_i32(tmp, cpu_cc);
        } else {
            tcg_gen_mov_i32(tmp, cpu_cc);
        }
        break;
    case 11:
        if (dest16) {
            tcg_gen_shli_i32(tmp, cpu_dp, 8);
        } else {
            tcg_gen_mov_i32(tmp, cpu_dp);
        }
        break;
    case 12:
    case 13:
        tcg_gen_movi_i32(tmp, 0);
        break;
    case 14:
        if (dest16) {
            gen_get_w(tmp);
        } else {
            tcg_gen_mov_i32(tmp, cpu_e);
        }
        break;
    case 15:
        if (dest16) {
            gen_get_w(tmp);
        } else {
            tcg_gen_mov_i32(tmp, cpu_f);
        }
        break;
    default:
        g_assert_not_reached();
    }
    if (!dest16) {
        tcg_gen_andi_i32(tmp, tmp, 0xff);
    } else {
        tcg_gen_andi_i32(tmp, tmp, 0xffff);
    }
}

static void rr_write(DisasContext *ctx, int r, TCGv_i32 val, bool dest16)
{
    if (r == 12 || r == 13) {
        return;
    }
    if (dest16) {
        tfr_write(ctx, r, val);
    } else {
        tcg_gen_andi_i32(val, val, 0xff);
        switch (r) {
        case 8:
            tcg_gen_mov_i32(cpu_a, val);
            break;
        case 9:
            tcg_gen_mov_i32(cpu_b, val);
            break;
        case 10:
            tcg_gen_mov_i32(cpu_cc, val);
            break;
        case 11:
            tcg_gen_mov_i32(cpu_dp, val);
            break;
        case 14:
            tcg_gen_mov_i32(cpu_e, val);
            break;
        case 15:
            tcg_gen_mov_i32(cpu_f, val);
            break;
        default:
            /* 16-bit dest encoded as 8-bit: write low byte via TFR map. */
            tfr_write(ctx, r, val);
            break;
        }
    }
}

typedef enum {
    M6809_RR_ADD,
    M6809_RR_ADC,
    M6809_RR_SUB,
    M6809_RR_SBC,
    M6809_RR_AND,
    M6809_RR_OR,
    M6809_RR_EOR,
    M6809_RR_CMP,
} M6809RrOp;

static bool do_reg_reg(DisasContext *ctx, int post, M6809RrOp op)
{
    int src = (post >> 4) & 0xf;
    int dst = post & 0xf;
    bool dest16 = tfr_reg_is_16(dst) || dst == 12 || dst == 13;
    TCGv_i32 dval = tcg_temp_new_i32();
    TCGv_i32 sval = tcg_temp_new_i32();

    if (!require_6309(ctx)) {
        return true;
    }

    rr_read(ctx, dst, dest16, dval);
    rr_read(ctx, src, dest16, sval);

    switch (op) {
    case M6809_RR_ADD:
        if (dest16) {
            gen_add16_common(dval, sval, false);
        } else {
            gen_add8_noh(dval, sval, false);
        }
        break;
    case M6809_RR_ADC:
        if (dest16) {
            gen_add16_common(dval, sval, true);
        } else {
            gen_add8_noh(dval, sval, true);
        }
        break;
    case M6809_RR_SUB:
        if (dest16) {
            gen_sub16_common(dval, sval, false, true);
        } else {
            gen_sub8(dval, sval);
        }
        break;
    case M6809_RR_SBC:
        if (dest16) {
            gen_sub16_common(dval, sval, true, true);
        } else {
            gen_sbc8(dval, sval);
        }
        break;
    case M6809_RR_AND:
        if (dest16) {
            gen_logic16(dval, sval, M6809_LOGIC_AND, true);
        } else {
            gen_logic8(dval, sval, M6809_LOGIC_AND, true);
        }
        break;
    case M6809_RR_OR:
        if (dest16) {
            gen_logic16(dval, sval, M6809_LOGIC_OR, true);
        } else {
            gen_logic8(dval, sval, M6809_LOGIC_OR, true);
        }
        break;
    case M6809_RR_EOR:
        if (dest16) {
            gen_logic16(dval, sval, M6809_LOGIC_EOR, true);
        } else {
            gen_logic8(dval, sval, M6809_LOGIC_EOR, true);
        }
        break;
    case M6809_RR_CMP:
        if (dest16) {
            gen_sub16_common(dval, sval, false, false);
        } else {
            gen_cmp8(dval, sval);
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (op != M6809_RR_CMP) {
        rr_write(ctx, dst, dval, dest16);
        if (dst == 10) {
            gen_exit_after_cc_write(ctx);
        }
    }
    return true;
}

static bool trans_ADDR(DisasContext *ctx, arg_ADDR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_ADD);
}

static bool trans_ADCR(DisasContext *ctx, arg_ADCR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_ADC);
}

static bool trans_SUBR(DisasContext *ctx, arg_SUBR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_SUB);
}

static bool trans_SBCR(DisasContext *ctx, arg_SBCR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_SBC);
}

static bool trans_ANDR(DisasContext *ctx, arg_ANDR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_AND);
}

static bool trans_ORR(DisasContext *ctx, arg_ORR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_OR);
}

static bool trans_EORR(DisasContext *ctx, arg_EORR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_EOR);
}

static bool trans_CMPR(DisasContext *ctx, arg_CMPR *a)
{
    return do_reg_reg(ctx, a->post, M6809_RR_CMP);
}

static bool trans_PSHSW(DisasContext *ctx, arg_PSHSW *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    gen_get_w(w);
    gen_push16(cpu_s, w);
    return true;
}

static bool trans_PULSW(DisasContext *ctx, arg_PULSW *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    gen_pull16(cpu_s, w);
    gen_set_w(w);
    return true;
}

static bool trans_PSHUW(DisasContext *ctx, arg_PSHUW *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    gen_get_w(w);
    gen_push16(cpu_u, w);
    return true;
}

static bool trans_PULUW(DisasContext *ctx, arg_PULUW *a)
{
    TCGv_i32 w;

    if (!require_6309(ctx)) {
        return true;
    }
    w = tcg_temp_new_i32();
    gen_pull16(cpu_u, w);
    gen_set_w(w);
    return true;
}

static bool do_divd(DisasContext *ctx, TCGv_i32 src)
{
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next & 0xffff);
    gen_helper_divd(tcg_env, src);
    return true;
}

static bool trans_DIVD_imm(DisasContext *ctx, arg_DIVD_imm *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_divd(ctx, tcg_constant_i32(a->imm & 0xff));
}

static bool trans_DIVD_dir(DisasContext *ctx, arg_DIVD_dir *a)
{
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    src = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(src, gen_ea_direct(a->addr), 0, MO_UB);
    return do_divd(ctx, src);
}

static bool trans_DIVD_idx(DisasContext *ctx, arg_DIVD_idx *a)
{
    TCGv_i32 ea;
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    if (gen_ea_indexed(ctx, &ea)) {
        src = tcg_temp_new_i32();
        tcg_gen_qemu_ld_i32(src, ea, 0, MO_UB);
        do_divd(ctx, src);
    }
    return true;
}

static bool trans_DIVD_ext(DisasContext *ctx, arg_DIVD_ext *a)
{
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    src = tcg_temp_new_i32();
    tcg_gen_qemu_ld_i32(src, tcg_constant_i32(a->addr & 0xffff), 0, MO_UB);
    return do_divd(ctx, src);
}

static bool do_divq(DisasContext *ctx, TCGv_i32 src)
{
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next & 0xffff);
    gen_helper_divq(tcg_env, src);
    return true;
}

static bool trans_DIVQ_imm(DisasContext *ctx, arg_DIVQ_imm *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_divq(ctx, tcg_constant_i32(a->imm & 0xffff));
}

static bool trans_DIVQ_dir(DisasContext *ctx, arg_DIVQ_dir *a)
{
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    src = tcg_temp_new_i32();
    gen_ld16_wrapped(src, gen_ea_direct(a->addr));
    return do_divq(ctx, src);
}

static bool trans_DIVQ_idx(DisasContext *ctx, arg_DIVQ_idx *a)
{
    TCGv_i32 ea;
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    if (gen_ea_indexed(ctx, &ea)) {
        src = tcg_temp_new_i32();
        gen_ld16_wrapped(src, ea);
        do_divq(ctx, src);
    }
    return true;
}

static bool trans_DIVQ_ext(DisasContext *ctx, arg_DIVQ_ext *a)
{
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    src = tcg_temp_new_i32();
    gen_ld16_wrapped(src, tcg_constant_i32(a->addr & 0xffff));
    return do_divq(ctx, src);
}

static bool do_muld(DisasContext *ctx, TCGv_i32 src)
{
    gen_helper_muld(tcg_env, src);
    return true;
}

static bool trans_MULD_imm(DisasContext *ctx, arg_MULD_imm *a)
{
    if (!require_6309(ctx)) {
        return true;
    }
    return do_muld(ctx, tcg_constant_i32(a->imm & 0xffff));
}

static bool trans_MULD_dir(DisasContext *ctx, arg_MULD_dir *a)
{
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    src = tcg_temp_new_i32();
    gen_ld16_wrapped(src, gen_ea_direct(a->addr));
    return do_muld(ctx, src);
}

static bool trans_MULD_idx(DisasContext *ctx, arg_MULD_idx *a)
{
    TCGv_i32 ea;
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    if (gen_ea_indexed(ctx, &ea)) {
        src = tcg_temp_new_i32();
        gen_ld16_wrapped(src, ea);
        do_muld(ctx, src);
    }
    return true;
}

static bool trans_MULD_ext(DisasContext *ctx, arg_MULD_ext *a)
{
    TCGv_i32 src;

    if (!require_6309(ctx)) {
        return true;
    }
    src = tcg_temp_new_i32();
    gen_ld16_wrapped(src, tcg_constant_i32(a->addr & 0xffff));
    return do_muld(ctx, src);
}

static bool do_bitop(DisasContext *ctx, int op, int post, int addr)
{
    if (!require_6309(ctx)) {
        return true;
    }
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next & 0xffff);
    gen_helper_bitop(tcg_env, tcg_constant_i32(op),
                     tcg_constant_i32(post & 0xff),
                     tcg_constant_i32(addr & 0xff));
    if (((post >> 6) & 3) == 0) {
        gen_exit_after_cc_write(ctx);
    }
    return true;
}

static bool trans_BAND(DisasContext *ctx, arg_BAND *a)
{
    return do_bitop(ctx, 0, a->post, a->addr);
}

static bool trans_BIAND(DisasContext *ctx, arg_BIAND *a)
{
    return do_bitop(ctx, 1, a->post, a->addr);
}

static bool trans_BOR(DisasContext *ctx, arg_BOR *a)
{
    return do_bitop(ctx, 2, a->post, a->addr);
}

static bool trans_BIOR(DisasContext *ctx, arg_BIOR *a)
{
    return do_bitop(ctx, 3, a->post, a->addr);
}

static bool trans_BEOR(DisasContext *ctx, arg_BEOR *a)
{
    return do_bitop(ctx, 4, a->post, a->addr);
}

static bool trans_BIEOR(DisasContext *ctx, arg_BIEOR *a)
{
    return do_bitop(ctx, 5, a->post, a->addr);
}

static bool trans_LDBT(DisasContext *ctx, arg_LDBT *a)
{
    return do_bitop(ctx, 6, a->post, a->addr);
}

static bool trans_STBT(DisasContext *ctx, arg_STBT *a)
{
    return do_bitop(ctx, 7, a->post, a->addr);
}

static bool do_tfm(DisasContext *ctx, int variant, int post)
{
    if (!require_6309(ctx)) {
        return true;
    }
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next & 0xffff);
    gen_helper_tfm(tcg_env, tcg_constant_i32(variant),
                   tcg_constant_i32(post & 0xff));
    return true;
}

static bool trans_TFM_pp(DisasContext *ctx, arg_TFM_pp *a)
{
    return do_tfm(ctx, 0, a->post);
}

static bool trans_TFM_mm(DisasContext *ctx, arg_TFM_mm *a)
{
    return do_tfm(ctx, 1, a->post);
}

static bool trans_TFM_p0(DisasContext *ctx, arg_TFM_p0 *a)
{
    return do_tfm(ctx, 2, a->post);
}

static bool trans_TFM_0p(DisasContext *ctx, arg_TFM_0p *a)
{
    return do_tfm(ctx, 3, a->post);
}

static bool trans_LDMD(DisasContext *ctx, arg_LDMD *a)
{
    uint32_t imm = a->imm & (MD_NM | MD_FM);

    if (!m6809_feature(ctx->env, M6809_FEATURE_6309)) {
        gen_illegal(ctx);
        return true;
    }

    /* NM and FM only; sticky IL/DZ are unchanged. */
    tcg_gen_andi_i32(cpu_md, cpu_md, ~(MD_NM | MD_FM) & 0xff);
    tcg_gen_ori_i32(cpu_md, cpu_md, imm);
    /* NM is a TB flag; leave the block even if this LDMD does not toggle it. */
    ctx->base.is_jmp = DISAS_UPDATE_EXIT;
    return true;
}

static bool trans_BITMD(DisasContext *ctx, arg_BITMD *a)
{
    TCGv_i32 t;
    TCGv_i32 z;
    uint32_t mask = a->imm & (MD_IL | MD_DZ);

    if (!require_6309(ctx)) {
        return true;
    }

    /* Test MD.IL/DZ against the corresponding immediate bits; then clear them. */
    t = tcg_temp_new_i32();
    z = tcg_temp_new_i32();
    tcg_gen_andi_i32(t, cpu_md, mask);
    tcg_gen_andi_i32(cpu_cc, cpu_cc, (uint32_t)~CC_Z);
    tcg_gen_setcondi_i32(TCG_COND_EQ, z, t, 0);
    tcg_gen_shli_i32(z, z, 2);
    tcg_gen_or_i32(cpu_cc, cpu_cc, z);
    tcg_gen_andi_i32(cpu_md, cpu_md, ~mask & 0xff);
    return true;
}

static bool trans_SYNC(DisasContext *ctx, arg_SYNC *a)
{
    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next & 0xffff);
    gen_helper_sync(tcg_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_SEX(DisasContext *ctx, arg_SEX *a)
{
    TCGv_i32 d = tcg_temp_new_i32();

    tcg_gen_ext8s_i32(d, cpu_b);
    tcg_gen_andi_i32(d, d, 0xffff);
    gen_set_d(d);
    gen_logic_cc16(d);
    return true;
}

static void m6809_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->env = cpu_env(cs);
    ctx->native = (ctx->base.tb->flags & TB_FLAGS_NATIVE) != 0;
}

static void m6809_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void m6809_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next, 0, 0);
}

static void m6809_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);
    uint32_t insn;

    tcg_gen_movi_i32(cpu_pc, ctx->base.pc_next);

    insn = decode_insn_load(ctx);
    if (!decode_insn(ctx, insn)) {
        gen_illegal(ctx);
    }
}

static void m6809_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_NEXT:
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, dcbase->pc_next);
        break;
    case DISAS_CHAIN:
        gen_goto_tb(ctx, 1, dcbase->pc_next);
        break;
    case DISAS_JUMP:
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_UPDATE_EXIT:
        tcg_gen_movi_i32(cpu_pc, dcbase->pc_next & 0xffff);
        /* fall through */
    case DISAS_EXIT:
        tcg_gen_exit_tb(NULL, 0);
        break;
    case DISAS_NORETURN:
        break;
    default:
        g_assert_not_reached();
    }
}

static bool m6809_tr_disas_log(const DisasContextBase *dcbase,
                               CPUState *cs, FILE *f)
{
    target_disas(f, cs, dcbase);
    return true;
}

static const TranslatorOps m6809_tr_ops = {
    .init_disas_context = m6809_tr_init_disas_context,
    .tb_start           = m6809_tr_tb_start,
    .insn_start         = m6809_tr_insn_start,
    .translate_insn     = m6809_tr_translate_insn,
    .tb_stop            = m6809_tr_tb_stop,
    .disas_log          = m6809_tr_disas_log,
};

void m6809_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext dc = { };

    translator_loop(cs, tb, max_insns, pc, host_pc, &m6809_tr_ops, &dc.base);
}
