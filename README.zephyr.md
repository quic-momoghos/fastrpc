 # FastRPC — Zephyr 

  This branch (`zephyr-fastrpc`) contains the Zephyr RTOS code of the Qualcomm
  FastRPC userspace library.

  ## What's New vs Upstream `main`

  ### New Directories

  | Path | Purpose |
  |---|---|
  | `src/OSAL/` | Zephyr-specific OS abstraction: thread daemons, ioctl bridge, rpcmem, sensors client |
  | `zephyr_compat/` | Shim headers that map Linux POSIX/ioctl APIs to Zephyr equivalents |
  | `kmd/` | Kernel-mode driver: `fastrpc_zephyr.c` (Zephyr), `fastrpc_linux.c` (Linux reference) |
  | `kmd/inc/` | KMD API header: `fastrpc.h` (Zephyr driver vtable) |

  ### Modified Files (vs upstream `main`)

  | File | Change |
  |---|---|
  | `src/fastrpc_apps_user.c` | `__ZEPHYR__` guards, Zephyr session model, open/close device node |
  | `inc/fastrpc_common.h` | Constants unchanged; Zephyr build picks up via CMake |
  | `inc/fastrpc_internal.h` | `handle_list` struct unchanged; macros unchanged |
  | `src/OSAL/fastrpc_ioctl_zephyr.c` | Replaces Linux ioctl() calls with Zephyr driver API calls |

  ## Session Model

  Linux uses `open("/dev/fastrpc-cdsp")` → `file->private_data = fl`.
  Zephyr uses `fastrpc_drv_session_open(dev, eff_domain_id)` →
  `data->sessions[eff_domain_id] = fl`.

  See `kmd/inc/fastrpc.h` for the full driver API vtable.

  ## Known Limitations

  - `fl->sctx` is NULL — no SMMU context bank allocation yet
  - `msg->client_id` is hardcoded to 0 — all sessions look identical to DSP
  - Transport (rpmsg/glink) is stub-completed — real send not yet wired
  - `cctx->users` list not populated — transport teardown notification not implemented

  ## Building

  Zephyr west build integration is in the parent repo (wasp_proc).
  This directory is consumed as a Zephyr module via `CMakeLists.txt`.

