/* Public domain. */

#ifndef _LINUXKPI_LINUX_PM_RUNTIME_H_
#define _LINUXKPI_LINUX_PM_RUNTIME_H_

#include <linux/device.h>
#include <linux/pm.h>

#define pm_runtime_mark_last_busy(x) (void)(x)
#define pm_runtime_use_autosuspend(x) (void)(x)
#define pm_runtime_dont_use_autosuspend(x) (void)(x)
#define pm_runtime_put_autosuspend(x) (void)(x)
#define pm_runtime_set_autosuspend_delay(x, y) (void)(x); (void)(y)
#define pm_runtime_set_active(x) (void)(x)
#define pm_runtime_allow(x) (void)(x)
#define pm_runtime_put_noidle(x) (void)(x)
#define pm_runtime_forbid(x) (void)(x)
#define pm_runtime_get_noresume(x) (void)(x)
#define pm_runtime_put(x) (void)(x)
/*
 * Same no-op as the rest of this header: LinuxKPI does not implement
 * runtime PM. Kept distinct from pm_runtime_put() rather than aliased to
 * it so a real implementation can tell the two apart later.
 */
#define pm_runtime_put_sync_suspend(x) (void)(x)
#define pm_runtime_enable(x) (void)(x)
#define pm_runtime_disable(x) (void)(x)
#define pm_runtime_autosuspend(x) (void)(x)
#define pm_runtime_resume(x) (void)(x)

static inline int
pm_runtime_get_sync(struct device *dev)
{
	return 0;
}

static inline int
pm_runtime_get_if_in_use(struct device *dev)
{
	return 1;
}

#if defined(LINUXKPI_VERSION) && LINUXKPI_VERSION < 60900
static inline int
pm_runtime_get_if_active(struct device *dev, bool x)
#else
static inline int
pm_runtime_get_if_active(struct device *dev)
#endif
{
	return 1;
}

static inline int
pm_runtime_suspended(struct device *dev)
{
	return 0;
}


/*
 * Added for vc4 (#51). These were kernel patch 0045, which moved out of the
 * kernel with the rest of the vc4 LinuxKPI work.
 *
 * LinuxKPI has no runtime PM: nothing here is ever suspended, so "resume and
 * get a reference" always succeeds and "is it suspended" is always false.
 * Answering that way is correct for this platform rather than merely
 * convenient -- a driver that checks these gets the truth.
 */
static inline int
pm_runtime_resume_and_get(struct device *dev __unused)
{

	return (0);
}

static inline bool
pm_runtime_status_suspended(struct device *dev __unused)
{

	return (false);
}

#ifndef pm_runtime_put_sync_suspend
#define	pm_runtime_put_sync_suspend(x)	(void)(x)
#endif


/*
 * The remainder of what vc4 calls. No runtime PM here, so every one succeeds
 * and reports "running". pm_runtime_put() must RETURN an int -- vc4_hdmi.c
 * assigns it and logs on a negative result -- which is why a void macro is not
 * enough.
 */
#undef pm_runtime_put
static inline int
pm_runtime_put(struct device *dev __unused)
{

	return (0);
}

#undef pm_runtime_put_sync
static inline int
pm_runtime_put_sync(struct device *dev __unused)
{

	return (0);
}

static inline int
devm_pm_runtime_enable(struct device *dev __unused)
{

	return (0);
}

/*
 * dev_pm_ops carries the callbacks; nothing here ever invokes them, because
 * nothing suspends. The macro exists so the initialiser compiles with the
 * function names still visible to a reader.
 */
#ifndef SET_RUNTIME_PM_OPS
#define	SET_RUNTIME_PM_OPS(suspend_fn, resume_fn, idle_fn)		\
	.runtime_suspend = (suspend_fn),				\
	.runtime_resume = (resume_fn),					\
	.runtime_idle = (idle_fn)
#endif

/* struct dev_pm_ops already exists in LinuxKPI; do not redefine it. */

#endif	/* _LINUXKPI_LINUX_PM_RUNTIME_H_ */
