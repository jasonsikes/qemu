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

/* No illegal-instruction vector; halt with PC at the faulting opcode. */
G_NORETURN void helper_raise_illegal_instruction(CPUM6809State *env)
{
    CPUState *cs = env_cpu(env);

    qemu_log_mask(LOG_UNIMP, "m6809: illegal instruction at PC=%04x\n",
                  env->pc);
    cs->halted = 1;
    cs->exception_index = EXCP_HLT;
    cpu_loop_exit(cs);
}

static void m6809_push8(CPUM6809State *env, uint32_t val)
{
    env->s = (env->s - 1) & 0xffff;
    cpu_stb_mmuidx_ra(env, env->s, val & 0xff, 0, 0);
}

static void m6809_push16(CPUM6809State *env, uint32_t val)
{
    m6809_push8(env, val);
    m6809_push8(env, val >> 8);
}

/*
 * Stack an interrupt frame on S. The full frame is PC, U, Y, X, DP, B, A, CC,
 * matching PSHS with a $FF postbyte; the short FIRQ frame is only PC and CC.
 * CC must already carry the E bit that says which of the two was used, since
 * that is what RTI reads back to size the frame.
 */
static void m6809_stack_interrupt_frame(CPUM6809State *env, bool full_frame)
{
    m6809_push16(env, env->pc);
    if (full_frame) {
        m6809_push16(env, env->u);
        m6809_push16(env, env->y);
        m6809_push16(env, env->x);
        m6809_push8(env, env->dp);
        m6809_push8(env, env->b);
        m6809_push8(env, env->a);
    }
    m6809_push8(env, env->cc);
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
        full_frame = false;
        break;
    case EXCP_IRQ:
        vector = M6809_VEC_IRQ;
        set_mask = CC_I;
        break;
    default:
        g_assert_not_reached();
    }

    /* CWAI already pushed the frame. */
    if (env->wait_state != M6809_WAIT_CWAI) {
        if (full_frame) {
            env->cc |= CC_E;
        } else {
            env->cc &= ~CC_E;
        }
        m6809_stack_interrupt_frame(env, full_frame);
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
    m6809_stack_interrupt_frame(env, true);

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
