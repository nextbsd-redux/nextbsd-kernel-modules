/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *    Gerd Hoffmann <kraxel@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/wait.h>

#include <drm/drm.h>
#include <drm/drm_aperture.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_file.h>

#include "virtgpu_drv.h"

#ifdef __FreeBSD__
#include <sys/param.h>
#include <sys/sysctl.h>

/*
 * The Makefile passes -DDRM_SYSCTL_PARAM_PREFIX=_virtio_gpu_drm, so linuxkpi's
 * moduleparam.h hangs this driver's tunables off hw.virtio_gpu_drm; that parent
 * node must be declared in the SAME translation unit the parameters expand in,
 * and module_param_named(modeset, ...) below is in this one. Every drm-kmod
 * driver does the same in its own source (i915_params.c, radeon_drv.c,
 * amdgpu_drv.c); putting it in the companion glue file gives a duplicate
 * symbol at link, and putting it nowhere gives an undefined one.
 */
SYSCTL_NODE(_hw, OID_AUTO, virtio_gpu_drm, CTLFLAG_RW | CTLFLAG_MPSAFE, 0,
    "virtio-gpu DRM parameters");

/*
 * pci_is_vga() is absent from linuxkpi. Transcribed from Linux v6.12's
 * include/linux/pci.h, both arms: a virtio-vga function presents class
 * 03 00 (PCI_CLASS_DISPLAY_VGA), while plain virtio-gpu-pci presents display
 * class "other" and is correctly NOT a VGA device -- which is the whole point
 * of the test here, since only the VGA form owns the legacy aperture that has
 * to be taken away from efifb.
 */
#ifndef PCI_CLASS_NOT_DEFINED_VGA
#define	PCI_CLASS_NOT_DEFINED_VGA	0x0001
#endif
static inline bool
pci_is_vga(struct pci_dev *pdev)
{
	if ((pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA)
		return (true);
	if ((pdev->class >> 8) == PCI_CLASS_NOT_DEFINED_VGA)
		return (true);
	return (false);
}
#endif /* __FreeBSD__ */

static const struct drm_driver driver;

static int virtio_gpu_modeset = -1;

MODULE_PARM_DESC(modeset, "Disable/Enable modesetting");
module_param_named(modeset, virtio_gpu_modeset, int, 0400);

static int virtio_gpu_pci_quirk(struct drm_device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev->dev);
	const char *pname = dev_name(&pdev->dev);
	bool vga = pci_is_vga(pdev);
	int ret;

	DRM_INFO("pci: %s detected at %s\n",
		 vga ? "virtio-vga" : "virtio-gpu-pci",
		 pname);
	if (vga) {
		ret = drm_aperture_remove_conflicting_pci_framebuffers(pdev, &driver);
		if (ret)
			return ret;
	}

	return 0;
}

static int virtio_gpu_probe(struct virtio_device *vdev)
{
	struct drm_device *dev;
	int ret;

	if (drm_firmware_drivers_only() && virtio_gpu_modeset == -1)
		return -EINVAL;

	if (virtio_gpu_modeset == 0)
		return -EINVAL;

	/*
	 * The virtio-gpu device is a virtual device that doesn't have DMA
	 * ops assigned to it, nor DMA mask set and etc. Its parent device
	 * is actual GPU device we want to use it for the DRM's device in
	 * order to benefit from using generic DRM APIs.
	 */
	dev = drm_dev_alloc(&driver, vdev->dev.parent);
	if (IS_ERR(dev))
		return PTR_ERR(dev);
	vdev->priv = dev;

	if (dev_is_pci(vdev->dev.parent)) {
		ret = virtio_gpu_pci_quirk(dev);
		if (ret)
			goto err_free;
	}

	dma_set_max_seg_size(dev->dev, dma_max_mapping_size(dev->dev) ?: UINT_MAX);
	ret = virtio_gpu_init(vdev, dev);
	if (ret)
		goto err_free;

	ret = drm_dev_register(dev, 0);
	if (ret)
		goto err_deinit;

	/*
	 * fbdev emulation, which on this driver is the console itself: with no
	 * linear-framebuffer GOP on arm64 (Blt-only under OVMF, absent entirely
	 * under Apple's Virtualization.framework) there is no efifb to fall back
	 * to, so vt(4) has nothing to attach to until this publishes a
	 * framebuffer. drm_fbdev_shmem.c is built from a vendored copy; see
	 * graphics/drm_shmem_helpers/Makefile.
	 */
	drm_fbdev_shmem_setup(vdev->priv, 32);
	return 0;

err_deinit:
	virtio_gpu_deinit(dev);
err_free:
	drm_dev_put(dev);
	return ret;
}

static void virtio_gpu_remove(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;

	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
	virtio_gpu_deinit(dev);
	drm_dev_put(dev);
}

static void virtio_gpu_config_changed(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->config_changed_work);
}

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GPU, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static unsigned int features[] = {
#ifdef __LITTLE_ENDIAN
	/*
	 * Gallium command stream send by virgl is native endian.
	 * Because of that we only support little endian guests on
	 * little endian hosts.
	 */
	VIRTIO_GPU_F_VIRGL,
#endif
	VIRTIO_GPU_F_EDID,
	VIRTIO_GPU_F_RESOURCE_UUID,
	VIRTIO_GPU_F_RESOURCE_BLOB,
	VIRTIO_GPU_F_CONTEXT_INIT,
};
static struct virtio_driver virtio_gpu_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = virtio_gpu_probe,
	.remove = virtio_gpu_remove,
	.config_changed = virtio_gpu_config_changed
};

#ifdef __FreeBSD__
/*
 * No module_virtio_driver(). On Linux that macro registers the driver with the
 * virtio bus; on FreeBSD the bus is newbus and binding is declared at compile
 * time by DRIVER_MODULE() in virtio_gpu_drm_freebsd.c, which is also where the
 * probe/attach/detach methods live.
 *
 * These four wrappers exist because everything the glue needs to reach --
 * probe, remove, config_changed and the feature table -- is static in this
 * file, and vendoring discipline says to add a delta rather than restructure
 * upstream's file.
 */
int
virtio_gpu_bsd_probe(struct virtio_device *vdev)
{
	return virtio_gpu_probe(vdev);
}

void
virtio_gpu_bsd_remove(struct virtio_device *vdev)
{
	virtio_gpu_remove(vdev);
}

void
virtio_gpu_bsd_config_changed(struct virtio_device *vdev)
{
	virtio_gpu_config_changed(vdev);
}

const unsigned int *
virtio_gpu_bsd_features(unsigned int *count)
{
	*count = ARRAY_SIZE(features);
	return features;
}
#else
module_virtio_driver(virtio_gpu_driver);
#endif

#ifndef __FreeBSD__
MODULE_DEVICE_TABLE(virtio, id_table);
#else
/*
 * linuxkpi's MODULE_DEVICE_TABLE only knows the pci bus; spelled with `virtio`
 * it expands to something the compiler reads as a function declaration with an
 * untyped parameter list (-Wimplicit-int, measured on both arches). The
 * equivalent metadata -- what devmatch reads to autoload the module for a
 * device type -- is emitted by VIRTIO_SIMPLE_PNPINFO() in the newbus glue.
 */
#endif
MODULE_DESCRIPTION("Virtio GPU driver");
MODULE_LICENSE("GPL and additional rights");
MODULE_AUTHOR("Dave Airlie <airlied@redhat.com>");
MODULE_AUTHOR("Gerd Hoffmann <kraxel@redhat.com>");
MODULE_AUTHOR("Alon Levy");

DEFINE_DRM_GEM_FOPS(virtio_gpu_driver_fops);

static const struct drm_driver driver = {
	/*
	 * If KMS is disabled DRIVER_MODESET and DRIVER_ATOMIC are masked
	 * out via drm_device::driver_features:
	 */
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_RENDER | DRIVER_ATOMIC |
			   DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE | DRIVER_CURSOR_HOTSPOT,
	.open = virtio_gpu_driver_open,
	.postclose = virtio_gpu_driver_postclose,

	.dumb_create = virtio_gpu_mode_dumb_create,
	.dumb_map_offset = virtio_gpu_mode_dumb_mmap,

#if defined(CONFIG_DEBUG_FS)
	.debugfs_init = virtio_gpu_debugfs_init,
#endif
	.gem_prime_import = virtgpu_gem_prime_import,
	.gem_prime_import_sg_table = virtgpu_gem_prime_import_sg_table,

	.gem_create_object = virtio_gpu_create_object,
	.fops = &virtio_gpu_driver_fops,

	.ioctls = virtio_gpu_ioctls,
	.num_ioctls = DRM_VIRTIO_NUM_IOCTLS,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,

	.release = virtio_gpu_release,
};
