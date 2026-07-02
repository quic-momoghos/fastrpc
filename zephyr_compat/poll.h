/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <poll.h>
 * ----------------------------------------
 * On Linux, <poll.h> declares poll() and struct pollfd.  On Zephyr the
 * equivalent is provided by the POSIX layer in
 * <zephyr/posix/poll.h> (requires CONFIG_POSIX_API=y).
 *
 * This shim is placed in zephyr_compat/ so that any UMD source file that
 * includes <poll.h> directly (e.g. fastrpc_apps_user.c, fastrpc_mem.c,
 * log_config.c) resolves to the Zephyr POSIX implementation instead of
 * the Linux system header.
 *
 * Primary consumers in this tree:
 *   - src/fastrpc_apps_user.c  (includes <poll.h> inside #ifndef _WIN32)
 *   - src/fastrpc_mem.c        (includes <poll.h> inside #ifndef _WIN32)
 *   - src/log_config.c         (includes <poll.h> directly)
 */

#ifndef ZEPHYR_COMPAT_POLL_H_
#define ZEPHYR_COMPAT_POLL_H_

#include <zephyr/posix/poll.h>

#endif /* ZEPHYR_COMPAT_POLL_H_ */
