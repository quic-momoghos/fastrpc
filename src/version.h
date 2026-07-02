/* Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Zephyr compatibility redirect for version.h
 * -------------------------------------------
 * The Zephyr build system generates its own version.h in the include search
 * path before lib/fastrpc_umd/inc/version.h.  Files in src/ that do
 * #include "version.h" find this redirect first (same-directory lookup),
 * which then pulls in the fastrpc_umd version definitions including
 * VERSION_STRING.
 */
#include "../inc/version.h"
