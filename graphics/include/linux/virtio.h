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
 * LinuxKPI virtio shim -- the Linux <linux/virtio.h> API on top of FreeBSD's
 * native virtio bus (dev/virtio/).
 *
 * FreeBSD base has no linuxkpi virtio at all, which is what has kept every
 * Linux virtio driver -- virtio-gpu first among them -- off drm-kmod. The
 * shim exists because FreeBSD's public virtqueue KPI already covers what the
 * Linux one does, only spelled differently and with the notify decision made
 * inside virtqueue_notify() rather than split across kick_prepare/notify:
 *
 *   Linux                        FreeBSD
 *   ---------------------------  ------------------------------------------
 *   virtqueue_add_sgs()          virtqueue_enqueue() + a hand-built sglist
 *   virtqueue_kick_prepare()     folded into virtqueue_notify()
 *   virtqueue_notify()           virtqueue_notify()
 *   virtqueue_get_buf()          virtqueue_dequeue()
 *   virtqueue_enable_cb()        virtqueue_enable_intr()   (inverted sense)
 *   virtqueue_disable_cb()       virtqueue_disable_intr()
 *   virtio_has_feature()         virtio_with_feature()     (bit vs mask)
 *   virtio_cread_le()            virtio_read_device_config()
 *   virtio_find_vqs()            virtio_alloc_virtqueues() + virtio_setup_intr()
 *   vdev->config->del_vqs()      virtio_stop() (the bus frees the queues)
 *
 * Deliberately NOT general: this covers the calls virtio-gpu makes and no
 * more. Growing it for a second driver is expected; guessing at that driver's
 * needs now is not.
 *
 * struct virtqueue is spelled `lkpi_virtqueue` and macro-renamed below. This
 * is not cosmetic. FreeBSD's dev/virtio/virtqueue.h forward-declares
 * `struct virtqueue` and every one of its functions takes that pointer. If the
 * Linux-side struct kept the same tag, a translation unit seeing both would
 * find them silently type-compatible, and handing FreeBSD our wrapper where it
 * expects its own queue compiles clean and corrupts the ring at run time.
 */

#ifndef _LINUXKPI_LINUX_VIRTIO_H_
#define	_LINUXKPI_LINUX_VIRTIO_H_

#include <linux/types.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/scatterlist.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/uuid.h>

/*
 * uuid_t, supplemented rather than supplied.
 *
 * linuxkpi DOES ship <linux/uuid.h>, and its -I comes before ours by design --
 * our headers must only ever be findable, never shadow a real one. So a
 * uuid.h of our own is dead code that can never be reached, which is exactly
 * what the first attempt at this was: the file existed, was never included by
 * anything, and virtgpu_drv.h still failed with "unknown type name 'uuid_t'"
 * in four translation units.
 *
 * What linuxkpi's header actually provides is guid_t, UUID_SIZE and the
 * guid helpers -- but not uuid_t, which is the one virtio-gpu needs for the
 * resource UUID that names a buffer to a second device across a dma-buf
 * export. So it is defined here, in a header linuxkpi has no copy of and
 * cannot shadow. Same shape as Linux' (16 opaque bytes) and as the wire
 * format. If linuxkpi ever adds uuid_t this becomes a redefinition error,
 * which is the right way to find out.
 */
#ifndef UUID_SIZE
#define	UUID_SIZE	16
#endif
typedef struct {
	__u8	b[UUID_SIZE];
} uuid_t;

struct lkpi_virtqueue;
struct virtio_device;
struct virtio_config_ops;

/*
 * Linux carries these in <linux/mod_devicetable.h>; linuxkpi's copy of that
 * header has no virtio section (it covers PCI, DMI and USB only), so they are
 * defined here rather than by patching base.
 */
struct virtio_device_id {
	uint32_t	device;
	uint32_t	vendor;
};
#define	VIRTIO_DEV_ANY_ID	0xffffffff

/*
 * See the header comment: the tag must differ from FreeBSD's, but the driver
 * sources are vendored unmodified and spell it `struct virtqueue`.
 */
#define	virtqueue	lkpi_virtqueue

typedef void vq_callback_t(struct lkpi_virtqueue *);

struct lkpi_virtqueue {
	struct virtio_device	*vdev;
	unsigned int		 index;
	const char		*name;
	vq_callback_t		*callback;

	/*
	 * FreeBSD's `struct virtqueue *`, held as void * so that no translation
	 * unit needs both tags visible at once.
	 */
	void			*vq_bsd;

	/*
	 * Pre-allocated scatter/gather list, sized to the ring at find_vqs()
	 * time. virtqueue_add_sgs() runs under a spinlock in GFP_ATOMIC
	 * context, so it must not allocate.
	 */
	void			*vq_sglist;
	int			 vq_sgmax;

	void			*priv;
};

/*
 * Linux exposes the free-descriptor count as a struct field (vq->num_free);
 * FreeBSD only offers it as a function call, so a wrapper field would be a
 * snapshot that goes stale exactly when it matters -- the queue-full predicates
 * that gate a sleep. There is no field here on purpose: the three sites in
 * virtgpu_vq.c that read vq->num_free carry an #ifdef __FreeBSD__ delta calling
 * this instead, so a fourth site added by a future rebase fails to compile
 * rather than silently reading a stale value.
 */
unsigned int	virtqueue_num_free(struct lkpi_virtqueue *vq);

struct virtio_device {
	struct device			 dev;		/* linuxkpi device */
	const struct virtio_config_ops	*config;
	device_t			 bsddev;	/* newbus device */
	uint64_t			 features;
	void				*priv;

	/*
	 * The queues found by virtio_find_vqs(). Held because FreeBSD starts a
	 * virtqueue with its interrupt disabled -- native drivers arm it at
	 * "up" time -- so virtio_device_ready() has to walk them, and because
	 * del_vqs() is the only place the wrappers can be released.
	 */
	struct lkpi_virtqueue		**vqs;
	unsigned int			 nvqs;
};

struct virtio_driver {
	struct device_driver	 driver;
	const struct virtio_device_id *id_table;
	const unsigned int	*feature_table;
	unsigned int		 feature_table_size;
	int			(*probe)(struct virtio_device *);
	void			(*remove)(struct virtio_device *);
	void			(*config_changed)(struct virtio_device *);
};

/*
 * Queue operations. Every one is a thin translation; the interesting one is
 * add_sgs, whose contract differs in a way that matters for memory safety --
 * see lkpi_virtqueue_add_sgs() in linux_virtio.c.
 */
int	lkpi_virtqueue_add_sgs(struct lkpi_virtqueue *vq,
	    struct scatterlist *sgs[], unsigned int out_sgs,
	    unsigned int in_sgs, void *data, gfp_t gfp);
bool	lkpi_virtqueue_kick_prepare(struct lkpi_virtqueue *vq);
void	lkpi_virtqueue_notify(struct lkpi_virtqueue *vq);
void	*lkpi_virtqueue_get_buf(struct lkpi_virtqueue *vq, unsigned int *len);
bool	lkpi_virtqueue_enable_cb(struct lkpi_virtqueue *vq);
void	lkpi_virtqueue_disable_cb(struct lkpi_virtqueue *vq);

/*
 * virtqueue_notify is the one name that collides with a symbol FreeBSD's
 * virtio.ko already exports, so all six are renamed rather than just that one:
 * a module defining a global that shadows the kernel's is resolved
 * module-locally today and is a trap for whoever links against us tomorrow.
 */
#define	virtqueue_add_sgs(vq, sgs, out, in, data, gfp)			\
	lkpi_virtqueue_add_sgs((vq), (sgs), (out), (in), (data), (gfp))
#define	virtqueue_kick_prepare(vq)	lkpi_virtqueue_kick_prepare(vq)
#define	virtqueue_notify(vq)		lkpi_virtqueue_notify(vq)
#define	virtqueue_get_buf(vq, len)	lkpi_virtqueue_get_buf((vq), (len))
#define	virtqueue_enable_cb(vq)		lkpi_virtqueue_enable_cb(vq)
#define	virtqueue_disable_cb(vq)	lkpi_virtqueue_disable_cb(vq)

#define	dev_to_virtio(_dev)	container_of((_dev), struct virtio_device, dev)

/*
 * Lifecycle, called from a driver's newbus glue rather than from vendored
 * Linux code: FreeBSD's virtio child is a newbus device, so something has to
 * build the Linux-side view of it at attach and tear it down at detach.
 * lkpi_virtio_device_alloc() also performs feature negotiation, because
 * FreeBSD requires it to happen before the virtqueues are allocated and Linux
 * drivers assume it already happened by the time probe() runs.
 */
struct virtio_device *lkpi_virtio_device_alloc(device_t bsddev,
	    const unsigned int *feature_table, unsigned int feature_table_size);
void	lkpi_virtio_device_free(struct virtio_device *vdev);
void	lkpi_virtio_free_vq(struct lkpi_virtqueue *vq);

int	lkpi_register_virtio_driver(struct virtio_driver *drv);
void	lkpi_unregister_virtio_driver(struct virtio_driver *drv);

#define	register_virtio_driver(drv)	lkpi_register_virtio_driver(drv)
#define	unregister_virtio_driver(drv)	lkpi_unregister_virtio_driver(drv)

/*
 * module_virtio_driver() is deliberately absent. LinuxKPI routes a DRM driver
 * onto the right devclass by driver NAME, and the newbus attach that a virtio
 * child needs has no LinuxKPI equivalent at all, so the driver's module glue is
 * hand-written (virtio_gpu_drm/virtio_gpu_drm_freebsd.c) rather than macro
 * expanded. Providing a macro here would invite the i915-style path that fails
 * with ENOEXEC.
 */

#endif	/* _LINUXKPI_LINUX_VIRTIO_H_ */
