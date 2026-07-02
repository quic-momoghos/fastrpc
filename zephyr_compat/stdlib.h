/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <stdlib.h>
 * -----------------------------------------
 * Pulls in the real <stdlib.h> via #include_next and then adds declarations
 * for GNU/BSD extensions that are present in picolibc but not exposed by its
 * standard headers.
 *
 * Additions:
 *
 *   __progname  — GNU extension: pointer to the program name string.
 *                 Used unconditionally in fastrpc_log.c::HAP_debug_runtime().
 *                 On Linux it is declared in <stdlib.h>; picolibc omits it.
 *                 We provide a weak-alias fallback so the symbol links even
 *                 when the runtime does not supply it.
 *
 *   strlcpy /   — BSD string helpers present in picolibc but not declared in
 *   strlcat       its <string.h>.  Declared here (alongside stdlib) so that
 *                 all TUs that include <stdlib.h> get the prototypes and the
 *                 implicit-declaration warnings are suppressed.
 */

#ifndef ZEPHYR_COMPAT_STDLIB_H_
#define ZEPHYR_COMPAT_STDLIB_H_

/* Pull in the real stdlib.h first */
#include_next <stdlib.h>

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * __progname
 * GNU extension: name of the running program.  fastrpc_log.c references
 * this symbol in HAP_debug_runtime() to annotate log lines.  On Zephyr
 * there is no argv[0] concept; we declare the symbol as a weak reference
 * so it resolves to NULL when the runtime does not provide it, rather than
 * causing a link error.
 * ----------------------------------------------------------------------- */
#ifndef __PROGNAME_DECLARED
#define __PROGNAME_DECLARED
extern const char *__progname __attribute__((weak));
#endif

/* -----------------------------------------------------------------------
 * strlcpy / strlcat
 * BSD string functions available in picolibc but not declared in its
 * <string.h>.  Forward-declare them here to silence implicit-declaration
 * warnings in fastrpc_cap.c, fastrpc_config.c, and fastrpc_log.c.
 * ----------------------------------------------------------------------- */
#ifndef __STRLCPY_DECLARED
#define __STRLCPY_DECLARED
size_t strlcpy(char *dst, const char *src, size_t size);
size_t strlcat(char *dst, const char *src, size_t size);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_STDLIB_H_ */
