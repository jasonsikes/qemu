/*
 * QEMU M6809 CPU
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

#ifndef M6809_CPU_PARAM_H
#define M6809_CPU_PARAM_H

/*
 * Page size is 512 bytes so $FFxx I/O does not share a RAM TLB page;
 * $FE00 is byte-wide MMIO.
 */
#define TARGET_PAGE_BITS 9
#define TARGET_PHYS_ADDR_SPACE_BITS 16
#define TARGET_VIRT_ADDR_SPACE_BITS 16

#endif /* M6809_CPU_PARAM_H */
