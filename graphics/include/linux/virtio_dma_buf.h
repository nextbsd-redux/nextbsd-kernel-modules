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
 * LinuxKPI virtio shim: the virtio dma-buf wrapper.
 *
 * This is a validation shell around dma_buf_export(): the point of the Linux
 * original is that a virtio-exported dma-buf is identifiable by its ops
 * pointer, so an importer can ask for the resource UUID that names the buffer
 * on the host side. The behaviour is entirely in the ops table, so this is a
 * faithful reimplementation rather than a stub.
 */

#ifndef _LINUXKPI_LINUX_VIRTIO_DMA_BUF_H_
#define	_LINUXKPI_LINUX_VIRTIO_DMA_BUF_H_

#include <linux/dma-buf.h>
/* uuid_t comes from here -- see the note in <linux/virtio.h>. */
#include <linux/virtio.h>

struct virtio_dma_buf_ops {
	struct dma_buf_ops	 ops;
	int			(*device_attach)(struct dma_buf *,
				    struct dma_buf_attachment *);
	int			(*get_uuid)(struct dma_buf *, uuid_t *);
};

int	virtio_dma_buf_attach(struct dma_buf *, struct dma_buf_attachment *);
struct dma_buf *virtio_dma_buf_export(
	    const struct dma_buf_export_info *exp_info);
bool	is_virtio_dma_buf(struct dma_buf *dma_buf);
int	virtio_dma_buf_get_uuid(struct dma_buf *dma_buf, uuid_t *uuid);

#endif	/* _LINUXKPI_LINUX_VIRTIO_DMA_BUF_H_ */
