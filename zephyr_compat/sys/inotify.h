/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility stub for <sys/inotify.h>
 * ----------------------------------------------
 * inotify is a Linux-specific filesystem event notification API.  It is
 * not available on Zephyr.  This stub satisfies the #include directive in
 * src/log_config.c so that the translation unit can be compiled.
 *
 * log_config.c is compiled only when CONFIG_FASTRPC_UMD_FULL is enabled
 * (see CMakeLists.txt).  When that option is set the caller is responsible
 * for providing a platform-specific implementation or for ensuring that
 * the inotify code paths are never reached at runtime.
 *
 * Stub definitions provided:
 *   - struct inotify_event  (minimal layout)
 *   - inotify_init()        → returns -1 (ENOSYS)
 *   - inotify_add_watch()   → returns -1 (ENOSYS)
 *   - inotify_rm_watch()    → returns -1 (ENOSYS)
 *   - IN_CREATE / IN_DELETE / IN_MODIFY event mask bits
 */

#ifndef ZEPHYR_COMPAT_SYS_INOTIFY_H_
#define ZEPHYR_COMPAT_SYS_INOTIFY_H_

#include <stdint.h>
#include <errno.h>

/* inotify event mask bits */
#define IN_CREATE  0x00000100U
#define IN_DELETE  0x00000200U
#define IN_MODIFY  0x00000002U

/* Minimal inotify event structure */
struct inotify_event {
    int      wd;     /* watch descriptor */
    uint32_t mask;   /* event mask       */
    uint32_t cookie; /* unique cookie    */
    uint32_t len;    /* length of name[] */
    char     name[]; /* optional name    */
};

/* Stub function declarations — implementations return -1 / ENOSYS */
static inline int inotify_init(void)
{
    errno = ENOSYS;
    return -1;
}

static inline int inotify_add_watch(int fd, const char *pathname,
                                    uint32_t mask)
{
    (void)fd; (void)pathname; (void)mask;
    errno = ENOSYS;
    return -1;
}

static inline int inotify_rm_watch(int fd, int wd)
{
    (void)fd; (void)wd;
    errno = ENOSYS;
    return -1;
}

#endif /* ZEPHYR_COMPAT_SYS_INOTIFY_H_ */
