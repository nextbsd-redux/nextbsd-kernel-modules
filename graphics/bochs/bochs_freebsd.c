// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FreeBSD module dependency edges for bochs.
 *
 * bochs.c registers itself through drm_module_pci_driver(), which drm-kmod's
 * include/drm/drm_module.h maps onto LinuxKPI's module_driver/pci_register_driver
 * -- so no init/exit glue is needed here, only the kld dependency graph.
 *
 * The drm_extra_helpers edge is the one that is NOT inherited from drm-kmod:
 * drm_simple_kms_helper and drm_gem_vram_helper live in that companion module
 * because drm-kmod omits both.
 */
#include <linux/module.h>

/*
 * The hw.bochs sysctl node is declared in bochs.c, not here: linuxkpi's
 * moduleparam.h expands the driver's tunables in whichever TU declares them,
 * and bochs.c is the file with module_param_named(). Declaring it in both
 * places is a duplicate symbol at link time (ld.lld: duplicate symbol
 * sysctl___hw_bochs), which is exactly how iteration 6 failed.
 */

MODULE_DEPEND(bochs, drmn, 2, 2, 2);
MODULE_DEPEND(bochs, ttm, 1, 1, 1);
MODULE_DEPEND(bochs, linuxkpi, 1, 1, 1);
MODULE_DEPEND(bochs, drm_extra_helpers, 1, 1, 1);
