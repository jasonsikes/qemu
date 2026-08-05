/*
 * QEMU M6809 CPU helpers
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
#include "qemu/log.h"
#include "cpu.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/cpu-loop.h"
#include "qemu/plugin.h"

void helper_daa(CPUM6809State *env)
{
    uint32_t a = env->a & 0xff;
    uint32_t correction = 0;
    uint32_t result;
    bool carry = env->cc & CC_C;
    bool half_carry = env->cc & CC_H;
    uint32_t msn = a >> 4;
    uint32_t lsn = a & 0xf;

    if (half_carry || lsn > 9) {
        correction |= 0x06;
    }
    if (carry || msn > 9 || (msn > 8 && lsn > 9)) {
        correction |= 0x60;
    }

    result = a + correction;
    env->a = result & 0xff;

    /* H/V unchanged. N/Z from A; C sticky. */
    env->cc &= ~(CC_N | CC_Z | CC_C);
    if (env->a & 0x80) {
        env->cc |= CC_N;
    }
    if (env->a == 0) {
        env->cc |= CC_Z;
    }
    if (carry || result > 0xff) {
        env->cc |= CC_C;
    }
}

/* 6809: no illegal-instruction vector; halt with PC at the faulting opcode.
 * 6309: set MD.IL and trap (full frame, like SWI; I/F unchanged).
 */
G_NORETURN void helper_raise_illegal_instruction(CPUM6809State *env)
{
    CPUState *cs = env_cpu(env);

    if (m6809_feature(env, M6809_FEATURE_6309)) {
        uint8_t b0, b1, b2;

        cs->neg.can_do_io = true;
        b0 = cpu_ldub_data(env, env->pc);
        b1 = cpu_ldub_data(env, (env->pc + 1) & 0xffff);
        b2 = cpu_ldub_data(env, (env->pc + 2) & 0xffff);
        qemu_log_mask(LOG_UNIMP,
                      "m6809: 6309 illegal at PC=%04x bytes %02x %02x %02x\n",
                      env->pc, b0, b1, b2);
        env->md |= MD_IL;
        cs->exception_index = EXCP_ILLEGAL;
        cpu_loop_exit(cs);
    }

    qemu_log_mask(LOG_UNIMP, "m6809: illegal instruction at PC=%04x\n",
                  env->pc);
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

G_NORETURN void helper_raise_division_by_zero(CPUM6809State *env)
{
    CPUState *cs = env_cpu(env);

    env->md |= MD_DZ;
    cs->exception_index = EXCP_DIV0;
    cpu_loop_exit(cs);
}

static void m6809_push8(CPUM6809State *env, uint32_t val, uintptr_t ra)
{
    env->s = (env->s - 1) & 0xffff;
    cpu_stb_mmuidx_ra(env, env->s, val & 0xff, 0, ra);
}

static void m6809_push16(CPUM6809State *env, uint32_t val, uintptr_t ra)
{
    m6809_push8(env, val, ra);
    m6809_push8(env, val >> 8, ra);
}

/*
 * Stack an interrupt frame on S.
 *
 * Entire (6809 / 6309 emulation): PC, U, Y, X, DP, B, A, CC (12 bytes).
 * Entire (6309 native):           PC, U, Y, X, DP, F, E, B, A, CC (14).
 *   W sits between DP and D; push W as 16-bit so memory is E then F.
 * Short FIRQ: PC, CC. MD.FM selects entire vs short for FIRQ only.
 *
 * CC must already carry the E bit that says which of the two was used,
 * since that is what RTI reads back to size the frame.
 */
static void m6809_stack_interrupt_frame(CPUM6809State *env, bool full_frame,
                                        uintptr_t ra)
{
    m6809_push16(env, env->pc, ra);
    if (full_frame) {
        m6809_push16(env, env->u, ra);
        m6809_push16(env, env->y, ra);
        m6809_push16(env, env->x, ra);
        m6809_push8(env, env->dp, ra);
        if (m6809_native_stack(env)) {
            m6809_push16(env, m6809_get_w(env), ra);
        }
        m6809_push8(env, env->b, ra);
        m6809_push8(env, env->a, ra);
    }
    m6809_push8(env, env->cc, ra);
}

void m6809_cpu_do_interrupt(CPUState *cs)
{
    CPUM6809State *env = cpu_env(cs);
    uint32_t ret_pc = env->pc;
    bool full_frame = true;
    uint32_t set_mask;
    uint16_t vector;

    switch (cs->exception_index) {
    case EXCP_NMI:
        vector = M6809_VEC_NMI;
        set_mask = CC_F | CC_I;
        break;
    case EXCP_FIRQ:
        vector = M6809_VEC_FIRQ;
        set_mask = CC_F | CC_I;
        full_frame = env->md & MD_FM;
        break;
    case EXCP_IRQ:
        vector = M6809_VEC_IRQ;
        set_mask = CC_I;
        break;
    case EXCP_ILLEGAL:
        vector = M6309_VEC_ILLEGAL;
        set_mask = 0;
        break;
    case EXCP_DIV0:
        vector = M6309_VEC_DIV0;
        set_mask = 0;
        break;
    default:
        g_assert_not_reached();
    }

    cs->neg.can_do_io = true;

    /* CWAI already pushed the frame. */
    if (env->wait_state != M6809_WAIT_CWAI) {
        if (full_frame) {
            env->cc |= CC_E;
        } else {
            env->cc &= ~CC_E;
        }
        m6809_stack_interrupt_frame(env, full_frame, 0);
    }

    env->wait_state = M6809_WAIT_NONE;
    env->cc |= set_mask;
    env->pc = cpu_lduw_be_mmuidx_ra(env, vector, 0, 0);

    qemu_log_mask(CPU_LOG_INT,
                  "m6809: interrupt %d from PC=%04x, vector $%04x -> %04x\n",
                  cs->exception_index, ret_pc, vector, env->pc);

    cs->exception_index = -1;
    qemu_plugin_vcpu_interrupt_cb(cs, ret_pc);
}

bool m6809_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    CPUM6809State *env = cpu_env(cs);
    int excp;

    if (!(interrupt_request & CPU_INTERRUPT_HARD)) {
        return false;
    }

    excp = m6809_cpu_pending_interrupt(env);
    if (excp < 0) {
        if (env->wait_state == M6809_WAIT_SYNC && env->intsrc != 0) {
            env->wait_state = M6809_WAIT_NONE;
        }
        return false;
    }

    if (excp == EXCP_NMI) {
        env->intsrc &= ~M6809_INT_NMI;
    }
    if (env->intsrc == 0) {
        cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
    }

    cs->exception_index = excp;
    m6809_cpu_do_interrupt(cs);
    return true;
}

G_NORETURN void helper_cwai(CPUM6809State *env, uint32_t imm)
{
    CPUState *cs = env_cpu(env);

    env->cc &= imm & 0xff;
    env->cc |= CC_E;
    m6809_stack_interrupt_frame(env, true, GETPC());

    env->wait_state = M6809_WAIT_CWAI;
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

G_NORETURN void helper_sync(CPUM6809State *env)
{
    CPUState *cs = env_cpu(env);

    env->wait_state = M6809_WAIT_SYNC;
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

static void m6809_set_nz32(CPUM6809State *env, uint32_t q)
{
    env->cc &= ~(CC_N | CC_Z | CC_V | CC_C);
    if (q & 0x80000000u) {
        env->cc |= CC_N;
    }
    if (q == 0) {
        env->cc |= CC_Z;
    }
}

void helper_muld(CPUM6809State *env, uint32_t src)
{
    int32_t result = (int16_t)m6809_get_d(env) * (int16_t)(src & 0xffff);

    m6809_set_q(env, (uint32_t)result);
    m6809_set_nz32(env, (uint32_t)result);
}

void helper_divd(CPUM6809State *env, uint32_t src)
{
    int32_t dividend = (int16_t)m6809_get_d(env);
    int32_t divisor = (int8_t)(src & 0xff);
    int32_t quot;
    int32_t rem;
    uint8_t b;

    if (divisor == 0) {
        helper_raise_division_by_zero(env);
    }

    quot = dividend / divisor;
    rem = dividend % divisor;

    env->cc &= ~(CC_N | CC_Z | CC_V | CC_C);

    /* Range overflow: quotient does not fit in 8 bits. */
    if (quot > 255 || quot < -128) {
        env->cc |= CC_V;
        return;
    }

    b = quot & 0xff;
    env->a = rem & 0xff;
    env->b = b;
    if (b & 1) {
        env->cc |= CC_C;
    }
    if (b == 0) {
        env->cc |= CC_Z;
    }
    if (b & 0x80) {
        env->cc |= CC_N;
    }
    /* Two's-complement overflow: fits unsigned 8-bit, not signed. */
    if (quot > 127) {
        env->cc |= CC_V;
    }
}

void helper_divq(CPUM6809State *env, uint32_t src)
{
    int64_t dividend = (int32_t)m6809_get_q(env);
    int64_t divisor = (int16_t)(src & 0xffff);
    int64_t quot;
    int64_t rem;
    uint16_t w;

    if (divisor == 0) {
        helper_raise_division_by_zero(env);
    }

    quot = dividend / divisor;
    rem = dividend % divisor;

    env->cc &= ~(CC_N | CC_Z | CC_V | CC_C);

    if (quot > 65535 || quot < -32768) {
        env->cc |= CC_V;
        return;
    }

    w = quot & 0xffff;
    m6809_set_d(env, rem & 0xffff);
    m6809_set_w(env, w);
    if (w & 1) {
        env->cc |= CC_C;
    }
    if (w == 0) {
        env->cc |= CC_Z;
    }
    if (w & 0x8000) {
        env->cc |= CC_N;
    }
    if (quot > 32767) {
        env->cc |= CC_V;
    }
}

static uint16_t tfm_get_reg(CPUM6809State *env, int r)
{
    switch (r) {
    case 0:
        return m6809_get_d(env);
    case 1:
        return env->x;
    case 2:
        return env->y;
    case 3:
        return env->u;
    case 4:
        return env->s;
    default:
        g_assert_not_reached();
    }
}

static void tfm_set_reg(CPUM6809State *env, int r, uint16_t val)
{
    switch (r) {
    case 0:
        m6809_set_d(env, val);
        break;
    case 1:
        env->x = val;
        break;
    case 2:
        env->y = val;
        break;
    case 3:
        env->u = val;
        break;
    case 4:
        env->s = val;
        env->nmi_armed = true;
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * W is the count and addresses wrap at $FFFF.
 * TFM specification is interruptable, but since a copy of the maximum count is only a
 * few microseconds using TCG, we won't bother with it.
 */
void helper_tfm(CPUM6809State *env, uint32_t variant, uint32_t post)
{
    int src = (post >> 4) & 0xf;
    int dst = post & 0xf;
    uint16_t count;
    uintptr_t ra = GETPC();

    if (src > 4 || dst > 4) {
        helper_raise_illegal_instruction(env);
    }

    count = m6809_get_w(env);
    while (count) {
        uint16_t sa = tfm_get_reg(env, src);
        uint16_t da = tfm_get_reg(env, dst);
        uint8_t b = cpu_ldub_mmuidx_ra(env, sa, 0, ra);

        cpu_stb_mmuidx_ra(env, da, b, 0, ra);
        switch (variant) {
        case 0:
            sa++;
            da++;
            break;
        case 1:
            sa--;
            da--;
            break;
        case 2:
            sa++;
            break;
        case 3:
            da++;
            break;
        default:
            g_assert_not_reached();
        }
        tfm_set_reg(env, src, sa & 0xffff);
        tfm_set_reg(env, dst, da & 0xffff);
        count--;
        m6809_set_w(env, count);
    }
}

static uint32_t *bitop_reg(CPUM6809State *env, int code)
{
    switch (code) {
    case 0:
        return &env->cc;
    case 1:
        return &env->a;
    case 2:
        return &env->b;
    default:
        return NULL;
    }
}

void helper_bitop(CPUM6809State *env, uint32_t op, uint32_t post, uint32_t addr)
{
    int rcode = (post >> 6) & 3;
    int bit_hi = (post >> 3) & 7;
    int bit_lo = post & 7;
    uint32_t *reg = bitop_reg(env, rcode);
    uint8_t mem;
    int mem_bit;
    int reg_bit;
    bool src;
    bool dst;
    bool result;
    uintptr_t ra = GETPC();

    if (!reg) {
        helper_raise_illegal_instruction(env);
    }

    addr = ((env->dp << 8) | (addr & 0xff)) & 0xffff;
    mem = cpu_ldub_mmuidx_ra(env, addr, 0, ra);

    if (op == 7) {
        /* STBT: register bit_hi -> memory bit_lo */
        reg_bit = bit_hi;
        mem_bit = bit_lo;
        src = (*reg >> reg_bit) & 1;
        mem = (mem & ~(1u << mem_bit)) | ((uint8_t)src << mem_bit);
        cpu_stb_mmuidx_ra(env, addr, mem, 0, ra);
        return;
    }

    /* Others: memory bit_hi, register bit_lo */
    mem_bit = bit_hi;
    reg_bit = bit_lo;
    src = (mem >> mem_bit) & 1;
    dst = (*reg >> reg_bit) & 1;
    switch (op) {
    case 0: /* BAND */
        result = dst && src;
        break;
    case 1: /* BIAND */
        result = dst && !src;
        break;
    case 2: /* BOR */
        result = dst || src;
        break;
    case 3: /* BIOR */
        result = dst || !src;
        break;
    case 4: /* BEOR */
        result = dst != src;
        break;
    case 5: /* BIEOR */
        result = dst == src;
        break;
    case 6: /* LDBT */
        result = src;
        break;
    default:
        g_assert_not_reached();
    }
    *reg = (*reg & ~(1u << reg_bit)) | ((uint32_t)result << reg_bit);
    *reg &= 0xff;
}

hwaddr m6809_cpu_get_phys_addr_debug(CPUState *cs, vaddr addr)
{
    return addr & 0xffff;
}

bool m6809_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr)
{
    uint32_t physical;
    int prot;

    /* CPU addresses are 16-bit; the GIME aliases 8 KiB windows onto RAM. */
    address &= TARGET_PAGE_MASK;
    physical = address;
    prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
    tlb_set_page(cs, address, physical, prot, mmu_idx, TARGET_PAGE_SIZE);
    return true;
}
