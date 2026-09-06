/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * drm/drm_fbdev_dma.h (nextbsd-kernel-extensions#51).
 *
 * One symbol: drm_fbdev_dma_setup(), which sets up fbdev emulation over a DMA
 * GEM framebuffer so the console can draw on the KMS device.
 *
 * A no-op here, which has a visible consequence worth stating: without fbdev
 * emulation the vt console does NOT move onto this driver. That is exactly
 * what firmware KMS does today on the Pi 5 -- the console stays on the BCM2835
 * firmware framebuffer while /dev/dri/card0 exists alongside it:
 *
 *	fb0: <BCM2835 VT framebuffer driver> on simplebus0
 *	VT: initialize with new VT driver "fb".
 *
 * So the display comes up and X can drive it, but the console does not follow.
 * Wiring this to drm-kmod's fbdev path is its own piece of work; stubbed here
 * so the probe measures the KMS surface rather than stopping at this include.
 */
#ifndef _LINUXKPI_DRM_DRM_FBDEV_DMA_H_
#define	_LINUXKPI_DRM_DRM_FBDEV_DMA_H_

#include <linux/types.h>

struct drm_device;

static inline void
drm_fbdev_dma_setup(struct drm_device *dev, unsigned int preferred_bpp)
{
}

#endif /* _LINUXKPI_DRM_DRM_FBDEV_DMA_H_ */
