/*
 * UFS BSG (block layer SG v4) transport endpoint
 *
 * Creates /dev/bsg/ufs-bsgN allowing userspace (e.g. QCOM librpmb,
 * ufs-utils, recovery GPT utils) to exchange UPIUs (QUERY / NOP / UIC)
 * with the UFS device. Legacy RPMB frames themselves go through the
 * RPMB well-known LUN (UFS_UPIU_RPMB_WLUN = 0xC4) as SCSI SECURITY
 * PROTOCOL IN/OUT commands, see the RPMB WLUN sg node.
 *
 * Backported to 4.9 from the upstream driver (drivers/scsi/ufs/ufs_bsg.c,
 * "scsi: ufs: Add a bsg endpoint that supports UPIUs", Western Digital,
 * 2018) and adapted to the 4.9 bsg-lib API
 * (bsg_setup_queue(dev, q, name, job_fn, dd_size)).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _UFS_BSG_H
#define _UFS_BSG_H

#include <linux/types.h>
#include "ufs.h"

/*
 * Request (CDB) structure of the sg_io_v4, ABI-compatible with
 * include/uapi/scsi/scsi_bsg_ufs.h and bionic's scsi_bsg_ufs.h:
 *   u32 msgcode + 32-byte UPIU request = 36 bytes.
 */
struct ufs_bsg_request {
	__u32 msgcode;
	struct utp_upiu_req upiu_req;
};

/*
 * Response structure of the sg_io_v4:
 *   u32 result + u32 reply_payload_rcv_len + 32-byte UPIU response.
 */
struct ufs_bsg_reply {
	__u32 result;
	__u32 reply_payload_rcv_len;
	struct utp_upiu_req upiu_rsp;
};

struct ufs_hba;

#ifdef CONFIG_SCSI_UFS_BSG
int ufs_bsg_probe(struct ufs_hba *hba);
void ufs_bsg_remove(struct ufs_hba *hba);
#else
static inline int ufs_bsg_probe(struct ufs_hba *hba)
{
	return 0;
}
static inline void ufs_bsg_remove(struct ufs_hba *hba)
{
}
#endif

#endif /* _UFS_BSG_H */
