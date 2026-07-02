/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <sys/eventfd.h>
 * -----------------------------------------------
 * On Linux, <sys/eventfd.h> declares eventfd(), eventfd_read(),
 * eventfd_write(), and the eventfd_t type.  On Zephyr the equivalent is
 * provided by the POSIX layer in <zephyr/posix/sys/eventfd.h>
 * (requires CONFIG_EVENTFD=y and CONFIG_POSIX_API=y).
 *
 * This shim is placed in zephyr_compat/sys/ so that any UMD source file
 * that includes <sys/eventfd.h> directly resolves to the Zephyr POSIX
 * implementation instead of the Linux system header.
 *
 * Primary consumers in this tree:
 *   - src/fastrpc_apps_user.c  (includes <sys/eventfd.h> inside #ifndef _WIN32)
 *   - src/fastrpc_mem.c        (includes <sys/eventfd.h> inside #ifndef _WIN32)
 *   - src/log_config.c         (includes <sys/eventfd.h> directly)
 *   - src/listener_android.c   (already uses #ifdef __ZEPHYR__ guard, but
 *                                this shim provides a safe fallback)
 */

#ifndef ZEPHYR_COMPAT_SYS_EVENTFD_H_
#define ZEPHYR_COMPAT_SYS_EVENTFD_H_

#include <zephyr/posix/sys/eventfd.h>

#endif /* ZEPHYR_COMPAT_SYS_EVENTFD_H_ */
