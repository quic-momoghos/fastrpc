/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <fcntl.h>
 * ----------------------------------------
 * Pulls in Zephyr's <fcntl.h> via #include_next and then adds O_CLOEXEC,
 * which is absent from Zephyr's POSIX fcntl.h (include/zephyr/posix/fcntl.h).
 *
 * O_CLOEXEC is a Linux extension that marks a file descriptor to be closed
 * automatically on exec().  Zephyr has no exec() concept, so this flag is
 * a no-op at runtime.  Defining it as 0 allows fastrpc_pm.c to compile:
 *
 *   open(WAKE_LOCK_FILE, O_RDWR | O_CLOEXEC)
 *     → open(WAKE_LOCK_FILE, O_RDWR | 0)
 *     → open(WAKE_LOCK_FILE, O_RDWR)
 *
 * Primary consumer: src/fastrpc_pm.c (fastrpc_wake_lock_init).
 */

#ifndef ZEPHYR_COMPAT_FCNTL_H_
#define ZEPHYR_COMPAT_FCNTL_H_

/* Pull in Zephyr's real fcntl.h (O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, …) */
#include_next <fcntl.h>

/* -----------------------------------------------------------------------
 * O_CLOEXEC — close-on-exec flag.
 * Not defined by Zephyr's fcntl.h.  Defined as 0 (no-op) because Zephyr
 * has no exec() and therefore no need to close file descriptors across it.
 * ----------------------------------------------------------------------- */
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

#endif /* ZEPHYR_COMPAT_FCNTL_H_ */
