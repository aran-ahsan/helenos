/*
 * Copyright (c) 2006 Martin Decky
 * Copyright (c) 2007 Jakub Jermar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * - The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/** @addtogroup fs
 * @{
 */ 

/**
 * @file	fat.c
 * @brief	FAT file system driver for HelenOS.
 */

#include <ipc/ipc.h>
#include <ipc/services.h>
#include <async.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <as.h>
#include "../../vfs/vfs.h"

#define dprintf(...)	printf(__VA_ARGS__)

vfs_info_t fat_vfs_info = {
	.name = "fat",
	.ops = {
		[IPC_METHOD_TO_VFS_OP(VFS_REGISTER)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_MOUNT)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_UNMOUNT)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_LOOKUP)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_OPEN)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_CREATE)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_CLOSE)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_READ)] = VFS_OP_DEFINED,
		[IPC_METHOD_TO_VFS_OP(VFS_WRITE)] = VFS_OP_NULL,
		[IPC_METHOD_TO_VFS_OP(VFS_SEEK)] = VFS_OP_DEFAULT
	}
};

uint8_t *plb_ro = NULL;

int fs_handle = 0;

/**
 * This connection fibril processes VFS requests from VFS.
 *
 * In order to support simultaneous VFS requests, our design is as follows.
 * The connection fibril accepts VFS requests from VFS. If there is only one
 * instance of the fibril, VFS will need to serialize all VFS requests it sends
 * to FAT. To overcome this bottleneck, VFS can send FAT the IPC_M_CONNECT_ME_TO
 * call. In that case, a new connection fibril will be created, which in turn
 * will accept the call. Thus, a new phone will be opened for VFS.
 *
 * There are few issues with this arrangement. First, VFS can run out of
 * available phones. In that case, VFS can close some other phones or use one
 * phone for more serialized requests. Similarly, FAT can refuse to duplicate
 * the connection. VFS should then just make use of already existing phones and
 * route its requests through them. To avoid paying the fibril creation price 
 * upon each request, FAT might want to keep the connections open after the
 * request has been completed.
 */
static void fat_connection(ipc_callid_t iid, ipc_call_t *icall)
{
	if (iid) {
		/*
		 * This only happens for connections opened by
		 * IPC_M_CONNECT_ME_TO calls as opposed to callback connections
		 * created by IPC_M_CONNECT_TO_ME.
		 */
		ipc_answer_fast_0(iid, EOK);
	}
	
	dprintf("VFS-FAT connection established.\n");
	while (1) {
		ipc_callid_t callid;
		ipc_call_t call;
	
		callid = async_get_call(&call);
		switch  (IPC_GET_METHOD(call)) {
		default:
			ipc_answer_fast_0(callid, ENOTSUP);
			break;
		}
	}
}

int main(int argc, char **argv)
{
	int vfs_phone;

	printf("FAT: HelenOS FAT file system server.\n");

	vfs_phone = ipc_connect_me_to(PHONE_NS, SERVICE_VFS, 0);
	while (vfs_phone < EOK) {
		usleep(10000);
		vfs_phone = ipc_connect_me_to(PHONE_NS, SERVICE_VFS, 0);
	}
	
	/*
	 * Tell VFS that we are here and want to get registered.
	 * We use the async framework because VFS will answer the request
	 * out-of-order, when it knows that the operation succeeded or failed.
	 */
	ipc_call_t answer;
	aid_t req = async_send_2(vfs_phone, VFS_REGISTER, 0, 0, &answer);

	/*
	 * Send our VFS info structure to VFS.
	 */
	int rc = ipc_data_send(vfs_phone, &fat_vfs_info, sizeof(fat_vfs_info)); 
	if (rc != EOK) {
		async_wait_for(req, NULL);
		return rc;
	}

	/*
	 * Ask VFS for callback connection.
	 */
	ipcarg_t phonehash;
	ipc_connect_to_me(vfs_phone, 0, 0, &phonehash);

	/*
	 * Allocate piece of address space for PLB.
	 */
	plb_ro = as_get_mappable_page(PLB_SIZE);
	if (!plb_ro) {
		async_wait_for(req, NULL);
		return ENOMEM;
	}

	/*
	 * Request sharing the Path Lookup Buffer with VFS.
	 */
	rc = ipc_call_sync_3(vfs_phone, IPC_M_AS_AREA_RECV, (ipcarg_t) plb_ro,
	    PLB_SIZE, 0, NULL, NULL, NULL);
	if (rc) {
		async_wait_for(req, NULL);
		return rc;
	}
	 
	/*
	 * Pick up the answer for the request to the VFS_REQUEST call.
	 */
	async_wait_for(req, NULL);
	fs_handle = (int) IPC_GET_ARG1(answer);
	dprintf("FAT filesystem registered, fs_handle=%d.\n", fs_handle);

	/*
	 * Create a connection fibril to handle the callback connection.
	 */
	async_new_connection(phonehash, 0, NULL, fat_connection);
	
	/*
	 * Tell the async framework that other connections are to be handled by
	 * the same connection fibril as well.
	 */
	async_set_client_connection(fat_connection);

	async_manager();
	/* not reached */
	return 0;
}

/**
 * @}
 */ 
