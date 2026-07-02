/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <dirent.h>
 * -----------------------------------------
 * Zephyr's POSIX dirent.h (include/zephyr/posix/dirent.h) defines
 * struct dirent with only two fields:
 *
 *   struct dirent {
 *       unsigned int d_ino;
 *       char d_name[PATH_MAX + 1];
 *   };
 *
 * The d_type field and the DT_* constants (DT_REG, DT_DIR, …) are absent.
 * src/fastrpc_procbuf.c::get_non_preload_lib_names() uses both:
 *
 *   if (entry->d_type == DT_REG) { … }
 *
 * This shim replaces the system dirent.h with an extended version that:
 *   1. Adds the d_type field to struct dirent.
 *   2. Defines the DT_* file-type constants.
 *   3. Re-declares the standard directory functions (opendir/readdir/closedir).
 *
 * Because this file is found BEFORE the real <dirent.h> (the zephyr_compat/
 * directory is prepended to the include path by CMakeLists.txt), it
 * completely shadows the system header.  The system header's include guard
 * (ZEPHYR_INCLUDE_POSIX_DIRENT_H_) is also defined here so that if the
 * system header is somehow reached via #include_next it becomes a no-op.
 *
 * Runtime behaviour note:
 *   Zephyr's readdir() does not populate d_type; the field will always be
 *   0 (DT_UNKNOWN) after a readdir() call.  Consequently the
 *   get_non_preload_lib_names() filter (entry->d_type == DT_REG) will never
 *   match and the custom-library list sent to the DSP will be empty.  This
 *   is acceptable on Zephyr where dynamic library loading is not supported.
 */

#ifndef ZEPHYR_COMPAT_DIRENT_H_
#define ZEPHYR_COMPAT_DIRENT_H_

/*
 * Prevent the system dirent.h from being included again if it is reached
 * via #include_next or a direct path.
 */
#define ZEPHYR_INCLUDE_POSIX_DIRENT_H_

#include <limits.h>   /* PATH_MAX */
#include <stddef.h>   /* NULL     */

/* PATH_MAX may not be defined by the GCC bare-metal sysroot's <limits.h>
 * (it is a POSIX extension, not a C standard constant).  Provide a
 * reasonable fallback so the struct dirent definition below compiles. */
#ifndef PATH_MAX
#define PATH_MAX 260
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * DT_* file-type constants (POSIX.1 extension, from <dirent.h> on Linux).
 * Values match the Linux / glibc definitions.
 * ----------------------------------------------------------------------- */
#define DT_UNKNOWN  0   /* unknown file type                  */
#define DT_FIFO     1   /* named pipe (FIFO)                  */
#define DT_CHR      2   /* character device                   */
#define DT_DIR      4   /* directory                          */
#define DT_BLK      6   /* block device                       */
#define DT_REG      8   /* regular file                       */
#define DT_LNK      10  /* symbolic link                      */
#define DT_SOCK     12  /* UNIX domain socket                 */
#define DT_WHT      14  /* BSD whiteout (not used on Zephyr)  */

/* -----------------------------------------------------------------------
 * DIR opaque type.
 * Zephyr's implementation uses void * for DIR (same as the system header).
 * ----------------------------------------------------------------------- */
typedef void DIR;

/* -----------------------------------------------------------------------
 * struct dirent — extended to include d_type.
 *
 * IMPORTANT: d_type is placed AFTER d_name to preserve the binary layout
 * of the original Zephyr struct dirent:
 *
 *   Original Zephyr layout (include/zephyr/posix/dirent.h):
 *     offset 0 : unsigned int  d_ino   (4 bytes)
 *     offset 4 : char          d_name  (PATH_MAX + 1 bytes)
 *
 *   Extended layout (this header):
 *     offset 0 : unsigned int  d_ino   (4 bytes)          ← unchanged
 *     offset 4 : char          d_name  (PATH_MAX + 1 bytes) ← unchanged
 *     offset 4+PATH_MAX+1 : unsigned char d_type (1 byte) ← appended
 *
 * Zephyr's readdir() fills d_ino and d_name at the correct offsets.
 * d_type is not written by readdir() and will read as 0 (DT_UNKNOWN),
 * so the filter (entry->d_type == DT_REG) in fastrpc_procbuf.c will
 * always be false — acceptable on Zephyr where dlopen() is not supported.
 * ----------------------------------------------------------------------- */
struct dirent {
    unsigned int  d_ino;                /* inode number (offset 0)       */
    char          d_name[PATH_MAX + 1]; /* filename     (offset 4)       */
    unsigned char d_type;               /* file type DT_* (appended end) */
};

/* -----------------------------------------------------------------------
 * Directory-traversal functions.
 * Declarations match Zephyr's POSIX implementation in
 * include/zephyr/posix/dirent.h and the underlying fs/fs.h layer.
 * ----------------------------------------------------------------------- */
DIR            *opendir(const char *dirname);
int             closedir(DIR *dirp);
struct dirent  *readdir(DIR *dirp);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_COMPAT_DIRENT_H_ */
