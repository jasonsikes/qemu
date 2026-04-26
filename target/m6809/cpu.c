/*
 * QEMU Motorola 6809 CPU
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
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "cpu.h"
#include "migration/vmstate.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "hw/core/loader.h"
#include "hw/core/qdev-properties.h"
#include "disas/dis-asm.h"
#include "tcg/debug-assert.h"
#include "accel/tcg/cpu-ops.h"

static void m6809_cpu_set_pc(CPUState *cs, vaddr value)
{
    M6809CPU *cpu = M6809_CPU(cs);

    cpu->env.pc = value & 0xffff;
}

static vaddr m6809_cpu_get_pc(CPUState *cs)
{
    M6809CPU *cpu = M6809_CPU(cs);

    return cpu->env.pc;
}

static TCGTBCPUState m6809_get_tb_cpu_state(CPUState *cs)
{
    CPUM6809State *env = cpu_env(cs);

    return (TCGTBCPUState){ .pc = env->pc, .flags = 0 };
}

static void m6809_cpu_synchronize_from_tb(CPUState *cs,
                                          const TranslationBlock *tb)
{
    M6809CPU *cpu = M6809_CPU(cs);

    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu->env.pc = tb->pc;
}

static void m6809_restore_state_to_opc(CPUState *cs,
                                       const TranslationBlock *tb,
                                       const uint64_t *data)
{
    M6809CPU *cpu = M6809_CPU(cs);

    cpu->env.pc = data[0];
}

static bool m6809_cpu_has_work(CPUState *cs)
{
    CPUM6809State *env = cpu_env(cs);

    if (!cpu_test_interrupt(cs, CPU_INTERRUPT_HARD)) {
        return false;
    }

    if (env->wait_state == M6809_WAIT_SYNC) {
        return env->intsrc != 0;
    }
    return m6809_cpu_pending_interrupt(env) >= 0;
}

static int m6809_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    return 0;
}

static void m6809_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    M6809CPUClass *mcc = M6809_CPU_GET_CLASS(obj);
    CPUM6809State *env = cpu_env(cs);
    uint16_t *resetvec;

    if (mcc->parent_phases.hold) {
        mcc->parent_phases.hold(obj, type);
    }

    memset(env, 0, offsetof(CPUM6809State, end_reset_fields));

    env->cc = CC_I | CC_F;

    resetvec = rom_ptr(M6809_VEC_RESET, 2);
    if (resetvec) {
        env->pc = lduw_be_p(resetvec);
    } else {
        /* $FFFE is MMIO; $8000 until the board writes the RESET vector. */
        env->pc = 0x8000;
    }
}

static ObjectClass *m6809_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;
    char *typename;

    oc = object_class_by_name(cpu_model);
    if (oc != NULL && object_class_dynamic_cast(oc, TYPE_M6809_CPU) != NULL) {
        return oc;
    }

    typename = g_strdup_printf(M6809_CPU_TYPE_NAME("%s"), cpu_model);
    oc = object_class_by_name(typename);
    g_free(typename);
    return oc;
}

static void m6809_cpu_realize(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    M6809CPUClass *mcc = M6809_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    mcc->parent_realize(dev, errp);
}

static void m6809_cpu_disas_set_info(const CPUState *cpu, disassemble_info *info)
{
    info->endian = BFD_ENDIAN_BIG;
    info->mach = bfd_arch_m6809;
    info->print_insn = m6809_print_insn;
}

static void m6809_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPUM6809State *env = cpu_env(cs);

    qemu_fprintf(f, "PC=%04x  S=%04x  U=%04x\n", env->pc, env->s, env->u);
    qemu_fprintf(f, "X=%04x   Y=%04x  DP=%02x\n", env->x, env->y, env->dp);
    qemu_fprintf(f, "A=%02x    B=%02x   D=%04x\n",
                 env->a, env->b, m6809_get_d(env));
    qemu_fprintf(f, "CC=%02x [%c%c%c%c%c%c%c%c]\n",
                 env->cc,
                 (env->cc & CC_E) ? 'E' : '.',
                 (env->cc & CC_F) ? 'F' : '.',
                 (env->cc & CC_H) ? 'H' : '.',
                 (env->cc & CC_I) ? 'I' : '.',
                 (env->cc & CC_N) ? 'N' : '.',
                 (env->cc & CC_Z) ? 'Z' : '.',
                 (env->cc & CC_V) ? 'V' : '.',
                 (env->cc & CC_C) ? 'C' : '.');
}

/*
 * IRQ and FIRQ are level sensitive, so they mirror the line. NMI is edge
 * sensitive: latch the rising edge and let interrupt entry consume it.
 */
static void m6809_cpu_set_irq(void *opaque, int irq, int level)
{
    M6809CPU *cpu = opaque;
    CPUM6809State *env = &cpu->env;
    CPUState *cs = CPU(cpu);
    uint32_t mask = 1u << irq;

    assert(irq < M6809_NUM_IRQ_LINES);

    if (irq == M6809_CPU_NMI) {
        if (!level) {
            return;
        }
    } else if (!level) {
        env->intsrc &= ~mask;
        if (env->intsrc == 0) {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
        return;
    }

    env->intsrc |= mask;
    cpu_interrupt(cs, CPU_INTERRUPT_HARD);
}

static void m6809_cpu_init(Object *obj)
{
    M6809CPU *cpu = M6809_CPU(obj);

    qdev_init_gpio_in(DEVICE(cpu), m6809_cpu_set_irq, M6809_NUM_IRQ_LINES);
}

#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps m6809_sysemu_ops = {
    .has_work = m6809_cpu_has_work,
    .get_phys_page_debug = m6809_cpu_get_phys_addr_debug,
};

static vaddr m6809_pointer_wrap(CPUState *cs, int mmu_idx,
                                vaddr result, vaddr base)
{
    return (uint16_t)result;
}

static const TCGCPUOps m6809_tcg_ops = {
    .guest_default_memory_order = TCG_MO_ALL,
    .mttcg_supported = false,

    .initialize = m6809_tcg_init,
    .translate_code = m6809_translate_code,
    .get_tb_cpu_state = m6809_get_tb_cpu_state,
    .synchronize_from_tb = m6809_cpu_synchronize_from_tb,
    .restore_state_to_opc = m6809_restore_state_to_opc,
    .mmu_index = m6809_cpu_mmu_index,
    .tlb_fill = m6809_cpu_tlb_fill,
    .pointer_wrap = m6809_pointer_wrap,

    .cpu_exec_interrupt = m6809_cpu_exec_interrupt,
    .cpu_exec_halt = m6809_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = m6809_cpu_do_interrupt,
};

static void m6809_cpu_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    CPUClass *cc = CPU_CLASS(oc);
    M6809CPUClass *mcc = M6809_CPU_CLASS(oc);
    ResettableClass *rc = RESETTABLE_CLASS(oc);

    device_class_set_parent_realize(dc, m6809_cpu_realize, &mcc->parent_realize);
    resettable_class_set_parent_phases(rc, NULL, m6809_cpu_reset_hold, NULL,
                                       &mcc->parent_phases);

    cc->class_by_name = m6809_cpu_class_by_name;
    cc->dump_state = m6809_cpu_dump_state;
    cc->set_pc = m6809_cpu_set_pc;
    cc->get_pc = m6809_cpu_get_pc;
    dc->vmsd = &vms_m6809_cpu;
    cc->sysemu_ops = &m6809_sysemu_ops;
    cc->disas_set_info = m6809_cpu_disas_set_info;
    cc->tcg_ops = &m6809_tcg_ops;
}

static const TypeInfo m6809_cpu_type_info[] = {
    {
        .name = TYPE_M6809_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(M6809CPU),
        .instance_align = __alignof(M6809CPU),
        .instance_init = m6809_cpu_init,
        .abstract = true,
        .class_size = sizeof(M6809CPUClass),
        .class_init = m6809_cpu_class_init,
    },
    {
        .name = M6809_CPU_TYPE_NAME("m6809"),
        .parent = TYPE_M6809_CPU,
    },
};

DEFINE_TYPES(m6809_cpu_type_info)
