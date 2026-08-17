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
 * LinuxKPI virtio shim -- implementation.
 *
 * The whole point of this file is that it needs no FreeBSD base change. The
 * plan this work follows assumed otherwise for a long time, on the grounds
 * that Linux' virtqueue_kick() has no FreeBSD counterpart. It does: FreeBSD
 * folds the "does the host need a notification" decision into
 * virtqueue_notify() rather than exposing it, so Linux' two-step
 * kick_prepare/notify maps onto it exactly, with the prepare step reporting
 * "yes, call notify" unconditionally and the real decision made one layer
 * down. Nothing else in the API was ever the obstacle.
 *
 * Include order matters here and nowhere else in the tree: FreeBSD's virtio
 * headers must be seen BEFORE <linux/virtio.h> renames the `virtqueue` struct
 * tag, and the typedef below captures FreeBSD's spelling while it is still
 * reachable. Reversing these two blocks compiles clean and hands FreeBSD's
 * virtqueue functions a pointer to the Linux wrapper.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sglist.h>

#include <machine/bus.h>

#include <dev/virtio/virtio.h>
#include <dev/virtio/virtqueue.h>

typedef struct virtqueue bsd_virtqueue_t;

/*
 * FreeBSD's virtqueue_notify() captured before <linux/virtio.h> #defines that
 * name onto lkpi_virtqueue_notify(). It is the one function whose name exists
 * on both sides, so without this the shim's own implementation calls itself --
 * measured as an incompatible-pointer error on both arches, and it would have
 * been infinite recursion had the types happened to match.
 */
static inline void
bsd_virtqueue_notify(bsd_virtqueue_t *vq)
{

	virtqueue_notify(vq);
}

#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_dma_buf.h>

MALLOC_DEFINE(M_LKPI_VIRTIO, "lkpivirtio", "LinuxKPI virtio shim");

#define	VQ_BSD(vq)	((bsd_virtqueue_t *)(vq)->vq_bsd)

/*
 * Feature bits this shim always asks the transport for, on top of whatever the
 * driver's feature_table names. VERSION_1 selects the modern (non-legacy)
 * layout, which is what every virtio-gpu host presents; INDIRECT_DESC is what
 * keeps a multi-segment transfer from consuming one ring slot per page.
 */
#define	LKPI_VIRTIO_TRANSPORT_FEATURES					\
	((1ULL << VIRTIO_F_VERSION_1) | (1ULL << VIRTIO_RING_F_INDIRECT_DESC))

/* ------------------------------------------------------------------------ */
/* Virtqueue operations							    */
/* ------------------------------------------------------------------------ */

unsigned int
virtqueue_num_free(struct lkpi_virtqueue *vq)
{

	return ((unsigned int)virtqueue_nfree(VQ_BSD(vq)));
}

/*
 * Physical address of one scatterlist entry.
 *
 * linuxkpi's sg_dma_len() is #defined to sg->length, the same field sg_init_one()
 * fills for an unmapped buffer, so it cannot be used to tell a DMA-mapped entry
 * from a plain one. dma_address is the discriminator: zero until something maps
 * the entry. This mirrors what Linux' vring_map_one_sg() decides with the
 * use_dma_api flag it has and we do not.
 */
static inline vm_paddr_t
lkpi_sg_paddr(struct scatterlist *sg)
{

	if (sg_dma_address(sg) != 0)
		return ((vm_paddr_t)sg_dma_address(sg));
	return ((vm_paddr_t)sg_phys(sg));
}

/*
 * Linux hands virtqueue_add_sgs() an array of scatterlist CHAINS, the first
 * out_sgs of which are device-readable and the rest device-writable. FreeBSD's
 * virtqueue_enqueue() takes ONE sglist plus the counts, readable segments
 * first. So the chains are flattened in order.
 *
 * The segments are written into sg_segs[] by hand rather than through
 * sglist_append_phys(). That is a safety requirement, not a shortcut:
 * sglist_append_phys() coalesces a segment onto its predecessor whenever the
 * two are physically adjacent, and it has no notion of where the readable run
 * ends. A command buffer that happened to be allocated immediately before the
 * response buffer would be merged into one segment spanning the boundary, and
 * since the count of readable segments is computed from the caller's out_sgs,
 * the merged segment would land on the writable side -- handing the host write
 * access to the guest's command buffer. It would work, silently, until a host
 * wrote there.
 */
int
lkpi_virtqueue_add_sgs(struct lkpi_virtqueue *vq, struct scatterlist *sgs[],
    unsigned int out_sgs, unsigned int in_sgs, void *data, gfp_t gfp)
{
	struct sglist *sg;
	struct scatterlist *s;
	unsigned int i;
	int error, nseg, readable;

	(void)gfp;

	sg = vq->vq_sglist;
	nseg = 0;
	readable = 0;

	for (i = 0; i < out_sgs + in_sgs; i++) {
		for (s = sgs[i]; s != NULL; s = sg_next(s)) {
			if (nseg >= vq->vq_sgmax) {
				/*
				 * The caller pre-checked ring space with
				 * virtqueue_num_free(), so overflowing here
				 * means the request itself is larger than the
				 * ring can describe -- ENOSPC, matching what
				 * Linux returns for a full queue.
				 */
				return (-ENOSPC);
			}
			sg->sg_segs[nseg].ss_paddr = lkpi_sg_paddr(s);
			sg->sg_segs[nseg].ss_len = s->length;
			nseg++;
			if (i < out_sgs)
				readable++;
		}
	}

	sg->sg_nseg = nseg;

	error = virtqueue_enqueue(VQ_BSD(vq), data, sg, readable,
	    nseg - readable);
	if (error != 0)
		return (-error);

	return (0);
}

/*
 * FreeBSD has no kick_prepare: virtqueue_notify() consults the used-event
 * index itself and skips the notification when the host has suppressed it. So
 * the answer here is always "yes, go on to notify", and the real decision
 * happens there. Splitting it any other way would duplicate the EVENT_IDX
 * logic in the shim, against ring state the shim does not own.
 */
bool
lkpi_virtqueue_kick_prepare(struct lkpi_virtqueue *vq)
{

	(void)vq;
	return (true);
}

void
lkpi_virtqueue_notify(struct lkpi_virtqueue *vq)
{

	bsd_virtqueue_notify(VQ_BSD(vq));
}

void *
lkpi_virtqueue_get_buf(struct lkpi_virtqueue *vq, unsigned int *len)
{
	uint32_t bsdlen;
	void *cookie;

	cookie = virtqueue_dequeue(VQ_BSD(vq), &bsdlen);
	if (cookie != NULL && len != NULL)
		*len = bsdlen;

	return (cookie);
}

/*
 * Sense inversion, and it is load-bearing. Linux' virtqueue_enable_cb() returns
 * TRUE when the queue is empty -- "callbacks on, nothing pending, you may
 * stop polling". FreeBSD's virtqueue_enable_intr() returns NONZERO when more
 * descriptors arrived while interrupts were off -- the opposite claim.
 * virtgpu_vq.c drains with `do { ... } while (!virtqueue_enable_cb(vq));`, so
 * getting this backwards produces a drain loop that either spins forever or
 * exits with work still in the ring.
 */
bool
lkpi_virtqueue_enable_cb(struct lkpi_virtqueue *vq)
{

	return (virtqueue_enable_intr(VQ_BSD(vq)) == 0);
}

void
lkpi_virtqueue_disable_cb(struct lkpi_virtqueue *vq)
{

	virtqueue_disable_intr(VQ_BSD(vq));
}

/* ------------------------------------------------------------------------ */
/* Device configuration space						    */
/* ------------------------------------------------------------------------ */

bool
lkpi_virtio_has_feature(struct virtio_device *vdev, unsigned int fbit)
{

	return (virtio_with_feature(vdev->bsddev, 1ULL << fbit));
}

/*
 * The device-config region is little-endian in the modern layout, and both
 * architectures this ships on are little-endian, so the reads are byte-for-byte
 * copies. The le*toh() calls are kept anyway: they cost nothing here and they
 * are the difference between this being correct and being accidentally correct
 * if the shim is ever reused on a big-endian target.
 */
void
lkpi_virtio_cread_bytes(struct virtio_device *vdev, unsigned int offset,
    void *buf, size_t len)
{
	uint16_t v16;
	uint32_t v32;
	uint64_t v64;

	switch (len) {
	case 1:
		virtio_read_device_config(vdev->bsddev, offset, buf, 1);
		break;
	case 2:
		virtio_read_device_config(vdev->bsddev, offset, &v16, 2);
		*(uint16_t *)buf = le16toh(v16);
		break;
	case 4:
		virtio_read_device_config(vdev->bsddev, offset, &v32, 4);
		*(uint32_t *)buf = le32toh(v32);
		break;
	case 8:
		virtio_read_device_config(vdev->bsddev, offset, &v64, 8);
		*(uint64_t *)buf = le64toh(v64);
		break;
	default:
		virtio_read_device_config(vdev->bsddev, offset, buf, len);
		break;
	}
}

void
lkpi_virtio_cwrite_bytes(struct virtio_device *vdev, unsigned int offset,
    const void *buf, size_t len)
{
	uint16_t v16;
	uint32_t v32;
	uint64_t v64;

	switch (len) {
	case 1:
		virtio_write_device_config(vdev->bsddev, offset, buf, 1);
		break;
	case 2:
		v16 = htole16(*(const uint16_t *)buf);
		virtio_write_device_config(vdev->bsddev, offset, &v16, 2);
		break;
	case 4:
		v32 = htole32(*(const uint32_t *)buf);
		virtio_write_device_config(vdev->bsddev, offset, &v32, 4);
		break;
	case 8:
		v64 = htole64(*(const uint64_t *)buf);
		virtio_write_device_config(vdev->bsddev, offset, &v64, 8);
		break;
	default:
		virtio_write_device_config(vdev->bsddev, offset, buf, len);
		break;
	}
}

/* ------------------------------------------------------------------------ */
/* Virtqueue discovery							    */
/* ------------------------------------------------------------------------ */

/*
 * FreeBSD delivers a queue interrupt as intr(void *arg); Linux delivers it as
 * callback(struct virtqueue *). The wrapper IS the argument, so the trampoline
 * is a cast and a call.
 */
static void
lkpi_virtqueue_intr(void *arg)
{
	struct lkpi_virtqueue *vq = arg;

	if (vq->callback != NULL)
		vq->callback(vq);
}

static void lkpi_virtio_del_vqs(struct virtio_device *vdev);

static const struct virtio_config_ops lkpi_virtio_config_ops = {
	.del_vqs = lkpi_virtio_del_vqs,
};

int
lkpi_virtio_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
    struct lkpi_virtqueue *vqs[], struct virtqueue_info vqs_info[], void *desc)
{
	struct vq_alloc_info *info;
	bsd_virtqueue_t **bsdvqs;
	struct lkpi_virtqueue *vq;
	unsigned int i, j;
	int error, indirect, nseg;

	(void)desc;

	info = malloc(sizeof(*info) * nvqs, M_LKPI_VIRTIO, M_WAITOK | M_ZERO);
	bsdvqs = malloc(sizeof(*bsdvqs) * nvqs, M_LKPI_VIRTIO,
	    M_WAITOK | M_ZERO);

	/*
	 * Indirect descriptors are what let one command occupy a single ring
	 * slot regardless of how many segments back it. FreeBSD caps an
	 * indirect list at one page -- VIRTIO_MAX_INDIRECT, 256 descriptors --
	 * because each list is malloc(9)'d and must be physically contiguous.
	 * A virgl command buffer larger than 256 segments (1 MB of 4 KB pages)
	 * therefore cannot be described indirectly and falls back to consuming
	 * ring slots directly. That ceiling is FreeBSD's, not the spec's.
	 */
	indirect = virtio_with_feature(vdev->bsddev,
	    1ULL << VIRTIO_RING_F_INDIRECT_DESC) ? VIRTIO_MAX_INDIRECT : 0;

	for (i = 0; i < nvqs; i++) {
		vq = malloc(sizeof(*vq), M_LKPI_VIRTIO, M_WAITOK | M_ZERO);
		vq->vdev = vdev;
		vq->index = i;
		vq->name = vqs_info[i].name;
		vq->callback = vqs_info[i].callback;
		vqs[i] = vq;

		VQ_ALLOC_INFO_INIT(&info[i], indirect, lkpi_virtqueue_intr, vq,
		    &bsdvqs[i], "%s", vqs_info[i].name != NULL ?
		    vqs_info[i].name : "vq");
	}

#if __FreeBSD_version >= 1500000
	error = virtio_alloc_virtqueues(vdev->bsddev, nvqs, info);
#else
	/* The flags argument was removed in 15.0; it was always 0 here. */
	error = virtio_alloc_virtqueues(vdev->bsddev, 0, nvqs, info);
#endif
	if (error != 0)
		goto fail;

	error = virtio_setup_intr(vdev->bsddev, INTR_TYPE_MISC);
	if (error != 0)
		goto fail;

	for (i = 0; i < nvqs; i++) {
		vqs[i]->vq_bsd = bsdvqs[i];

		/*
		 * One sglist per queue, sized to the ring and allocated here so
		 * that virtqueue_add_sgs() -- which runs under a spinlock in
		 * GFP_ATOMIC context -- never has to.
		 */
		nseg = virtqueue_size(bsdvqs[i]);
		if (indirect != 0 && indirect > nseg)
			nseg = indirect;
		vqs[i]->vq_sglist = sglist_alloc(nseg, M_WAITOK);
		vqs[i]->vq_sgmax = nseg;
	}

	/*
	 * Kept so that virtio_device_ready() can arm them and del_vqs() can
	 * release the wrappers; the caller's vqs[] array is the driver's.
	 */
	vdev->vqs = malloc(sizeof(*vdev->vqs) * nvqs, M_LKPI_VIRTIO,
	    M_WAITOK | M_ZERO);
	for (i = 0; i < nvqs; i++)
		vdev->vqs[i] = vqs[i];
	vdev->nvqs = nvqs;

	free(bsdvqs, M_LKPI_VIRTIO);
	free(info, M_LKPI_VIRTIO);
	return (0);

fail:
	/*
	 * Bounded by nvqs rather than by i: both goto's are past the allocation
	 * loop, so all nvqs wrappers exist, and spelling it this way does not
	 * silently leak if a failure path is ever added earlier.
	 */
	for (j = 0; j < nvqs; j++) {
		if (vqs[j] != NULL) {
			if (vqs[j]->vq_sglist != NULL)
				sglist_free(vqs[j]->vq_sglist);
			free(vqs[j], M_LKPI_VIRTIO);
			vqs[j] = NULL;
		}
	}
	free(bsdvqs, M_LKPI_VIRTIO);
	free(info, M_LKPI_VIRTIO);

	return (-ENODEV);
}

/*
 * Linux' del_vqs frees the queues; on FreeBSD the bus owns them and releases
 * them when the child detaches, so this drops only what the shim allocated.
 * Freeing the FreeBSD virtqueues here would double-free at detach.
 */
static void
lkpi_virtio_del_vqs(struct virtio_device *vdev)
{
	unsigned int i;

	virtio_stop(vdev->bsddev);

	for (i = 0; i < vdev->nvqs; i++)
		lkpi_virtio_free_vq(vdev->vqs[i]);
	free(vdev->vqs, M_LKPI_VIRTIO);
	vdev->vqs = NULL;
	vdev->nvqs = 0;
}

void
lkpi_virtio_free_vq(struct lkpi_virtqueue *vq)
{

	if (vq == NULL)
		return;
	if (vq->vq_sglist != NULL)
		sglist_free(vq->vq_sglist);
	free(vq, M_LKPI_VIRTIO);
}

/*
 * Linux drivers call virtio_device_ready() when they are prepared to take
 * interrupts; the DRIVER_OK status bit itself is set by FreeBSD's virtio bus
 * once the child's attach returns, so what is left to do here is arm the
 * queues -- FreeBSD leaves a freshly allocated virtqueue with its interrupt
 * disabled, and native drivers enable it at "up" time. virtio-gpu calls this
 * immediately before issuing its first command, which is exactly that moment;
 * without it the first response never wakes anybody and probe hangs in the
 * five-second display-info wait.
 */
void
lkpi_virtio_device_ready(struct virtio_device *vdev)
{
	unsigned int i;

	for (i = 0; i < vdev->nvqs; i++)
		virtqueue_enable_intr(VQ_BSD(vdev->vqs[i]));
}

void
lkpi_virtio_reset_device(struct virtio_device *vdev)
{

	virtio_stop(vdev->bsddev);
}

/* ------------------------------------------------------------------------ */
/* Device lifecycle, for the driver's newbus glue			    */
/* ------------------------------------------------------------------------ */

struct virtio_device *
lkpi_virtio_device_alloc(device_t bsddev, const unsigned int *feature_table,
    unsigned int feature_table_size)
{
	struct virtio_device *vdev;
	uint64_t features;
	unsigned int i;

	vdev = malloc(sizeof(*vdev), M_LKPI_VIRTIO, M_WAITOK | M_ZERO);
	vdev->bsddev = bsddev;
	vdev->config = &lkpi_virtio_config_ops;

	/*
	 * Linux' feature_table is a list of bit NUMBERS; FreeBSD negotiates
	 * with a MASK. Confusing the two does not fail loudly -- it silently
	 * requests the wrong features, and a virtio-gpu that believes it
	 * negotiated VIRGL against a host that never offered it will fail much
	 * later and much less legibly.
	 */
	features = LKPI_VIRTIO_TRANSPORT_FEATURES;
	for (i = 0; i < feature_table_size; i++)
		features |= 1ULL << feature_table[i];

	vdev->features = virtio_negotiate_features(bsddev, features);

	return (vdev);
}

void
lkpi_virtio_device_free(struct virtio_device *vdev)
{

	free(vdev, M_LKPI_VIRTIO);
}

/* ------------------------------------------------------------------------ */
/* virtio dma-buf							    */
/* ------------------------------------------------------------------------ */

int
virtio_dma_buf_attach(struct dma_buf *dma_buf, struct dma_buf_attachment *attach)
{
	const struct virtio_dma_buf_ops *ops;
	int ret;

	ops = container_of(dma_buf->ops, struct virtio_dma_buf_ops, ops);
	if (ops->device_attach != NULL) {
		ret = ops->device_attach(dma_buf, attach);
		if (ret != 0)
			return (ret);
	}

	return (0);
}

struct dma_buf *
virtio_dma_buf_export(const struct dma_buf_export_info *exp_info)
{
	const struct virtio_dma_buf_ops *ops;

	ops = container_of(exp_info->ops, struct virtio_dma_buf_ops, ops);
	if (exp_info->ops == NULL ||
	    exp_info->ops->attach != &virtio_dma_buf_attach ||
	    ops->get_uuid == NULL)
		return (ERR_PTR(-EINVAL));

	return (dma_buf_export(exp_info));
}

bool
is_virtio_dma_buf(struct dma_buf *dma_buf)
{

	return (dma_buf->ops->attach == &virtio_dma_buf_attach);
}

int
virtio_dma_buf_get_uuid(struct dma_buf *dma_buf, uuid_t *uuid)
{
	const struct virtio_dma_buf_ops *ops;

	if (!is_virtio_dma_buf(dma_buf))
		return (-EINVAL);

	ops = container_of(dma_buf->ops, struct virtio_dma_buf_ops, ops);

	return (ops->get_uuid(dma_buf, uuid));
}

/* ------------------------------------------------------------------------ */
/* Driver registration							    */
/* ------------------------------------------------------------------------ */

/*
 * Linux' register_virtio_driver() hooks a driver onto the virtio bus. FreeBSD
 * does that with DRIVER_MODULE() at compile time, which the driver's own glue
 * declares, so nothing is left to do at run time. These exist so the vendored
 * driver's module init/exit compile unchanged.
 */
int
lkpi_register_virtio_driver(struct virtio_driver *drv)
{

	(void)drv;
	return (0);
}

void
lkpi_unregister_virtio_driver(struct virtio_driver *drv)
{

	(void)drv;
}

/* ------------------------------------------------------------------------ */
/* Module								    */
/* ------------------------------------------------------------------------ */

static int
lkpi_virtio_modevent(module_t mod __unused, int what, void *arg __unused)
{

	switch (what) {
	case MOD_LOAD:
	case MOD_UNLOAD:
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t lkpi_virtio_moduledata = {
	"linux_virtio",
	lkpi_virtio_modevent,
	NULL
};

DECLARE_MODULE(linux_virtio, lkpi_virtio_moduledata, SI_SUB_DRIVERS,
    SI_ORDER_ANY);
MODULE_VERSION(linux_virtio, 1);
MODULE_DEPEND(linux_virtio, linuxkpi, 1, 1, 1);
MODULE_DEPEND(linux_virtio, virtio, 1, 1, 1);
/*
 * dmabuf is a direct dependency because virtio_dma_buf_export() calls
 * dma_buf_export(). MODULE_DEPEND is depth-1 -- kern_linker.c looks up
 * symbols in file->deps[] without recursing -- so every module used directly
 * must be named here even when something else already depends on it.
 */
MODULE_DEPEND(linux_virtio, dmabuf, 1, 1, 1);
