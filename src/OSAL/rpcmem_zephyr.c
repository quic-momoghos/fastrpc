/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * rpcmem_zephyr.c - Zephyr port of rpcmem_linux.c
 *
 * Provides the same public rpcmem_*() API as rpcmem_linux.c but replaces
 * every Linux-specific mechanism with Zephyr equivalents:
 *
 *   Linux mechanism                Zephyr replacement
 *   ─────────────────────────────  ──────────────────────────────────────
 *   DMA-heap (/dev/dma_heap/…)     pluggable rpcmem_backend_ops (default:
 *   FastRPC ioctl DMA alloc        k_malloc / k_free from Zephyr heap)
 *   mmap / munmap                  direct pointer from allocator
 *   open() / close() device nodes  not needed
 *   pthread_mutex_t                struct k_mutex
 *
 * The allocation strategy is fully decoupled from the rest of the code
 * through struct rpcmem_backend_ops (see inc/OSAL/rpcmem_backend.h).  Call
 * rpcmem_register_backend() before rpcmem_init() to plug in a different
 * allocator (e.g. a contiguous DMA pool or a future DMA-buf port).
 */

/*
 * Enable FARF_LOW for this translation unit.
 * Use #undef first to silence the "redefined" warning that arises because
 * CMakeLists.txt passes -DFARF_LOW=0 on the command line.
 */
#undef  FARF_LOW
#define FARF_LOW 1

#include <zephyr/kernel.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "AEEQList.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "fastrpc_common.h"
#include "rpcmem.h"
#include "OSAL/rpcmem_backend.h"
#include "verify.h"

#include <fastrpc_kmd/inc/fastrpc.h>   /* fastrpc_shared_alloc / fastrpc_shared_free */

/* =========================================================================
 * Default backend: Zephyr system heap  (k_malloc / k_free)
 *
 * This backend satisfies the rpcmem_backend_ops contract using the Zephyr
 * kernel heap.  It is intentionally kept minimal so that it is easy to
 * swap out for a more capable allocator (e.g. one that returns physically
 * contiguous memory or a DMA-buf fd) without touching any other code.
 * =========================================================================
 */

/**
 * zephyr_heap_alloc() - allocate from the Zephyr system heap
 *
 * @size:   number of bytes to allocate
 * @flags:  rpcmem heap flags — unused by this backend; reserved for future
 *          backends that may select cached vs. uncached memory, etc.
 * @out_fd: set to -1 — heap memory has no associated file descriptor
 * @out_dsp_shareable: set to 0 — plain heap memory is CPU-only and is NOT
 *          shared with the DSP (no pool allocation, no DSP-visible IOVA).
 *
 * Returns a non-NULL pointer on success, NULL on failure.
 */
static void *zephyr_heap_alloc(size_t size, uint32_t flags, int *out_fd,
			       uint32_t *out_dsp_shareable)
{
	ARG_UNUSED(flags);
	*out_fd = -1; /* no fd concept for plain heap memory */
	*out_dsp_shareable = 0; /* CPU-only; not shared with the DSP */
	return k_malloc(size);
}

/**
 * zephyr_heap_free() - release memory back to the Zephyr system heap
 *
 * @buf:  pointer previously returned by zephyr_heap_alloc()
 * @fd:   ignored (-1 for this backend)
 * @size: ignored (k_free does not need the size)
 */
static void zephyr_heap_free(void *buf, int fd, size_t size)
{
	ARG_UNUSED(fd);
	ARG_UNUSED(size);
	k_free(buf);
}

/** Built-in Zephyr heap backend ops — used when no custom backend is set. */
static const struct rpcmem_backend_ops zephyr_heap_backend = {
	.alloc = zephyr_heap_alloc,
	.free  = zephyr_heap_free,
};

/* =========================================================================
 * FR pool backend: FastRPC DSP-shareable memory  (fastrpc_shared_alloc/free)
 *
 * This is the default backend.  It allocates from the same FASTRPC_PHYS_POOL
 * the KMD driver uses, so an rpcmem buffer is physically identical to a
 * driver-allocated one and can be given a DSP-visible IOVA lazily at invoke
 * time.  Buffers from this backend are marked dsp_shareable = 1; the fd stays
 * -1 (Zephyr has no dma-buf fd — the buffer VA, not an fd, identifies it).
 * =========================================================================
 */

/**
 * fr_pool_alloc() - allocate DSP-shareable memory from the FastRPC pool
 *
 * @size:   number of bytes to allocate
 * @flags:  rpcmem heap flags — unused (pool memory is cached WB; coherency is
 *          handled by the invoke path's flush/invalidate)
 * @out_fd: set to -1 — no dma-buf fd concept on Zephyr
 * @out_dsp_shareable: set to 1 on success — the buffer is shared with the DSP
 *
 * Returns a non-NULL CPU virtual address on success, NULL on failure.
 */
static void *fr_pool_alloc(size_t size, uint32_t flags, int *out_fd,
			   uint32_t *out_dsp_shareable)
{
	void *va;

	ARG_UNUSED(flags);
	va = fastrpc_shared_alloc(size);
	*out_fd = -1;                       /* no fd on Zephyr */
	*out_dsp_shareable = (va != NULL) ? 1u : 0u;
	return va;
}

/**
 * fr_pool_free() - release memory allocated by fr_pool_alloc()
 *
 * @buf:  pointer previously returned by fr_pool_alloc()
 * @fd:   ignored (-1 for this backend)
 * @size: ignored (the registry remembers the size)
 */
static void fr_pool_free(void *buf, int fd, size_t size)
{
	ARG_UNUSED(fd);
	ARG_UNUSED(size);
	fastrpc_shared_free(buf);
}

/** FastRPC pool backend ops — the default DSP-shareable allocator. */
static const struct rpcmem_backend_ops fr_pool_backend = {
	.alloc = fr_pool_alloc,
	.free  = fr_pool_free,
};

/*
 * active_backend points to the ops that rpcmem_alloc_internal() and
 * rpcmem_free_internal() will call.  Defaults to the FastRPC pool backend so
 * that rpcmem_alloc() returns DSP-shareable memory (mirrors Linux, where
 * rpcmem is always backed by the dma-heap).  Override with
 * rpcmem_register_backend() before rpcmem_init(); pass NULL there to restore
 * this default.
 */
static const struct rpcmem_backend_ops *active_backend = &fr_pool_backend;

/* =========================================================================
 * Backend registration  (public API — declared in inc/OSAL/rpcmem_backend.h)
 * =========================================================================
 */

/**
 * rpcmem_register_backend() - replace the active memory allocation backend
 * @ops: pointer to a caller-owned rpcmem_backend_ops structure, or NULL to
 *       restore the default Zephyr heap backend.
 *
 * Must be called before rpcmem_init().  The ops pointer must remain valid
 * until after rpcmem_deinit() returns.
 */
void rpcmem_register_backend(const struct rpcmem_backend_ops *ops)
{
	active_backend = (ops != NULL) ? ops : &fr_pool_backend;
}

/* =========================================================================
 * Internal state
 * =========================================================================
 */

/** Linked list of all live rpc_info records (one per outstanding allocation). */
static QList          rpclst;

/** Mutex protecting rpclst. */
static struct k_mutex rpcmt;

/**
 * struct rpc_info - per-allocation tracking record
 *
 * @qn:          intrusive list node (embedded in rpclst)
 * @buf:         raw pointer returned by the backend's alloc()
 * @aligned_buf: page-aligned view of the buffer exposed to callers.
 *               For the heap backend this equals @buf because k_malloc
 *               already returns suitably aligned memory.  A future DMA
 *               backend may return a larger slab and set aligned_buf to
 *               the first page-aligned address within it.
 * @size:        allocation size passed to the backend
 * @fd:          pseudo file-descriptor returned by the backend (-1 if none).
 *               Kept for Linux/dma-buf parity and the invoke wire format
 *               (rpra.dma.fd); on Zephyr it is always -1.
 * @dsp_shareable: 1 when this buffer is shared with the DSP (allocated from
 *               the FastRPC pool, so it can be IOMMU-mapped for the DSP);
 *               0 for CPU-only heap memory.  Mirrors the "dma" marker in the
 *               Linux struct rpc_info.  This flag — not @fd — is what marks a
 *               buffer for the DSP map path.
 */
struct rpc_info {
	QNode    qn;
	void    *buf;
	void    *aligned_buf;
	size_t   size;
	int      fd;
	uint32_t dsp_shareable;
};

/* =========================================================================
 * Public API  (mirrors rpcmem_linux.c exactly)
 * =========================================================================
 */

/**
 * rpcmem_init() - initialise the rpcmem subsystem
 *
 * Must be called once before any rpcmem_alloc*() call.
 * If a custom backend is desired, call rpcmem_register_backend() first.
 */
void rpcmem_init(void)
{
	QList_Ctor(&rpclst);
	k_mutex_init(&rpcmt);
}

/**
 * rpcmem_deinit() - tear down the rpcmem subsystem
 *
 * The backend owns its resources; there are no device nodes to close.
 * A custom backend that holds open handles should release them before
 * this call (or provide its own cleanup via rpcmem_register_backend(NULL)).
 */
void rpcmem_deinit(void)
{
	/* Nothing to do for the default heap backend. */
}

/**
 * rpcmem_to_fd_internal() - look up the fd associated with a buffer pointer
 * @po: aligned_buf pointer previously returned by rpcmem_alloc_internal()
 *
 * Returns the fd stored in the matching rpc_info, or -1 if not found.
 */
int rpcmem_to_fd_internal(void *po)
{
	struct rpc_info *rinfo;
	QNode *pn, *pnn;
	int fd = -1;

	k_mutex_lock(&rpcmt, K_FOREVER);
	QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn) {
		rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
		if (rinfo->aligned_buf == po) {
			fd = rinfo->fd;
			break;
		}
	}
	k_mutex_unlock(&rpcmt);
	return fd;
}

/** rpcmem_to_fd() - public wrapper around rpcmem_to_fd_internal() */
int rpcmem_to_fd(void *po)
{
	return rpcmem_to_fd_internal(po);
}

/**
 * rpcmem_is_dsp_shareable() - report whether a buffer is shared with the DSP
 * @po: aligned_buf pointer previously returned by rpcmem_alloc_internal()
 *
 * Returns 1 when the buffer was allocated from the FastRPC pool (it can be
 * IOMMU-mapped and given a DSP-visible IOVA), 0 otherwise (CPU-only heap
 * memory, or the pointer is not a live rpcmem allocation).  This is the
 * discriminator that replaces the fd sentinel on Zephyr.
 */
uint32_t rpcmem_is_dsp_shareable(void *po)
{
	struct rpc_info *rinfo;
	QNode *pn, *pnn;
	uint32_t shareable = 0;

	k_mutex_lock(&rpcmt, K_FOREVER);
	QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn) {
		rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
		if (rinfo->aligned_buf == po) {
			shareable = rinfo->dsp_shareable;
			break;
		}
	}
	k_mutex_unlock(&rpcmt);
	return shareable;
}

/**
 * rpcmem_alloc_internal() - allocate a shared memory buffer
 * @heapid: heap identifier forwarded to the backend for future use;
 *          the default heap backend ignores it
 * @flags:  rpcmem allocation flags (RPCMEM_FLAG_*); forwarded to the backend
 * @size:   number of bytes to allocate
 *
 * Allocates memory via the active backend, registers the buffer with the
 * FastRPC framework via remote_register_buf(), and returns the aligned
 * buffer pointer.  Returns NULL on any failure.
 */
void *rpcmem_alloc_internal(int heapid, uint32_t flags, size_t size)
{
	struct rpc_info *rinfo = NULL;
	int fd = -1;
	uint32_t dsp_shareable = 0;
	void *buf;

	if (size == 0) {
		FARF(ERROR, "Error: rpcmem_alloc_internal called with size 0");
		return NULL;
	}

	/* ---- Step 1: allocate raw memory from the active backend ---------- */
	buf = active_backend->alloc(size, flags, &fd, &dsp_shareable);
	if (!buf) {
		FARF(ERROR,
		     "Error: backend alloc failed heapid %d size %zu flags %u",
		     heapid, size, flags);
		return NULL;
	}

	/* ---- Step 2: allocate the tracking record ------------------------- */
	rinfo = calloc(1, sizeof(*rinfo));
	if (!rinfo) {
		FARF(ERROR, "Error: calloc for rpc_info failed (OOM)");
		active_backend->free(buf, fd, size);
		return NULL;
	}

	/* ---- Step 3: fill in the record ----------------------------------- */
	rinfo->buf  = buf;
	/*
	 * For the heap backend aligned_buf == buf because k_malloc guarantees
	 * at least sizeof(void *) alignment.  A DMA backend that returns a
	 * larger slab should set aligned_buf to the first page-aligned address
	 * within the slab (and adjust size accordingly).
	 */
	rinfo->aligned_buf   = buf;
	rinfo->size          = size;
	rinfo->fd            = fd;
	rinfo->dsp_shareable = dsp_shareable;

	/* ---- Step 4: add to the live-allocation list ---------------------- */
	k_mutex_lock(&rpcmt, K_FOREVER);
	QList_AppendNode(&rpclst, &rinfo->qn);
	k_mutex_unlock(&rpcmt);

	printk("rpcmem_alloc: heapid %d size %zu ptr %p fd %d dsp_shareable %u",
	     heapid, size, rinfo->aligned_buf, fd, dsp_shareable);

	/* ---- Step 5: register with the FastRPC framework ------------------ */
	remote_register_buf(rinfo->buf, rinfo->size, rinfo->fd);

	return rinfo->aligned_buf;
}

/**
 * rpcmem_free_internal() - free a buffer allocated by rpcmem_alloc_internal()
 * @po: aligned_buf pointer previously returned by rpcmem_alloc_internal()
 *
 * Deregisters the buffer from the FastRPC framework, releases the memory
 * via the active backend, and removes the tracking record.
 */
void rpcmem_free_internal(void *po)
{
	struct rpc_info *rinfo, *rfree = NULL;
	QNode *pn, *pnn;

	k_mutex_lock(&rpcmt, K_FOREVER);
	QLIST_NEXTSAFE_FOR_ALL(&rpclst, pn, pnn) {
		rinfo = STD_RECOVER_REC(struct rpc_info, qn, pn);
		if (rinfo->aligned_buf == po) {
			rfree = rinfo;
			QNode_Dequeue(&rinfo->qn);
			break;
		}
	}
	k_mutex_unlock(&rpcmt);

	if (rfree) {
		/* Deregister from FastRPC before releasing the memory. */
		remote_register_buf(rfree->buf, rfree->size, -1);
		active_backend->free(rfree->buf, rfree->fd, rfree->size);
		free(rfree);
	}
}

/** rpcmem_free() - public wrapper around rpcmem_free_internal() */
void rpcmem_free(void *po)
{
	rpcmem_free_internal(po);
}

/**
 * rpcmem_alloc() - allocate shared memory (int size variant)
 * @heapid: heap identifier
 * @flags:  allocation flags
 * @size:   number of bytes (int; negative values return NULL)
 */
void *rpcmem_alloc(int heapid, uint32_t flags, int size)
{
	if (size <= 0) {
		FARF(ERROR, "Error: rpcmem_alloc called with size %d", size);
		return NULL;
	}
	return rpcmem_alloc_internal(heapid, flags, (size_t)size);
}

/**
 * rpcmem_alloc2() - allocate shared memory (size_t size variant)
 * @heapid: heap identifier
 * @flags:  allocation flags
 * @size:   number of bytes
 */
void *rpcmem_alloc2(int heapid, uint32_t flags, size_t size)
{
	return rpcmem_alloc_internal(heapid, flags, size);
}

/** rpcmem_deinit_internal() - internal deinit wrapper */
void rpcmem_deinit_internal(void)
{
	rpcmem_deinit();
}

/** rpcmem_init_internal() - internal init wrapper */
void rpcmem_init_internal(void)
{
	rpcmem_init();
}
