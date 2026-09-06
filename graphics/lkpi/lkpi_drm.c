/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * DRM helpers vc4 needs that drm-kmod does not provide
 * (nextbsd-kernel-extensions#51). See lkpi_drm.h for why these live in the
 * module rather than in a drm-kmod patch.
 */

#include <sys/param.h>
#include <sys/systm.h>

#include <linux/mutex.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-array.h>
#include <linux/err.h>

#include <drm/drm_device.h>
#include <drm/drm_managed.h>

#include "lkpi_drm.h"

static void
drmm_mutex_release(struct drm_device *dev __unused, void *p)
{

	linux_mutex_destroy((struct mutex *)p);
}

/*
 * Ordering matters: the mutex is initialised BEFORE the release action is
 * registered. If registration fails, an already-initialised mutex is destroyed
 * rather than left live, and the caller sees the error.
 */
int
drmm_mutex_init(struct drm_device *dev, struct mutex *lock)
{
	int ret;

	if (dev == NULL || lock == NULL)
		return (-EINVAL);

	linux_mutex_init(lock, "drmm_mutex", SX_NOWITNESS);
	ret = drmm_add_action(dev, drmm_mutex_release, lock);
	if (ret != 0)
		linux_mutex_destroy(lock);
	return (ret);
}

bool
dma_fence_match_context(struct dma_fence *fence, u64 context)
{
	struct dma_fence_array *array;
	struct dma_fence *f;
	unsigned int i;

	if (fence == NULL)
		return (false);

	if (!dma_fence_is_array(fence))
		return (fence->context == context);

	/*
	 * Every member must match. One member from another context means the
	 * caller still has to wait, so a single mismatch fails the whole test.
	 */
	array = to_dma_fence_array(fence);
	for (i = 0; i < array->num_fences; i++) {
		f = array->fences[i];
		if (f->context != context)
			return (false);
	}
	return (true);
}
