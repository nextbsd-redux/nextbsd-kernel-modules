/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * DRM helpers vc4 needs that drm-kmod does not provide
 * (nextbsd-kernel-extensions#51).
 *
 * FORCE-INCLUDED, not a shadowing header. The Makefile adds
 *
 *	CFLAGS+= -include ${.CURDIR:H}/lkpi/lkpi_drm.h
 *
 * so this is pulled in ahead of every source in the module, and it includes
 * the real <drm/drm_managed.h> and <linux/dma-fence.h> itself before adding
 * what is missing.
 *
 * That is deliberate. The obvious alternative -- shipping our own
 * <linux/dma-fence.h> in lkpi/linux/ -- would shadow drm-kmod's copy of the
 * same header for this module and silently drop whatever drm-kmod had added
 * to it. That mistake broke `drm-kmod build` on both arches earlier in #51,
 * when lkpi/linux/{dma-mapping,mm,iosys-map}.h shadowed drm-kmod's versions.
 * The rule that came out of it: only shadow a header whose consumers are all
 * inside this module. dma-fence.h and drm_managed.h are drm core; they are
 * not.
 *
 * The earlier attempt at this (closed PR #56) patched drm-kmod instead. Two of
 * its three pieces did not need to -- they add functions, not struct members --
 * and those two are here.
 */
#ifndef _LKPI_DRM_H_
#define	_LKPI_DRM_H_

#ifndef LKPI_PFX
#error "LKPI_PFX must be defined by the module Makefile"
#endif
#define	LKPI_DRM_SYM2(p, n)	p ## n
#define	LKPI_DRM_SYM1(p, n)	LKPI_DRM_SYM2(p, n)
#define	LKPI_DRM_SYM(n)		LKPI_DRM_SYM1(LKPI_PFX, n)

#define	drmm_mutex_init		LKPI_DRM_SYM(drmm_mutex_init)
#define	dma_fence_match_context	LKPI_DRM_SYM(dma_fence_match_context)

#include <linux/types.h>
#include <linux/dma-fence.h>
#include <drm/drm_managed.h>


/*
 * Pixel formats the Raspberry Pi tree adds and drm-kmod does not carry.
 *
 * All are 4:2:0 YCbCr the HVS on gen6 can scan out directly, used only by
 * hvs6_only entries in vc4_plane.c's hvs_formats[]. Missing, they take the
 * whole table's initialiser with them -- which is where the "incomplete type
 * 'const struct hvs_format[]'" errors came from, rather than anything wrong
 * with the table itself.
 *
 * fourcc_code() is the standard encoding; these follow it exactly, so the
 * values match upstream and a buffer negotiated with a Linux client agrees.
 * Guarded in case drm-kmod picks them up later.
 */
#include <drm/drm_fourcc.h>

#ifndef DRM_FORMAT_P030
/* 2-plane 10-bit 4:2:0, 3 pixels packed per 32 bits */
#define	DRM_FORMAT_P030		fourcc_code('P', '0', '3', '0')
#endif
#ifndef DRM_FORMAT_S010
/* 3-plane 10-bit 4:2:0, samples in the low bits */
#define	DRM_FORMAT_S010		fourcc_code('S', '0', '1', '0')
#endif
#ifndef DRM_FORMAT_S012
/* 3-plane 12-bit 4:2:0 */
#define	DRM_FORMAT_S012		fourcc_code('S', '0', '1', '2')
#endif
#ifndef DRM_FORMAT_S016
/* 3-plane 16-bit 4:2:0 */
#define	DRM_FORMAT_S016		fourcc_code('S', '0', '1', '6')
#endif


/*
 * struct cec_msg (#51).
 *
 * LinuxKPI's <media/cec.h> is a passthrough that never defines it, and vc4_hdmi
 * embeds one BY VALUE (vc4_hdmi.h: struct cec_msg cec_rx_msg), so struct
 * vc4_hdmi is incomplete without it and every file touching a vc4_hdmi fails.
 *
 * All CEC *code* in vc4_hdmi.c is already behind CONFIG_DRM_VC4_HDMI_CEC,
 * which this module does not define -- so nothing here is ever read or
 * transmitted. Only the field's size matters, and it matters only to the
 * compiler. Laid out as upstream's uapi struct so it stays recognisable if CEC
 * is ever wired up.
 */
#ifndef CEC_MAX_MSG_SIZE
#define	CEC_MAX_MSG_SIZE	16
#endif

struct cec_msg {
	uint64_t	tx_ts;
	uint64_t	rx_ts;
	uint32_t	len;
	uint32_t	timeout;
	uint32_t	sequence;
	uint32_t	flags;
	uint8_t		msg[CEC_MAX_MSG_SIZE];
	uint8_t		reply;
	uint8_t		rx_status;
	uint8_t		tx_status;
	uint8_t		tx_arb_lost_cnt;
	uint8_t		tx_nack_cnt;
	uint8_t		tx_low_drive_cnt;
	uint8_t		tx_error_cnt;
};

struct mutex;
struct drm_device;

/*
 * A mutex destroyed when the drm_device is. The point of the drmm_ family is
 * that a driver need not unwind by hand: everything registered is released in
 * reverse order when the device goes, and vc4 uses it for locks that live
 * exactly as long as the device.
 */
int	drmm_mutex_init(struct drm_device *dev, struct mutex *lock);

/*
 * Is every fence here from `context`?
 *
 * vc4 uses it to skip waiting on a fence it produced itself -- a fence from
 * the caller's own context is already ordered by submission order.
 *
 * The array case is what matters for correctness: an array fence whose members
 * span contexts must NOT report a match. Reporting one makes the caller skip a
 * wait it genuinely needs, and that surfaces much later as rendering against a
 * buffer that was not ready.
 */
bool	dma_fence_match_context(struct dma_fence *fence, u64 context);

#endif /* _LKPI_DRM_H_ */
