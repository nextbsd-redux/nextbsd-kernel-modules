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
 * <sys/uuid.h> declares `struct uuid` and typedefs uuid_t onto it -- but the
 * typedef is inside its `#else /* _KERNEL */` arm, so KERNEL code gets the
 * struct and no typedef at all. An earlier version of this header guarded on
 * _SYS_UUID_H_ to avoid a clash, which suppressed the definition without
 * anything supplying one; measured on both arches as "unknown type name
 * 'uuid_t'" in three files. The definition is therefore unconditional: in
 * kernel code there is nothing to collide with.
 *
 * Layout follows Linux' -- 16 opaque bytes -- which is also what the virtio
 * spec puts on the wire. virtio-gpu only ever memcpy()s the value between the
 * host response and userspace and never interprets the fields, so byte
 * identity is the whole requirement.
 */

#ifndef _LINUXKPI_LINUX_UUID_H_
#define	_LINUXKPI_LINUX_UUID_H_

#include <linux/types.h>

#define	UUID_SIZE	16
#define	GUID_SIZE	16

typedef struct {
	__u8	b[UUID_SIZE];
} uuid_t;

typedef struct {
	__u8	b[GUID_SIZE];
} guid_t;

#endif	/* _LINUXKPI_LINUX_UUID_H_ */
