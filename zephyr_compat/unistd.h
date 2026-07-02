/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <unistd.h>
 * -----------------------------------------
 * Pulls in the real <unistd.h> via #include_next, then provides an inline
 * replacement for a POSIX function that Zephyr declares but does not
 * implement.
 *
 * Function shimmed:
 *
 *   rmdir() — declared in Zephyr's POSIX <unistd.h> (CONFIG_POSIX_API=y)
 *             but not implemented in the Zephyr POSIX layer for all targets.
 *             apps_std_imp.c calls rmdir() in apps_std_rmdir(); on Zephyr
 *             the call will fail gracefully with ENOSYS and the error is
 *             propagated back to the DSP caller.
 *
 * Technique: same #undef + #define-to-inline-helper pattern used in the
 * other zephyr_compat shims (stdio.h, sys/mman.h).
 */

#ifndef ZEPHYR_COMPAT_UNISTD_H_
#define ZEPHYR_COMPAT_UNISTD_H_

/* Pull in the real Zephyr/picolibc unistd.h first */
#include_next <unistd.h>

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * rmdir() inline stub
 *
 * Zephyr's POSIX layer declares rmdir() but does not provide an
 * implementation for bare-metal / QEMU targets.  Return -1/ENOSYS so
 * that callers (apps_std_rmdir) handle the error gracefully.
 * ----------------------------------------------------------------------- */
static inline int __zephyr_rmdir(const char *pathname)
{
	(void)pathname;
	errno = ENOSYS;
	return -1;
}

/* Redirect every call-site to the inline stub above.
 * #undef first in case the real header defined rmdir as a macro. */
#undef  rmdir
#define rmdir(path)  __zephyr_rmdir(path)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_UNISTD_H_ */
