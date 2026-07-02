/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <sys/ioctl.h>
 * --------------------------------------------
 * The ioctl() syscall does not exist on Zephyr.  This empty stub is placed
 * in zephyr_compat/sys/ so that any UMD source file that includes
 * <sys/ioctl.h> compiles without error.
 *
 * The actual ioctl_*() functions are implemented in
 *   src/OSAL/fastrpc_ioctl_zephyr.c
 * using the Zephyr fastrpc driver API (<zephyr/drivers/fastrpc.h>).
 * No ioctl() call numbers or the ioctl() function prototype are needed.
 *
 * Primary consumer in this tree: inc/fastrpc_ioctl.h.
 * That header is itself shadowed by zephyr_compat/fastrpc_ioctl.h which
 * does NOT include <sys/ioctl.h>, so this stub is mainly a safety net for
 * any other translation unit that might include <sys/ioctl.h> directly
 * (e.g. src/fastrpc_apps_user.c when CONFIG_FASTRPC_UMD_FULL is enabled).
 */

#ifndef ZEPHYR_COMPAT_SYS_IOCTL_H_
#define ZEPHYR_COMPAT_SYS_IOCTL_H_

/*
 * On Zephyr, sys/ioctl.h is provided by the POSIX layer and declares
 * ioctl() for file-descriptor operations.  Include it via #include_next
 * so that Zephyr's ioctl() declaration and FIONBIO/FIONREAD macros remain
 * available to any fastrpc_umd source file that needs them.
 *
 * The Linux _IO/_IOR/_IOW/_IOWR ioctl-number macros are NOT provided here;
 * they are guarded by #ifndef __ZEPHYR__ in inc/fastrpc_ioctl.h and are
 * never referenced in a Zephyr build.
 */
#include_next <sys/ioctl.h>

#endif /* ZEPHYR_COMPAT_SYS_IOCTL_H_ */
