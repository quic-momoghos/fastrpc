/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility stub for <dlfcn.h>
 * ----------------------------------------
 * Dynamic linking (dlopen/dlclose/dlsym/dlerror) is a Linux/POSIX feature
 * that does not exist on Zephyr.  This stub satisfies the #include <dlfcn.h>
 * directives in:
 *   - src/fastrpc_notif.c  (calls dlerror() to clear error state)
 *   - src/mod_table.c      (uses DLOPEN/DLCLOSE/DLSYM/DLERROR macros)
 *
 * All stub functions return failure/NULL.  On Zephyr, reverse-RPC modules
 * must be registered statically via mod_table_register_static() rather than
 * being loaded at runtime.  The open_mod_table_open_dynamic() code path in
 * mod_table.c handles a NULL dlhandle gracefully (sets dlErr and falls back
 * to the static table), so returning NULL from dlopen() is safe.
 *
 * dlerror() returning NULL is also safe: the callers check for NULL before
 * using the string.
 */

#ifndef ZEPHYR_COMPAT_DLFCN_H_
#define ZEPHYR_COMPAT_DLFCN_H_

#include <stddef.h>  /* NULL */
#include <errno.h>   /* ENOSYS */

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * dlopen() mode flags
 * These constants are referenced by mod_table.c (RTLD_NOW).
 * ----------------------------------------------------------------------- */
#ifndef RTLD_LAZY
#define RTLD_LAZY   0x00001
#endif
#ifndef RTLD_NOW
#define RTLD_NOW    0x00002
#endif
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0x00100
#endif
#ifndef RTLD_LOCAL
#define RTLD_LOCAL  0x00000
#endif

/* -----------------------------------------------------------------------
 * Stub implementations
 * Dynamic loading is not supported on Zephyr.  All functions are static
 * inline so they produce no object-file symbols and are optimised away.
 * ----------------------------------------------------------------------- */

/**
 * dlopen() — open a shared object.
 * Always returns NULL on Zephyr (no dynamic loader).
 */
static inline void *dlopen(const char *filename, int flags)
{
    (void)filename;
    (void)flags;
    errno = ENOSYS;
    return NULL;
}

/**
 * dlclose() — close a shared-object handle.
 * No-op on Zephyr; returns 0 (success) unconditionally.
 */
static inline int dlclose(void *handle)
{
    (void)handle;
    return 0;
}

/**
 * dlsym() — look up a symbol in a shared object.
 * Always returns NULL on Zephyr.
 */
static inline void *dlsym(void *handle, const char *symbol)
{
    (void)handle;
    (void)symbol;
    return NULL;
}

/**
 * dlerror() — return a human-readable error string.
 * Returns NULL (no pending error) on Zephyr.  Callers in fastrpc_notif.c
 * and mod_table.c check for NULL before using the returned pointer.
 */
static inline const char *dlerror(void)
{
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_DLFCN_H_ */
