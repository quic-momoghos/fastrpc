/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <sys/time.h>
 * ------------------------------------------
 * Zephyr's POSIX sys/time.h (include/zephyr/posix/sys/time.h) provides
 * struct timeval and gettimeofday() but omits struct timezone (the second
 * parameter of gettimeofday is declared as void * on Zephyr).
 *
 * Linux code that declares a local 'struct timezone tz' fails to compile
 * because the struct definition is absent.  This shim:
 *   1. Pulls in the real Zephyr sys/time.h via #include_next.
 *   2. Adds the missing struct timezone definition.
 *   3. Provides a gettid() alias (getpid()) used alongside gettimeofday()
 *      in fastrpc_log.c.
 *
 * This file is found BEFORE the real <sys/time.h> because CMakeLists.txt
 * adds zephyr_compat/ to the beginning of the include path via
 *   target_include_directories(${ZEPHYR_CURRENT_LIBRARY} BEFORE PRIVATE ...)
 */

#ifndef ZEPHYR_COMPAT_SYS_TIME_H_
#define ZEPHYR_COMPAT_SYS_TIME_H_

/* Pull in the real Zephyr sys/time.h (struct timeval, gettimeofday) */
#include_next <sys/time.h>

/* -----------------------------------------------------------------------
 * struct timezone
 * POSIX defines gettimeofday(struct timeval *, struct timezone *) but
 * Zephyr uses void * for the second parameter and never defines the struct.
 * Provide the definition so that code declaring 'struct timezone tz' works.
 * ----------------------------------------------------------------------- */
#ifndef _STRUCT_TIMEZONE
#define _STRUCT_TIMEZONE
struct timezone {
    int tz_minuteswest; /* minutes west of Greenwich */
    int tz_dsttime;     /* type of DST correction    */
};
#endif /* _STRUCT_TIMEZONE */

/* -----------------------------------------------------------------------
 * gettid()
 * Linux syscall that returns the calling thread's TID.  Map to getpid()
 * on Zephyr so that log messages that print both pid and tid compile and
 * produce a meaningful (if identical) value.
 * ----------------------------------------------------------------------- */
#ifndef gettid
#include <unistd.h>
#define gettid() ((pid_t)getpid())
#endif

#endif /* ZEPHYR_COMPAT_SYS_TIME_H_ */
