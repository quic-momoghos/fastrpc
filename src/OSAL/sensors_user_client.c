// Copyright (c) Qualcomm Technologies, Inc.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file sensors_user_client.c
 * @brief User thread that reserves a new session and opens a handle on the
 *        ADSP Sensors PD, independent of the sensorspd daemon session.
 *
 * Flow:
 *   1. FASTRPC_RESERVE_NEW_SESSION  -> gets session_id=2, eff_domain_id=16
 *   2. remote_handle64_open("createstaticpd:sensorspd&_dom=adsp&_session=2")
 *      -> attaches to the sensors PD on the new session
 *   3. (optional) FASTRPC_GET_URI + remote_handle64_open -> module handle
 *   4. remote_handle64_close(pd_handle)
 */

#include <string.h>

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "verify.h"
#include "remote.h"
#include "fastrpc_internal.h"
#include "fastrpc_common.h"
#include "fastrpc_apps_user.h"

#ifndef VERIFY_PRINT_ERROR
#define VERIFY_PRINT_ERROR
#endif

/*
 * URI buffer size:
 *   ITRANSPORT_PREFIX "createstaticpd:sensorspd" "&_dom=adsp" "&_session=N" + NUL
 *   = 4 + 24 + 9 + 10 + 2 + 1 = ~50 bytes; 128 is ample.
 */
#define SENSORS_PD_URI_LEN  128

void sensors_user_client_run(void)
{
    int nErr = AEE_SUCCESS;
    remote_handle64 pd_handle = INVALID_HANDLE;
    char pd_uri[SENSORS_PD_URI_LEN];

    /* ----------------------------------------------------------------
     * Step 1 - Reserve a new session slot.
     *
     * The sensorspd daemon already holds session_id=1 (eff_domain_id=8).
     * This call gets the next free slot: session_id=2, eff_domain_id=16.
     * ---------------------------------------------------------------- */
    remote_rpc_reserve_new_session_t reserve;
    memset(&reserve, 0, sizeof(reserve));
    reserve.domain_name      = "adsp";
    reserve.domain_name_len  = strlen("adsp");
    reserve.session_name     = "sensors_user";
    reserve.session_name_len = strlen("sensors_user");

    nErr = remote_session_control(FASTRPC_RESERVE_NEW_SESSION,
                                  &reserve, sizeof(reserve));
    if (nErr != AEE_SUCCESS) {
        VERIFY_EPRINTF("sensors_user_client: RESERVE_NEW_SESSION failed 0x%x", nErr);
        goto bail;
    }
    VERIFY_EPRINTF("sensors_user_client: reserved session_id=%u eff_domain_id=%u",
                   reserve.session_id, reserve.effective_domain_id);

    /* ----------------------------------------------------------------
     * Step 2 - Open the sensors PD on the reserved session.
     *
     * URI format:
     *   ITRANSPORT_PREFIX "createstaticpd:sensorspd" FASTRPC_DOMAIN_URI
     *   "adsp" FASTRPC_SESSION_URI "<session_id>"
     *
     * Example result:
     *   "'\":;./\\createstaticpd:sensorspd&_dom=adsp&_session=2"
     * ---------------------------------------------------------------- */
    snprintf(pd_uri, sizeof(pd_uri),
             "%s%s%s%s%s%u",
             ITRANSPORT_PREFIX,           /* "'\":;./\\"  */
             "createstaticpd:sensorspd",  /* PD name      */
             FASTRPC_DOMAIN_URI,          /* "&_dom="     */
             "adsp",                      /* domain name  */
             FASTRPC_SESSION_URI,         /* "&_session=" */
             reserve.session_id);         /* e.g. 2       */

    nErr = remote_handle64_open(pd_uri, &pd_handle);
    if (nErr != AEE_SUCCESS) {
        VERIFY_EPRINTF("sensors_user_client: open sensors PD failed 0x%x uri=%s",
                       nErr, pd_uri);
        goto bail;
    }
    VERIFY_EPRINTF("sensors_user_client: sensors PD opened handle=0x%llx session=%u",
                   (unsigned long long)pd_handle, reserve.session_id);

    /*
     * At this point the session is live on the sensors PD.
     * To open a module handle on this session use FASTRPC_GET_URI to build
     * the full URI then call remote_handle64_open:
     *
     *   char mod_uri[256];
     *   remote_rpc_get_uri_t get_uri;
     *   memset(&get_uri, 0, sizeof(get_uri));
     *   get_uri.domain_name     = "adsp";
     *   get_uri.domain_name_len = strlen("adsp");
     *   get_uri.session_id      = reserve.session_id;
     *   get_uri.module_uri      = "file:///libmysensor_skel.so"
     *                             "?mysensor_skel_handle_invoke&_modver=1.0";
     *   get_uri.module_uri_len  = strlen(get_uri.module_uri);
     *   get_uri.uri             = mod_uri;
     *   get_uri.uri_len         = sizeof(mod_uri);
     *   remote_session_control(FASTRPC_GET_URI, &get_uri, sizeof(get_uri));
     *
     *   remote_handle64 mod_handle = INVALID_HANDLE;
     *   remote_handle64_open(mod_uri, &mod_handle);
     *   // ... invoke sensor functions ...
     *   remote_handle64_close(mod_handle);
     */

bail:
    if (pd_handle != INVALID_HANDLE)
        remote_handle64_close(pd_handle);

    /* Release the session slot reserved by FASTRPC_RESERVE_NEW_SESSION.
     * remote_handle64_close(pd_handle) is a no-op for static PD handles
     * (IS_STATICPD_HANDLE=true for handle 0x102), so domain_deinit is
     * never triggered and is_session_reserved is never cleared automatically.
     * This call explicitly frees the slot so RESERVE_NEW_SESSION can reuse it.
     * Guard: only call if RESERVE_NEW_SESSION succeeded (on failure it leaves
     * effective_domain_id = NUM_DOMAINS_EXTEND as the invalid sentinel). */
    if (reserve.effective_domain_id != NUM_DOMAINS_EXTEND)
        fastrpc_release_session_reservation(
                (int)reserve.effective_domain_id);

    if (nErr != AEE_SUCCESS)
        VERIFY_EPRINTF("sensors_user_client: exiting with error 0x%x", nErr);
    else
        VERIFY_EPRINTF("sensors_user_client: done, exiting cleanly");
}
