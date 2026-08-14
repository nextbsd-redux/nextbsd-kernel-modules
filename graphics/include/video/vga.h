/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * video/vga.h -- minimal shim.
 *
 * drm-kmod (6.12-lts) has no include/video/ at all, and FreeBSD's linuxkpi does
 * not provide this header either -- it is the ONLY header the vendored bochs /
 * DRM-helper sources need that neither supplies (verified by diffing every
 * #include in drm_simple_kms_helper.c, drm_gem_vram_helper.c and bochs.c against
 * drm-kmod's include/ and sys/compat/linuxkpi/common/include/).
 *
 * Linux's real include/video/vga.h is ~490 lines, nearly all of it register
 * definitions for hardware nobody here touches. bochs.c references exactly four
 * symbols, on the legacy VGA I/O-port path it uses to blank the display:
 *
 *     bochs_vga_writeb(bochs, VGA_MIS_W, VGA_MIS_COLOR);
 *     (void)bochs_vga_readb(bochs, VGA_IS1_RC);
 *     bochs_vga_writeb(bochs, VGA_ATT_W, blank ? 0 : 0x20);
 *
 * so this carries those four, values copied verbatim from Linux v6.12's
 * include/video/vga.h (the same tree the drivers are vendored from). Vendoring
 * the whole header would drag in hundreds of unused GPL definitions; if a later
 * driver needs more, replace this shim with the real header rather than growing
 * it piecemeal.
 */
#ifndef _NEXTBSD_VIDEO_VGA_H_
#define _NEXTBSD_VIDEO_VGA_H_

/* VGA data register ports (colour emulation addressing). */
#define VGA_ATT_W	0x3C0	/* Attribute Controller Data Write Register */
#define VGA_MIS_W	0x3C2	/* Misc Output Write Register */
#define VGA_IS1_RC	0x3DA	/* Input Status Register 1 - color emulation */

/* Miscellaneous output register bits. */
#define VGA_MIS_COLOR	0x01

#endif /* _NEXTBSD_VIDEO_VGA_H_ */
