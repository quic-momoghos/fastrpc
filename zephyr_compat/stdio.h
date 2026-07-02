/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <stdio.h>
 * ----------------------------------------
 * Pulls in the real <stdio.h> via #include_next, then provides inline
 * replacements for ISO C functions that are declared by picolibc but whose
 * object code is absent from the Zephyr picolibc build.
 *
 * Functions shimmed:
 *
 *   fgetpos() — declared in picolibc's <stdio.h> but not linked into the
 *               Zephyr picolibc library.  Implemented here via ftell().
 *
 *   fsetpos() — same situation.  Implemented here via fseek(SEEK_SET).
 *
 * Technique: after pulling in the real header we #undef the names and
 * redefine them as macros that call private static-inline helpers.  This
 * avoids "static declaration follows non-static declaration" errors that
 * would arise from directly redefining the functions, while still
 * eliminating the undefined-reference linker errors.
 */

#ifndef ZEPHYR_COMPAT_STDIO_H_
#define ZEPHYR_COMPAT_STDIO_H_

/* Pull in the real picolibc stdio.h first */
#include_next <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * fgetpos / fsetpos inline stubs
 *
 * fpos_t is a scalar (long) on all 32-bit picolibc / ARM Cortex-M targets.
 * The cast between fpos_t and long is safe on these platforms.
 * ----------------------------------------------------------------------- */
static inline int __zephyr_fgetpos(FILE *stream, fpos_t *pos)
{
	long p;

	if (!stream || !pos)
		return -1;
	p = ftell(stream);
	if (p < 0)
		return -1; /* errno already set by ftell */
	*pos = (fpos_t)p;
	return 0;
}

static inline int __zephyr_fsetpos(FILE *stream, const fpos_t *pos)
{
	if (!stream || !pos)
		return -1;
	return fseek(stream, (long)*pos, SEEK_SET);
}

/* Redirect every call-site to the inline stubs above.
 * #undef first in case picolibc already defined them as macros. */
#undef  fgetpos
#define fgetpos(stream, pos)  __zephyr_fgetpos((stream), (pos))

#undef  fsetpos
#define fsetpos(stream, pos)  __zephyr_fsetpos((stream), (pos))

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_STDIO_H_ */
