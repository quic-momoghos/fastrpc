// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/*
 * pthread_stubs_zephyr.c — weak POSIX stubs missing from Zephyr 4.3.0
 *
 * Zephyr's POSIX signal.h declares pthread_kill() when CONFIG_POSIX_SIGNALS
 * is enabled, but the function body is absent from Zephyr 4.3.0's
 * lib/posix/options/signal.c.  fastrpc_notif_domain_deinit() calls
 * pthread_kill(thread, SIGUSR1) as a last-resort fallback when the
 * DSPRPC_NOTIF_WAKE ioctl fails.  On Zephyr the ioctl path is the primary
 * wake-up mechanism; the pthread_kill path is never reached in practice.
 *
 * Provide a __attribute__((weak)) implementation so the linker is satisfied.
 * If Zephyr ever adds a real implementation the strong symbol will override
 * this one automatically.
 */

#include <pthread.h>
#include <signal.h>

/* Only provide the weak implementation when Zephyr's POSIX signal layer is
 * active (CONFIG_POSIX_SIGNALS=y).  In that configuration Zephyr's
 * include/zephyr/posix/signal.h declares pthread_kill() as an extern but
 * does not implement it in Zephyr 4.3.0, so the linker needs a definition.
 *
 * When CONFIG_POSIX_SIGNALS is NOT defined (e.g. the qemu build without the
 * full POSIX layer), the zephyr_compat/signal.h shim already provides a
 * static-inline pthread_kill() stub.  Defining it again here would cause a
 * "redefinition" compile error, so we skip it in that case. */
#ifdef CONFIG_POSIX_SIGNALS
int __attribute__((weak)) pthread_kill(pthread_t thread, int sig)
{
    (void)thread;
    (void)sig;
    return 0;
}
#endif /* CONFIG_POSIX_SIGNALS */
