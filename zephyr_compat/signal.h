/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <signal.h>
 * -----------------------------------------
 * Zephyr's POSIX signal.h (include/zephyr/posix/signal.h) provides signal
 * numbers (SIGUSR1, SIGABRT, …) and sigset_t, but does NOT declare:
 *
 *   struct sigaction  — POSIX signal action structure
 *   sigaction()       — examine/change a signal action
 *   pthread_kill()    — send a signal to a specific thread
 *
 * All three are used by src/fastrpc_notif.c:
 *
 *   fastrpc_notif_domain_init() registers a SIGUSR1 handler via sigaction()
 *   so that the notification worker thread can be interrupted while it is
 *   blocked in the kernel.  fastrpc_notif_domain_deinit() calls
 *   pthread_kill(thread, SIGUSR1) as a fallback when the
 *   DSPRPC_NOTIF_WAKE ioctl fails.
 *
 * On Zephyr, POSIX signals are not delivered to threads in the same way as
 * on Linux.  The primary thread-exit mechanism is the DSPRPC_NOTIF_WAKE
 * ioctl (fastrpc_exit_notif_thread()), which unblocks the thread from the
 * kernel.  The sigaction / pthread_kill path is a Linux-specific fallback
 * that is a no-op on Zephyr.
 *
 * Stub behaviour:
 *   sigaction()    — returns 0 (success), does not register a handler.
 *   pthread_kill() — returns 0 (success), does not send a signal.
 */

#ifndef ZEPHYR_COMPAT_SIGNAL_H_
#define ZEPHYR_COMPAT_SIGNAL_H_

/* Pull in Zephyr's real signal.h:
 *   - signal numbers: SIGUSR1, SIGABRT, SIGTERM, …
 *   - sigset_t typedef
 *   - sigemptyset(), sigfillset(), sigaddset(), …
 *   - pthread_sigmask()
 */
#include_next <signal.h>

#include <pthread.h>  /* pthread_t — no circular dependency: Zephyr's
                       * pthread.h does not include signal.h */

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * struct sigaction  +  sigaction()
 *
 * When building with picolibc, its <signal.h> already provides:
 *   - struct sigaction  (picolibc signal.h:103)
 *   - int sigaction(…)  declared as a non-static extern (signal.h:246),
 *     implemented by Zephyr's POSIX layer (lib/posix/options/).
 *
 * Redefining struct sigaction causes a "redefinition" error, and providing
 * a static-inline sigaction() after a non-static extern declaration causes
 * a "static declaration follows non-static declaration" error.
 *
 * Guard both definitions so they are only emitted when picolibc is NOT in
 * use (i.e. when building with Zephyr's minimal libc, which omits them).
 *
 * The inner _STRUCT_SIGACTION guard is kept as a secondary safety net for
 * any future libc that defines the struct but not the macro.
 * ----------------------------------------------------------------------- */
/*
 * struct sigaction + sigaction() stub.
 *
 * Four cases:
 *
 * 1. picolibc build WITH _POSIX_C_SOURCE (balsam, clang):
 *    __PICOLIBC__ is defined AND _POSIX_C_SOURCE is defined.
 *    picolibc's <signal.h> provides struct sigaction and declares sigaction()
 *    as an extern; Zephyr's POSIX layer implements it.
 *    → skip both the struct definition and the stub.
 *
 * 2. picolibc build WITHOUT _POSIX_C_SOURCE (qemu, GCC + -specs=picolibc.specs
 *    but no -D_POSIX_C_SOURCE):
 *    __PICOLIBC__ is defined but _POSIX_C_SOURCE is NOT defined.
 *    picolibc's <signal.h> guards struct sigaction behind _POSIX_C_SOURCE, so
 *    the struct is NOT defined by picolibc in this configuration.
 *    → emit the full struct definition and the no-op stub.
 *
 * 3. Zephyr minimal-libc build with CONFIG_POSIX_SIGNALS (qemu + POSIX):
 *    Zephyr's POSIX signal.h fully defines struct sigaction and declares
 *    sigaction().
 *    → skip both the struct definition and the stub.
 *
 * 4. Zephyr minimal-libc build WITHOUT CONFIG_POSIX_SIGNALS (qemu default):
 *    Zephyr's signal.h may forward-declare struct sigaction (incomplete
 *    type) but does not provide a full definition.  Neither picolibc nor
 *    Zephyr's POSIX layer provides sigaction().
 *    → emit the full struct definition and the no-op stub.
 *
 * Summary of the guard:
 *   Skip our definition only when picolibc IS present AND _POSIX_C_SOURCE IS
 *   defined (case 1), or when Zephyr's POSIX layer provides it (case 3).
 *   In all other cases (cases 2 and 4) we must emit the definition ourselves.
 */
#if !(defined(__PICOLIBC__) && defined(_POSIX_C_SOURCE)) && \
    !defined(CONFIG_POSIX_SIGNALS)

/* sigset_t fallback
 * -----------------
 * picolibc's signal.h only defines sigset_t when _POSIX_C_SOURCE is set.
 * On the qemu build (GCC + picolibc-via-specs, no _POSIX_C_SOURCE) and on
 * Zephyr minimal-libc builds without CONFIG_POSIX_SIGNALS, sigset_t is
 * absent when this header is processed.  Provide a minimal typedef so that
 * struct sigaction can be defined.  The sa_mask field is never inspected
 * because sigaction() is a no-op stub on Zephyr.
 *
 * The guard _SIGSET_T_DECLARED is the same one used by picolibc and many
 * BSD-derived libc implementations, so we will not redefine the type if it
 * was already provided by the #include_next above. */
#ifndef _SIGSET_T_DECLARED
typedef unsigned long sigset_t;
#define _SIGSET_T_DECLARED
/* sigemptyset stub: clears the signal set (no-op on Zephyr). */
static inline int sigemptyset(sigset_t *set)
{
    if (set) *set = 0UL;
    return 0;
}
#endif /* _SIGSET_T_DECLARED */

typedef void (*_zephyr_sig_handler_t)(int);

struct sigaction {
    _zephyr_sig_handler_t sa_handler; /* SIG_DFL, SIG_IGN, or handler fn */
    sigset_t              sa_mask;    /* signals blocked during handler   */
    int                   sa_flags;   /* SA_RESTART, SA_NOCLDSTOP, …      */
};

/* Stub: always returns 0; the DSPRPC_NOTIF_WAKE ioctl is the real
 * wake-up mechanism on Zephyr — the SIGUSR1 handler is never invoked. */
static inline int sigaction(int signum,
                             const struct sigaction *act,
                             struct sigaction *oldact)
{
    (void)signum;
    (void)act;
    (void)oldact;
    return 0;
}

#endif /* !(picolibc && _POSIX_C_SOURCE) && !CONFIG_POSIX_SIGNALS */

/* -----------------------------------------------------------------------
 * pthread_kill() — send a signal to a thread.
 *
 * Stub: always returns 0 (success) without sending a signal.
 * fastrpc_notif_domain_deinit() calls pthread_kill(thread, SIGUSR1) only
 * when fastrpc_exit_notif_thread() (the ioctl path) fails.  On Zephyr the
 * ioctl path is the primary mechanism; this fallback is a no-op.
 *
 * Use the same guard as struct sigaction above: provide the stub whenever
 * picolibc is absent or _POSIX_C_SOURCE is not defined, and Zephyr's POSIX
 * layer does not supply pthread_kill().
 * ----------------------------------------------------------------------- */
#if !(defined(__PICOLIBC__) && defined(_POSIX_C_SOURCE)) && \
    !defined(CONFIG_POSIX_SIGNALS)
static inline int pthread_kill(pthread_t thread, int sig)
{
    (void)thread;
    (void)sig;
    return 0;
}
#endif /* !(picolibc && _POSIX_C_SOURCE) && !CONFIG_POSIX_SIGNALS */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_SIGNAL_H_ */
