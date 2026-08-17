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
 * LinuxKPI virtio shim: feature negotiation, device-config accessors and
 * virtqueue discovery. See linux/virtio.h for the overall mapping.
 */

#ifndef _LINUXKPI_LINUX_VIRTIO_CONFIG_H_
#define	_LINUXKPI_LINUX_VIRTIO_CONFIG_H_

#include <linux/virtio.h>
#include <linux/err.h>
#include <linux/bug.h>

/*
 * Transport feature bits, from the virtio spec. Spelled here rather than
 * included from FreeBSD's dev/virtio/virtio_config.h because that header is
 * not on a linuxkpi include path and carries its own VIRTIO_* status macros
 * that would collide.
 */
#define	VIRTIO_RING_F_INDIRECT_DESC	28
#define	VIRTIO_RING_F_EVENT_IDX		29
#define	VIRTIO_F_VERSION_1		32
#define	VIRTIO_F_ACCESS_PLATFORM	33

struct virtio_shm_region {
	uint64_t	addr;
	uint64_t	len;
};

/*
 * struct virtqueue_info is the v6.12 shape of find_vqs(); earlier kernels
 * passed three parallel arrays. Matching 6.12 exactly keeps the vendored
 * driver diffable against the tag drm-kmod tracks.
 */
struct virtqueue_info {
	const char	*name;
	vq_callback_t	*callback;
	bool		 ctx;
};

struct virtio_config_ops {
	void	(*del_vqs)(struct virtio_device *);
};

bool	lkpi_virtio_has_feature(struct virtio_device *vdev, unsigned int fbit);
void	lkpi_virtio_cread_bytes(struct virtio_device *vdev, unsigned int offset,
	    void *buf, size_t len);
void	lkpi_virtio_cwrite_bytes(struct virtio_device *vdev,
	    unsigned int offset, const void *buf, size_t len);
int	lkpi_virtio_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
	    struct lkpi_virtqueue *vqs[], struct virtqueue_info vqs_info[],
	    void *desc);
void	lkpi_virtio_device_ready(struct virtio_device *vdev);
void	lkpi_virtio_reset_device(struct virtio_device *vdev);

/*
 * Linux passes a feature BIT number; FreeBSD's virtio_with_feature() takes a
 * MASK. Getting this wrong does not fail loudly -- it silently reports the
 * wrong features, and virtio-gpu would then negotiate VIRGL against a host
 * that never offered it.
 */
#define	virtio_has_feature(vdev, fbit)	lkpi_virtio_has_feature((vdev), (fbit))

/*
 * VIRTIO_F_ACCESS_PLATFORM quirk detection. FreeBSD's busdma path already
 * hands the device whatever addresses the platform requires, so there is no
 * quirk to report; virtio-gpu uses this only to decide whether to bounce.
 */
#define	virtio_has_dma_quirk(vdev)	(false)

#define	virtio_cread_le(vdev, structname, member, ptr)			\
	lkpi_virtio_cread_bytes((vdev), offsetof(structname, member),	\
	    (ptr), sizeof(((structname *)0)->member))

#define	virtio_cwrite_le(vdev, structname, member, ptr)			\
	lkpi_virtio_cwrite_bytes((vdev), offsetof(structname, member),	\
	    (ptr), sizeof(((structname *)0)->member))

#define	virtio_find_vqs(vdev, nvqs, vqs, vqs_info, desc)		\
	lkpi_virtio_find_vqs((vdev), (nvqs), (vqs), (vqs_info), (desc))

#define	virtio_device_ready(vdev)	lkpi_virtio_device_ready(vdev)
#define	virtio_reset_device(vdev)	lkpi_virtio_reset_device(vdev)

/*
 * Shared-memory regions are how the host exposes a host-visible blob window.
 * FreeBSD's virtio bus has no get_shm_region method -- neither virtio_pci nor
 * virtio_mmio implements one -- so this reports "no region", which is the
 * same answer the driver gets from a host that does not offer one, and the
 * blob paths that would use it are compiled out (see virtgpu_drv.h). Adding
 * the bus method is a FreeBSD base change, and is the gate on blob support.
 */
static inline bool
virtio_get_shm_region(struct virtio_device *vdev,
    struct virtio_shm_region *region, uint8_t id)
{

	(void)vdev;
	(void)region;
	(void)id;
	return (false);
}

#endif	/* _LINUXKPI_LINUX_VIRTIO_CONFIG_H_ */
