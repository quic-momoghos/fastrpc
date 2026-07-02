/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for inc/fastrpc_ioctl.h
 * --------------------------------------------------
 * This file is found BEFORE inc/fastrpc_ioctl.h because zephyr_compat/ is
 * prepended to the include search path in CMakeLists.txt.  It uses the same
 * include-guard token (FASTRPC_INTERNAL_UPSTREAM_H) so the original header
 * is never opened.
 *
 * What this shim does differently from the Linux original:
 *
 *  1. Does NOT include <sys/ioctl.h> or <linux/types.h> — neither exists on
 *     Zephyr.
 *
 *  2. Does NOT redefine struct fastrpc_invoke_args — it is already declared
 *     in <fastrpc_kmd/inc/fastrpc.h> using portable stdint types.  Including
 *     that header here makes the struct available to every translation unit
 *     that pulls in fastrpc_internal.h → fastrpc_ioctl.h.
 *     NOTE: <zephyr/drivers/fastrpc.h> is the intended long-term install
 *     path but the header is not yet installed there; <fastrpc_kmd/inc/fastrpc.h>
 *     is used instead (resolvable because fastrpc_kmd/CMakeLists.txt exports its
 *     parent directory via zephyr_include_directories).
 *
 *  3. Provides all helper macros (INITIALIZE_REMOTE_ARGS, set_args, …) and
 *     control structures (fastrpc_ctrl_latency, …) that the UMD source files
 *     reference.
 *
 *  4. Omits FASTRPC_IOCTL_* command numbers — the Zephyr OSAL layer
 *     (fastrpc_ioctl_zephyr.c) calls the driver API directly; no ioctl()
 *     numbers are needed.
 *
 *  5. Keeps device-node path strings as no-op stubs so that any source file
 *     that references them still compiles (they are never opened on Zephyr).
 */

#ifndef FASTRPC_INTERNAL_UPSTREAM_H
#define FASTRPC_INTERNAL_UPSTREAM_H

#include <stdint.h>
#include <stdlib.h>   /* calloc / free */

/*
 * struct fastrpc_invoke_args and the full Zephyr FastRPC driver API are
 * defined in <fastrpc_kmd/inc/fastrpc.h> (the fastrpc_kmd Zephyr module header).
 * <zephyr/drivers/fastrpc.h> is the intended long-term install path but
 * the header is not yet installed there; use the module-relative path
 * instead.  fastrpc_kmd/CMakeLists.txt exports its parent directory so that
 * this angle-bracket include resolves correctly.
 *
 * Pull it in here so every UMD translation unit that includes
 * fastrpc_internal.h → fastrpc_ioctl.h gets the definition.
 */
#include <fastrpc_kmd/inc/fastrpc.h>

/* -------------------------------------------------------------------------
 * Device-node path strings
 * Not used on Zephyr (no VFS open), but kept so source files that reference
 * them compile without modification.
 * -------------------------------------------------------------------------
 */
#define ADSPRPC_DEVICE          "/dev/fastrpc-adsp"
#define SDSPRPC_DEVICE          "/dev/fastrpc-sdsp"
#define MDSPRPC_DEVICE          "/dev/fastrpc-mdsp"
#define CDSPRPC_DEVICE          "/dev/fastrpc-cdsp"
#define CDSP1RPC_DEVICE         "/dev/fastrpc-cdsp1"
#define GDSP0RPC_DEVICE         "/dev/fastrpc-gdsp0"
#define GDSP1RPC_DEVICE         "/dev/fastrpc-gdsp1"
#define ADSPRPC_SECURE_DEVICE   "/dev/fastrpc-adsp-secure"
#define SDSPRPC_SECURE_DEVICE   "/dev/fastrpc-sdsp-secure"
#define MDSPRPC_SECURE_DEVICE   "/dev/fastrpc-mdsp-secure"
#define CDSPRPC_SECURE_DEVICE   "/dev/fastrpc-cdsp-secure"
#define CDSP1RPC_SECURE_DEVICE  "/dev/fastrpc-cdsp1-secure"
#define GDSP0RPC_SECURE_DEVICE  "/dev/fastrpc-gdsp0-secure"
#define GDSP1RPC_SECURE_DEVICE  "/dev/fastrpc-gdsp1-secure"

/* -------------------------------------------------------------------------
 * Buffer attribute flag used by the invoke path
 * -------------------------------------------------------------------------
 */
#define FASTRPC_ATTR_NOVA  (256)

/* -------------------------------------------------------------------------
 * Invoke argument array helpers
 *
 * These macros are used by fastrpc_apps_user.c to build the args[] array
 * that is passed to ioctl_invoke().
 *
 * struct fastrpc_invoke_args is defined in <fastrpc/inc/fastrpc.h>:
 *   uint64_t ptr;    – pointer to buffer
 *   uint64_t length; – buffer length
 *   int32_t  fd;     – DMA-buf fd (-1 if N/A)
 *   uint32_t attr;   – per-buffer attributes
 * -------------------------------------------------------------------------
 */
#define INITIALIZE_REMOTE_ARGS(total)                                   \
	int *pfds = NULL;                                               \
	unsigned int *pattrs = NULL;                                    \
	args = (struct fastrpc_invoke_args *)                           \
		calloc((total), sizeof(*args));                         \
	if (args == NULL) {                                             \
		goto bail;                                              \
	}

#define DESTROY_REMOTE_ARGS()                                           \
	if (args) {                                                     \
		free(args);                                             \
	}

#define set_args(i, _ptr, _len, _fd, _attr)                             \
	do {                                                            \
		args[i].ptr    = (uint64_t)(uintptr_t)(_ptr);          \
		args[i].length = (uint64_t)(_len);                     \
		args[i].fd     = (int32_t)(_fd);                       \
		args[i].attr   = (uint32_t)(_attr);                    \
	} while (0)

#define set_args_ptr(i, _ptr)      (args[i].ptr    = (uint64_t)(uintptr_t)(_ptr))
#define set_args_len(i, _len)      (args[i].length = (uint64_t)(_len))
#define set_args_attr(i, _attr)    (args[i].attr   = (uint32_t)(_attr))
#define set_args_fd(i, _fd)        (args[i].fd     = (int32_t)(_fd))
#define get_args_ptr(i)            (args[i].ptr)
#define get_args_len(i)            (args[i].length)
#define get_args_attr(i)           (args[i].attr)
#define get_args_fd(i)             (args[i].fd)
#define append_args_attr(i, _attr) (args[i].attr  |= (uint32_t)(_attr))
#define get_args()                 (args)

/* Always report "upstream" kernel path (no downstream ioctl extensions) */
#define is_upstream()  1

/* -------------------------------------------------------------------------
 * Notification helper macros
 * INVOKE2 status notifications are not supported in the upstream driver
 * path; return sentinel values so callers degrade gracefully.
 * -------------------------------------------------------------------------
 */
#define NOTIF_GETDOMAIN(r)   (-1)
#define NOTIF_GETSESSION(r)  (-1)
#define NOTIF_GETSTATUS(r)   (-1)

#define FASTRPC_INVOKE2_STATUS_NOTIF         2
#define FASTRPC_INVOKE2_KERNEL_OPTIMIZATIONS 1

#ifndef FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK
#define FASTRPC_MAX_DSP_ATTRIBUTES_FALLBACK  1
#endif

/* -------------------------------------------------------------------------
 * Control structures
 *
 * Referenced by ioctl_control() callers in the UMD (fastrpc_apps_user.c,
 * fastrpc_pm.c, …).  On Zephyr, ioctl_control() returns AEE_EUNSUPPORTED
 * for all requests, but the structures must still be present so the call
 * sites compile.
 * -------------------------------------------------------------------------
 */
struct fastrpc_ctrl_latency {
	uint32_t enable;
	uint32_t latency;
};

struct fastrpc_ctrl_smmu {
	uint32_t sharedcb;
};

struct fastrpc_ctrl_kalloc {
	uint32_t kalloc_support;
};

struct fastrpc_ctrl_wakelock {
	uint32_t enable;
};

struct fastrpc_ctrl_pm {
	uint32_t timeout;
};

/* -------------------------------------------------------------------------
 * Multi-domain context management
 * Not supported in the upstream driver path; types kept for source compat.
 * -------------------------------------------------------------------------
 */
enum fastrpc_mdctx_manage_req {
	FASTRPC_MDCTX_SETUP,
	FASTRPC_MDCTX_REMOVE,
};

#endif /* FASTRPC_INTERNAL_UPSTREAM_H */
