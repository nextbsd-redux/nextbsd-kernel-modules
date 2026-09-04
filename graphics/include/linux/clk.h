/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * linux/clk.h -- deliberately empty (nextbsd-kernel#176).
 *
 * vc4_firmware_kms.c includes this header and never uses it. Measured on the
 * 2079-line source: zero calls matching clk_*, and the only occurrence of the
 * string "clk" outside this include is a DRM_DEBUG_KMS format specifier.
 *
 * The firmware KMS driver asks the VideoCore firmware to set a mode over the
 * mailbox; the firmware owns the pixel clock and never exposes it, which is
 * the whole reason this driver is tractable where full vc4 is not. So there is
 * nothing to shim -- the include just has to resolve.
 *
 * If a future consumer needs real clk support, this file is the place, and its
 * emptiness is a measurement rather than an oversight.
 */
#ifndef _LINUXKPI_LINUX_CLK_H_
#define	_LINUXKPI_LINUX_CLK_H_

#endif /* _LINUXKPI_LINUX_CLK_H_ */
