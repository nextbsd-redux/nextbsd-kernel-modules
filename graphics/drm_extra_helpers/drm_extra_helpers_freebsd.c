// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FreeBSD module glue for drm_extra_helpers.
 *
 * The helpers themselves are vendored Linux sources with no module_init of
 * their own -- they are a library other DRM drivers link against. This file
 * supplies the newbus/kld module declaration and the dependency edges so the
 * .ko loads standalone and its EXPORT_SYMS resolve for consumers (bochs,
 * vboxvideo). Shape copied from drm-kmod's dummygfx_drv.c.
 */
#include <linux/module.h>

static int __init
drm_extra_helpers_init(void)
{
	return (0);
}

static void __exit
drm_extra_helpers_exit(void)
{
}

LKPI_DRIVER_MODULE(drm_extra_helpers, drm_extra_helpers_init,
    drm_extra_helpers_exit);
MODULE_DEPEND(drm_extra_helpers, drmn, 2, 2, 2);
MODULE_DEPEND(drm_extra_helpers, ttm, 1, 1, 1);
MODULE_DEPEND(drm_extra_helpers, linuxkpi, 1, 1, 1);
