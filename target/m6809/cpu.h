/*
 * QEMU Motorola 6809 CPU definitions
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

#ifndef QEMU_M6809_CPU_H
#define QEMU_M6809_CPU_H

#include "cpu-qom.h"
#include "exec/cpu-common.h"
#include "exec/cpu-interrupt.h"
#include "disas/dis-asm.h"
#include "exec/cpu-defs.h"

#ifdef CONFIG_USER_ONLY
#error "M6809 does not support user mode emulation"
#endif

#define CPU_RESOLVING_TYPE TYPE_M6809_CPU

/* Condition code register bits (6809 CC). */
#define CC_C    0x01
#define CC_V    0x02
#define CC_Z    0x04
#define CC_N    0x08
#define CC_I    0x10
#define CC_H    0x20
#define CC_F    0x40
#define CC_E    0x80

/* 6309 MD (mode) register. Bits 2–5 are unused. */
#define MD_NM   0x01 /* native-mode stacking */
#define MD_FM   0x02 /* FIRQ uses entire frame */
#define MD_IL   0x40 /* illegal-opcode trap sticky */
#define MD_DZ   0x80 /* divide-by-zero trap sticky */

/* Translation-block flags (m6809_get_tb_cpu_state). */
enum {
    TB_FLAGS_NATIVE = 1, /* MD.NM; 6309 only */
};

/* Architectural reset / IRQ / FIRQ / SWI / NMI vectors. */
#define M6809_VEC_RESET  0xfffe
#define M6809_VEC_NMI    0xfffc
#define M6809_VEC_SWI    0xfffa
#define M6809_VEC_IRQ    0xfff8
#define M6809_VEC_FIRQ   0xfff6
#define M6809_VEC_SWI2   0xfff4
#define M6809_VEC_SWI3     0xfff2
#define M6309_VEC_ILLEGAL  0xfff0
#define M6309_VEC_DIV0     0xffee

/* Meanings of the M6809CPU object's inbound GPIO lines. */
#define M6809_CPU_IRQ   0
#define M6809_CPU_FIRQ  1
#define M6809_CPU_NMI   2
#define M6809_NUM_IRQ_LINES 3

#define M6809_INT_IRQ   (1u << M6809_CPU_IRQ)
#define M6809_INT_FIRQ  (1u << M6809_CPU_FIRQ)
#define M6809_INT_NMI   (1u << M6809_CPU_NMI)

/*
 * SYNC: wake on any request; masked = fall through, no stack.
 * CWAI: stack a full frame before waiting; interrupt entry skips stacking.
 */
#define M6809_WAIT_NONE 0
#define M6809_WAIT_SYNC 1
#define M6809_WAIT_CWAI 2

/*
 * SWI, SWI2 and SWI3 are translated inline. Hardware IRQ/FIRQ/NMI and the
 * 6309 illegal / divide-by-zero traps use an exception index.
 */
#define EXCP_IRQ      1
#define EXCP_FIRQ     2
#define EXCP_NMI      3
#define EXCP_ILLEGAL  4
#define EXCP_DIV0     5

/*
 * Independent CPU-model bits. 6309 and Turbo9 are both 6809 supersets;
 * they must not share a boolean. Set at instance_init; reset must not
 * clear them.
 */
enum m6809_features {
    M6809_FEATURE_6309,
    M6809_FEATURE_TURBO9,
};

typedef struct CPUArchState {
    uint32_t a;
    uint32_t b;
    uint32_t x;
    uint32_t y;
    uint32_t u;
    uint32_t s;
    uint32_t pc;
    uint32_t dp;
    uint32_t cc;

    /* 6309 extra state; unused (and zero) on a 6809. W = E:F, Q = D:W. */
    uint32_t e;
    uint32_t f;
    uint32_t v;
    uint32_t md;

    uint32_t wait_state; /* M6809_WAIT_* */

    /*
     * NMI is ignored from reset until the program loads S, because the CPU
     * cannot stack an interrupt frame before the stack pointer is valid.
     */
    bool nmi_armed;

    /* Fields up to this point are cleared by a CPU reset. */
    struct {} end_reset_fields;

    /*
     * Level of the external interrupt lines as a mask of M6809_INT_*. IRQ and
     * FIRQ track the line level; NMI is a latch, since it is edge triggered.
     * This survives reset so a device holding a line asserted is not lost.
     */
    uint32_t intsrc;

    /* Independent CPU-model bits. Set at instance_init; survives reset. */
    uint64_t features;
} CPUM6809State;

/**
 * M6809CPU:
 * @env: #CPUM6809State
 */
struct ArchCPU {
    CPUState parent_obj;

    CPUM6809State env;
};

/**
 * M6809CPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 */
struct M6809CPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    ResettablePhases parent_phases;
};

extern const struct VMStateDescription vms_m6809_cpu;

void m6809_cpu_do_interrupt(CPUState *cpu);
bool m6809_cpu_exec_interrupt(CPUState *cpu, int int_req);
hwaddr m6809_cpu_get_phys_addr_debug(CPUState *cpu, vaddr addr);
bool m6809_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
int m6809_print_insn(bfd_vma addr, disassemble_info *info);

void m6809_tcg_init(void);
void m6809_translate_code(CPUState *cs, TranslationBlock *tb,
                          int *max_insns, vaddr pc, void *host_pc);

static inline bool m6809_feature(CPUM6809State *env, int feature)
{
    return (env->features & BIT_ULL(feature)) != 0;
}

static inline void m6809_set_feature(CPUM6809State *env, int feature)
{
    env->features |= BIT_ULL(feature);
}

static inline uint16_t m6809_get_d(CPUM6809State *env)
{
    return ((uint16_t)env->a << 8) | env->b;
}

static inline void m6809_set_d(CPUM6809State *env, uint16_t d)
{
    env->a = d >> 8;
    env->b = d & 0xff;
}

static inline uint16_t m6809_get_w(CPUM6809State *env)
{
    return ((uint16_t)env->e << 8) | env->f;
}

static inline void m6809_set_w(CPUM6809State *env, uint16_t w)
{
    env->e = w >> 8;
    env->f = w & 0xff;
}

static inline uint32_t m6809_get_q(CPUM6809State *env)
{
    return ((uint32_t)m6809_get_d(env) << 16) | m6809_get_w(env);
}

static inline void m6809_set_q(CPUM6809State *env, uint32_t q)
{
    m6809_set_d(env, q >> 16);
    m6809_set_w(env, q & 0xffff);
}

static inline int m6809_cpu_pending_interrupt(CPUM6809State *env)
{
    if ((env->intsrc & M6809_INT_NMI) && env->nmi_armed) {
        return EXCP_NMI;
    }
    if ((env->intsrc & M6809_INT_FIRQ) && !(env->cc & CC_F)) {
        return EXCP_FIRQ;
    }
    if ((env->intsrc & M6809_INT_IRQ) && !(env->cc & CC_I)) {
        return EXCP_IRQ;
    }
    return -1;
}

#endif /* QEMU_M6809_CPU_H */
