/*
 * M6809 gdb server stub
 *
 * Copyright (c) 2026 Jason G. Sikes
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
#include "gdbstub/helpers.h"

int m6809_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPUM6809State *env = cpu_env(cs);

    switch (n) {
    case 0:
        return gdb_get_reg16(mem_buf, m6809_get_d(env));
    case 1:
        return gdb_get_reg16(mem_buf, env->x);
    case 2:
        return gdb_get_reg16(mem_buf, env->y);
    case 3:
        return gdb_get_reg16(mem_buf, env->u);
    case 4:
        return gdb_get_reg16(mem_buf, env->s);
    case 5:
        return gdb_get_reg16(mem_buf, env->pc);
    case 6:
        return gdb_get_reg8(mem_buf, env->a);
    case 7:
        return gdb_get_reg8(mem_buf, env->b);
    case 8:
        return gdb_get_reg8(mem_buf, env->dp);
    case 9:
        return gdb_get_reg8(mem_buf, env->cc);
    case 10:
        return gdb_get_reg8(mem_buf, env->e);
    case 11:
        return gdb_get_reg8(mem_buf, env->f);
    case 12:
        return gdb_get_reg16(mem_buf, m6809_get_w(env));
    case 13:
        return gdb_get_reg16(mem_buf, env->v);
    case 14:
        return gdb_get_reg8(mem_buf, env->md);
    default:
        return 0;
    }
}

int m6809_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUM6809State *env = cpu_env(cs);

    switch (n) {
    case 0:
        m6809_set_d(env, lduw_be_p(mem_buf));
        return 2;
    case 1:
        env->x = lduw_be_p(mem_buf);
        return 2;
    case 2:
        env->y = lduw_be_p(mem_buf);
        return 2;
    case 3:
        env->u = lduw_be_p(mem_buf);
        return 2;
    case 4:
        env->s = lduw_be_p(mem_buf);
        return 2;
    case 5:
        env->pc = lduw_be_p(mem_buf);
        return 2;
    case 6:
        env->a = *mem_buf;
        return 1;
    case 7:
        env->b = *mem_buf;
        return 1;
    case 8:
        env->dp = *mem_buf;
        return 1;
    case 9:
        env->cc = *mem_buf;
        return 1;
    case 10:
        env->e = *mem_buf;
        return 1;
    case 11:
        env->f = *mem_buf;
        return 1;
    case 12:
        m6809_set_w(env, lduw_be_p(mem_buf));
        return 2;
    case 13:
        env->v = lduw_be_p(mem_buf);
        return 2;
    case 14:
        env->md = *mem_buf;
        return 1;
    default:
        return 0;
    }
}
