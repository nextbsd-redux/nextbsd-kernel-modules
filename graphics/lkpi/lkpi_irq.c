/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * More than one interrupt per device-tree device
 * (nextbsd-kernel-extensions#51).
 *
 * LinuxKPI's request_irq() finds its device with lkpi_pci_find_irq_dev(),
 * which searches the PCI list. Kernel patch 0044 added a fallback so a
 * device-tree device can register at all, but that fallback allocates rid 0
 * unconditionally -- one interrupt per device, no more.
 *
 * bcm2712 needs more than one. The HVS requests three named interrupts
 * (ch0-eof, ch1-eof, ch2-eof) and on gen6 those ARE vblank:
 * vc6_hvs_eof_irq_handler, gated by eof_irq[channel].enabled through
 * vc4_hvs_irq_enable_eof(). The HDMI controller requests four more
 * (hpd-connected, hpd-removed, cec-rx, cec-tx).
 *
 * And the failure is silent. vc4_hvs.c assigns devm_request_irq()'s return to
 * ret and never checks it before storing the descriptor, so with a single rid
 * the second and third requests fail quietly and all three handlers end up
 * aliased to one line: mis-wired vblank, not absent vblank.
 *
 * So platform_get_irq()/platform_get_irq_byname() return the resource rid
 * tagged with LKPI_IRQ_OF, and this allocates exactly that rid. Anything
 * untagged is handed to the kernel's lkpi_request_irq() unchanged -- the
 * firmware-KMS path is byte-for-byte what it was, which matters because that
 * is the driver that currently works.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>	/* LKPI_IRQ_OF* */
#include <linux/errno.h>

struct lkpi_of_irq {
	TAILQ_ENTRY(lkpi_of_irq)	 link;
	struct device			*dev;
	unsigned int			 irq;		/* tagged value */
	void				*arg;
	struct resource			*res;
	void				*cookie;
	irq_handler_t			 handler;
	irq_handler_t			 thread_handler;
	void				*handler_arg;
};

static TAILQ_HEAD(, lkpi_of_irq) lkpi_of_irqs =
    TAILQ_HEAD_INITIALIZER(lkpi_of_irqs);
static struct mtx lkpi_of_irq_mtx;
MTX_SYSINIT(lkpi_of_irq_mtx, &lkpi_of_irq_mtx, "lkpi-of-irqs", MTX_DEF);

/*
 * Linux handlers return irqreturn_t and take (irq, arg); newbus wants a void
 * filter taking a single cookie. This adapts one to the other.
 *
 * Registered as a FILTER because the Linux contract is that the primary
 * handler runs in interrupt context and returns quickly. The two models line
 * up almost exactly: Linux's primary/threaded split is newbus's filter/ithread
 * split, and IRQ_WAKE_THREAD is FILTER_SCHEDULE_THREAD.
 */
static int
lkpi_of_irq_filter(void *p)
{
	struct lkpi_of_irq *e = p;
	irqreturn_t ret;

	/*
	 * No primary handler. Linux substitutes irq_default_primary_handler(),
	 * which does nothing but wake the thread, and that is not a corner
	 * case here -- every threaded request vc4_hdmi.c makes passes NULL:
	 *
	 *	devm_request_threaded_irq(&pdev->dev, hpd, NULL,
	 *	    vc4_hdmi_hpd_irq_thread, IRQF_ONESHOT, ...)
	 *
	 * so all of the work is in the thread half.
	 */
	if (e->handler == NULL)
		return (e->thread_handler != NULL ?
		    FILTER_SCHEDULE_THREAD : FILTER_STRAY);

	ret = e->handler((int)e->irq, e->handler_arg);
	if (ret == IRQ_WAKE_THREAD) {
		/*
		 * Asking for a thread that was never registered would panic in
		 * the ithread layer, so a mismatched pair is treated as merely
		 * handled.
		 */
		return (e->thread_handler != NULL ?
		    FILTER_SCHEDULE_THREAD : FILTER_HANDLED);
	}
	if (ret == IRQ_HANDLED)
		return (FILTER_HANDLED);
	return (FILTER_STRAY);
}

/*
 * The thread half. newbus runs this on the interrupt's own ithread once the
 * filter returns FILTER_SCHEDULE_THREAD.
 *
 * IRQF_ONESHOT, which is what vc4_hdmi passes, asks Linux to keep the line
 * masked until the thread completes. newbus already does not re-run a source
 * while its ithread is pending, so the flag needs no extra handling -- but
 * that is a property of the ithread layer being relied on, not a coincidence.
 */
static void
lkpi_of_irq_thread(void *p)
{
	struct lkpi_of_irq *e = p;

	if (e->thread_handler != NULL)
		e->thread_handler((int)e->irq, e->handler_arg);
}

int
lkpi_of_request_irq(struct device *xdev, unsigned int irq,
    irq_handler_t handler, irq_handler_t thread_handler,
    unsigned long flags, const char *name, void *arg)
{
	struct lkpi_of_irq *e;
	struct resource *res;
	int rid, error;
	unsigned int resflags;

	/*
	 * Untagged: dev->irq, rid 0. Straight to the kernel, unchanged -- this
	 * is the path firmware KMS takes and it must not move.
	 */
	if (!LKPI_IRQ_IS_OF(irq))
		return (lkpi_request_irq(xdev, irq, handler, thread_handler,
		    flags, name, arg));

	if (xdev == NULL || xdev->bsddev == NULL)
		return (-ENXIO);

	rid = LKPI_IRQ_OF_RID(irq);
	resflags = RF_ACTIVE;
	if ((flags & IRQF_SHARED) != 0)
		resflags |= RF_SHAREABLE;

	res = bus_alloc_resource_any(xdev->bsddev, SYS_RES_IRQ, &rid, resflags);
	if (res == NULL)
		return (-ENXIO);

	e = malloc(sizeof(*e), M_DEVBUF, M_WAITOK | M_ZERO);
	e->dev = xdev;
	e->irq = irq;
	e->arg = arg;
	e->res = res;
	e->handler = handler;
	e->thread_handler = thread_handler;
	e->handler_arg = arg;

	error = bus_setup_intr(xdev->bsddev, res,
	    INTR_TYPE_MISC | INTR_MPSAFE, lkpi_of_irq_filter,
	    thread_handler != NULL ? lkpi_of_irq_thread : NULL, e,
	    &e->cookie);
	if (error != 0) {
		bus_release_resource(xdev->bsddev, SYS_RES_IRQ, rid, res);
		free(e, M_DEVBUF);
		return (-error);
	}

	mtx_lock(&lkpi_of_irq_mtx);
	TAILQ_INSERT_TAIL(&lkpi_of_irqs, e, link);
	mtx_unlock(&lkpi_of_irq_mtx);
	return (0);
}

void
lkpi_of_free_irq(struct device *xdev, unsigned int irq, void *arg)
{
	struct lkpi_of_irq *e, *tmp;

	if (!LKPI_IRQ_IS_OF(irq)) {
		lkpi_free_irq(irq, arg);
		return;
	}

	mtx_lock(&lkpi_of_irq_mtx);
	TAILQ_FOREACH_SAFE(e, &lkpi_of_irqs, link, tmp) {
		if (e->dev != xdev || e->irq != irq || e->arg != arg)
			continue;
		TAILQ_REMOVE(&lkpi_of_irqs, e, link);
		mtx_unlock(&lkpi_of_irq_mtx);
		if (e->cookie != NULL)
			bus_teardown_intr(xdev->bsddev, e->res, e->cookie);
		if (e->res != NULL)
			bus_release_resource(xdev->bsddev, SYS_RES_IRQ,
			    LKPI_IRQ_OF_RID(irq), e->res);
		free(e, M_DEVBUF);
		return;
	}
	mtx_unlock(&lkpi_of_irq_mtx);
}
