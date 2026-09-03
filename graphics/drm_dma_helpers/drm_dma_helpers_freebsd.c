// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FreeBSD module glue for drm_dma_helpers.
 *
 * The helpers themselves are vendored Linux sources with no module_init of
 * their own -- they are a library other DRM drivers link against. This file
 * supplies the kld module declaration and the dependency edges so the .ko
 * loads standalone and its EXPORT_SYMS resolve for consumers. Same shape as
 * drm_extra_helpers_freebsd.c and drm_shmem_helpers_freebsd.c.
 */
#include <linux/module.h>

static int __init
drm_dma_helpers_init(void)
{
	return (0);
}

static void __exit
drm_dma_helpers_exit(void)
{
}

LKPI_DRIVER_MODULE(drm_dma_helpers, drm_dma_helpers_init,
    drm_dma_helpers_exit);

/*
 * MODULE_VERSION is required rather than decorative: this module exists to be
 * depended on, and FreeBSD cannot satisfy a versioned dependency against a
 * module that never declared a version. A consumer carrying
 * MODULE_DEPEND(<drv>, drm_dma_helpers, 1, 1, 1) would otherwise fail kldload
 * with "depends on drm_dma_helpers - not available or version mismatch",
 * which reaches kextload as a bare "Exec format error". drm_extra_helpers
 * records the same lesson at more length.
 *
 * No ttm edge here, unlike drm_extra_helpers: the DMA GEM helpers are built on
 * the DRM core's own GEM and the DMA API, not on TTM.
 */
MODULE_VERSION(drm_dma_helpers, 1);
MODULE_DEPEND(drm_dma_helpers, drmn, 2, 2, 2);
MODULE_DEPEND(drm_dma_helpers, linuxkpi, 1, 1, 1);

/*
 * dmabuf, for the same reason drm_shmem_helpers carries it: the PRIME import
 * and vmap paths here reference real dma_buf symbols --
 * dma_buf_vmap_unlocked(), dma_buf_vunmap_unlocked() and dma_buf_attachment --
 * so the edge is load-bearing, not decorative. MODULE_DEPEND is depth-1, so it
 * has to be stated here rather than inherited through drmn.
 */
MODULE_DEPEND(drm_dma_helpers, dmabuf, 1, 1, 1);
