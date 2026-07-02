/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility stub for <asm/ioctl.h>
 * -------------------------------------------
 * <asm/ioctl.h> is a Linux kernel header that defines the _IO/_IOR/_IOW/
 * _IOWR macros used to construct ioctl command numbers.  On Zephyr the
 * ioctl() syscall does not exist; the fastrpc OSAL layer calls the Zephyr
 * driver API directly (see src/OSAL/fastrpc_ioctl_zephyr.c).
 *
 * This stub satisfies the #include <asm/ioctl.h> directive in
 * inc/fastrpc_apps_user.h without pulling in any Linux-specific
 * definitions.  The _IO/_IOR/_IOW/_IOWR macros are already guarded by
 * #ifndef __ZEPHYR__ in inc/fastrpc_ioctl.h so they are never referenced
 * in a Zephyr build.
 */

#ifndef ZEPHYR_COMPAT_ASM_IOCTL_H_
#define ZEPHYR_COMPAT_ASM_IOCTL_H_

/* intentionally empty — ioctl command numbers are not used on Zephyr */

#endif /* ZEPHYR_COMPAT_ASM_IOCTL_H_ */
