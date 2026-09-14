/*
 * Color Computer 3 cartridge slot and ROM cartridge
 *
 * Copyright (c) 2026 Jason G. Sikes
 *
 * This work is licensed under the terms of the GNU GPLv2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/datadir.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"
#include "hw/core/loader.h"
#include "coco3.h"

static Coco3State *coco3_cart_sys(DeviceState *dev)
{
    return COCO3(qdev_get_parent_bus(dev)->parent);
}

static void coco3_cart_realize(DeviceState *dev, Error **errp)
{
    Coco3CartState *s = COCO3_CART(dev);
    Coco3CartBus *bus = COCO3_CART_BUS(qdev_get_parent_bus(dev));
    g_autofree char *path = NULL;
    int64_t size;

    if (!s->romfile || !s->romfile[0]) {
        error_setg(errp, "coco3-cart: romfile property is required");
        return;
    }

    path = qemu_find_file(QEMU_FILE_TYPE_BIOS, s->romfile);
    if (!path) {
        error_setg(errp, "coco3-cart: cannot find romfile '%s'", s->romfile);
        return;
    }

    size = get_image_size(path, NULL);
    if (size < 0) {
        error_setg(errp, "coco3-cart: cannot read romfile '%s'", s->romfile);
        return;
    }
    if (size == 0) {
        error_setg(errp, "coco3-cart: romfile '%s' is empty", s->romfile);
        return;
    }
    if (size > COCO3_CART_ROM_SIZE) {
        error_setg(errp,
                   "coco3-cart: romfile '%s' is %" PRId64
                   " bytes; max is %d",
                   s->romfile, size, (int)COCO3_CART_ROM_SIZE);
        return;
    }

    memory_region_init_rom(&s->rom, OBJECT(s), "coco3.cart.rom",
                           COCO3_CART_ROM_SIZE, &error_fatal);
    if (load_image_size(path, memory_region_get_ram_ptr(&s->rom),
                        COCO3_CART_ROM_SIZE) < 0) {
        error_setg(errp, "coco3-cart: failed to load romfile '%s'",
                   s->romfile);
        return;
    }

    memory_region_add_subregion(&bus->mr, 0, &s->rom);
    s->mapped = true;
    bus->present = true;
    coco3_cart_remap(coco3_cart_sys(dev));
}

static void coco3_cart_unrealize(DeviceState *dev)
{
    Coco3CartState *s = COCO3_CART(dev);
    Coco3CartBus *bus = COCO3_CART_BUS(qdev_get_parent_bus(dev));

    if (s->mapped) {
        memory_region_del_subregion(&bus->mr, &s->rom);
        s->mapped = false;
        bus->present = false;
        coco3_cart_remap(coco3_cart_sys(dev));
    }
}

static const Property coco3_cart_properties[] = {
    DEFINE_PROP_STRING("romfile", Coco3CartState, romfile),
};

static void coco3_cart_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->desc = "CoCo 3 cartridge ROM";
    dc->realize = coco3_cart_realize;
    dc->unrealize = coco3_cart_unrealize;
    dc->bus_type = TYPE_COCO3_CART_BUS;
    dc->hotpluggable = false;
    set_bit(DEVICE_CATEGORY_MISC, dc->categories);
    device_class_set_props(dc, coco3_cart_properties);
}

static void coco3_cart_bus_init(Object *obj)
{
    Coco3CartBus *bus = COCO3_CART_BUS(obj);

    memory_region_init(&bus->mr, obj, "coco3.cart", COCO3_CART_ROM_SIZE);
}

static void coco3_cart_bus_class_init(ObjectClass *oc, const void *data)
{
    BusClass *bc = BUS_CLASS(oc);

    bc->max_dev = 1;
}

static const TypeInfo coco3_cart_types[] = {
    {
        .name = TYPE_COCO3_CART_BUS,
        .parent = TYPE_BUS,
        .instance_size = sizeof(Coco3CartBus),
        .instance_init = coco3_cart_bus_init,
        .class_init = coco3_cart_bus_class_init,
    },
    {
        .name = TYPE_COCO3_CART,
        .parent = TYPE_DEVICE,
        .instance_size = sizeof(Coco3CartState),
        .class_init = coco3_cart_class_init,
    }
};

DEFINE_TYPES(coco3_cart_types)
