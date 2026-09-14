/*
 * CoCo 3 virt RTC
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_M6809_VIRT_RTC_H
#define HW_M6809_VIRT_RTC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_COCO3_VIRT_RTC "coco3-virt-rtc"

typedef struct Coco3VirtRtcState Coco3VirtRtcState;
DECLARE_INSTANCE_CHECKER(Coco3VirtRtcState, COCO3_VIRT_RTC,
                         TYPE_COCO3_VIRT_RTC)

#define COCO3_VRTC_IO_SIZE 0x10

struct Coco3VirtRtcState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    uint8_t packet[6];          /* OS-9: YY MM DD HH MM SS */
    bool latched;
};

#endif /* HW_M6809_VIRT_RTC_H */
