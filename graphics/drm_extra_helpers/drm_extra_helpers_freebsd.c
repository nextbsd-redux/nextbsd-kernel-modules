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

/*
 * REQUIRED, not decorative. This module exists to be depended on: bochs (and
 * later vboxvideo) carry MODULE_DEPEND(<drv>, drm_extra_helpers, 1, 1, 1), and
 * FreeBSD cannot satisfy a versioned dependency against a module that never
 * declared a version. Without this the dependent's kldload fails with
 *
 *     KLD <drv>: depends on drm_extra_helpers - not available or version mismatch
 *     linker_load_file: ... - unsupported file type
 *
 * which surfaces to kextload as a bare "Exec format error" -- an unhelpful
 * message for a dependency problem, and exactly how iteration 11 failed.
 * drm-kmod declares versions for drmn/ttm/linuxkpi for the same reason, which
 * is why those three edges resolved while this one did not.
 */
MODULE_VERSION(drm_extra_helpers, 1);
MODULE_DEPEND(drm_extra_helpers, drmn, 2, 2, 2);
MODULE_DEPEND(drm_extra_helpers, ttm, 1, 1, 1);
MODULE_DEPEND(drm_extra_helpers, linuxkpi, 1, 1, 1);
