// Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @file fastrpc_ioctl_zephyr.c
 * @brief Zephyr port of the FastRPC ioctl abstraction layer.
 *
 * Session model change
 * ====================
 * Previously 'dev' was (int)(uintptr_t)zephyr_device_ptr.
 * Now 'dev' IS the effective_domain_id (integer 0..NUM_DOMAINS_EXTEND-1).
 *
 * The Zephyr device pointer is a compile-time singleton obtained via
 * DEVICE_DT_GET(DT_ALIAS(fastrpc)).  It is the same for every session.
 *
 * Flow:
 *   open_device_node(eff_domain_id)
 *     -> fastrpc_session_open(zdev, eff_domain_id)   [creates fastrpc_user]
 *     -> returns eff_domain_id                        [stored in hlist[].dev]
 *
 *   ioctl_*(dev=eff_domain_id, ...)
 *     -> zdev = DEVICE_DT_GET(DT_ALIAS(fastrpc))     [singleton]
 *     -> fastrpc_*(zdev, eff_domain_id, ...)          [driver looks up fl]
 *
 *   close_device_node(domain_id, dev=eff_domain_id)
 *     -> fastrpc_session_close(zdev, eff_domain_id)  [destroys fastrpc_user]
 */

#include "AEEStdErr.h"
#include "HAP_farf.h"
#include "fastrpc_internal.h"
#include "fastrpc_notif.h"
#include "remote.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <fastrpc_kmd/inc/fastrpc.h>

/* -------------------------------------------------------------------------
 * Helpers
 * -------------------------------------------------------------------------
 */

/**
 * @brief Get the singleton Zephyr fastrpc device pointer.
 *
 * The device pointer is the same for every session.  It is obtained at
 * compile time via DEVICE_DT_GET so there is no runtime lookup overhead.
 */
static inline const struct device *get_fastrpc_zdev(void)
{
	return DEVICE_DT_GET(DT_ALIAS(fastrpc));
}

/**
 * @brief Extract the effective domain ID from the 'dev' handle.
 *
 * 'dev' IS the eff_domain_id in the new session model.
 * This wrapper makes the intent explicit at call sites.
 */
static inline int eff_domain_from_handle(int dev)
{
	return dev; /* dev == eff_domain_id */
}

/* -------------------------------------------------------------------------
 * ioctl_init
 * -------------------------------------------------------------------------
 */
int ioctl_init(int dev, uint32_t flags, int attr,
	       unsigned char *shell, int shelllen, int shellfd,
	       char *mem, int memlen, int memfd, int tessiglen)
{
	const struct device *zdev = get_fastrpc_zdev();
	int eff_domain_id = eff_domain_from_handle(dev);
	int ioErr = 0;

	switch (flags) {

	case FASTRPC_INIT_ATTACH:
		ioErr = fastrpc_init_attach(zdev, eff_domain_id, FASTRPC_PD_ROOT);
		break;

	case FASTRPC_INIT_ATTACH_SENSORS:
		ioErr = fastrpc_init_attach(zdev, eff_domain_id, FASTRPC_PD_SENSORS);
		break;

	case FASTRPC_INIT_CREATE_STATIC: {
		struct fastrpc_init_create_static_msg init_static = {0};

		init_static.namelen = (uint32_t)shelllen;
		init_static.memlen  = (uint32_t)memlen;
		init_static.name    = (uint64_t)(uintptr_t)shell;

		ioErr = fastrpc_init_create_static(zdev, eff_domain_id, &init_static);
		break;
	}

	case FASTRPC_INIT_CREATE: {
		struct fastrpc_init_create_msg init = {0};

		init.file    = (uint64_t)(uintptr_t)shell;
		init.filelen = (uint32_t)shelllen;
		init.filefd  = shellfd;
		init.attrs   = (uint32_t)attr;
		init.siglen  = (uint32_t)tessiglen;

		ioErr = fastrpc_init_create(zdev, eff_domain_id, &init);
		break;
	}

	default:
		FARF(ERROR, "ERROR: %s Invalid init flags %d", __func__, flags);
		ioErr = AEE_EBADPARM;
		break;
	}

	return ioErr;
}

/* -------------------------------------------------------------------------
 * ioctl_invoke
 * -------------------------------------------------------------------------
 */
int ioctl_invoke(int dev, int req, remote_handle handle, uint32_t sc,
		 void *pra, int *fds, unsigned int *attrs,
		 unsigned int *crc, uint64_t *perf_kernel,
		 uint64_t *perf_dsp)
{
	const struct device *zdev = get_fastrpc_zdev();
	int eff_domain_id = eff_domain_from_handle(dev);
	struct fastrpc_invoke_msg invoke = {0};

	if (req < INVOKE || req > INVOKE_FD) {
		return AEE_EUNSUPPORTED;
	}

	invoke.handle = (uint32_t)handle;
	invoke.sc     = sc;
	invoke.args   = (uint64_t)(uintptr_t)pra;

	return fastrpc_invoke(zdev, eff_domain_id, &invoke);
}

/* -------------------------------------------------------------------------
 * ioctl_invoke2_notif
 * -------------------------------------------------------------------------
 */
int ioctl_invoke2_notif(int dev, int *domain, int *session, int *status)
{
	return AEE_EUNSUPPORTED;
}

/* -------------------------------------------------------------------------
 * ioctl_mmap
 * -------------------------------------------------------------------------
 */
int ioctl_mmap(int dev, int req, uint32_t flags, int attr, int fd,
	       int offset, size_t len, uintptr_t vaddrin,
	       uint64_t *vaddrout)
{
	const struct device *zdev = get_fastrpc_zdev();
	int eff_domain_id = eff_domain_from_handle(dev);
	int ioErr = AEE_SUCCESS;

	switch (req) {

	case MEM_MAP: {
		struct fastrpc_mem_map_msg map = {0};

		map.version  = 0;
		map.fd       = fd;
		map.offset   = offset;
		map.flags    = flags;
		map.vaddrin  = (uint64_t)vaddrin;
		map.length   = (uint64_t)len;
		map.attrs    = attr;

		ioErr = fastrpc_mem_map(zdev, eff_domain_id, &map);
		*vaddrout = map.vaddrout;
		break;
	}

	case MMAP:
	case MMAP_64: {
		struct fastrpc_req_mmap_msg map = {0};

		map.fd      = fd;
		map.flags   = flags;
		map.vaddrin = (uint64_t)vaddrin;
		map.size    = (uint64_t)len;

		ioErr = fastrpc_drv_mmap(zdev, eff_domain_id, &map);
		*vaddrout = map.vaddrout;
		break;
	}

	default:
		FARF(ERROR, "ERROR: %s Invalid request %d", __func__, req);
		ioErr = AEE_EBADPARM;
		break;
	}

	return ioErr;
}

/* -------------------------------------------------------------------------
 * ioctl_munmap
 * -------------------------------------------------------------------------
 */
int ioctl_munmap(int dev, int req, int attr, void *buf, int fd, int len,
		 uint64_t vaddr)
{
	const struct device *zdev = get_fastrpc_zdev();
	int eff_domain_id = eff_domain_from_handle(dev);
	int ioErr = AEE_SUCCESS;

	switch (req) {

	case MEM_UNMAP:
	case MUNMAP_FD: {
		struct fastrpc_mem_unmap_msg unmap = {0};

		unmap.version = 0;
		unmap.fd      = fd;
		unmap.vaddr   = vaddr;
		unmap.length  = (uint64_t)len;

		ioErr = fastrpc_mem_unmap(zdev, eff_domain_id, &unmap);
		break;
	}

	case MUNMAP:
	case MUNMAP_64: {
		struct fastrpc_req_munmap_msg unmap = {0};

		unmap.vaddrout = vaddr;
		unmap.size     = (uint64_t)len;

		ioErr = fastrpc_drv_munmap(zdev, eff_domain_id, &unmap);
		break;
	}

	default:
		FARF(ERROR, "ERROR: %s Invalid request %d", __func__, req);
		break;
	}

	return ioErr;
}

/* -------------------------------------------------------------------------
 * ioctl_getinfo
 * -------------------------------------------------------------------------
 */
int ioctl_getinfo(int dev, uint32_t *info)
{
	*info = FASTRPC_INFO_SMMU;
	return AEE_SUCCESS;
}

/* -------------------------------------------------------------------------
 * ioctl_getdspinfo
 * -------------------------------------------------------------------------
 */
int ioctl_getdspinfo(int dev, int domain, uint32_t attr,
		     uint32_t *capability)
{
	const struct device *zdev = get_fastrpc_zdev();
	int eff_domain_id = eff_domain_from_handle(dev);
	printk("%s: eff_domain_id %d", __func__, eff_domain_id);
	struct fastrpc_dsp_capability_msg cap = {0};
	int ioErr = AEE_SUCCESS;

	if (attr >= PERF_V2_DRIVER_SUPPORT && attr < FASTRPC_MAX_ATTRIBUTES) {
		*capability = 0;
		return AEE_SUCCESS;
	}

	cap.domain       = (uint32_t)eff_domain_id;
	cap.attribute_id = attr;
	cap.capability   = 0;

	ioErr = fastrpc_get_dsp_info(zdev, eff_domain_id, &cap);
	*capability = cap.capability;

	return ioErr;
}

/* -------------------------------------------------------------------------
 * ioctl_setmode
 * -------------------------------------------------------------------------
 */
int ioctl_setmode(int dev, int mode)
{
	if (mode == FASTRPC_SESSION_ID1) {
		return AEE_SUCCESS;
	}
	return AEE_EUNSUPPORTED;
}

/* -------------------------------------------------------------------------
 * Unsupported operations
 * -------------------------------------------------------------------------
 */
int ioctl_control(int dev, int req, void *c)          { return AEE_EUNSUPPORTED; }
int ioctl_getperf(int dev, int key, void *data, int *datalen) { return AEE_EUNSUPPORTED; }
int ioctl_signal_create(int dev, uint32_t signal, uint32_t flags) { return AEE_EUNSUPPORTED; }
int ioctl_signal_destroy(int dev, uint32_t signal)    { return AEE_EUNSUPPORTED; }
int ioctl_signal_signal(int dev, uint32_t signal)     { return AEE_EUNSUPPORTED; }
int ioctl_signal_wait(int dev, uint32_t signal, uint32_t timeout_usec) { return AEE_EUNSUPPORTED; }
int ioctl_signal_cancel_wait(int dev, uint32_t signal) { return AEE_EUNSUPPORTED; }
int ioctl_sharedbuf(int dev, struct fastrpc_proc_sharedbuf_info *sharedbuf_info) { return AEE_EUNSUPPORTED; }
int ioctl_session_info(int dev, struct fastrpc_proc_sess_info *sess_info) { return AEE_EUNSUPPORTED; }
int ioctl_optimization(int dev, uint32_t max_concurrency) { return AEE_EUNSUPPORTED; }
int ioctl_mdctx_manage(int dev, int req, void *user_ctx,
		       unsigned int *domain_ids,
		       unsigned int num_domain_ids, uint64_t *ctx) { return AEE_EUNSUPPORTED; }
int fastrpc_async_get_status(fastrpc_async_jobid jobid, int timeout_us, int *result) { return AEE_EUNSUPPORTED; }
int fastrpc_release_async_job(fastrpc_async_jobid jobid) { return AEE_EUNSUPPORTED; }
