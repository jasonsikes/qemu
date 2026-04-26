/*
 * QEMU M6809 CPU QOM header
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

#ifndef TARGET_M6809_CPU_QOM_H
#define TARGET_M6809_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_M6809_CPU "m6809-cpu"

OBJECT_DECLARE_CPU_TYPE(M6809CPU, M6809CPUClass, M6809_CPU)

#define M6809_CPU_TYPE_SUFFIX "-" TYPE_M6809_CPU
#define M6809_CPU_TYPE_NAME(name) (name M6809_CPU_TYPE_SUFFIX)

#endif /* TARGET_M6809_CPU_QOM_H */
