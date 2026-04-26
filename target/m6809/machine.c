/*
 * QEMU M6809 CPU migration / VMState
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
#include "migration/vmstate.h"

const VMStateDescription vms_m6809_cpu = {
    .name = "cpu",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(env.a, M6809CPU),
        VMSTATE_UINT32(env.b, M6809CPU),
        VMSTATE_UINT32(env.x, M6809CPU),
        VMSTATE_UINT32(env.y, M6809CPU),
        VMSTATE_UINT32(env.u, M6809CPU),
        VMSTATE_UINT32(env.s, M6809CPU),
        VMSTATE_UINT32(env.pc, M6809CPU),
        VMSTATE_UINT32(env.dp, M6809CPU),
        VMSTATE_UINT32(env.cc, M6809CPU),
        VMSTATE_UINT32(env.wait_state, M6809CPU),
        VMSTATE_BOOL(env.nmi_armed, M6809CPU),
        VMSTATE_UINT32(env.intsrc, M6809CPU),
        VMSTATE_END_OF_LIST()
    }
};
