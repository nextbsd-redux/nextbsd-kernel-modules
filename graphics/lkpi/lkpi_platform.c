/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * The platform-device registry (nextbsd-kernel-extensions#51).
 *
 * LinuxKPI has no platform bus. vc4's master builds its component match list
 * by asking, for each entry in component_drivers[], which devices are bound to
 * that driver -- platform_find_device_by_driver(). With no bus there was
 * nothing to answer with, and the function was deliberately left undefined so
 * the build would fail rather than quietly produce an empty match list.
 *
 * That caution was right. An empty match list is COMPLETE by definition, so
 * the master binds immediately with zero components, reports success, and
 * leaves the display dark -- a failure that points nowhere near its cause.
 *
 * The newbus shims already build one struct platform_device per attached
 * block, so the registry is just those: recorded at attach, removed at detach.
 * A vc4 pipeline is five devices, so a list is the right shape.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/queue.h>

#include <linux/device.h>
#include <linux/platform_device.h>

struct lkpi_pdev_ent {
	TAILQ_ENTRY(lkpi_pdev_ent)	 link;
	struct platform_device		*pdev;
	const struct lkpi_driver	*drv;
};

static TAILQ_HEAD(, lkpi_pdev_ent) lkpi_pdevs =
    TAILQ_HEAD_INITIALIZER(lkpi_pdevs);
static struct mtx lkpi_pdev_mtx;
MTX_SYSINIT(lkpi_pdev_mtx, &lkpi_pdev_mtx, "lkpi-pdevs", MTX_DEF);

void
lkpi_platform_device_register(struct platform_device *pdev,
    const struct lkpi_driver *drv)
{
	struct lkpi_pdev_ent *e;

	if (pdev == NULL)
		return;
	e = malloc(sizeof(*e), M_DEVBUF, M_WAITOK | M_ZERO);
	e->pdev = pdev;
	e->drv = drv;
	mtx_lock(&lkpi_pdev_mtx);
	TAILQ_INSERT_TAIL(&lkpi_pdevs, e, link);
	mtx_unlock(&lkpi_pdev_mtx);
}

void
lkpi_platform_device_unregister(struct platform_device *pdev)
{
	struct lkpi_pdev_ent *e, *tmp;

	mtx_lock(&lkpi_pdev_mtx);
	TAILQ_FOREACH_SAFE(e, &lkpi_pdevs, link, tmp) {
		if (e->pdev != pdev)
			continue;
		TAILQ_REMOVE(&lkpi_pdevs, e, link);
		mtx_unlock(&lkpi_pdev_mtx);
		free(e, M_DEVBUF);
		return;
	}
	mtx_unlock(&lkpi_pdev_mtx);
}

/*
 * Iterator, matching the shape vc4_match_add_drivers() expects: pass NULL to
 * begin, then pass the previous result to continue. Returns NULL when the
 * driver has no more devices.
 *
 * Upstream also takes a reference on the returned device and expects the
 * caller to put_device() it. Nothing here is reference counted -- these
 * devices live exactly as long as their newbus attachment, which outlives the
 * master's bind -- so put_device() is the no-op it already was, and this hands
 * the pointer back directly.
 */
struct device *
platform_find_device_by_driver(struct device *start,
    const struct lkpi_driver *drv)
{
	struct lkpi_pdev_ent *e;
	struct device *found = NULL;
	bool seen;

	if (drv == NULL)
		return (NULL);

	seen = (start == NULL);
	mtx_lock(&lkpi_pdev_mtx);
	TAILQ_FOREACH(e, &lkpi_pdevs, link) {
		if (!seen) {
			if (&e->pdev->dev == start)
				seen = true;
			continue;
		}
		if (e->drv == drv) {
			found = &e->pdev->dev;
			break;
		}
	}
	mtx_unlock(&lkpi_pdev_mtx);
	return (found);
}
