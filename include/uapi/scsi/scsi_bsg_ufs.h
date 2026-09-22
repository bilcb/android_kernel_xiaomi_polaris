/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * UFS Transport SGIO v4 BSG Message Support
 *
 * Copyright (C) 2011-2013 Samsung India Software Operations
 * Copyright (C) 2018 Western Digital Corporation
 *
 * This file is intended to be included by both kernel and user space.
 * Keep in sync with drivers/scsi/ufs/ufs_bsg.h (kernel side). The kernel
 * uses its internal UPIU definitions; the layouts below must match:
 * request = u32 msgcode + 32-byte UPIU, reply = u32 result +
 * u32 reply_payload_rcv_len + 32-byte UPIU.
 */
#ifndef UAPI_SCSI_BSG_UFS_H
#define UAPI_SCSI_BSG_UFS_H

#include <linux/types.h>

#define UFS_CDB_SIZE	16
/* UIC commands are 4 dwords long, per UFSHCI spec */
#define UIC_CMD_SIZE	(sizeof(__u32) * 4)

/* UPIU transaction codes (initiator -> target) usable via BSG */
enum ufs_bsg_msg_code {
	UPIU_TRANSACTION_NOP_OUT	= 0x00,
	UPIU_TRANSACTION_COMMAND		= 0x01,
	UPIU_TRANSACTION_TASK_REQ	= 0x04,
	UPIU_TRANSACTION_QUERY_REQ	= 0x16,
	UPIU_TRANSACTION_UIC_CMD	= 0x1F,
};

/**
 * struct utp_upiu_header - UPIU header structure
 * @dword_0: UPIU header DW-0
 * @dword_1: UPIU header DW-1
 * @dword_2: UPIU header DW-2
 */
struct utp_upiu_header {
	__be32 dword_0;
	__be32 dword_1;
	__be32 dword_2;
};

/**
 * struct utp_upiu_cmd - Command UPIU structure
 * @exp_data_transfer_len: Data Transfer Length DW-3
 * @cdb: Command Descriptor Block CDB DW-4 to DW-7
 */
struct utp_upiu_cmd {
	__be32 exp_data_transfer_len;
	__u8 cdb[UFS_CDB_SIZE];
};

/**
 * struct utp_upiu_query - UPIU request buffer structure for query request
 * @opcode: command to perform B-0
 * @idn: a value that indicates the particular type of data B-1
 * @index: Index to further identify data B-2
 * @selector: Index to further identify data B-3
 * @reserved_osf: spec reserved field B-4,5
 * @length: number of descriptor bytes to read/write B-6,7
 * @value: Attribute value to be written DW-5
 * @reserved: spec reserved DW-6,7
 */
struct utp_upiu_query {
	__u8 opcode;
	__u8 idn;
	__u8 index;
	__u8 selector;
	__be16 reserved_osf;
	__be16 length;
	__be32 value;
	__be32 reserved[2];
};

/**
 * struct utp_upiu_req - general UPIU request structure
 * @header: UPIU header structure DW-0 to DW-2
 * @sc: fields structure for SCSI command DW-3 to DW-7
 * @qr: fields structure for query request DW-3 to DW-7
 * @uc: use utp_upiu_query to host the 4 dwords of a UIC command
 */
struct utp_upiu_req {
	struct utp_upiu_header header;
	union {
		struct utp_upiu_cmd sc;
		struct utp_upiu_query qr;
		/* use utp_upiu_query to host the 4 dwords of uic command */
		struct utp_upiu_query uc;
	};
};

/* request (CDB) structure of the sg_io_v4 */
struct ufs_bsg_request {
	__u32 msgcode;
	struct utp_upiu_req upiu_req;
};

/* response (request sense data) structure of the sg_io_v4 */
struct ufs_bsg_reply {
	/*
	 * The completion result. Result exists in two forms:
	 * if negative, it is an -Exxx system errno value. There will
	 * be no further reply information supplied.
	 * else, it's the 4-byte SCSI error result, with driver, host,
	 * msg and status fields. The per-msgcode reply structure
	 * will contain valid data.
	 */
	__u32 result;
	/* If there was reply_payload, how much was received? */
	__u32 reply_payload_rcv_len;
	struct utp_upiu_req upiu_rsp;
};

#endif /* UAPI_SCSI_BSG_UFS_H */
