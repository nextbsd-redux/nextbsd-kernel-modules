// SPDX-License-Identifier: MIT
/*
 * FreeBSD module dependency edges for vboxvideo.
 *
 * vbox_drv.c carries the registration itself (LKPI_DRIVER_MODULE, see the long
 * comment there), so no init/exit glue is needed here -- only the kld
 * dependency graph.
 *
 * The drm_extra_helpers edge is the one that is NOT inherited from drm-kmod:
 * drm_gem_vram_helper, drm_simple_kms_helper and drm_gem_framebuffer_helper all
 * live in that companion module because drm-kmod omits them. vboxvideo is the
 * second consumer of that module, which is the whole reason it was built as a
 * shared export rather than bundled into bochs.
 */
#include <linux/module.h>

/*
 * The hw.vboxvideo sysctl node is declared in vbox_drv.c, not here: linuxkpi's
 * moduleparam.h expands the driver's tunables in whichever TU declares them,
 * and vbox_drv.c is the file with module_param_named(). Declaring it in both
 * places is a duplicate symbol at link time (ld.lld: duplicate symbol
 * sysctl___hw_vboxvideo), which is how the equivalent bochs iteration failed.
 */

MODULE_DEPEND(vboxvideo, drmn, 2, 2, 2);
MODULE_DEPEND(vboxvideo, ttm, 1, 1, 1);
MODULE_DEPEND(vboxvideo, linuxkpi, 1, 1, 1);
MODULE_DEPEND(vboxvideo, drm_extra_helpers, 1, 1, 1);
