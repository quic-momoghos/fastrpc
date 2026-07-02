// Copyright (c) Qualcomm Technologies, Inc.
// SPDX-License-Identifier: BSD-3-Clause

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <stdio.h>
#include <string.h>

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "verify.h"
#include "fastrpc_common.h"

/* No dynamic loading in Zephyr */
extern int adsp_default_listener_start(int argc, char *argv[]);

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif
#define VERIFY_PRINT_INFO 0

/*
 * Semaphore used to synchronize the listener threads with the main thread.
 *
 * Initial count = 0  → both threads block on k_sem_take() at entry.
 * main() calls frpc_listener_signal() at the end (before return 0), which
 * posts this semaphore twice — once for frpc_listener_thread (rootpd) and
 * once for frpc_audiopd_thread (audiopd).
 *
 * Max count = 4: rootpd + audiopd + sensorspd + sensors user client.
 */
K_SEM_DEFINE(frpc_listener_sem, 0, 4);

/**
 * frpc_listener_signal() - Signal both listener threads to start.
 *
 * Call this from main() just before return 0.  Both listener threads are
 * waiting on frpc_listener_sem and will unblock immediately.
 *
 * Declared extern in main.c:
 *   extern void frpc_listener_signal(void);
 */
void frpc_listener_signal(void)
{
    printk("frpc_listener_signal: main done, starting listener daemons\n");
    k_sem_give(&frpc_listener_sem);   /* unblocks frpc_listener_thread  (rootpd)  */
    //k_sleep(K_MSEC(50));              /* yield CPU so watchdog kick thread can run */
    k_sem_give(&frpc_listener_sem);   /* unblocks frpc_audiopd_thread   (audiopd) */
    //k_sleep(K_MSEC(50));              /* yield CPU so watchdog kick thread can run */
    k_sem_give(&frpc_listener_sem);   /* unblocks frpc_sensorspd_thread (sensorspd) */
    //k_sleep(K_MSEC(50));              /* yield CPU so watchdog kick thread can run */
    k_sem_give(&frpc_listener_sem);   /* unblocks frpc_sensors_user_thread */
}

/**
 * start_listener_daemon() - Listener daemon for the ADSP Root PD.
 *
 * Restarts up to MAX_RESTARTS times for bring-up validation.
 * Mirrors the pattern used in start_audiopd_daemon().
 */
void start_listener_daemon(void)
{
    int nErr = 0;
    const char *dsp_name;
    dsp_name = "ADSP";

    VERIFY_EPRINTF("%s rootpd daemon starting \n", dsp_name);

    char *argv[] = {"adsprpcd", "rootpd", "adsp"};
    int argc = 3;

    while (1) {

        VERIFY_IPRINTF("Calling adsp_default_listener_start");

        nErr = adsp_default_listener_start(argc, argv);

        if (nErr == AEE_ECONNREFUSED) {
            VERIFY_EPRINTF("fastRPC device not accessible, daemon exiting...");
            break;
        }

#ifdef __ZEPHYR__
        VERIFY_EPRINTF("%s rootpd daemon: listener exited (err=0x%x), "
                       " (DSP not up yet.) ",
                       dsp_name, nErr);
        break;
#endif /* __ZEPHYR__ */

        /* Zephyr sleep instead of usleep */
        k_sleep(K_MSEC(100));
    }

    VERIFY_EPRINTF("rootpd daemon exiting tid: %p, nErr: %x", (void *)k_current_get(), nErr);
}

/**
 * start_audiopd_daemon() - Listener daemon for the ADSP Audio PD.
 *
 * Mirrors start_listener_daemon() but targets "audiopd" on the ADSP.
 * argv[1] = "audiopd" causes adsp_default_listener_start() to open
 * the static Audio PD (FASTRPC_INIT_CREATE_STATIC) instead of attaching
 * to the root guest OS.
 */
void start_audiopd_daemon(void)
{
    int nErr = 0;
    const char *dsp_name = "ADSP";

    VERIFY_EPRINTF("%s audiopd daemon starting\n", dsp_name);

    char *argv[] = {"audioadsprpcd", "audiopd", "adsp"};
    int argc = 3;

    while (1) {
        nErr = adsp_default_listener_start(argc, argv);

        if (nErr == AEE_ECONNREFUSED) {
            VERIFY_EPRINTF("fastRPC device not accessible, audiopd daemon exiting...");
            break;
        }

#ifdef __ZEPHYR__
        VERIFY_EPRINTF("%s audiopd daemon: listener exited (err=0x%x), "
                       " (DSP not up yet.)",
                       dsp_name, nErr);
        break;
#endif /* __ZEPHYR__ */

        VERIFY_EPRINTF("%s audiopd daemon restart after 100ms...", dsp_name);
        k_sleep(K_MSEC(100));
    }

    VERIFY_EPRINTF("audiopd daemon exiting tid: %p, nErr: %x",
                   (void *)k_current_get(), nErr);
}

/**
 * frpc_listener_thread - Zephyr thread entry for the fastrpc rootpd listener daemon.
 *
 * The thread starts at boot (delay=0) but immediately blocks on
 * frpc_listener_sem.  It unblocks only after main() calls
 * frpc_listener_signal(), which happens at the very end of main() —
 * after all main-thread work (CLK_READY SMP2P, etc.) is done.
 *
 * Execution order:
 *   POST_KERNEL SYS_INIT (all drivers init)
 *   → main() runs: LOG_INF, SMP2P CLK_READY, ...
 *   → main() calls frpc_listener_signal()  ← sem posted twice
 *   → main() returns 0 and exits
 *   → frpc_listener_thread unblocks → start_listener_daemon() runs
 *   → frpc_audiopd_thread  unblocks → start_audiopd_daemon()  runs
 */
void frpc_listener_thread(void *p1, void *p2, void *p3)
{
    printk("frpc_listener_thread %p: waiting for main() to complete...\n",
           (void *)k_current_get());

    /* Block until main() signals via frpc_listener_signal() */
    k_sem_take(&frpc_listener_sem, K_FOREVER);

    printk("frpc_listener_thread %p: main done, starting rootpd listener daemon\n",
           (void *)k_current_get());
    start_listener_daemon();
}

/**
 * frpc_audiopd_thread - Zephyr thread entry for the ADSP Audio PD listener.
 *
 * Waits on the same frpc_listener_sem as frpc_listener_thread so both
 * daemons start only after main() has completed its initialization.
 * Runs at the same priority as frpc_listener_thread (prio=7).
 */
void frpc_audiopd_thread(void *p1, void *p2, void *p3)
{
    printk("frpc_audiopd_thread: waiting for main() to complete...\n");

    /* Block until main() signals via frpc_listener_signal() */
    k_sem_take(&frpc_listener_sem, K_FOREVER);

    printk("frpc_audiopd_thread: main done, starting audiopd listener daemon\n");
    start_audiopd_daemon();
}

/**
 * start_sensorspd_daemon() - Listener daemon for the ADSP Sensors PD.
 *
 * Mirrors start_audiopd_daemon() but targets "sensorspd" on the ADSP.
 * argv[1] = "sensorspd" causes adsp_default_listener_start() to open
 * the static Sensors PD (FASTRPC_INIT_ATTACH_SENSORS) instead of the
 * Audio PD.  The Sensors PD uses INIT_ATTACH with pd=SENSORS rather
 * than INIT_CREATE_STATIC, so no remote heap allocation is needed.
 */
void start_sensorspd_daemon(void)
{
    int nErr = 0;
    const char *dsp_name = "ADSP";

    VERIFY_EPRINTF("%s sensorspd daemon starting\n", dsp_name);

    char *argv[] = {"sscrpcd", "sensorspd", "adsp"};
    int argc = 3;

    while (1) {
        nErr = adsp_default_listener_start(argc, argv);

        if (nErr == AEE_ECONNREFUSED) {
            VERIFY_EPRINTF("fastRPC device not accessible, sensorspd daemon exiting...");
            break;
        }

#ifdef __ZEPHYR__
        VERIFY_EPRINTF("%s sensorspd daemon: listener exited (err=0x%x), "
                       " (DSP not up yet.)",
                       dsp_name, nErr);
        break;
#endif /* __ZEPHYR__ */

        VERIFY_EPRINTF("%s sensorspd daemon restart after 100ms...", dsp_name);
        k_sleep(K_MSEC(100));
    }

    VERIFY_EPRINTF("sensorspd daemon exiting tid: %p, nErr: %x",
                   (void *)k_current_get(), nErr);
}

/**
 * frpc_sensorspd_thread - Zephyr thread entry for the ADSP Sensors PD listener.
 *
 * Waits on the same frpc_listener_sem as frpc_listener_thread and
 * frpc_audiopd_thread so all three daemons start only after main()
 * has completed its initialization.
 * Runs at the same priority as the other daemon threads (prio=7).
 */
void frpc_sensorspd_thread(void *p1, void *p2, void *p3)
{
    printk("frpc_sensorspd_thread: waiting for main() to complete...\n");

    /* Block until main() signals via frpc_listener_signal() */
    k_sem_take(&frpc_listener_sem, K_FOREVER);

    printk("frpc_sensorspd_thread: main done, starting sensorspd listener daemon\n");
    start_sensorspd_daemon();
}

/*
 * Both threads use a 16 KB stack.
 * The deep call chain (adsp_default_listener_start → remote_session_control
 * → fastrpc_init_once → fastrpc_apps_user_init → remote_handle64_open
 * → domain_init → listener_android_domain_init → pthread_create)
 * exceeds 4096 bytes, so 16384 is required to avoid stack overflow.
 *
 * Both threads start immediately (delay=0) but block on frpc_listener_sem
 * until main() calls frpc_listener_signal().
 */
K_THREAD_DEFINE(frpc_listener_id, 16384, frpc_listener_thread,
                NULL, NULL, NULL, 7, 0, 0);

K_THREAD_DEFINE(frpc_audiopd_id, 16384, frpc_audiopd_thread,
                NULL, NULL, NULL, 7, 0, 0);

/* Sensors PD listener thread — same stack size and priority as the other daemons. */
K_THREAD_DEFINE(frpc_sensorspd_id, 16384, frpc_sensorspd_thread,
                NULL, NULL, NULL, 7, 0, 0);

/* =========================================================================
 * Sensors PD user client thread
 * =========================================================================
 */

/* Defined in sensors_user_client.c */
extern void sensors_user_client_run(void);

/**
 * frpc_sensors_user_thread - Zephyr thread entry for the sensors PD user client.
 *
 * Waits on frpc_listener_sem like the daemon threads so it starts only after
 * main() has completed its initialization.  Starts 50 ms after sensorspd so
 * the sensors PD session is fully initialised before this thread opens its
 * own new session on the same PD.
 * Runs at the same priority as the daemon threads (prio=7).
 */
void frpc_sensors_user_thread(void *p1, void *p2, void *p3)
{
    printk("frpc_sensors_user_thread: waiting for main() to complete...\n");

    /* Block until main() signals via frpc_listener_signal() */
    k_sem_take(&frpc_listener_sem, K_FOREVER);

    printk("frpc_sensors_user_thread: main done, opening sensors PD handle\n");
    sensors_user_client_run();
}

/* Sensors user client thread — reserves its own session and opens a handle
 * on the sensors PD.  Same stack size and priority as the daemon threads. */
K_THREAD_DEFINE(frpc_sensors_user_id, 16384, frpc_sensors_user_thread,
                NULL, NULL, NULL, 7, 0, 0);
