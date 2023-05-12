/******************************************************************************

			L I B R M N E T C T L . C

Copyright (c) 2013-2015, 2018 The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
	* Redistributions of source code must retain the above copyright
	  notice, this list of conditions and the following disclaimer.
	* Redistributions in binary form must reproduce the above
	  copyright notice, this list of conditions and the following
	  disclaimer in the documentation and/or other materials provided
	  with the distribution.
	* Neither the name of The Linux Foundation nor the names of its
	  contributors may be used to endorse or promote products derived
	  from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Changes from Qualcomm Innovation Center are provided under the following license:

  Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
  SPDX-License-Identifier: BSD-3-Clause-Clear

******************************************************************************/

/*!
* @file    librmnetctl.c
* @brief   rmnet control API's implementation file
*/

/*===========================================================================
			INCLUDE FILES
===========================================================================*/

#include <sys/socket.h>
#include <stdint.h>
#include <linux/netlink.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <linux/rtnetlink.h>
#include <linux/gen_stats.h>
#include <net/if.h>
#include <asm/types.h>
#include "librmnetctl_hndl.h"
#include "librmnetctl.h"

#ifdef USE_GLIB
#include <glib.h>
#define strlcpy g_strlcpy
#endif

#define RMNETCTL_SOCK_FLAG 0
#define ROOT_USER_ID 0
#define MIN_VALID_PROCESS_ID 0
#define MIN_VALID_SOCKET_FD 0
#define KERNEL_PROCESS_ID 0
#define UNICAST 0
#define MAX_BUF_SIZE sizeof(struct nlmsghdr) + sizeof(struct rmnet_nl_msg_s)
#define INGRESS_FLAGS_MASK   (RMNET_INGRESS_FIX_ETHERNET | \
			      RMNET_INGRESS_FORMAT_MAP | \
			      RMNET_INGRESS_FORMAT_DEAGGREGATION | \
			      RMNET_INGRESS_FORMAT_DEMUXING | \
			      RMNET_INGRESS_FORMAT_MAP_COMMANDS | \
			      RMNET_INGRESS_FORMAT_MAP_CKSUMV3 | \
			      RMNET_INGRESS_FORMAT_MAP_CKSUMV4)
#define EGRESS_FLAGS_MASK    (RMNET_EGRESS_FORMAT__RESERVED__ | \
			      RMNET_EGRESS_FORMAT_MAP | \
			      RMNET_EGRESS_FORMAT_AGGREGATION | \
			      RMNET_EGRESS_FORMAT_MUXING | \
			      RMNET_EGRESS_FORMAT_MAP_CKSUMV3 | \
			      RMNET_EGRESS_FORMAT_MAP_CKSUMV4)

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define NLMSG_TAIL(nmsg) \
    ((struct rtattr *) (((char *)(nmsg)) + NLMSG_ALIGN((nmsg)->nlmsg_len)))

#define NLMSG_DATA_SIZE  500

#define CHECK_MEMSCPY(x) ({if (x < 0 ){return RMNETCTL_LIB_ERR;}})

struct nlmsg {
	struct nlmsghdr nl_addr;
	struct ifinfomsg ifmsg;
	char data[NLMSG_DATA_SIZE];
};

/* 0 reserved, 1-15 for data, 16-30 for acks */
#define RMNETCTL_NUM_TX_QUEUES 31

/* This needs to be hardcoded here because some legacy linux systems
 * do not have this definition
 */
#define RMNET_IFLA_NUM_TX_QUEUES 31

/*===========================================================================
			LOCAL FUNCTION DEFINITIONS
===========================================================================*/
/* Helper functions
 * @brief helper function to implement a secure memcpy
 * @details take source and destination buffer size into
 *          considerations before copying
 * @param dst destination buffer
 * @param dst_size size of destination buffer
 * @param src source buffer
 * @param src_size size of source buffer
 * @return size of the smallest of two buffer
 */

static inline size_t memscpy(void *dst, size_t dst_size, const void *src,
			     size_t src_size) {
	size_t min_size = dst_size < src_size ? dst_size : src_size;
	memcpy(dst, src, min_size);
	return min_size;
}

/*
* @brief helper function to implement a secure memcpy
 * for a concatenating buffer where offset is kept
 * track of
 * @details take source and destination buffer size into
 *          considerations before copying
 * @param dst destination buffer
 * @param dst_size pointer used to decrement
 * @param src source buffer
 * @param src_size size of source buffer
 * @return size of the remaining buffer
 */


static inline int  memscpy_repeat(void* dst, size_t *dst_size,
	const void* src, size_t src_size)
{
	if( !dst_size || *dst_size <= src_size || !dst || !src)
		return RMNETCTL_LIB_ERR;
	else {
		*dst_size -= memscpy(dst, *dst_size, src, src_size);
	}
	return *dst_size;
}

/*!
* @brief Static function to check the dev name
* @details Checks if the name is not NULL and if the name is less than the
* RMNET_MAX_STR_LEN
* @param dev_name Name of the device
* @return RMNETCTL_SUCCESS if successful
* @return RMNETCTL_INVALID_ARG if invalid arguments were passed to the API
*/
static inline int _rmnetctl_check_dev_name(const char *dev_name) {
	int return_code = RMNETCTL_INVALID_ARG;
	do {
	if (!dev_name)
		break;
	if (strlen(dev_name) >= IFNAMSIZ)
		break;
	return_code = RMNETCTL_SUCCESS;
	} while(0);
	return return_code;
}

/*===========================================================================
				EXPOSED API
===========================================================================*/
/*
 *                       NEW DRIVER API
 */
/* @brief Synchronous method to receive messages to and from the kernel
 * using netlink sockets
 * @details Receives the ack response from the kernel.
 * @param *hndl RmNet handle for this transaction
 * @param *error_code Error code if transaction fails
 * @return RMNETCTL_API_SUCCESS if successfully able to send and receive message
 * from the kernel
 * @return RMNETCTL_API_ERR_HNDL_INVALID if RmNet handle for the transaction was
 * NULL
 * @return RMNETCTL_API_ERR_MESSAGE_RECEIVE if could not receive message from
 * the kernel
 * @return RMNETCTL_API_ERR_MESSAGE_TYPE if the response type does not
 * match
 */
static int rmnet_get_ack(rmnetctl_hndl_t *hndl, uint16_t *error_code)
{
	struct nlack {
		struct nlmsghdr ackheader;
		struct nlmsgerr ackdata;
		char   data[256];

	} ack;
	int i;

	if (!hndl || !error_code)
		return RMNETCTL_INVALID_ARG;

	if ((i = recv(hndl->netlink_fd, &ack, sizeof(ack), 0)) < 0) {
		*error_code = errno;
		return RMNETCTL_API_ERR_MESSAGE_RECEIVE;
	}

	/*Ack should always be NLMSG_ERROR type*/
	if (ack.ackheader.nlmsg_type == NLMSG_ERROR) {
		if (ack.ackdata.error == 0) {
			*error_code = RMNETCTL_API_SUCCESS;
			return RMNETCTL_SUCCESS;
		} else {
			*error_code = -ack.ackdata.error;
			return RMNETCTL_KERNEL_ERR;
		}
	}

	*error_code = RMNETCTL_API_ERR_RETURN_TYPE;
	return RMNETCTL_API_FIRST_ERR;
}

/*
 *                       EXPOSED NEW DRIVER API
 */
int rtrmnet_ctl_init(rmnetctl_hndl_t **hndl, uint16_t *error_code)
{
	struct sockaddr_nl __attribute__((__may_alias__)) *saddr_ptr;
	int netlink_fd = -1;
	pid_t pid = 0;

	if (!hndl || !error_code)
		return RMNETCTL_INVALID_ARG;

	*hndl = (rmnetctl_hndl_t *)malloc(sizeof(rmnetctl_hndl_t));
	if (!*hndl) {
		*error_code = RMNETCTL_API_ERR_HNDL_INVALID;
		return RMNETCTL_LIB_ERR;
	}

	memset(*hndl, 0, sizeof(rmnetctl_hndl_t));

	pid = getpid();
	if (pid  < MIN_VALID_PROCESS_ID) {
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_PROCESS_ID;
		return RMNETCTL_LIB_ERR;
	}
	(*hndl)->pid = KERNEL_PROCESS_ID;
	netlink_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (netlink_fd < MIN_VALID_SOCKET_FD) {
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_NETLINK_FD;
		return RMNETCTL_LIB_ERR;
	}

	(*hndl)->netlink_fd = netlink_fd;

	memset(&(*hndl)->src_addr, 0, sizeof(struct sockaddr_nl));

	(*hndl)->src_addr.nl_family = AF_NETLINK;
	(*hndl)->src_addr.nl_pid = (*hndl)->pid;

	saddr_ptr = &(*hndl)->src_addr;
	if (bind((*hndl)->netlink_fd,
		(struct sockaddr *)saddr_ptr,
		sizeof(struct sockaddr_nl)) < 0) {
		close((*hndl)->netlink_fd);
		free(*hndl);
		*error_code = RMNETCTL_INIT_ERR_BIND;
		return RMNETCTL_LIB_ERR;
	}

	memset(&(*hndl)->dest_addr, 0, sizeof(struct sockaddr_nl));

	(*hndl)->dest_addr.nl_family = AF_NETLINK;
	(*hndl)->dest_addr.nl_pid = KERNEL_PROCESS_ID;
	(*hndl)->dest_addr.nl_groups = UNICAST;

	return RMNETCTL_SUCCESS;
}

int rtrmnet_ctl_deinit(rmnetctl_hndl_t *hndl)
{
	if (!hndl)
		return RMNETCTL_SUCCESS;

	close(hndl->netlink_fd);
	free(hndl);

	return RMNETCTL_SUCCESS;
}

int rtrmnet_ctl_newvnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
		       uint16_t *error_code, uint8_t  index,
		       uint32_t flagconfig)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct ifla_vlan_flags flags;
	unsigned int devindex = 0, val = 0;
	char *kind = "rmnet";
	struct nlmsg req;
	short id;
	size_t reqsize;

	if (!hndl || !devname || !vndname || !error_code ||
	   _rmnetctl_check_dev_name(vndname) || _rmnetctl_check_dev_name(devname))
		return RMNETCTL_INVALID_ARG;

	memset(&req, 0, sizeof(req));
	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL |
				  NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type = RMNET_IFLA_NUM_TX_QUEUES;
	attrinfo->rta_len = RTA_LENGTH(4);
	*(int *)RTA_DATA(attrinfo) = RMNETCTL_NUM_TX_QUEUES;
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH((4)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	id = index;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_ID;
	attrinfo->rta_len = RTA_LENGTH(sizeof(id));
	*(short *)RTA_DATA(attrinfo) = id;
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(id)));

	if (flagconfig != 0) {
		flags.mask  = flagconfig;
		flags.flags = flagconfig;

		attrinfo = (struct rtattr *)(((char *)&req) +
					     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
		attrinfo->rta_type =  IFLA_VLAN_FLAGS;
		attrinfo->rta_len = RTA_LENGTH(sizeof(flags));
		CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flags,
			       sizeof(flags)));
		req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
					RTA_ALIGN(RTA_LENGTH(sizeof(flags)));
	}

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}

int rtrmnet_ctl_delvnd(rmnetctl_hndl_t *hndl, char *vndname,
		       uint16_t *error_code)
{
	unsigned int devindex = 0;
	struct nlmsg req;

	if (!hndl || !vndname || !error_code)
		return RMNETCTL_INVALID_ARG;

	memset(&req, 0, sizeof(req));
	req.nl_addr.nlmsg_type = RTM_DELLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of vndname*/
	devindex = if_nametoindex(vndname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup index attribute */
	req.ifmsg.ifi_index = devindex;
	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_ctl_changevnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
			  uint16_t *error_code, uint8_t  index,
			  uint32_t flagconfig)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct ifla_vlan_flags flags;
	char *kind = "rmnet";
	struct nlmsg req;
	unsigned int devindex = 0, val = 0;
	size_t reqsize;
	short id;

	memset(&req, 0, sizeof(req));

	if (!hndl || !devname || !vndname || !error_code ||
	    _rmnetctl_check_dev_name(vndname) || _rmnetctl_check_dev_name(devname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	id = index;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_ID;
	attrinfo->rta_len = RTA_LENGTH(sizeof(id));
	*(short *)RTA_DATA(attrinfo) = id;
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(id)));

	if (flagconfig != 0) {
		flags.mask  = flagconfig;
		flags.flags = flagconfig;

		attrinfo = (struct rtattr *)(((char *)&req) +
					     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
		attrinfo->rta_type =  IFLA_VLAN_FLAGS;
		attrinfo->rta_len = RTA_LENGTH(sizeof(flags));
		CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flags,
			       sizeof(flags)));
		req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
					RTA_ALIGN(RTA_LENGTH(sizeof(flags)));
	}

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}

int rtrmnet_ctl_bridgevnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
			  uint16_t *error_code)
{
	unsigned int devindex = 0, vndindex = 0;
	struct rtattr *masterinfo;
	struct nlmsg req;
	size_t reqsize;

	if (!hndl || !vndname || !devname || !error_code || _rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	memset(&req, 0, sizeof(req));
	reqsize = NLMSG_DATA_SIZE - sizeof(*masterinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of vndname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	vndindex = if_nametoindex(vndname);
	if (vndindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup index attribute */
	req.ifmsg.ifi_index = devindex;
	masterinfo = (struct rtattr *)(((char *)&req) +
				       NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	masterinfo->rta_type =  IFLA_MASTER;
	masterinfo->rta_len = RTA_LENGTH(sizeof(vndindex));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(masterinfo), &reqsize, &vndindex, sizeof(vndindex)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(vndindex)));

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_activate_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint32_t flow_id,
			  int ip_type,
			  uint32_t tcm_handle,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	unsigned int devindex = 0;
	unsigned int val = 0;
	size_t reqsize =0;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));


	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_handle = tcm_handle;
	flowinfo.tcm_family = 0x1;
	flowinfo.tcm__pad1 = bearer_id;
	flowinfo.tcm_ifindex = ip_type;
	flowinfo.tcm_parent = flow_id;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_delete_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint32_t flow_id,
			  int ip_type,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	unsigned int devindex = 0;
	unsigned int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_family = 0x2;
	flowinfo.tcm_ifindex = ip_type;
	flowinfo.tcm__pad1 = bearer_id;
	flowinfo.tcm_parent = flow_id;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));


	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;

	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}

int rtrmnet_control_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint16_t sequence,
			  uint32_t grantsize,
			  uint8_t ack,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	unsigned int devindex = 0;
	unsigned int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_family = 0x3;
	flowinfo.tcm__pad1 = bearer_id;
	flowinfo.tcm__pad2 = sequence;
	flowinfo.tcm_parent = ack;
	flowinfo.tcm_info = grantsize;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;
	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_flow_state_up(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint32_t instance,
			  uint32_t ep_type,
			  uint32_t ifaceid,
			  int flags,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	unsigned int devindex = 0;
	unsigned int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(*attrinfo);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_handle = instance;
	flowinfo.tcm_family = 0x4;
	flowinfo.tcm_ifindex = flags;
	flowinfo.tcm_parent = ifaceid;
	flowinfo.tcm_info = ep_type;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));

	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;
	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}


int rtrmnet_flow_state_down(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint32_t instance,
			  uint16_t *error_code)
{
	struct rtattr *attrinfo, *datainfo, *linkinfo;
	struct tcmsg  flowinfo;
	char *kind = "rmnet";
	struct nlmsg req;
	unsigned int devindex = 0;
	unsigned int val = 0;
	size_t reqsize;

	memset(&req, 0, sizeof(req));
	memset(&flowinfo, 0, sizeof(flowinfo));

	if (!hndl || !devname || !error_code ||_rmnetctl_check_dev_name(devname) ||
		_rmnetctl_check_dev_name(vndname))
		return RMNETCTL_INVALID_ARG;

	reqsize = NLMSG_DATA_SIZE - sizeof(struct rtattr);
	req.nl_addr.nlmsg_type = RTM_NEWLINK;
	req.nl_addr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifinfomsg));
	req.nl_addr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
	req.nl_addr.nlmsg_seq = hndl->transaction_id;
	hndl->transaction_id++;

	/* Get index of devname*/
	devindex = if_nametoindex(devname);
	if (devindex == 0) {
		*error_code = errno;
		return RMNETCTL_KERNEL_ERR;
	}

	/* Setup link attr with devindex as data */
	val = devindex;
	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type = IFLA_LINK;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(sizeof(val)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &val, sizeof(val)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(val)));

	/* Set up IFLA info kind  RMNET that has linkinfo and type */

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_IFNAME;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, vndname, strlen(vndname) + 1));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(vndname) + 1));

	linkinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	linkinfo->rta_type = IFLA_LINKINFO;
	linkinfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	attrinfo->rta_type =  IFLA_INFO_KIND;
	attrinfo->rta_len = RTA_ALIGN(RTA_LENGTH(strlen(kind)));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, kind, strlen(kind)));

	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(strlen(kind)));

	datainfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));

	datainfo->rta_type =  IFLA_INFO_DATA;
	datainfo->rta_len = RTA_ALIGN(RTA_LENGTH(0));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(0));

	flowinfo.tcm_handle = instance;
	flowinfo.tcm_family = 0x5;

	attrinfo = (struct rtattr *)(((char *)&req) +
				     NLMSG_ALIGN(req.nl_addr.nlmsg_len));
	attrinfo->rta_type =  IFLA_VLAN_EGRESS_QOS;
	attrinfo->rta_len = RTA_LENGTH(sizeof(flowinfo));
	CHECK_MEMSCPY(memscpy_repeat(RTA_DATA(attrinfo), &reqsize, &flowinfo, sizeof(flowinfo)));
	req.nl_addr.nlmsg_len = NLMSG_ALIGN(req.nl_addr.nlmsg_len) +
				RTA_ALIGN(RTA_LENGTH(sizeof(flowinfo)));


	datainfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)datainfo;
	linkinfo->rta_len = (char *)NLMSG_TAIL(&req.nl_addr) - (char *)linkinfo;

	if (send(hndl->netlink_fd, &req, req.nl_addr.nlmsg_len, 0) < 0) {
		*error_code = RMNETCTL_API_ERR_MESSAGE_SEND;
		return RMNETCTL_LIB_ERR;
	}

	return rmnet_get_ack(hndl, error_code);
}
