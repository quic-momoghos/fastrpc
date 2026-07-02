# Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause

zephyr_library()

# ---------------------------------------------------------------------------
# Include paths
#
# zephyr_compat/ MUST come before inc/ AND before the Zephyr system headers
# so that the Zephyr-specific shim headers shadow both the Linux originals
# in inc/ and the incomplete POSIX headers supplied by Zephyr itself.
#
# zephyr_include_directories() appends to the global include list that is
# shared with all Zephyr targets; it cannot be placed before the Zephyr
# system headers that were already registered.  Instead we use
# target_include_directories(... BEFORE PRIVATE ...) which inserts the
# directory at the very front of THIS library's include search path,
# ahead of every system header.
#
# Shims provided in zephyr_compat/:
#   fastrpc_ioctl.h   – shadows inc/fastrpc_ioctl.h (no sys/ioctl.h, no
#                       linux/types.h, no struct fastrpc_invoke_args clash)
#   linux/types.h     – maps __u8/__u16/… to stdint types
#   sys/ioctl.h       – empty stub (ioctl() syscall absent on Zephyr)
#   sys/time.h        – adds struct timezone + gettid() alias missing from
#                       Zephyr's POSIX sys/time.h
#   sys/inotify.h     – stub (inotify not available on Zephyr)
#   asm/ioctl.h       – empty stub (Linux kernel header, not on Zephyr)
#   poll.h            – redirects to <zephyr/posix/poll.h>
#   sys/eventfd.h     – redirects to <zephyr/posix/sys/eventfd.h>
# ---------------------------------------------------------------------------

# Step 1: register zephyr_compat/, inc/, and inc/OSAL/ in the normal
#         (appended) list so that the paths are visible to dependent targets
#         as well.  inc/OSAL/ is added so that OSAL headers (e.g.
#         rpcmem_backend.h) can be included as "OSAL/rpcmem_backend.h"
#         (relative to inc/) or directly as "rpcmem_backend.h" (relative to
#         inc/OSAL/).
zephyr_include_directories(
    zephyr_compat
    inc
    inc/OSAL
)

# Step 2: also insert zephyr_compat/ and inc/OSAL/ at the FRONT of this
#         library's own include path so zephyr_compat/ shadows Zephyr system
#         headers (e.g. the POSIX sys/time.h) and OSAL headers are directly
#         reachable without a path prefix.
target_include_directories(${ZEPHYR_CURRENT_LIBRARY} BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/zephyr_compat
    ${CMAKE_CURRENT_SOURCE_DIR}/inc/OSAL
)

# ---------------------------------------------------------------------------
# OSAL layer — Zephyr port
#
# fastrpc_ioctl_zephyr.c implements the same ioctl_*() interface as the
# Linux fastrpc_ioctl.c but calls the Zephyr fastrpc driver API directly
# (see <zephyr/drivers/fastrpc.h>) instead of issuing ioctl() syscalls.
#
# src/fastrpc_ioctl.c is intentionally EXCLUDED; the OSAL file below
# provides all the same symbols.
# ---------------------------------------------------------------------------
zephyr_library_sources(
    src/OSAL/fastrpc_ioctl_zephyr.c
)

# ---------------------------------------------------------------------------
# Core UMD sources
#
# NOTE: Files that still carry heavy Linux-specific dependencies
# (dirent, mmap, eventfd, inotify, dlopen, …) are listed but guarded by
# CONFIG_FASTRPC_UMD_FULL so the initial port compiles cleanly.
# Uncomment / enable CONFIG_FASTRPC_UMD_FULL as each file is ported.
# ---------------------------------------------------------------------------

# --- Utility / helper modules (minimal Linux deps, port-friendly) ----------
zephyr_library_sources(
    src/BufBound.c
    src/std_dtoa.c
    src/gpls.c
    src/pl_list.c
    src/platform_libs.c
    src/fastrpc_log.c
    src/fastrpc_cap.c
    src/fastrpc_config.c
    src/fastrpc_context.c
    src/fastrpc_perf.c
    src/fastrpc_pm.c
    src/fastrpc_procbuf.c
    src/fastrpc_notif.c
    src/mod_table.c
)

# ---------------------------------------------------------------------------
# rpcmem — Zephyr port  (OSAL layer)
#
# src/OSAL/rpcmem_zephyr.c replaces rpcmem_linux.c for Zephyr builds.  It
# provides the identical public rpcmem_*() API but uses the Zephyr kernel
# heap (k_malloc / k_free) instead of the Linux DMA-heap / FastRPC ioctl
# path.
#
# The allocation strategy is pluggable via rpcmem_register_backend()
# (see inc/OSAL/rpcmem_backend.h), so it can be swapped for a
# contiguous-memory allocator or a future DMA-buf port without modifying
# this file.
#
# rpcmem_linux.c is intentionally EXCLUDED from Zephyr builds.
# ---------------------------------------------------------------------------
zephyr_library_sources(
    src/OSAL/rpcmem_zephyr.c
    # pthread_stubs_zephyr.c provides a weak pthread_kill() implementation.
    # Zephyr 4.3.0 declares pthread_kill() in its POSIX signal.h when
    # CONFIG_POSIX_SIGNALS is enabled but does not supply a function body.
    # fastrpc_notif.c calls pthread_kill() as a last-resort fallback; the
    # weak symbol satisfies the linker and is overridden if Zephyr ever adds
    # a real implementation.
    src/OSAL/pthread_stubs_zephyr.c
)

# --- Modules that need further porting (enable with CONFIG_FASTRPC_UMD_FULL) ---
zephyr_library_sources_ifdef(CONFIG_FASTRPC_UMD_FULL
    # dsprpcd_zephyr.c calls adsp_default_listener_start() which is defined
    # in adsp_default_listener_zephyr.c (also in this FULL block).
    # Compiling it without CONFIG_FASTRPC_UMD_FULL causes an undefined-
    # reference linker error on builds that do not enable the FULL set.
    src/OSAL/dsprpcd_zephyr.c

    # sensors_user_client.c defines sensors_user_client_run() which is
    # called by frpc_sensors_user_thread() in dsprpcd_zephyr.c.
    src/OSAL/sensors_user_client.c

    # adsp_default_listener_zephyr.c calls get_domain_from_name,
    # remote_handle64_open, remote_handle_open, adsp_default_listener1_open,
    # adsp_default_listener1_register, adsp_default_listener_register, etc.
    # All of these symbols are defined in the FULL source files below.
    # Compiling it without CONFIG_FASTRPC_UMD_FULL causes undefined-reference
    # linker errors on builds that do not enable the FULL set (e.g. qemu).
    src/OSAL/adsp_default_listener_zephyr.c

    # log_config.c uses inotify + eventfd — Linux-specific filesystem
    # event notification; not available on Zephyr without a porting layer.
    src/log_config.c

    # Uses open()/close()/mmap()/dlopen() — needs Zephyr VFS / rpcmem port
    src/fastrpc_apps_user.c
    src/fastrpc_mem.c
    src/std_path.c
    #src/fastrpc_config_parser.c [yaml not supported]
    src/dspsignal.c

    # Android/Linux listener daemon — needs significant rework for Zephyr
    src/listener_android.c

    # File-system-heavy skel/imp files
    src/apps_std_imp.c
    src/apps_std_skel.c
    src/apps_mem_imp.c
    src/apps_mem_skel.c
    src/apps_remotectl_skel.c

    # RPC stub/skel glue (auto-generated; depend on the above)
    src/adsp_current_process_stub.c
    src/adsp_current_process1_stub.c
    #src/adsp_default_listener.c
    src/adsp_default_listener_stub.c
    src/adsp_default_listener1_stub.c
    src/adsp_listener_stub.c
    src/adsp_listener1_stub.c
    src/adsp_perf_stub.c
    src/adsp_perf1_stub.c
    src/adspmsgd_adsp_stub.c
    src/adspmsgd_adsp1_stub.c
    src/adspmsgd_apps_skel.c
    src/adspmsgd_printf.c
    src/adspmsgd.c
    src/remotectl_stub.c
    src/remotectl1_stub.c

    # DSP queue sources are excluded from Zephyr builds — dspqueue_cpu.c
    # calls fastrpc_munmap() with a Linux-specific signature that does not
    # match the Zephyr fastrpc API.  Enable CONFIG_FASTRPC_UMD_DSPQUEUE
    # only after the dspqueue layer has been ported to Zephyr.
    # src/dspqueue/dspqueue_cpu.c
    # src/dspqueue/dspqueue_rpc_stub.c
)

# src/dsprpcd.c — Linux daemon entry point; excluded entirely for Zephyr.

# ---------------------------------------------------------------------------
# Compile definitions
# ---------------------------------------------------------------------------
zephyr_library_compile_definitions(
    # Enable FARF logging levels used across the UMD
    FARF_ERROR=1
    FARF_HIGH=1
    FARF_MED=1
    FARF_LOW=1
    FARF_CRITICAL=1
    FARF_RUNTIME_RPC_HIGH=1

    # Enable VERIFY macro error/warning printing
    VERIFY_PRINT_ERROR
    VERIFY_PRINT_ERROR_ALWAYS
    VERIFY_PRINT_WARN

    # Upstream (non-downstream) kernel path — matches fastrpc_ioctl_zephyr.c
    # which implements the upstream ioctl interface

    # Disable Android systrace (ATRACE_BEGIN / ATRACE_END) on Zephyr.
    # Without this, fastrpc_trace.h expands FASTRPC_ATRACE_BEGIN/END into
    # calls to ATRACE_BEGIN() / ATRACE_END() which are Android-only symbols
    # and produce undefined-reference linker errors on Zephyr.
    DISABLE_ATRACE
)

# ---------------------------------------------------------------------------
# Link against the Zephyr kernel (provides device model, POSIX layer, etc.)
# ---------------------------------------------------------------------------
zephyr_library_link_libraries(zephyr_interface)
