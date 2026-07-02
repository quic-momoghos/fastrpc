// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif /* VERIFY_PRINT_ERROR */

#define FARF_HIGH 1
#define FARF_LOW 1

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#ifdef __ZEPHYR__
/* On Zephyr, eventfd is provided by the Zephyr POSIX layer (CONFIG_EVENTFD=y).
 * Use <zephyr/posix/sys/eventfd.h> instead of the Linux <sys/eventfd.h>.
 * This is the same include used in adsp_default_listener_zephyr.c. */
#include <zephyr/posix/sys/eventfd.h>
#else
#include <sys/eventfd.h>
#endif
#include <unistd.h>

#ifdef __ZEPHYR__
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(listener_android, CONFIG_FASTRPC_LOG_LEVEL);
#endif

#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "rpcmem_internal.h"
#include "adsp_listener.h"
#include "adsp_listener1.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "listener_buf.h"
#include "mod_table.h"
#include "platform_libs.h"
#include "rpcmem.h"
#include "shared.h"
#include "verify.h"
#include "fastrpc_hash_table.h"

#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
/*
 * Zephyr pthread_create() requires an explicit stack; passing attr = NULL
 * (as the Linux path does) returns EINVAL because Zephyr's pthread layer
 * marks a default-constructed attr as "not runnable" when no stack has been
 * set.  Define one stack slot per effective domain so each call to
 * listener_android_domain_init() gets its own pre-allocated stack.
 *
 * Stack size is tunable via CONFIG_FASTRPC_LISTENER_STACK_SIZE; the default
 * of 8 KB is sufficient for the listener thread's call depth.
 */
#ifndef CONFIG_FASTRPC_LISTENER_STACK_SIZE
/*
 * 16 KB required: the listener() function places remote_arg args[512] on the
 * stack.  On ARM64, remote_arg = 16 bytes → args[512] = 8192 bytes alone.
 * Adding other locals (~136 bytes) + call overhead (~64 bytes) = ~8392 bytes
 * total, which overflows an 8192-byte stack causing a crash.
 * 16 KB provides sufficient headroom.
 */
#define CONFIG_FASTRPC_LISTENER_STACK_SIZE 16384
#endif
/*
 * Number of concurrent listener thread stacks to pre-allocate.
 * Each slot costs CONFIG_FASTRPC_LISTENER_STACK_SIZE bytes in the noinit
 * section.  Keep this at 1 for targets with limited RAM; increase it when
 * multiple domains must run listener threads simultaneously.
 */
#ifndef CONFIG_FASTRPC_LISTENER_MAX_DOMAINS
#define CONFIG_FASTRPC_LISTENER_MAX_DOMAINS 4
#endif
#define LISTENER_MAX_DOMAINS CONFIG_FASTRPC_LISTENER_MAX_DOMAINS
K_THREAD_STACK_ARRAY_DEFINE(listener_thread_stacks, LISTENER_MAX_DOMAINS,
			     CONFIG_FASTRPC_LISTENER_STACK_SIZE);
#endif /* __ZEPHYR__ */

typedef struct {
  pthread_t thread;
  int eventfd;
  int update_requested;
  int params_updated;
  sem_t *r_sem;
  remote_handle64 adsp_listener1_handle;
#ifdef __ZEPHYR__
  struct k_sem exit_sem; /* Zephyr-native exit signal — replaces eventfd */
#endif
  ADD_DOMAIN_HASH();
} listener_config;

DECLARE_HASH_TABLE(listener, listener_config);

extern void set_thread_context(int domain);

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_remotectl_open)(const char *name, uint32_t *handle, char *dlStr,
                                 int dlerrorLen,
                                 int *dlErr) __QAIC_IMPL_ATTRIBUTE {
  int domain = get_current_domain();
  int nErr = AEE_SUCCESS;
  remote_handle64 local;
  VERIFY(AEE_SUCCESS ==
         (nErr = mod_table_open(name, handle, dlStr, dlerrorLen, dlErr)));
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              REVERSE_HANDLE_LIST_PREPEND, domain, (remote_handle)*handle, &local, NULL)));
bail:
  return nErr;
}

__QAIC_IMPL_EXPORT int
__QAIC_IMPL(apps_remotectl_close)(uint32_t handle, char *errStr, int errStrLen,
                                  int *dlErr) __QAIC_IMPL_ATTRIBUTE {
  int domain = get_current_domain();
  int nErr = AEE_SUCCESS;

  if (AEE_SUCCESS !=
      (nErr = mod_table_close(handle, errStr, errStrLen, dlErr))) {
    if(!is_process_exiting(domain)) {
      FARF(ERROR,
          "Error 0x%x: %s: mod_table_close failed for handle:0x%x (dlErr %s)",
          nErr, __func__, handle, (char *)dlErr);
    }
    goto bail;
  }
  VERIFY(AEE_SUCCESS ==
         (nErr = fastrpc_update_module_list(
              REVERSE_HANDLE_LIST_DEQUEUE, domain, (remote_handle)handle, NULL, NULL)));
bail:
  return nErr;
}

#define RPC_FREEIF(buf)                                                        \
  do {                                                                         \
    if (buf) {                                                                 \
      rpcmem_free_internal(buf);                                               \
      buf = 0;                                                                 \
    }                                                                          \
  } while (0)

static __inline void *rpcmem_realloc(int heapid, uint32_t flags, void *buf,
                                     int oldsize, size_t size) {
  void *bufnew = rpcmem_alloc_internal(heapid, flags, size);
  if (buf && bufnew) {
    memmove(bufnew, buf, STD_MIN(oldsize, size));
    rpcmem_free_internal(buf);
    buf = NULL;
  }
  return bufnew;
}

#define MIN_BUF_SIZE 0x1000
#define ALIGNB(sz) ((sz) == 0 ? MIN_BUF_SIZE : _SBUF_ALIGN((sz), MIN_BUF_SIZE))

static void listener(listener_config *me) {
  int nErr = AEE_SUCCESS, i = 0, domain = me->domain, ref = 0;
  adsp_listener1_invoke_ctx ctx = 0;
  uint8_t *outBufs = 0;
  int outBufsLen = 0, outBufsCapacity = 0;
  uint8_t *inBufs = 0;
  int inBufsLen = 0, inBufsLenReq = 0;
  int result = -1, bufs_len = 0;
  adsp_listener1_remote_handle handle = -1;
  uint32_t sc = 0;
  const char *eheap = getenv("ADSP_LISTENER_HEAP_ID");
  int heapid = eheap == 0 ? -1 : atoi(eheap);
  const char *eflags = getenv("ADSP_LISTENER_HEAP_FLAGS");
  uint32_t flags = eflags == 0 ? 0 : (uint32_t)atoi(eflags);
  const char *emin = getenv("ADSP_LISTENER_MEM_CACHE_SIZE");
  int cache_size = emin == 0 ? 0 : atoi(emin);
  remote_arg args[512];
  struct sbuf buf;
  eventfd_t event = 0xff;

  FARF(ALWAYS, "%s thread starting tid: %p \n", __func__, (void *)k_current_get());
  memset(args, 0, sizeof(args));
  if (eheap || eflags || emin) {
    FARF(RUNTIME_RPC_HIGH,
         "listener using ion heap: %d flags: %x cache: %lld\n", (int)heapid,
         (int)flags, cache_size);
  }

  do {
  invoke:
    sc = 0xffffffff;
    if (result != 0) {
      outBufsLen = 0;
    }
    FARF(RUNTIME_RPC_HIGH,
         "%s responding 0x%x for ctx 0x%x, handle 0x%x, sc 0x%x", __func__,
         result, ctx, handle, sc);
    FASTRPC_PUT_REF(domain);
    if (me->adsp_listener1_handle != INVALID_HANDLE) {
      nErr = __QAIC_HEADER(adsp_listener1_next2)(
          me->adsp_listener1_handle, ctx, result, outBufs, outBufsLen, &ctx,
          &handle, &sc, inBufs, inBufsLen, &inBufsLenReq);
    } else {
      nErr = __QAIC_HEADER(adsp_listener_next2)(
          ctx, result, outBufs, outBufsLen, &ctx, &handle, &sc, inBufs,
          inBufsLen, &inBufsLenReq);
    }
    if (nErr) {
      if (nErr == AEE_EINTERRUPTED) {
        /* UserPD in CPZ migration. Keep retrying until migration is complete.
         * Also reset the context, as previous context is invalid after CPZ
         * migration
        */
        ctx = 0;
        result = -1;
        goto invoke;
      } else if (nErr == (DSP_AEE_EOFFSET + AEE_EBADSTATE)) {
          /* UserPD in irrecoverable bad state. Exit listener */
          goto bail;
      }
      /* For any other error, retry once and exit if error seen again */
      if (me->adsp_listener1_handle != INVALID_HANDLE) {
        nErr = __QAIC_HEADER(adsp_listener1_next2)(
            me->adsp_listener1_handle, ctx, nErr, 0, 0, &ctx, &handle, &sc,
            inBufs, inBufsLen, &inBufsLenReq);
      } else {
        nErr = __QAIC_HEADER(adsp_listener_next2)(ctx, nErr, 0, 0, &ctx,
                                                  &handle, &sc, inBufs,
                                                  inBufsLen, &inBufsLenReq);
      }
      if (nErr) {
        FARF(RUNTIME_HIGH,
               "Error 0x%x: %s response with result 0x%x for ctx 0x%x, handle "
               "0x%x, sc 0x%x failed\n",
               nErr, __func__, result, ctx, handle, sc);
        goto bail;
      }
    }
#ifdef __ZEPHYR__
    /*
     * adsp_listener1_next2() is a stub (transport not ready): it returns 0
     * but never writes ctx/handle/sc.  sc retains the 0xffffffff sentinel
     * set at the top of the loop.  Without this guard the loop spins at
     * 100% CPU — sc=0xffffffff has INHANDLES=0xf > 0, so the INHANDLES
     * check fires, sets result=AEE_EBADPARM, and goto invoke loops forever.
     */
    if (handle == 0 && sc == 0) {
        FARF(ALWAYS, "%s: adsp_listener1_next2 returned handle=0 sc=0"
             " — DSP not up (stub output), exiting listener\n", __func__);
        goto bail;
    }
#endif /* __ZEPHYR__ */
    FASTRPC_GET_REF(domain);
    if (__builtin_smul_overflow(inBufsLenReq, 2, &bufs_len)) {
      FARF(ERROR,
           "Error: %s: overflow occurred while multiplying input buffer size: "
           "%d * 2 = %d for handle 0x%x, sc 0x%x",
           __func__, inBufsLenReq, bufs_len, handle, sc);
      result = AEE_EBADSIZE;
      goto invoke;
    }
    if (ALIGNB(bufs_len) < inBufsLen && inBufsLen > cache_size) {
      void *buf;
      int size = ALIGNB(bufs_len);
      if (NULL ==
          (buf = rpcmem_realloc(heapid, flags, inBufs, inBufsLen, size))) {
        result = AEE_ENORPCMEMORY;
        FARF(RUNTIME_RPC_HIGH, "rpcmem_realloc shrink failed");
        goto invoke;
      }
      inBufs = buf;
      inBufsLen = size;
    }
    if (inBufsLenReq > inBufsLen) {
      void *buf;
      int req;
      int oldLen = inBufsLen;
      int size = _SBUF_ALIGN(inBufsLenReq, MIN_BUF_SIZE);
      if (AEE_SUCCESS ==
          (buf = rpcmem_realloc(heapid, flags, inBufs, inBufsLen, size))) {
        result = AEE_ENORPCMEMORY;
        FARF(ERROR, "rpcmem_realloc failed");
        goto invoke;
      }
      inBufs = buf;
      inBufsLen = size;
      if (me->adsp_listener1_handle != INVALID_HANDLE) {
        result = __QAIC_HEADER(adsp_listener1_get_in_bufs2)(
            me->adsp_listener1_handle, ctx, oldLen, inBufs + oldLen,
            inBufsLen - oldLen, &req);
      } else {
        result = __QAIC_HEADER(adsp_listener_get_in_bufs2)(
            ctx, oldLen, inBufs + oldLen, inBufsLen - oldLen, &req);
      }
      if (AEE_SUCCESS != result) {
        FARF(RUNTIME_RPC_HIGH, "adsp_listener_invoke_get_in_bufs2 failed  %x",
             result);
        goto invoke;
      }
      if (req > inBufsLen) {
        result = AEE_EBADPARM;
        FARF(RUNTIME_RPC_HIGH,
             "adsp_listener_invoke_get_in_bufs2 failed, size is invalid req %d "
             "inBufsLen %d result %d",
             req, inBufsLen, result);
        goto invoke;
      }
    }
    if (REMOTE_SCALARS_INHANDLES(sc) + REMOTE_SCALARS_OUTHANDLES(sc) > 0) {
      result = AEE_EBADPARM;
      goto invoke;
    }

    sbuf_init(&buf, 0, inBufs, inBufsLen);
    unpack_in_bufs(&buf, args, REMOTE_SCALARS_INBUFS(sc));
    unpack_out_lens(&buf, args + REMOTE_SCALARS_INBUFS(sc),
                    REMOTE_SCALARS_OUTBUFS(sc));

    sbuf_init(&buf, 0, 0, 0);
    pack_out_bufs(&buf, args + REMOTE_SCALARS_INBUFS(sc),
                  REMOTE_SCALARS_OUTBUFS(sc));
    outBufsLen = sbuf_needed(&buf);

    if (__builtin_smul_overflow(outBufsLen, 2, &bufs_len)) {
      FARF(ERROR,
           "%s: Overflow occured while multiplying output buffer size: %d * 2 "
           "= %d",
           __func__, outBufsLen, bufs_len);
      result = AEE_EBADSIZE;
      goto invoke;
    }
    if (ALIGNB(bufs_len) < outBufsCapacity && outBufsCapacity > cache_size) {
      void *buf;
      int size = ALIGNB(bufs_len);
      if (NULL == (buf = rpcmem_realloc(heapid, flags, outBufs, outBufsCapacity,
                                        size))) {
        result = AEE_ENORPCMEMORY;
        FARF(RUNTIME_RPC_HIGH, "listener rpcmem_realloc shrink failed");
        goto invoke;
      }
      outBufs = buf;
      outBufsCapacity = size;
    }
    if (outBufsLen > outBufsCapacity) {
      void *buf;
      int size = ALIGNB(outBufsLen);
      if (NULL == (buf = rpcmem_realloc(heapid, flags, outBufs, outBufsCapacity,
                                        size))) {
        result = AEE_ENORPCMEMORY;
        FARF(ERROR, "listener rpcmem_realloc failed");
        goto invoke;
      }
      outBufs = buf;
      outBufsLen = size;
      outBufsCapacity = size;
    }
    sbuf_init(&buf, 0, outBufs, outBufsLen);
    pack_out_bufs(&buf, args + REMOTE_SCALARS_INBUFS(sc),
                  REMOTE_SCALARS_OUTBUFS(sc));
    result = mod_table_invoke(handle, sc, args);
    if (result && is_process_exiting(domain))
      result = AEE_EBADSTATE; // override result as process is exiting
#ifdef __ZEPHYR__
    /*
     * On Zephyr, only exit the listener loop for fatal DSP-side errors that
     * indicate the DSP is truly gone (AEE_EBADSTATE).  All other mod_table
     * failures — including AEE_EBADPARM from the DSP-side probe invocation
     * (handle=0, sc=0) that arrives while the DSP is still initialising —
     * are non-fatal: send the error code back to the DSP via the next
     * adsp_listener1_next2() call (result != 0 path at the top of the loop)
     * and wait for the next valid invocation, exactly as Linux does.
     *
     * Background: when the DSP first comes up it sends handle=0, sc=0.
     * mod_table_invoke() dispatches to apps_remotectl_skel_invoke() (handle 0
     * is registered as the "apps_remotectl" const handle).  The skel asserts
     * INBUFS==2 but sc=0 has INBUFS=0, so it returns AEE_EBADPARM (0xe).
     * That error is harmless — the DSP discards it and sends the real first
     * invocation.  Exiting here was wrong: it caused a 100 ms restart loop
     * (visible in the log at 00:00:32 and 00:01:12) because dsprpcd_zephyr.c
     * unconditionally restarts the daemon on any non-zero return.
     *
     * The only case where we must exit is AEE_EBADSTATE, which means the DSP
     * UserPD is in an irrecoverable state and no further invocations will
     * arrive.  That case is already handled above by is_process_exiting().
     */
    if (result == AEE_EBADSTATE) {
      FARF(ERROR, "%s: mod_table_invoke returned AEE_EBADSTATE for handle 0x%x"
           " sc 0x%x — DSP UserPD is gone, exiting listener loop\n",
           __func__, handle, sc);
      goto bail;
    }
#endif /* __ZEPHYR__ */
  } while (1);
bail:
  me->adsp_listener1_handle = INVALID_HANDLE;
  RPC_FREEIF(outBufs);
  RPC_FREEIF(inBufs);
  if (nErr != AEE_SUCCESS) {
    if(!is_process_exiting(domain)) {
      FARF(ERROR,
          "Error 0x%x: %s response with result 0x%x for ctx 0x%x, handle 0x%x, "
          "sc 0x%x failed : listener thread exited (errno %s)",
          nErr, __func__, result, ctx, handle, sc, strerror(errno));
    }
  }
#ifdef __ZEPHYR__
  k_sem_give(&me->exit_sem);
  FARF(ALWAYS, "%s thread exiting tid %p \n", __func__, (void *)k_current_get());
#else
  for (i = 0; i < RETRY_WRITE; i++) {
    if (AEE_SUCCESS == (nErr = eventfd_write(me->eventfd, event))) {
      break;
    }
    // Sleep for 1 sec before retry writing
    sleep(1);
  }
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF(
        "Error 0x%x : Writing to listener event_fd %d failed (errno %s)", nErr,
        me->eventfd, strerror(errno));
  }
  FARF(ALWAYS, "%s thread exiting\n", __func__);
  dlerror();
#endif /* __ZEPHYR__ */
}

extern int apps_remotectl_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int apps_std_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int apps_mem_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int adspmsgd_apps_skel_invoke(uint32_t _sc, remote_arg *_pra);
extern int fastrpc_set_remote_uthread_params(int domain);

PL_DEP(mod_table);
PL_DEP(apps_std);

static void *listener_start_thread(void *arg) {
  int nErr = AEE_SUCCESS;
  listener_config *me = (listener_config *)arg;
  int domain = me->domain;
  remote_handle64 adsp_listener1_handle = INVALID_HANDLE;
  LOG_INF("listener start thread tid: %p.", (void *)k_current_get());

  /*
   * Need to set TLS key of listener thread to right domain.
   * Otherwise, the init2() call will go to default domain.
   */
  set_thread_context(domain);
  if ((adsp_listener1_handle = get_adsp_listener1_handle(domain)) != INVALID_HANDLE) {
    nErr = __QAIC_HEADER(adsp_listener1_init2)(adsp_listener1_handle);
    if ((nErr == DSP_AEE_EOFFSET + AEE_ERPC) ||
        nErr == DSP_AEE_EOFFSET + AEE_ENOSUCHMOD) {
      FARF(ALWAYS,
           "Warning: %s domain support is unavailable on DSP in listener",
           __func__);
      fastrpc_update_module_list(DOMAIN_LIST_DEQUEUE, domain, _const_adsp_listener1_handle, NULL, NULL);
      adsp_listener1_handle = INVALID_HANDLE;
      VERIFY(AEE_SUCCESS == (nErr = __QAIC_HEADER(adsp_listener_init2)()));
    } else if (nErr == AEE_SUCCESS) {
      me->adsp_listener1_handle = adsp_listener1_handle;
      LOG_INF("adsp_listener1_handle saved.");
    }
  } else {
    VERIFY(AEE_SUCCESS == (nErr = __QAIC_HEADER(adsp_listener_init2)()));
  }

  if (me->update_requested) {
    /* Update parameters on DSP and signal main thread to proceed */
    me->params_updated = fastrpc_set_remote_uthread_params(domain);
    sem_post(me->r_sem);
    VERIFY(AEE_SUCCESS == (nErr = me->params_updated));
  }
  listener(me);
bail:
  me->adsp_listener1_handle = INVALID_HANDLE;
  if (nErr != AEE_SUCCESS) {
    sem_post(me->r_sem);
    VERIFY_EPRINTF("Error 0x%x: %s failed for domain %d\n", nErr, __func__,
                   domain);
  }
  return (void *)(uintptr_t)nErr;
}

void listener_android_deinit(void) {
  HASH_TABLE_CLEANUP(listener_config);
  PL_DEINIT(mod_table);
  PL_DEINIT(apps_std);
}

int listener_android_init(void) {
  int nErr = 0;

  HASH_TABLE_INIT(listener_config);

  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(mod_table)));
  VERIFY(AEE_SUCCESS == (nErr = PL_INIT(apps_std)));
  VERIFY(AEE_SUCCESS == (nErr = mod_table_register_const_handle(
                             0, "apps_remotectl", apps_remotectl_skel_invoke)));
  VERIFY(AEE_SUCCESS ==
         (nErr = mod_table_register_static("apps_std", apps_std_skel_invoke)));
  VERIFY(AEE_SUCCESS ==
         (nErr = mod_table_register_static("apps_mem", apps_mem_skel_invoke)));
  VERIFY(AEE_SUCCESS == (nErr = mod_table_register_static(
                             "adspmsgd_apps", adspmsgd_apps_skel_invoke)));
bail:
  if (nErr != AEE_SUCCESS) {
    listener_android_deinit();
    VERIFY_EPRINTF("Error %x: fastrpc listener initialization error", nErr);
  }
  return nErr;
}

void listener_android_domain_deinit(int domain) {
  listener_config *me = NULL;

  GET_HASH_NODE(listener_config, domain, me);
  if (!me)
    return;

  FARF(RUNTIME_RPC_HIGH, "fastrpc listener joining to exit");
  if (me->thread) {
    pthread_join(me->thread, 0);
    me->thread = 0;
  }
  FARF(RUNTIME_RPC_HIGH, "fastrpc listener joined");
  me->adsp_listener1_handle = INVALID_HANDLE;
#ifdef __ZEPHYR__
  me->eventfd = -1; /* k_sem needs no close */
#else
  if (me->eventfd != -1) {
    close(me->eventfd);
    FARF(RUNTIME_RPC_HIGH, "Closed Listener event_fd %d for domain %d\n",
         me->eventfd, domain);
    me->eventfd = -1;
  }
#endif /* __ZEPHYR__ */
}

int listener_android_domain_init(int domain, int update_requested,
                                 sem_t *r_sem) {
  listener_config *me = NULL;
  int nErr = AEE_SUCCESS;

  GET_HASH_NODE(listener_config, domain, me);
  if (!me) {
    ALLOC_AND_ADD_NEW_NODE_TO_TABLE(listener_config, domain, me);
  }

  me->eventfd = -1;
#ifdef __ZEPHYR__
  k_sem_init(&me->exit_sem, 0, 1);
  me->eventfd = domain; /* dummy: domain ID, not a real fd */
  FARF(RUNTIME_RPC_HIGH, "Initialized k_sem exit_sem for domain %d\n", domain);
#else
  VERIFYC(-1 != (me->eventfd = eventfd(0, 0)), AEE_EBADPARM);
  FARF(RUNTIME_RPC_HIGH, "Opened Listener event_fd %d for domain %d\n",
       me->eventfd, domain);
#endif /* __ZEPHYR__ */
  me->update_requested = update_requested;
  me->r_sem = r_sem;
  me->adsp_listener1_handle = INVALID_HANDLE;
  me->domain = domain;
#ifdef __ZEPHYR__
  /*
   * On Zephyr, pthread_create() with attr = NULL returns EINVAL because
   * the default-constructed attr has no stack and is therefore not
   * "runnable".  Use the pre-allocated per-domain stack defined above.
   */
  {
    pthread_attr_t attr;
    /*
     * Assign stack slots with a monotonic counter rather than using the
     * raw domain value as an index.
     *
     * Root cause of the crash when rootpd + audiopd run simultaneously:
     *   rootpd  uses eff_domain_id=8,  old logic: (8  < 1) ? 8  : 0 -> slot 0
     *   audiopd uses eff_domain_id=16, old logic: (16 < 1) ? 16 : 0 -> slot 0
     * Both threads shared listener_thread_stacks[0] and corrupted each other.
     *
     * Fix: atomic counter wrapping at LISTENER_MAX_DOMAINS guarantees each
     * concurrent listener_android_domain_init() call gets a distinct slot.
     * LISTENER_MAX_DOMAINS defaults to 2 (rootpd + audiopd) via Kconfig.
     */
    static atomic_t listener_stack_counter;
    int stack_idx = (int)(atomic_inc(&listener_stack_counter)
                          % (atomic_val_t)LISTENER_MAX_DOMAINS);

    pthread_attr_init(&attr);
#ifdef CONFIG_FASTRPC_LISTENER_INHERIT_PRIO
    /*
     * Inherit the calling daemon thread priority so the listener thread
     * is scheduled immediately after pthread_create().
     *
     * Without this, the listener thread runs at the POSIX default
     * priority (Zephyr prio 126 — lowest application priority).
     * When a higher-priority daemon restart loop (prio 7) runs
     * concurrently on another domain, the listener thread at prio 126
     * is never scheduled — causing a 3+ minute starvation block until
     * the other daemon exits and permanently frees a CPU core.
     *
     * Setting PTHREAD_INHERIT_SCHED explicitly after pthread_attr_init
     * ensures k_thread_create uses the calling thread priority (7),
     * eliminating the starvation entirely.
     */
    pthread_attr_setinheritsched(&attr, PTHREAD_INHERIT_SCHED);
#endif /* CONFIG_FASTRPC_LISTENER_INHERIT_PRIO */
    pthread_attr_setstack(&attr,
                          listener_thread_stacks[stack_idx],
                          CONFIG_FASTRPC_LISTENER_STACK_SIZE);
    nErr = pthread_create(&me->thread, &attr, listener_start_thread,
                          (void *)me);
    pthread_attr_destroy(&attr);
    VERIFY(AEE_SUCCESS == nErr);
    LOG_INF("%s: listener start thread called tid %p.", __func__, (void *)k_current_get());
  }
#else
  VERIFY(AEE_SUCCESS ==
         (nErr = pthread_create(&me->thread, 0, listener_start_thread,
                                (void *)me)));
#endif /* __ZEPHYR__ */

  if (me->update_requested) {
    /*
     * Semaphore initialized to 0. If main thread reaches wait first,
     * then it will wait for listener to increment semaphore to 1.
     * If listener posted semaphore first, then this wait will decrement
     * semaphore to 0 and proceed.
     */
    sem_wait(me->r_sem);
    VERIFY(AEE_SUCCESS == (nErr = me->params_updated));
  }
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error 0x%x: %s failed for domain %d\n", nErr, __func__,
                   domain);
    listener_android_domain_deinit(domain);
  }
  return nErr;
}

int close_reverse_handle(remote_handle64 h, char *dlerr, int dlerrorLen,
                         int *dlErr) {
  return apps_remotectl_close((uint32_t)h, dlerr, dlerrorLen, dlErr);
}

int listener_android_geteventfd(int domain, int *fd) {
  listener_config *me = NULL;
  int nErr = 0;

  GET_HASH_NODE(listener_config, domain, me);
  VERIFYC(me, AEE_ERESOURCENOTFOUND);
  VERIFYC(-1 != me->eventfd, AEE_EBADPARM);
  *fd = me->eventfd;
bail:
  if (nErr != AEE_SUCCESS) {
    VERIFY_EPRINTF("Error %x: listener android getevent file descriptor failed "
                   "for domain %d\n",
                   nErr, domain);
  }
  return nErr;
}

#ifdef __ZEPHYR__
int listener_android_wait_exit(int domain) {
  listener_config *me = NULL;
  GET_HASH_NODE(listener_config, domain, me);
  if (!me) return -1;
  k_sem_take(&me->exit_sem, K_FOREVER);
  return 0;
}
#endif /* __ZEPHYR__ */

PL_DEFINE(listener_android, listener_android_init, listener_android_deinit)
