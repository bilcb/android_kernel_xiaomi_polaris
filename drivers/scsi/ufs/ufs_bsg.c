/*
 * UFS BSG (block layer SG v4) transport endpoint for 4.9
 *
 * Backported from upstream commit df032bf27a41 ("scsi: ufs: Add a bsg
 * endpoint that supports UPIUs") and later QUERY/UIC extensions, adapted
 * to the 4.9 bsg-lib API. Exposes /dev/bsg/ufs-bsgN so that userspace
 * (QCOM librpmb, ufs-utils, recovery GPT tools) can send QUERY / NOP /
 * UIC UPIUs. Add a ueventd symlink /dev/ufs-bsg -> /dev/bsg/ufs-bsg0 on
 * targets whose proprietary blobs scan /dev for "ufs-bsg".
 *
 * Legacy RPMB data frames are NOT tunneled here: they are issued as
 * SCSI SECURITY PROTOCOL IN/OUT (0xA2/0xB5, protocol 0xEC) to the RPMB
 * well-known LUN (0xC4), reachable through its SCSI sg node. A
 * UPIU_TRANSACTION_COMMAND request therefore completes with -EOPNOTSUPP
 * and a hint in dmesg.
 *
 * Copyright (C) 2018 Western Digital Corporation (upstream driver)
 * Copyright (C) 2026 - 4.9 backport
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/blkdev.h>
#include <linux/bsg-lib.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <scsi/scsi_host.h>
#include <scsi/ufs/ufs.h>

#include "ufs.h"
#include "ufshcd.h"
#include "ufs_bsg.h"

#define UFS_BSG_NAME_LEN	20

static void ufs_bsg_node_release(struct device *dev)
{
	put_device(dev->parent);
}

static struct ufs_hba *ufs_bsg_job_to_hba(struct bsg_job *job)
{
	return shost_priv(dev_to_shost(job->dev->parent));
}

/*
 * Copy descriptor read data back to userspace. Upstream uses the request
 * (bidi dout) payload for this; accept the reply payload as fallback
 * since some SG_IO callers only attach din.
 */
static unsigned int ufs_bsg_copy_desc_to_job(struct bsg_job *job,
					     u8 *buf, unsigned int len)
{
	if (job->request_payload.sg_list && job->request_payload.sg_cnt)
		return sg_copy_from_buffer(job->request_payload.sg_list,
					   job->request_payload.sg_cnt,
					   buf, len);
	if (job->reply_payload.sg_list && job->reply_payload.sg_cnt)
		return sg_copy_from_buffer(job->reply_payload.sg_list,
					   job->reply_payload.sg_cnt,
					   buf, len);
	return 0;
}

static int ufs_bsg_handle_query(struct ufs_hba *hba, struct bsg_job *job,
				struct ufs_bsg_request *req,
				struct ufs_bsg_reply *rsp)
{
	u8 desc_buf[QUERY_DESC_MAX_SIZE];
	int opcode = req->upiu_req.qr.opcode;
	int idn = req->upiu_req.qr.idn;
	u8 index = req->upiu_req.qr.index;
	u8 selector = req->upiu_req.qr.selector;
	int len;
	int ret = 0;

	/* echo the request UPIU, then fill in the response fields */
	memcpy(&rsp->upiu_rsp, &req->upiu_req, sizeof(rsp->upiu_rsp));

	switch (opcode) {
	case UPIU_QUERY_OPCODE_READ_DESC:
		len = be16_to_cpu(req->upiu_req.qr.length);
		if (len <= 0 || len > QUERY_DESC_MAX_SIZE)
			len = QUERY_DESC_MAX_SIZE;
		memset(desc_buf, 0, sizeof(desc_buf));
		ret = ufshcd_query_descriptor(hba, UPIU_QUERY_OPCODE_READ_DESC,
					      idn, index, selector,
					      desc_buf, &len);
		if (ret) {
			dev_err(hba->dev,
				"%s: read desc idn %d failed %d\n",
				__func__, idn, ret);
			break;
		}
		rsp->reply_payload_rcv_len =
			ufs_bsg_copy_desc_to_job(job, desc_buf, len);
		rsp->upiu_rsp.qr.length = cpu_to_be16(len);
		break;
	case UPIU_QUERY_OPCODE_WRITE_DESC:
		len = be16_to_cpu(req->upiu_req.qr.length);
		if (len <= 0 || len > QUERY_DESC_MAX_SIZE) {
			ret = -EINVAL;
			break;
		}
		memset(desc_buf, 0, sizeof(desc_buf));
		if (job->request_payload.sg_list && job->request_payload.sg_cnt)
			sg_copy_to_buffer(job->request_payload.sg_list,
					  job->request_payload.sg_cnt,
					  desc_buf, len);
		else {
			ret = -EINVAL;
			break;
		}
		ret = ufshcd_query_descriptor(hba, UPIU_QUERY_OPCODE_WRITE_DESC,
					      idn, index, selector,
					      desc_buf, &len);
		if (ret)
			dev_err(hba->dev,
				"%s: write desc idn %d failed %d\n",
				__func__, idn, ret);
		break;
	case UPIU_QUERY_OPCODE_READ_ATTR:
	{
		u32 val = 0;

		ret = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_READ_ATTR,
					idn, index, selector, &val);
		if (ret) {
			dev_err(hba->dev,
				"%s: read attr idn %d failed %d\n",
				__func__, idn, ret);
			break;
		}
		rsp->upiu_rsp.qr.value = cpu_to_be32(val);
		break;
	}
	case UPIU_QUERY_OPCODE_WRITE_ATTR:
	{
		u32 val = be32_to_cpu(req->upiu_req.qr.value);

		ret = ufshcd_query_attr(hba, UPIU_QUERY_OPCODE_WRITE_ATTR,
					idn, index, selector, &val);
		if (ret)
			dev_err(hba->dev,
				"%s: write attr idn %d failed %d\n",
				__func__, idn, ret);
		break;
	}
	case UPIU_QUERY_OPCODE_READ_FLAG:
	{
		bool flag = false;

		ret = ufshcd_query_flag(hba, UPIU_QUERY_OPCODE_READ_FLAG,
					idn, &flag);
		if (ret) {
			dev_err(hba->dev,
				"%s: read flag idn %d failed %d\n",
				__func__, idn, ret);
			break;
		}
		/* report the flag in the value field */
		rsp->upiu_rsp.qr.value = cpu_to_be32(flag ? 1 : 0);
		break;
	}
	case UPIU_QUERY_OPCODE_SET_FLAG:
	case UPIU_QUERY_OPCODE_CLEAR_FLAG:
	case UPIU_QUERY_OPCODE_TOGGLE_FLAG:
	{
		bool dummy = false;

		ret = ufshcd_query_flag(hba, opcode, idn, &dummy);
		if (ret)
			dev_err(hba->dev,
				"%s: flag op %d idn %d failed %d\n",
				__func__, opcode, idn, ret);
		break;
	}
	default:
		dev_err(hba->dev, "%s: unsupported query opcode 0x%x\n",
			__func__, opcode);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

static int ufs_bsg_handle_uic(struct ufs_hba *hba,
			      struct ufs_bsg_request *req,
			      struct ufs_bsg_reply *rsp)
{
	struct uic_command uic_cmd;
	int ret;

	memset(&uic_cmd, 0, sizeof(uic_cmd));
	memcpy(&uic_cmd, &req->upiu_req.uc, UIC_CMD_SIZE);

	memcpy(&rsp->upiu_rsp, &req->upiu_req, sizeof(rsp->upiu_rsp));

	ret = ufshcd_send_uic_cmd(hba, &uic_cmd);
	if (ret)
		dev_err(hba->dev, "%s: send uic cmd 0x%x failed %d\n",
			__func__, uic_cmd.command, ret);

	memcpy(&rsp->upiu_rsp.uc, &uic_cmd, UIC_CMD_SIZE);
	return ret;
}

static int ufs_bsg_request(struct bsg_job *job)
{
	struct ufs_bsg_request *req = job->request;
	struct ufs_bsg_reply *rsp = job->reply;
	struct ufs_hba *hba = ufs_bsg_job_to_hba(job);
	int msgcode;
	int ret = 0;
	int pm_ret;

	if (!req || !rsp || job->request_len < sizeof(*req)) {
		pr_err("%s: short bsg request\n", __func__);
		/*
		 * Complete the job with an error instead of returning
		 * non-zero: bsg_request_fn() stops dispatching on non-zero
		 * return, leaving the already-dequeued request hanging.
		 */
		if (job->reply && job->reply_len >= sizeof(u32)) {
			*(u32 *)job->reply = (u32)-EINVAL;
			job->reply_len = sizeof(u32);
		}
		bsg_job_done(job, -EINVAL, 0);
		return 0;
	}

	rsp->reply_payload_rcv_len = 0;
	rsp->result = 0;

	/*
	 * The bsg queue has no blk_pm_runtime_init() hookup like the scsi
	 * device queues do, so take a runtime PM reference explicitly;
	 * otherwise queries issued while the hba is runtime suspended go
	 * to a device that was never resumed. Mirrors the pattern used by
	 * other ufshcd out-of-band command paths (get_sync -> hold_all).
	 */
	pm_ret = pm_runtime_get_sync(hba->dev);
	if (pm_ret < 0) {
		pm_runtime_put_noidle(hba->dev);
		ret = pm_ret;
		goto out;
	}

	ufshcd_hold(hba, false);

	msgcode = req->msgcode;
	switch (msgcode) {
	case UPIU_TRANSACTION_NOP_OUT:
		/* actually issue the NOP OUT UPIU and wait for NOP IN */
		ret = ufshcd_send_nop(hba);
		if (ret)
			dev_err(hba->dev, "%s: NOP OUT failed %d\n",
				__func__, ret);
		break;
	case UPIU_TRANSACTION_QUERY_REQ:
		ret = ufs_bsg_handle_query(hba, job, req, rsp);
		break;
	case UPIU_TRANSACTION_UIC_CMD:
		ret = ufs_bsg_handle_uic(hba, req, rsp);
		break;
	case UPIU_TRANSACTION_COMMAND:
		dev_err_once(hba->dev,
			"%s: COMMAND UPIU (e.g. RPMB SECURITY PROTOCOL) is not tunneled; use the RPMB WLUN (0xC4) sg node\n",
			__func__);
		ret = -EOPNOTSUPP;
		break;
	case UPIU_TRANSACTION_TASK_REQ:
	case UPIU_TRANSACTION_DATA_OUT:
	default:
		dev_err(hba->dev, "%s: unsupported msgcode 0x%x\n",
			__func__, msgcode);
		ret = -EOPNOTSUPP;
		break;
	}

	ufshcd_release(hba, false);
	pm_runtime_put(hba->dev);

out:
	rsp->result = ret;
	job->reply_len = sizeof(*rsp);
	bsg_job_done(job, ret, rsp->reply_payload_rcv_len);
	return 0;
}

/**
 * ufs_bsg_probe - add the ufs-bsg device node
 * @hba: per-adapter instance, must have a registered Scsi_Host
 *
 * Called after scsi_add_host(). Best-effort: the caller warns but does
 * not fail probe when this fails.
 */
int ufs_bsg_probe(struct ufs_hba *hba)
{
	struct device *bsg_dev = &hba->bsg_dev;
	struct Scsi_Host *shost = hba->host;
	struct request_queue *q;
	char name[UFS_BSG_NAME_LEN];
	int ret;

	snprintf(name, sizeof(name), "ufs-bsg%u", shost->host_no);

	device_initialize(bsg_dev);
	bsg_dev->parent = get_device(&shost->shost_gendev);
	bsg_dev->release = ufs_bsg_node_release;
	dev_set_name(bsg_dev, "%s", name);
	ret = device_add(bsg_dev);
	if (ret) {
		dev_err(bsg_dev, "failed to add bsg device, err %d\n", ret);
		goto out_put;
	}

	q = __scsi_alloc_queue(shost, bsg_request_fn);
	if (!q) {
		dev_err(bsg_dev, "failed to allocate bsg queue\n");
		ret = -ENOMEM;
		goto out_del;
	}

	ret = bsg_setup_queue(bsg_dev, q, name, ufs_bsg_request, 0);
	if (ret) {
		dev_err(bsg_dev, "failed to setup bsg queue, err %d\n", ret);
		blk_cleanup_queue(q);
		goto out_del;
	}

	hba->bsg_queue = q;
	dev_info(hba->dev, "ufs-bsg node /dev/bsg/%s created\n", name);
	return 0;

out_del:
	device_del(bsg_dev);
out_put:
	put_device(bsg_dev);
	hba->bsg_queue = NULL;
	return ret;
}

/**
 * ufs_bsg_remove - detach and remove the ufs-bsg node
 * @hba: per-adapter instance
 */
void ufs_bsg_remove(struct ufs_hba *hba)
{
	struct device *bsg_dev = &hba->bsg_dev;

	if (!hba->bsg_queue)
		return;

	bsg_unregister_queue(hba->bsg_queue);
	blk_cleanup_queue(hba->bsg_queue);
	hba->bsg_queue = NULL;
	device_del(bsg_dev);
	put_device(bsg_dev);
}

MODULE_DESCRIPTION("UFS BSG transport endpoint (QUERY/NOP/UIC)");
MODULE_AUTHOR("4.9 backport of upstream ufs_bsg");
MODULE_LICENSE("GPL v2");
