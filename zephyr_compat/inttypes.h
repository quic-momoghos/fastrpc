/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility shim for <inttypes.h>
 * ------------------------------------------
 * picolibc's inttypes.h defines (line 37):
 *
 *   typedef __WCHAR_TYPE__ _wchar_t;
 *
 * where __WCHAR_TYPE__ is 'unsigned int' (32-bit) on ARM Cortex-A/M.
 *
 * AEEStdDef.h (QIDL IDL runtime) defines:
 *
 *   typedef uint16_t _wchar_t;   (guarded by __QIDL_WCHAR_T_DEFINED__)
 *
 * for 16-bit Unicode wchar support in IDL-generated stubs.
 *
 * Conflict scenario
 * -----------------
 * fastrpc_ioctl_zephyr.c includes AEEStdErr.h before fastrpc_internal.h:
 *
 *   #include "AEEStdErr.h"          ← pulls in AEEStdDef.h
 *                                     → typedef uint16_t _wchar_t  (1st def)
 *   #include "fastrpc_internal.h"   ← pulls in <semaphore.h>
 *     → posix_types.h → kernel.h → kernel_includes.h → printk.h
 *       → picolibc inttypes.h
 *         → typedef __WCHAR_TYPE__ _wchar_t  (2nd def, unsigned int)
 *                                              ← ERROR: redefinition
 *
 * Fix
 * ---
 * When AEEStdDef.h has already defined _wchar_t (indicated by the
 * __QIDL_WCHAR_T_DEFINED__ macro it sets), redirect picolibc's conflicting
 * typedef to a private placeholder name before including the real inttypes.h,
 * then remove the redirection so _wchar_t remains the uint16_t typedef.
 *
 * When AEEStdDef.h has NOT yet been included, pass through to picolibc's
 * inttypes.h unchanged.
 */

#ifndef ZEPHYR_COMPAT_INTTYPES_H_
#define ZEPHYR_COMPAT_INTTYPES_H_

#ifdef __QIDL_WCHAR_T_DEFINED__
/*
 * _wchar_t is already typedef'd as uint16_t by AEEStdDef.h.
 *
 * Redirect picolibc's  typedef __WCHAR_TYPE__ _wchar_t;
 * to                   typedef __WCHAR_TYPE__ _picolibc_wchar_t_unused;
 * by temporarily making _wchar_t a macro.  After the include the macro is
 * removed, leaving the uint16_t typedef from AEEStdDef.h intact.
 */
#define _wchar_t _picolibc_wchar_t_unused
#include_next <inttypes.h>
#undef _wchar_t
#else
#include_next <inttypes.h>
#endif

#endif /* ZEPHYR_COMPAT_INTTYPES_H_ */
