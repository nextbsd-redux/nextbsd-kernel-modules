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
#include <sys/param.h>
#include <sys/sysctl.h>

#include <linux/module.h>

/*
 * The Makefile passes -DDRM_SYSCTL_PARAM_PREFIX=_${KMOD}, which makes
 * linuxkpi's moduleparam.h hang this driver's tunables off hw.bochs. That
 * parent node has to exist or every LINUXKPI_PARAM_* expansion fails with
 * "use of undeclared identifier 'sysctl___hw_bochs'". Each drm-kmod driver
 * declares its own: i915_params.c has SYSCTL_NODE(_hw, .., i915kms), radeon
 * radeonkms, amdgpu amdgpu. bochs.c is vendored from Linux and has no
 * FreeBSD section to carry it, so it lives here.
 */
SYSCTL_NODE(_hw, OID_AUTO, bochs, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "bochs-drm parameters");

MODULE_DEPEND(bochs, drmn, 2, 2, 2);
MODULE_DEPEND(bochs, ttm, 1, 1, 1);
MODULE_DEPEND(bochs, linuxkpi, 1, 1, 1);
MODULE_DEPEND(bochs, drm_extra_helpers, 1, 1, 1);
