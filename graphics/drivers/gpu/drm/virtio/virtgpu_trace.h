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
 * FreeBSD replacement for the driver's tracepoint header.
 *
 * This file is NOT vendored: Linux' virtgpu_trace.h is a TRACE_EVENT() macro
 * tree that expands against ftrace, which FreeBSD has no equivalent of.
 * drm-kmod's convention for this is KTR, and its own
 * linuxkpi/bsd/include/trace/events/dma_fence.h is the model followed here --
 * same CTR macros, same KTR_DRM class, so virtio-gpu tracing turns on with the
 * same KTR_COMPILE knob as the rest of the DRM stack.
 *
 * trace_dma_fence_emit() is also defined here, and does not belong to this
 * driver: drm-kmod's dma_fence trace stub covers init/destroy/signal/wait but
 * not emit, so virtgpu_fence.c would not compile without it. It is defined
 * under a guard so that it costs nothing when drm-kmod adds one upstream.
 */

#ifndef _VIRTGPU_TRACE_H_
#define	_VIRTGPU_TRACE_H_

#include <sys/param.h>
#include <sys/ktr.h>

#ifndef KTR_DRM
#define	KTR_DRM	KTR_DEV
#endif

static inline void
trace_virtio_gpu_cmd_queue(void *vq, void *hdr, uint32_t seqno)
{

	CTR3(KTR_DRM, "virtio_gpu_cmd_queue vq %p hdr %p seqno %u", vq, hdr,
	    seqno);
}

static inline void
trace_virtio_gpu_cmd_response(void *vq, void *hdr, uint32_t seqno)
{

	CTR3(KTR_DRM, "virtio_gpu_cmd_response vq %p hdr %p seqno %u", vq, hdr,
	    seqno);
}

#ifndef trace_dma_fence_emit
static inline void
trace_dma_fence_emit(void *fence)
{

	CTR1(KTR_DRM, "dma_fence_emit dma_fence %p", fence);
}
#define	trace_dma_fence_emit	trace_dma_fence_emit
#endif

#endif	/* _VIRTGPU_TRACE_H_ */
