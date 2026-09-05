/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Forced into every translation unit of this module by the Makefile's
 * -include (nextbsd-kernel#176).
 *
 * drm-kmod's moduleparam.h emits two sysctls for each module_param(): one
 * under LINUXKPI_PARAM_PARENT (hw.compat.linuxkpi) and a second under
 *
 *	DRM_PARAM_NAME = DRM_PARAM_PARENT ## DRM_SYSCTL_PARAM_PREFIX
 *
 * which with our -DDRM_SYSCTL_PARAM_PREFIX=_vc4_fkms is _hw_vc4_fkms. So
 * vc4_firmware_kms.c:43
 *
 *	module_param(fkms_max_refresh_rate, int, 0644);
 *
 * refers to sysctl___hw_vc4_fkms, and the node has to be visible there. The
 * vendored driver is unmodified, so it arrives by -include rather than by
 * editing it.
 *
 * DECL here and NODE in exactly one file: bochs records that defining the node
 * in two translation units is a duplicate symbol at link time. Ours is in
 * vc4_fkms_master.c.
 */
#ifndef _VC4_FKMS_PARAM_H_
#define	_VC4_FKMS_PARAM_H_

#include <sys/sysctl.h>

SYSCTL_DECL(_hw_vc4_fkms);

#endif /* _VC4_FKMS_PARAM_H_ */
