/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * rpcmem_backend.h - Pluggable memory-allocation backend interface for rpcmem
 *
 * The rpcmem Zephyr port (rpcmem_zephyr.c) delegates every allocation and
 * deallocation to a struct rpcmem_backend_ops instance.  The default backend
 * uses the Zephyr system heap (k_malloc / k_free).
 *
 * To substitute a different allocator (e.g. a contiguous DMA pool, a
 * custom memory region, or a future DMA-buf framework port):
 *
 *   1. Implement the two function pointers below.
 *   2. Call rpcmem_register_backend(&my_ops) BEFORE rpcmem_init().
 *
 * The ops pointer must remain valid for the entire lifetime of the rpcmem
 * subsystem (i.e. until after rpcmem_deinit() returns).
 */

#ifndef RPCMEM_BACKEND_H
#define RPCMEM_BACKEND_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * struct rpcmem_backend_ops - vtable for the rpcmem memory backend
 *
 * @alloc: Allocate a contiguous buffer of @size bytes.
 *         @flags  - rpcmem heap flags (RPCMEM_FLAG_*); the backend may
 *                   use these to select a memory type (cached, uncached, …).
 *         @out_fd - OUTPUT: set to a file-descriptor or pseudo-fd that
 *                   uniquely identifies the buffer (e.g. a DMA-buf fd).
 *                   Set to -1 when no fd concept applies (heap/FR backend).
 *         @out_dsp_shareable - OUTPUT: set to 1 when the returned buffer is
 *                   shared with the DSP (allocated from the FastRPC pool, so
 *                   it can be IOMMU-mapped and given a DSP-visible IOVA); set
 *                   to 0 for CPU-only memory.  On Zephyr this flag — not
 *                   @out_fd — is the discriminator the invoke/map path uses to
 *                   decide whether an arg is mapped for the DSP or copied
 *                   inline.  Mirrors the "dma" marker in Linux struct rpc_info.
 *         Returns a non-NULL pointer on success, NULL on failure.
 *
 * @free:  Release a buffer previously returned by @alloc.
 *         @buf  - pointer returned by @alloc.
 *         @fd   - the value written to *out_fd by @alloc.
 *         @size - the original allocation size passed to @alloc.
 */
struct rpcmem_backend_ops {
	void *(*alloc)(size_t size, uint32_t flags, int *out_fd,
		       uint32_t *out_dsp_shareable);
	void  (*free)(void *buf, int fd, size_t size);
};

/**
 * rpcmem_register_backend() - replace the active memory allocation backend
 * @ops: Pointer to a fully-initialised rpcmem_backend_ops structure.
 *       Pass NULL to restore the built-in Zephyr heap backend.
 *
 * Thread-safety: must be called before rpcmem_init() and must not be
 * called concurrently with any other rpcmem function.
 */
void rpcmem_register_backend(const struct rpcmem_backend_ops *ops);

#ifdef __cplusplus
}
#endif

#endif /* RPCMEM_BACKEND_H */
