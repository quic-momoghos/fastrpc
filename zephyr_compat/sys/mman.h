/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <sys/mman.h>
 * ------------------------------------------
 * Zephyr does not provide mmap()/munmap() — these are Linux-specific
 * virtual-memory syscalls.  The fastrpc_umd sources include <sys/mman.h>
 * and call mmap()/munmap() in a few paths that are not exercised on Zephyr
 * (e.g. apps_mem_share_map, remote_register_fd_attr).
 *
 * This shim provides:
 *   - The standard PROT_* / MAP_* constants
 *   - MAP_FAILED sentinel
 *   - Static-inline stubs for mmap() and munmap() that return MAP_FAILED /
 *     0 respectively, so the code compiles and links cleanly.  The callers
 *     already check the return value and handle failure gracefully.
 */

#ifndef ZEPHYR_COMPAT_SYS_MMAN_H_
#define ZEPHYR_COMPAT_SYS_MMAN_H_

#include <stddef.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Protection flags
 * ----------------------------------------------------------------------- */
#ifndef PROT_NONE
#define PROT_NONE  0x0
#endif
#ifndef PROT_READ
#define PROT_READ  0x1
#endif
#ifndef PROT_WRITE
#define PROT_WRITE 0x2
#endif
#ifndef PROT_EXEC
#define PROT_EXEC  0x4
#endif

/* -----------------------------------------------------------------------
 * Mapping flags
 * ----------------------------------------------------------------------- */
#ifndef MAP_SHARED
#define MAP_SHARED    0x01
#endif
#ifndef MAP_PRIVATE
#define MAP_PRIVATE   0x02
#endif
#ifndef MAP_FIXED
#define MAP_FIXED     0x10
#endif
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS 0x20
#endif
#ifndef MAP_ANON
#define MAP_ANON      MAP_ANONYMOUS
#endif

/* -----------------------------------------------------------------------
 * MAP_FAILED — returned by mmap() on error
 * ----------------------------------------------------------------------- */
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

/* -----------------------------------------------------------------------
 * mmap() stub — always fails on Zephyr.
 * Callers check for MAP_FAILED and handle the error path.
 * ----------------------------------------------------------------------- */
static inline void *mmap(void *addr, size_t length, int prot, int flags,
                          int fd, long offset)
{
	(void)addr;
	(void)length;
	(void)prot;
	(void)flags;
	(void)fd;
	(void)offset;
	errno = ENOSYS;
	return MAP_FAILED;
}

/* -----------------------------------------------------------------------
 * munmap() stub — no-op on Zephyr.
 * Memory was never actually mmap'd via the Linux syscall, so there is
 * nothing to unmap.  Return 0 (success) to avoid spurious error paths.
 * ----------------------------------------------------------------------- */
static inline int munmap(void *addr, size_t length)
{
	(void)addr;
	(void)length;
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_SYS_MMAN_H_ */
