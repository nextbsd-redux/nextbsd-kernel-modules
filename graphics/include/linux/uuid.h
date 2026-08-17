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
 * LinuxKPI addition: <linux/uuid.h>.
 *
 * linuxkpi has no uuid.h, and virtio-gpu needs uuid_t in two places -- the
 * resource UUID a buffer carries so a second device can identify it across a
 * dma-buf export, and the get_uuid op in struct virtio_dma_buf_ops. Measured:
 * without this, virtgpu_drv.h:99 and virtio_dma_buf.h both fail with "unknown
 * type name 'uuid_t'".
 *
 * FreeBSD's <sys/uuid.h> also defines a uuid_t -- `struct uuid`, a different
 * layout for the same 16 bytes. Which one a translation unit gets depends on
 * include order, so this defers to FreeBSD's whenever that header has already
 * been seen. Both are 16 bytes and virtio-gpu only ever memcpy()s the value
 * between the host response and userspace, never interpreting the fields, so
 * either spelling is correct here -- but defining a second uuid_t on top of
 * FreeBSD's would not compile, and silently disagreeing about the layout in
 * different TUs would be worse.
 */

#ifndef _LINUXKPI_LINUX_UUID_H_
#define	_LINUXKPI_LINUX_UUID_H_

#include <linux/types.h>

#define	UUID_SIZE	16

#ifndef _SYS_UUID_H_
typedef struct {
	__u8	b[UUID_SIZE];
} uuid_t;

#define	GUID_SIZE	16
typedef struct {
	__u8	b[GUID_SIZE];
} guid_t;
#endif	/* !_SYS_UUID_H_ */

#endif	/* _LINUXKPI_LINUX_UUID_H_ */
