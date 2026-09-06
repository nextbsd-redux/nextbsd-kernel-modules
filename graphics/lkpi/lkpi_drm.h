/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * DRM helpers vc4 needs that drm-kmod does not provide
 * (nextbsd-kernel-extensions#51).
 *
 * FORCE-INCLUDED, not a shadowing header. The Makefile adds
 *
 *	CFLAGS+= -include ${.CURDIR:H}/lkpi/lkpi_drm.h
 *
 * so this is pulled in ahead of every source in the module, and it includes
 * the real <drm/drm_managed.h> and <linux/dma-fence.h> itself before adding
 * what is missing.
 *
 * That is deliberate. The obvious alternative -- shipping our own
 * <linux/dma-fence.h> in lkpi/linux/ -- would shadow drm-kmod's copy of the
 * same header for this module and silently drop whatever drm-kmod had added
 * to it. That mistake broke `drm-kmod build` on both arches earlier in #51,
 * when lkpi/linux/{dma-mapping,mm,iosys-map}.h shadowed drm-kmod's versions.
 * The rule that came out of it: only shadow a header whose consumers are all
 * inside this module. dma-fence.h and drm_managed.h are drm core; they are
 * not.
 *
 * The earlier attempt at this (closed PR #56) patched drm-kmod instead. Two of
 * its three pieces did not need to -- they add functions, not struct members --
 * and those two are here.
 */
#ifndef _LKPI_DRM_H_
#define	_LKPI_DRM_H_

#ifndef LKPI_PFX
#error "LKPI_PFX must be defined by the module Makefile"
#endif
#define	LKPI_DRM_SYM2(p, n)	p ## n
#define	LKPI_DRM_SYM1(p, n)	LKPI_DRM_SYM2(p, n)
#define	LKPI_DRM_SYM(n)		LKPI_DRM_SYM1(LKPI_PFX, n)

#define	drmm_mutex_init		LKPI_DRM_SYM(drmm_mutex_init)
#define	dma_fence_match_context	LKPI_DRM_SYM(dma_fence_match_context)

#include <linux/types.h>
#include <linux/dma-fence.h>
#include <drm/drm_managed.h>

struct mutex;
struct drm_device;

/*
 * A mutex destroyed when the drm_device is. The point of the drmm_ family is
 * that a driver need not unwind by hand: everything registered is released in
 * reverse order when the device goes, and vc4 uses it for locks that live
 * exactly as long as the device.
 */
int	drmm_mutex_init(struct drm_device *dev, struct mutex *lock);

/*
 * Is every fence here from `context`?
 *
 * vc4 uses it to skip waiting on a fence it produced itself -- a fence from
 * the caller's own context is already ordered by submission order.
 *
 * The array case is what matters for correctness: an array fence whose members
 * span contexts must NOT report a match. Reporting one makes the caller skip a
 * wait it genuinely needs, and that surfaces much later as rendering against a
 * buffer that was not ready.
 */
bool	dma_fence_match_context(struct dma_fence *fence, u64 context);

#endif /* _LKPI_DRM_H_ */
