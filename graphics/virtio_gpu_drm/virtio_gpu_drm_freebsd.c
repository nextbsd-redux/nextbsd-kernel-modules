/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, Joseph Maloney
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * newbus attach glue for the vendored virtio-gpu DRM driver.
 *
 * Clean-room: written against FreeBSD's own virtio child drivers (if_vtnet.c
 * is the model for the method table and the config-change hook) rather than
 * derived from base's virtio_gpu(4), so nothing here inherits that driver's
 * licence or its console-ownership behaviour.
 *
 * Two things this file exists to bridge:
 *
 * 1. Binding. Linux registers a virtio driver at run time from
 *    module_virtio_driver(); FreeBSD declares it at compile time with
 *    DRIVER_MODULE() against virtio_pci and virtio_mmio. The vendored
 *    virtgpu_drv.c keeps its Linux shape and exports four wrappers for us.
 *
 * 2. The device hierarchy. drm_dev_alloc() wants the PARENT of the virtio
 *    device -- on Linux, the PCI function the virtio device hangs off. FreeBSD
 *    has that device_t but no linuxkpi `struct device` for it, because
 *    virtio_pci is a native driver and linuxkpi only builds pci_dev's for
 *    devices it attached itself. lkpinew_pci_dev() is the supported way to
 *    manufacture one, and it is what makes dev_is_pci()/to_pci_dev() answer
 *    correctly inside the vendored code -- which in turn is what lets
 *    virtio_gpu_pci_quirk() take the framebuffer away from efifb.
 *
 * Deliberately NOT here: `device virtio_gpu` in a kernel config, ever. Base's
 * module registers at VD_PRIORITY_GENERIC+10 against efifb's +1, so merely
 * compiling it in displaces the console driver that works. This module is
 * loadable-only and binds the same device by a different name.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <machine/bus.h>

/* is_pci_device(), used to tell the PCI transport from virtio-mmio. */
#include <dev/pci/pcivar.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>

#include <linux/device.h>
#include <linux/pci.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>

#include "virtio_if.h"

/* Exported by the vendored virtgpu_drv.c under __FreeBSD__. */
int		 virtio_gpu_bsd_probe(struct virtio_device *vdev);
void		 virtio_gpu_bsd_remove(struct virtio_device *vdev);
void		 virtio_gpu_bsd_config_changed(struct virtio_device *vdev);
const unsigned int *virtio_gpu_bsd_features(unsigned int *count);

struct virtio_gpu_drm_softc {
	device_t		 vgd_dev;
	struct virtio_device	*vgd_vdev;
	struct pci_dev		*vgd_pdev;	/* manufactured, PCI transport */
	struct device		 vgd_parent;	/* used when not PCI (mmio) */
};

/*
 * VIRTIO_SIMPLE_PNPINFO would also emit the PNP metadata that devmatch uses to
 * autoload a module by device type. That is exactly what we want here -- the
 * kext is loaded on match rather than at boot -- and it is why the module is
 * named virtio_gpu_drm: base already owns the name `virtio_gpu`, and two
 * modules claiming one PNP identity is a coin flip over which one binds.
 */
VIRTIO_SIMPLE_PNPINFO(virtio_gpu_drm, VIRTIO_ID_GPU,
    "VirtIO GPU (DRM/KMS)");

static int
virtio_gpu_drm_probe(device_t dev)
{

	return (VIRTIO_SIMPLE_PROBE(dev, virtio_gpu_drm));
}

static int
virtio_gpu_drm_attach(device_t dev)
{
	struct virtio_gpu_drm_softc *sc;
	const unsigned int *features;
	device_t parent;
	unsigned int nfeatures;
	int error;

	sc = device_get_softc(dev);
	sc->vgd_dev = dev;

	features = virtio_gpu_bsd_features(&nfeatures);

	/*
	 * Feature negotiation must precede virtqueue allocation on FreeBSD,
	 * and Linux drivers assume it already happened by the time probe()
	 * runs, so the shim does it as part of building the device.
	 */
	sc->vgd_vdev = lkpi_virtio_device_alloc(dev, features, nfeatures);
	if (sc->vgd_vdev == NULL)
		return (ENOMEM);

	/*
	 * The Linux-side view of this device and of its parent. The parent is
	 * what drm_dev_alloc() is handed, so it has to be the transport, not
	 * the virtio device itself.
	 */
	/*
	 * Note what is NOT called here: device_initialize(). linuxkpi's is not
	 * a plain field initialiser -- it dereferences dev->class->bsdclass and
	 * dev->class->kobj.name, and will call device_add_child() to
	 * manufacture a NEW newbus device when dev->parent is set. On a bare
	 * struct device with no class that is a null dereference, and the
	 * child it wants to create already exists: it is the device we are
	 * attaching to. Filling the two fields that get read is both necessary
	 * and sufficient.
	 */
	parent = device_get_parent(dev);
	if (is_pci_device(parent)) {
		/*
		 * lkpinew_pci_dev() builds a complete struct pci_dev around an
		 * existing newbus PCI device -- which is exactly our situation,
		 * because virtio_pci is a native FreeBSD driver and linuxkpi
		 * only builds pci_dev's for devices it attached itself. This is
		 * what makes dev_is_pci() and to_pci_dev() answer correctly
		 * inside the vendored probe, and so what lets
		 * virtio_gpu_pci_quirk() take the aperture from efifb.
		 */
		sc->vgd_pdev = lkpinew_pci_dev(parent);
		if (sc->vgd_pdev == NULL) {
			lkpi_virtio_device_free(sc->vgd_vdev);
			sc->vgd_vdev = NULL;
			return (ENOMEM);
		}
		sc->vgd_vdev->dev.parent = &sc->vgd_pdev->dev;
	} else {
		/*
		 * virtio-mmio: no PCI function to point at. dev_is_pci() will
		 * report false and the vendored probe skips the framebuffer
		 * handover, which is correct -- there is no PCI aperture to
		 * take over from. The devres list is initialised because this
		 * struct device is handed to drm_dev_alloc() as the DRM
		 * device's parent, and anything reaching for devm_* on it would
		 * otherwise walk an empty list head.
		 */
		sc->vgd_parent.bsddev = parent;
		INIT_LIST_HEAD(&sc->vgd_parent.devres_head);
		spin_lock_init(&sc->vgd_parent.devres_lock);
		sc->vgd_vdev->dev.parent = &sc->vgd_parent;
	}

	sc->vgd_vdev->dev.bsddev = dev;

	error = virtio_gpu_bsd_probe(sc->vgd_vdev);
	if (error != 0) {
		device_printf(dev, "virtio-gpu probe failed: %d\n", error);
		if (sc->vgd_pdev != NULL)
			pci_dev_put(sc->vgd_pdev);
		lkpi_virtio_device_free(sc->vgd_vdev);
		sc->vgd_vdev = NULL;
		/* Linux errno on the way in, FreeBSD errno on the way out. */
		return (-error);
	}

	return (0);
}

static int
virtio_gpu_drm_detach(device_t dev)
{
	struct virtio_gpu_drm_softc *sc;

	sc = device_get_softc(dev);
	if (sc->vgd_vdev != NULL) {
		virtio_gpu_bsd_remove(sc->vgd_vdev);
		if (sc->vgd_pdev != NULL)
			pci_dev_put(sc->vgd_pdev);
		lkpi_virtio_device_free(sc->vgd_vdev);
		sc->vgd_vdev = NULL;
	}

	return (0);
}

/*
 * The host changed something in the config space -- for virtio-gpu that means
 * the display topology moved, which is how a window resize on the host becomes
 * a mode change in the guest. The vendored handler defers to a workqueue, so
 * this is safe to call from the bus.
 */
static int
virtio_gpu_drm_config_change(device_t dev)
{
	struct virtio_gpu_drm_softc *sc;

	sc = device_get_softc(dev);
	if (sc->vgd_vdev != NULL)
		virtio_gpu_bsd_config_changed(sc->vgd_vdev);

	return (0);
}

static device_method_t virtio_gpu_drm_methods[] = {
	DEVMETHOD(device_probe,		virtio_gpu_drm_probe),
	DEVMETHOD(device_attach,	virtio_gpu_drm_attach),
	DEVMETHOD(device_detach,	virtio_gpu_drm_detach),

	DEVMETHOD(virtio_config_change,	virtio_gpu_drm_config_change),

	DEVMETHOD_END
};

static driver_t virtio_gpu_drm_driver = {
	"virtio_gpu_drm",
	virtio_gpu_drm_methods,
	sizeof(struct virtio_gpu_drm_softc),
};

VIRTIO_DRIVER_MODULE(virtio_gpu_drm, virtio_gpu_drm_driver, 0, 0);
MODULE_VERSION(virtio_gpu_drm, 1);

/*
 * MODULE_DEPEND is depth-1: kern_linker.c resolves symbols against file->deps[]
 * without recursing, so every module whose symbols this one references
 * directly has to be named here even when a dependency already names it. That
 * is why drmn and linuxkpi appear alongside linux_virtio.
 */
MODULE_DEPEND(virtio_gpu_drm, virtio, 1, 1, 1);
MODULE_DEPEND(virtio_gpu_drm, linux_virtio, 1, 1, 1);
MODULE_DEPEND(virtio_gpu_drm, linuxkpi, 1, 1, 1);
MODULE_DEPEND(virtio_gpu_drm, drmn, 2, 2, 2);
MODULE_DEPEND(virtio_gpu_drm, dmabuf, 1, 1, 1);
/*
 * Two helper modules, not one. drm_extra_helpers carries drm_simple_encoder_init
 * and drm_gem_fb_create_handle; drm_shmem_helpers carries the shmem GEM layer
 * and drm_fbdev_shmem. They are separate .ko's so that a failure in the newer,
 * unproven one cannot take down the module BochsGraphics resolves through.
 */
MODULE_DEPEND(virtio_gpu_drm, drm_extra_helpers, 1, 1, 1);
MODULE_DEPEND(virtio_gpu_drm, drm_shmem_helpers, 1, 1, 1);
