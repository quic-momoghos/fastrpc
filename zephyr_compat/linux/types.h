/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <linux/types.h>
 * ----------------------------------------------
 * Maps the Linux kernel fixed-width integer typedefs (__u8, __u16, …) to
 * the standard C99 equivalents from <stdint.h>.
 *
 * This header is found before any system linux/types.h because
 * zephyr_compat/ is prepended to the compiler include path in
 * lib/fastrpc_umd/CMakeLists.txt.
 *
 * NOTE: The primary consumer of these types in the fastrpc_umd tree is
 * inc/fastrpc_ioctl.h.  That header is itself shadowed by
 * zephyr_compat/fastrpc_ioctl.h (which does NOT include linux/types.h),
 * so this file is mainly a safety net for any other translation unit that
 * might include <linux/types.h> directly.
 */

#ifndef ZEPHYR_COMPAT_LINUX_TYPES_H_
#define ZEPHYR_COMPAT_LINUX_TYPES_H_

#include <stdint.h>

/* Unsigned */
typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

/* Signed */
typedef int8_t   __s8;
typedef int16_t  __s16;
typedef int32_t  __s32;
typedef int64_t  __s64;

/* Byte-swapped variants — define as plain types; no endian swap needed
 * for the fastrpc UMD (all data is native-endian on the application CPU). */
typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;
typedef uint16_t __le16;
typedef uint32_t __le32;
typedef uint64_t __le64;

#endif /* ZEPHYR_COMPAT_LINUX_TYPES_H_ */
