/******************************************************************************

			  L I B R M N E T C T L . H

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
*  @file    librmnetctl.h
*  @brief   rmnet control API's header file
*/

#ifndef LIBRMNETCTL_H
#define LIBRMNETCTL_H

/* RMNET API succeeded */
#define RMNETCTL_SUCCESS 0
/* RMNET API encountered an error while executing within the library. Check the
* error code in this case */
#define RMNETCTL_LIB_ERR 1
/* RMNET API encountered an error while executing in the kernel. Check the
* error code in this case */
#define RMNETCTL_KERNEL_ERR 2
/* RMNET API encountered an error because of invalid arguments*/
#define RMNETCTL_INVALID_ARG 3

/* Flag to associate a network device*/
#define RMNETCTL_DEVICE_ASSOCIATE 1
/* Flag to unassociate a network device*/
#define RMNETCTL_DEVICE_UNASSOCIATE 0
/* Flag to create a new virtual network device*/
#define RMNETCTL_NEW_VND 1
/* Flag to free a new virtual network device*/
#define RMNETCTL_FREE_VND 0
/* Flag to add a new flow*/
#define RMNETCTL_ADD_FLOW 1
/* Flag to delete an existing flow*/
#define RMNETCTL_DEL_FLOW 0

enum rmnetctl_error_codes_e {
	/* API succeeded. This should always be the first element. */
	RMNETCTL_API_SUCCESS = 0,

	RMNETCTL_API_FIRST_ERR = 1,
	/* API failed because not enough memory to create buffer to send
	 * message */
	RMNETCTL_API_ERR_REQUEST_INVALID = RMNETCTL_API_FIRST_ERR,
	/* API failed because not enough memory to create buffer for the
	 *  response message */
	RMNETCTL_API_ERR_RESPONSE_INVALID = 2,
	/* API failed because could not send the message to kernel */
	RMNETCTL_API_ERR_MESSAGE_SEND = 3,
	/* API failed because could not receive message from the kernel */
	RMNETCTL_API_ERR_MESSAGE_RECEIVE = 4,

	RMNETCTL_INIT_FIRST_ERR = 5,
	/* Invalid process id. So return an error. */
	RMNETCTL_INIT_ERR_PROCESS_ID = RMNETCTL_INIT_FIRST_ERR,
	/* Invalid socket descriptor id. So return an error. */
	RMNETCTL_INIT_ERR_NETLINK_FD = 6,
	/* Could not bind the socket to the Netlink file descriptor */
	RMNETCTL_INIT_ERR_BIND = 7,
	/* Invalid user id. Only root has access to this function. (NA) */
	RMNETCTL_INIT_ERR_INVALID_USER = 8,

	RMNETCTL_API_SECOND_ERR = 9,
	/* API failed because the RmNet handle for the transaction was NULL */
	RMNETCTL_API_ERR_HNDL_INVALID = RMNETCTL_API_SECOND_ERR,
	/* API failed because the request buffer for the transaction was NULL */
	RMNETCTL_API_ERR_REQUEST_NULL = 10,
	/* API failed because the response buffer for the transaction was NULL*/
	RMNETCTL_API_ERR_RESPONSE_NULL = 11,
	/* API failed because the request and response type do not match*/
	RMNETCTL_API_ERR_MESSAGE_TYPE = 12,
	/* API failed because the return type is invalid */
	RMNETCTL_API_ERR_RETURN_TYPE = 13,
	/* API failed because the string was truncated */
	RMNETCTL_API_ERR_STRING_TRUNCATION = 14,

	/* These error are 1-to-1 with rmnet_data config errors in rmnet_data.h
	   for each conversion.
	   please keep the enums synced.
	*/
	RMNETCTL_KERNEL_FIRST_ERR = 15,
	/* No error */
	RMNETCTL_KERNEL_ERROR_NO_ERR = RMNETCTL_KERNEL_FIRST_ERR,
	/* Invalid / unsupported message */
	RMNETCTL_KERNEL_ERR_UNKNOWN_MESSAGE = 16,
	/* Internal problem in the kernel module */
	RMNETCTL_KERNEL_ERR_INTERNAL = 17,
	/* Kernel is temporarily out of memory */
	RMNETCTL_KERNEL_ERR_OUT_OF_MEM = 18,
	/* Device already exists / Still in use */
	RMETNCTL_KERNEL_ERR_DEVICE_IN_USE = 19,
	/* Invalid request / Unsupported scenario */
	RMNETCTL_KERNEL_ERR_INVALID_REQUEST = 20,
	/* Device doesn't exist */
	RMNETCTL_KERNEL_ERR_NO_SUCH_DEVICE = 21,
	/* One or more of the arguments is invalid */
	RMNETCTL_KERNEL_ERR_BAD_ARGS = 22,
	/* Egress device is invalid */
	RMNETCTL_KERNEL_ERR_BAD_EGRESS_DEVICE = 23,
	/* TC handle is full */
	RMNETCTL_KERNEL_ERR_TC_HANDLE_FULL = 24,

	/* This should always be the last element */
	RMNETCTL_API_ERR_ENUM_LENGTH
};

#define RMNETCTL_ERR_MSG_SIZE 100

/*!
* @brief Contains a list of error message from API
*/
char rmnetctl_error_code_text
[RMNETCTL_API_ERR_ENUM_LENGTH][RMNETCTL_ERR_MSG_SIZE] = {
	"ERROR: API succeeded\n",
	"ERROR: Unable to allocate the buffer to send message\n",
	"ERROR: Unable to allocate the buffer to receive message\n",
	"ERROR: Could not send the message to kernel\n",
	"ERROR: Unable to receive message from the kernel\n",
	"ERROR: Invalid process id\n",
	"ERROR: Invalid socket descriptor id\n",
	"ERROR: Could not bind to netlink socket\n",
	"ERROR: Only root can access this API\n",
	"ERROR: RmNet handle for the transaction was NULL\n",
	"ERROR: Request buffer for the transaction was NULL\n",
	"ERROR: Response buffer for the transaction was NULL\n",
	"ERROR: Request and response type do not match\n",
	"ERROR: Return type is invalid\n",
	"ERROR: String was truncated\n",
	/* Kernel errors */
	"ERROR: Kernel call succeeded\n",
	"ERROR: Invalid / Unsupported directive\n",
	"ERROR: Internal problem in the kernel module\n",
	"ERROR: The kernel is temporarily out of memory\n",
	"ERROR: Device already exists / Still in use\n",
	"ERROR: Invalid request / Unsupported scenario\n",
	"ERROR: Device doesn't exist\n",
	"ERROR: One or more of the arguments is invalid\n",
	"ERROR: Egress device is invalid\n",
	"ERROR: TC handle is full\n"
};

/*===========================================================================
			 DEFINITIONS AND DECLARATIONS
===========================================================================*/
typedef struct rmnetctl_hndl_s rmnetctl_hndl_t;

/* @brief Public API to initialize the RTM_NETLINK RMNET control driver
 * @details Allocates memory for the RmNet handle. Creates and binds to a
 * netlink socket if successful
 * @param **rmnetctl_hndl_t_val RmNet handle to be initialized
 * @return RMNETCTL_SUCCESS if successful
 * @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
 * @return RMNETCTL_KERNEL_ERR if there was an error in the kernel.
 * Check error_code
 * @return RMNETCTL_INVALID_ARG if invalid arguments were passed to the API
 */
int rtrmnet_ctl_init(rmnetctl_hndl_t **hndl, uint16_t *error_code);

/* @brief Public API to clean up the RTM_NETLINK RmNeT control handle
 * @details Close the socket and free the RmNet handle
 * @param *rmnetctl_hndl_t_val RmNet handle to be initialized
 * @return void
 */
int rtrmnet_ctl_deinit(rmnetctl_hndl_t *hndl);

/* @brief Public API to create a new virtual device node
 * @details Message type is RTM_NEWLINK
 * @param hndl RmNet handle for the Netlink message
 * @param dev_name Device name new node will be connected to
 * @param vnd_name Name of virtual device to be created
 * @param error_code Status code of this operation returned from the kernel
 * @param index Index node will have
 * @param flagconfig Flag configuration device will have
 * @return RMNETCTL_SUCCESS if successful
 * @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
 * @return RMNETCTL_KERNEL_ERR if there was an error in the kernel.
 * Check error_code
 * @return RMNETCTL_INVALID_ARG if invalid arguments were passed to the API
 */
int rtrmnet_ctl_newvnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
		       uint16_t *error_code, uint8_t  index,
		       uint32_t flagconfig);

/* @brief Public API to delete a virtual device node
 * @details Message type is RTM_DELLINK
 * @param hndl RmNet handle for the Netlink message
 * @param vnd_name Name of virtual device to be deleted
 * @param error_code Status code of this operation returned from the kernel
 * @return RMNETCTL_SUCCESS if successful
 * @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
 * @return RMNETCTL_KERNEL_ERR if there was an error in the kernel.
 * Check error_code
 * @return RMNETCTL_INVALID_ARG if invalid arguments were passed to the API
 */
int rtrmnet_ctl_delvnd(rmnetctl_hndl_t *hndl, char *vndname,
		       uint16_t *error_code);

/* @brief Public API to change flag's of a virtual device node
 * @details Message type is RTM_NEWLINK
 * @param hndl RmNet handle for the Netlink message
 * @param dev_name Name of device node is connected to
 * @param vnd_name Name of virtual device to be changed
 * @param error_code Status code of this operation returned from the kernel
 * @param flagconfig New flag config vnd should have
 * @return RMNETCTL_SUCCESS if successful
 * @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
 * @return RMNETCTL_KERNEL_ERR if there was an error in the kernel.
 * Check error_code
 * @return RMNETCTL_INVALID_ARG if invalid arguments were passed to the API
 */
int rtrmnet_ctl_changevnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
			  uint16_t *error_code, uint8_t  index,
			  uint32_t flagconfig);

/* @brief Public API to bridge a vnd and device
 * @details Message type is RTM_NEWLINK
 * @param hndl RmNet handle for the Netlink message
 * @param dev_name device to bridge msg will be sent to
 * @param vnd_name vnd name of device that will be dev_name's master
 * @param error_code Status code of this operation returned from the kernel
 * @return RMNETCTL_SUCCESS if successful
 * @return RMNETCTL_LIB_ERR if there was a library error. Check error_code
 * @return RMNETCTL_KERNEL_ERR if there was an error in the kernel.
 * Check error_code
 * @return RMNETCTL_INVALID_ARG if invalid arguments were passed to the API
 */
int rtrmnet_ctl_bridgevnd(rmnetctl_hndl_t *hndl, char *devname, char *vndname,
			  uint16_t *error_code);

int rtrmnet_activate_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint32_t flow_id,
			  int ip_type,
			  uint32_t tcm_handle,
			  uint16_t *error_code);

int rtrmnet_delete_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint32_t flow_id,
			  int ip_type,
			  uint16_t *error_code);




int rtrmnet_control_flow(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint8_t bearer_id,
			  uint16_t sequence,
			  uint32_t grantsize,
			  uint8_t ack,
			  uint16_t *error_code);

int rtrmnet_flow_state_down(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint32_t instance,
			  uint16_t *error_code);


int rtrmnet_flow_state_up(rmnetctl_hndl_t *hndl,
			  char *devname,
			  char *vndname,
			  uint32_t instance,
			  uint32_t ep_type,
			  uint32_t ifaceid,
			  int flags,
			  uint16_t *error_code);

#endif /* not defined LIBRMNETCTL_H */

