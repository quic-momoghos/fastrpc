/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FastRPC & AEE Headers */
#include "adsp_default_listener.h"
#include "adsp_default_listener1.h"
#include "AEEStdErr.h"
#include "AEEstd.h"
#include "HAP_farf.h"
#include "fastrpc_common.h"
#include "fastrpc_internal.h"
#include "remote.h"
#include "verify.h"
#include "fastrpc_apps_user.h"

/* Zephyr POSIX EventFD (Requires CONFIG_EVENTFD=y, but NOT CONFIG_NET_SOCKETS) */
#include <zephyr/posix/sys/eventfd.h>
#include <unistd.h>

LOG_MODULE_REGISTER(frpc_listener, LOG_LEVEL_INF);

#define MAX_DOMAIN_URI_SIZE 12
#define ROOTPD_NAME "rootpd"
#define ATTACH_GUESTOS "attachguestos"
#define CREATE_STATICPD "createstaticpd:"

/* Array of supported domain names and its corresponding ID's. */
static domain_t supported_domains[] = {
    {ADSP_DOMAIN_ID, ADSP_DOMAIN},
    {MDSP_DOMAIN_ID, MDSP_DOMAIN},
    {SDSP_DOMAIN_ID, SDSP_DOMAIN},
    {CDSP_DOMAIN_ID, CDSP_DOMAIN},
    {CDSP1_DOMAIN_ID, CDSP1_DOMAIN},
    {GDSP0_DOMAIN_ID, GDSP0_DOMAIN},
    {GDSP1_DOMAIN_ID, GDSP1_DOMAIN}
};

static domain_t *get_domain_uri(int domain_id) {
    for (int i = 0; i < (sizeof(supported_domains) / sizeof(domain_t)); i++) {
        if (supported_domains[i].id == domain_id)
            return &supported_domains[i];
    }
    return NULL;
}

/**
 * adsp_default_listener_start() - Zephyr implementation
 *
 * Session-management flow (Zephyr-specific)
 * -----------------------------------------
 * remote_handle64_open() on Zephyr requires every URI to carry an
 * explicit "&_session=N" token (enforced in fastrpc_apps_user.c).
 * Before the first remote_handle64_open() call this function therefore:
 *
 *   1. Calls remote_session_control(FASTRPC_RESERVE_NEW_SESSION) to
 *      reserve a session slot and obtain session_id.
 *   2. Appends FASTRPC_SESSION_URI + session_id to every URI that is
 *      subsequently passed to remote_handle64_open() — including the
 *      PD-open URI, the listener registration URI, and the eventfd URI.
 *
 * The session suffix is stored in session_suffix[] and reused for all
 * three remote_handle64_open() calls within the same invocation.
 */
int adsp_default_listener_start(int argc, char *argv[]) {
    remote_handle64 fd = INVALID_HANDLE;
    remote_handle64 listener_fd = INVALID_HANDLE;
    int nErr = AEE_SUCCESS, domain_id = INVALID_DOMAIN_ID;
    int effective_domain_id = DEFAULT_DOMAIN_ID; /* set in argc>2 branch */
    char *name = NULL;
    char *adsp_default_listener1_URI_domain = NULL;
    int adsp_default_listener1_URI_domain_len = 0;
    domain_t *dsp_domain = NULL;
    int namelen = 0;
    char *eventfd_domain = NULL;

    /*
     * Session suffix: "&_session=<id>"
     * Populated by FASTRPC_RESERVE_NEW_SESSION in the argc > 2 branch.
     * Remains an empty string for the non-domain branches so that the
     * existing remote_handle_open() calls (which are not subject to the
     * Zephyr enforcement) are unaffected.
     */
    char session_suffix[32] = "";

    LOG_INF("Starting FastRPC Listener...");

    if (argc > 2) {
        domain_id = get_domain_from_name(argv[2], DOMAIN_NAME_STAND_ALONE);
        VERIFYC(INVALID_DOMAIN_ID != domain_id, AEE_EINVALIDDOMAIN);
        VERIFYC(NULL != (dsp_domain = get_domain_uri(domain_id)), AEE_EINVALIDDOMAIN);

        /* ----------------------------------------------------------------
         * Step 1 – Reserve a FastRPC session slot
         *
         * remote_handle64_open() on Zephyr rejects any URI that does not
         * carry "&_session=N" AND whose session was not reserved via
         * FASTRPC_RESERVE_NEW_SESSION.  Reserve the slot here, before
         * constructing any URI.
         * ---------------------------------------------------------------- */
        remote_rpc_reserve_new_session_t reserve_session;
        memset(&reserve_session, 0, sizeof(reserve_session));
        reserve_session.domain_name      = argv[2];          /* e.g. "adsp" */
        reserve_session.domain_name_len  = strlen(argv[2]);
        reserve_session.session_name     = argv[1];          /* e.g. "audiopd" / "rootpd" */
        reserve_session.session_name_len = strlen(argv[1]);
        nErr = remote_session_control(FASTRPC_RESERVE_NEW_SESSION,
                                      &reserve_session,
                                      sizeof(reserve_session));
        if (nErr != AEE_SUCCESS) {
            LOG_ERR("%s: FASTRPC_RESERVE_NEW_SESSION failed: 0x%x", __func__, nErr);
            goto bail;
        }
        effective_domain_id = (int)reserve_session.effective_domain_id;
        LOG_INF("%s: Reserved session_id=%u  effective_domain_id=%u",
                __func__,
                reserve_session.session_id,
                reserve_session.effective_domain_id);

        /* Build the session suffix that will be appended to every URI
         * passed to remote_handle64_open() in this invocation.         */
        snprintf(session_suffix, sizeof(session_suffix), "%s%u",
                 FASTRPC_SESSION_URI, reserve_session.session_id);
        /* session_suffix is now e.g. "&_session=1"                     */

        /* ----------------------------------------------------------------
         * Step 2 – Build the PD-open URI with domain + session suffix
         *
         * rootpd:  "'\":;./\\attachguestos&_dom=adsp&_session=1"
         * audiopd: "'\":;./\\createstaticpd:audiopd&_dom=adsp&_session=1"
         * ---------------------------------------------------------------- */
        namelen = strlen(ITRANSPORT_PREFIX CREATE_STATICPD) +
                  strlen(argv[1]) +
                  strlen(dsp_domain->uri) +
                  strlen(session_suffix);
        name = (char *)malloc(namelen + 1);
        VERIFYC(NULL != name, AEE_ENOMEMORY);

        if (!strncmp(argv[1], ROOTPD_NAME, strlen(argv[1]))) {
            strlcpy(name, ITRANSPORT_PREFIX ATTACH_GUESTOS, namelen + 1);
        } else {
            strlcpy(name, ITRANSPORT_PREFIX CREATE_STATICPD, namelen + 1);
            strlcat(name, argv[1], namelen + 1);
        }
        strlcat(name, dsp_domain->uri, namelen + 1);
        strlcat(name, session_suffix, namelen + 1);

        LOG_INF("%s: Calling remote_handle64_open('%s')", __func__, name);
        VERIFY(AEE_SUCCESS == (nErr = remote_handle64_open(name, &fd)));

    } else if (argc > 1) {
        domain_id = get_domain_from_name(argv[1], DOMAIN_NAME_IN_URI);
        if (domain_id != INVALID_DOMAIN_ID) {
            VERIFY(AEE_SUCCESS == (nErr = remote_handle64_open(argv[1], &fd)));
        } else {
            namelen = strlen(ITRANSPORT_PREFIX CREATE_STATICPD) + strlen(argv[1]);
            name = (char *)malloc(namelen + 1);
            VERIFYC(NULL != name, AEE_ENOMEMORY);
            strlcpy(name, ITRANSPORT_PREFIX CREATE_STATICPD, namelen + 1);
            strlcat(name, argv[1], namelen + 1);
            VERIFY(AEE_SUCCESS == (nErr = remote_handle_open(name, (remote_handle *)&fd)));
        }
    } else {
        namelen = strlen(ITRANSPORT_PREFIX ATTACH_GUESTOS);
        name = (char *)malloc(namelen + 1);
        VERIFYC(NULL != name, AEE_ENOMEMORY);
        strlcpy(name, ITRANSPORT_PREFIX ATTACH_GUESTOS, namelen + 1);
        VERIFY(AEE_SUCCESS == (nErr = remote_handle_open(name, (remote_handle *)&fd)));
    }

    /* ----------------------------------------------------------------
     * Register Listener
     *
     * For the domain case the listener URI also needs the session suffix
     * so that adsp_default_listener1_open() → remote_handle64_open()
     * passes the Zephyr enforcement gate.
     * ---------------------------------------------------------------- */
    if (domain_id != INVALID_DOMAIN_ID) {
        /* Build: "<adsp_default_listener1_URI>&_dom=<dsp>&_session=<id>" */
        adsp_default_listener1_URI_domain_len =
            strlen(adsp_default_listener1_URI) +
            strlen(dsp_domain->uri) +
            strlen(session_suffix) + 1;
        printk("fastrpc: adsp_default_listener1_URI_domain_len is %d", adsp_default_listener1_URI_domain_len);
        adsp_default_listener1_URI_domain =
            (char *)malloc(adsp_default_listener1_URI_domain_len);
        VERIFYC(NULL != adsp_default_listener1_URI_domain, AEE_ENOMEMORY);
        snprintf(adsp_default_listener1_URI_domain,
                 adsp_default_listener1_URI_domain_len,
                 "%s%s%s",
                 adsp_default_listener1_URI,
                 dsp_domain->uri,
                 session_suffix);
        LOG_INF("Calling adsp_default_listener1_open('%s') ...",
                adsp_default_listener1_URI_domain);
        nErr = adsp_default_listener1_open(adsp_default_listener1_URI_domain,
                                           &listener_fd);
        if (nErr == AEE_SUCCESS) {
            VERIFY(0 == (nErr = adsp_default_listener1_register(listener_fd)));
        }
    } else {
        VERIFY(0 == (nErr = remote_handle_open("adsp_default_listener",
                                               (remote_handle *)&listener_fd)));
        VERIFY(0 == (nErr = adsp_default_listener_register()));
    }

    /* ----------------------------------------------------------------
     * Wait for exit signal via k_sem (replaces eventfd on Zephyr).
     *
     * On Linux, adsp_default_listener_start() would:
     *   1. Open "geteventfd" URI → get the eventfd fd
     *   2. Call eventfd_read(exit_fd, &event) to block until listener exits
     *   3. listener() bail path calls eventfd_write() to signal exit
     *
     * On Zephyr, eventfd is limited by CONFIG_ZVFS_EVENTFD_MAX=1.
     * Instead, listener_android_domain_init() uses k_sem (exit_sem):
     *   - k_sem_give() in listener() bail path signals exit
     *   - listener_android_wait_exit() calls k_sem_take(K_FOREVER) to block
     *
     * No eventfd fd allocation needed — skip the geteventfd URI entirely.
     * ---------------------------------------------------------------- */

    /* Cleanup temporary strings before blocking. */
    if (name) { free(name); name = NULL; }
    if (eventfd_domain) { free(eventfd_domain); eventfd_domain = NULL; }
    if (adsp_default_listener1_URI_domain) {
        free(adsp_default_listener1_URI_domain);
        adsp_default_listener1_URI_domain = NULL;
    }

    LOG_INF("Listener registered. Waiting for exit signal via k_sem...");
    {
        extern int listener_android_wait_exit(int domain);
        listener_android_wait_exit(effective_domain_id);
        LOG_INF("Exit signal received. Shutting down listener.");
    }

bail:
    /* Release the session reservation so the slot can be reused on restart.
     * fastrpc_release_session_reservation() is idempotent: if domain_deinit
     * already ran (triggered by remote_handle64_close below), the flag is
     * already false and this is a no-op.  If domain_deinit did NOT run
     * (e.g. the session failed before any handle was opened), this ensures
     * the slot is freed so FASTRPC_RESERVE_NEW_SESSION can reuse it. */
    if (effective_domain_id != DEFAULT_DOMAIN_ID &&
        IS_VALID_EFFECTIVE_DOMAIN_ID(effective_domain_id)) {
        fastrpc_release_session_reservation(effective_domain_id);
    }
    /* fd was opened with remote_handle64_open(); must be closed on every
     * exit path to avoid exhausting the driver's handle pool. */
    if (fd != INVALID_HANDLE) remote_handle64_close(fd);
    if (listener_fd != INVALID_HANDLE) adsp_default_listener1_close(listener_fd);
    /* Free any heap strings not yet freed by the pre-block cleanup. */
    if (name) free(name);
    if (eventfd_domain) free(eventfd_domain);
    if (adsp_default_listener1_URI_domain) free(adsp_default_listener1_URI_domain);
    if (nErr != AEE_SUCCESS) LOG_ERR("Listener exited with error 0x%x", nErr);
    return nErr;
}
