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
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>

#include <drm/drm_gem.h>

/*
 * drm_gem_get_pages() / drm_gem_put_pages().
 *
 * These are drm CORE functions, not helpers -- drm-kmod ships them in
 * drivers/gpu/drm/drm_gem.c with their EXPORT_SYMBOLs intact, and compiles
 * them into NOTHING: both sit inside a `#ifdef __linux__` block, so on this
 * platform the symbols exist in source and in no .ko. Exactly the shape of the
 * drm_format_helper.c trap that drm_extra_helpers already works around, and
 * found the same way -- tools/check-kext-symbols.py, after the module built
 * and packaged cleanly.
 *
 * They are defined here rather than patched into drm-kmod because this module
 * is their only consumer: drm_gem_shmem_get_pages() calls them to populate a
 * GEM object from its swap backing. Nothing else in the tree defines these
 * symbols, so there is no duplicate-definition risk in claiming them.
 *
 * The FreeBSD difference is the mapping handle. Linux walks obj->filp->
 * f_mapping, an address_space; here the backing store is the OBJT_SWAP
 * vm_object linuxkpi hangs off struct linux_file as f_shmem, and
 * shmem_read_mapping_page() takes that object directly.
 */
struct page **
drm_gem_get_pages(struct drm_gem_object *obj)
{
	struct page **pages;
	struct page *p;
	vm_object_t mapping;
	unsigned int i, npages;

	if (obj->filp == NULL)
		return (ERR_PTR(-EINVAL));

	mapping = obj->filp->f_shmem;
	npages = obj->size >> PAGE_SHIFT;

	pages = kvmalloc_array(npages, sizeof(struct page *), GFP_KERNEL);
	if (pages == NULL)
		return (ERR_PTR(-ENOMEM));

	for (i = 0; i < npages; i++) {
		p = shmem_read_mapping_page(mapping, i);
		if (IS_ERR(p))
			goto fail;
		pages[i] = p;
	}

	return (pages);

fail:
	while (i-- > 0)
		put_page(pages[i]);
	kvfree(pages);

	return (ERR_CAST(p));
}

void
drm_gem_put_pages(struct drm_gem_object *obj, struct page **pages,
    bool dirty, bool accessed)
{
	unsigned int i, npages;

	npages = obj->size >> PAGE_SHIFT;

	for (i = 0; i < npages; i++) {
		if (pages[i] == NULL)
			continue;
		/*
		 * accessed is ignored: it drives Linux' LRU aging, which has no
		 * counterpart for a swap object's pages here. dirty is not --
		 * dropping it would let the pager discard written-to scanout
		 * pages as clean.
		 */
		if (dirty)
			set_page_dirty(pages[i]);
		put_page(pages[i]);
	}

	kvfree(pages);
}

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
