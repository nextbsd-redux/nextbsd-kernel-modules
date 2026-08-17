// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * FreeBSD module glue for drm_shmem_helpers.
 *
 * Same shape as drm_extra_helpers_freebsd.c: the helpers are vendored Linux
 * sources with no module_init of their own, so this file supplies the kld
 * module declaration and the dependency edges that let the .ko load standalone
 * and export its symbols to consumers (virtio-gpu today; ast, mgag200, cirrus
 * and Hyper-V are the same helper's other customers).
 *
 * Split from drm_extra_helpers deliberately rather than added to it. That
 * module is required by the boot test -- BochsGraphics resolves through
 * IOGraphicsExtras -- so folding unproven code into it would let a compile
 * error in the shmem layer take down a driver that already works.
 *
 * NOTE ON LICENCE: this glue is GPL-2.0-or-later to match its siblings, but
 * the helper it wraps, drivers/gpu/drm/drm_gem_shmem_helper.c, is
 * GPL-2.0-ONLY upstream. It is not the first GPL-2.0-only file vendored here
 * (lib/genalloc.c, which vboxvideo links, is the other); per-file SPDX
 * governs, and graphics/README.md records the position.
 */
#include <linux/module.h>

static int __init
drm_shmem_helpers_init(void)
{
	return (0);
}

static void __exit
drm_shmem_helpers_exit(void)
{
}

LKPI_DRIVER_MODULE(drm_shmem_helpers, drm_shmem_helpers_init,
    drm_shmem_helpers_exit);

/*
 * REQUIRED, not decorative: virtio_gpu_drm carries
 * MODULE_DEPEND(virtio_gpu_drm, drm_shmem_helpers, 1, 1, 1), and FreeBSD
 * cannot satisfy a versioned dependency against a module that never declared a
 * version -- the dependent's kldload fails with "not available or version
 * mismatch", which reaches kextload as a bare "Exec format error".
 */
MODULE_VERSION(drm_shmem_helpers, 1);
MODULE_DEPEND(drm_shmem_helpers, drmn, 2, 2, 2);
MODULE_DEPEND(drm_shmem_helpers, linuxkpi, 1, 1, 1);
/*
 * dmabuf is direct, not inherited: drm_gem_shmem_helper.c implements the
 * prime/dma-buf entry points (drm_gem_shmem_prime_import_sg_table and the
 * vmap/mmap paths dma_buf_ops reach). MODULE_DEPEND is depth-1.
 */
MODULE_DEPEND(drm_shmem_helpers, dmabuf, 1, 1, 1);
