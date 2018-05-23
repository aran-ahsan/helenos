/*
 * Copyright (c) 2018 Jakub Jermar
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

#include "virtio-net.h"

#include <stdio.h>
#include <stdint.h>

#include <as.h>
#include <ddf/driver.h>
#include <ddf/log.h>
#include <ops/nic.h>
#include <pci_dev_iface.h>

#include <nic.h>

#include <virtio-pci.h>

#define NAME	"virtio-net"

#define VIRTIO_NET_NUM_QUEUES	3

#define RX_QUEUE_1	0
#define TX_QUEUE_1	1
#define CT_QUEUE_1	2

#define BUFFER_SIZE	2048
#define RX_BUF_SIZE	BUFFER_SIZE
#define TX_BUF_SIZE	BUFFER_SIZE
#define CT_BUF_SIZE	BUFFER_SIZE


static errno_t virtio_net_setup_bufs(unsigned int buffers, size_t size,
    bool write, void *buf[], uintptr_t buf_p[])
{
	/*
	 * Allocate all buffers at once in one large chung.
	 */
	void *virt = AS_AREA_ANY;
	uintptr_t phys;
	errno_t rc = dmamem_map_anonymous(buffers * size, 0,
	    write ? AS_AREA_WRITE : AS_AREA_READ, 0, &phys, &virt);
	if (rc != EOK)
		return rc;

	ddf_msg(LVL_NOTE, "DMA buffers: %p-%p", virt, virt + buffers * size);

	/*
	 * Calculate addresses of the individual buffers for easy access.
	 */
	for (unsigned i = 0; i < buffers; i++) {
		buf[i] = virt + i * size;
		buf_p[i] = phys + i * size;
	}

	return EOK;
}

static void virtio_net_teardown_bufs(void *buf[])
{
	if (buf[0]) {
		dmamem_unmap_anonymous(buf[0]);
		buf[0] = NULL;
	}
}

static errno_t virtio_net_initialize(ddf_dev_t *dev)
{
	nic_t *nic_data = nic_create_and_bind(dev);
	if (!nic_data)
		return ENOMEM;

	virtio_net_t *virtio_net = calloc(1, sizeof(virtio_net_t));
	if (!virtio_net) {
		nic_unbind_and_destroy(dev);
		return ENOMEM;
	}

	nic_set_specific(nic_data, virtio_net);

	errno_t rc = virtio_pci_dev_initialize(dev, &virtio_net->virtio_dev);
	if (rc != EOK)
		return rc;

	virtio_dev_t *vdev = &virtio_net->virtio_dev;
	virtio_pci_common_cfg_t *cfg = virtio_net->virtio_dev.common_cfg;
	virtio_net_cfg_t *netcfg = virtio_net->virtio_dev.device_cfg;

	/* Reset the device and negotiate the feature bits */
	rc = virtio_device_setup_start(vdev,
	    VIRTIO_NET_F_MAC | VIRTIO_NET_F_CTRL_VQ);
	if (rc != EOK)
		goto fail;

	/* Perform device-specific setup */

	/*
	 * Discover and configure the virtqueues
	 */
	uint16_t num_queues = pio_read_16(&cfg->num_queues);
	if (num_queues != VIRTIO_NET_NUM_QUEUES) {
		ddf_msg(LVL_NOTE, "Unsupported number of virtqueues: %u",
		    num_queues);
		goto fail;
	}

	vdev->queues = calloc(sizeof(virtq_t), num_queues);
	if (!vdev->queues) {
		rc = ENOMEM;
		goto fail;
	}

	rc = virtio_virtq_setup(vdev, RX_QUEUE_1, RX_BUFFERS);
	if (rc != EOK)
		goto fail;
	rc = virtio_virtq_setup(vdev, TX_QUEUE_1, TX_BUFFERS);
	if (rc != EOK)
		goto fail;
	rc = virtio_virtq_setup(vdev, CT_QUEUE_1, CT_BUFFERS);
	if (rc != EOK)
		goto fail;

	/*
	 * Setup DMA buffers
	 */
	rc = virtio_net_setup_bufs(RX_BUFFERS, RX_BUF_SIZE, false,
	    virtio_net->rx_buf, virtio_net->rx_buf_p);
	if (rc != EOK)
		goto fail;
	rc = virtio_net_setup_bufs(TX_BUFFERS, TX_BUF_SIZE, true,
	    virtio_net->tx_buf, virtio_net->tx_buf_p);
	if (rc != EOK)
		goto fail;
	rc = virtio_net_setup_bufs(CT_BUFFERS, CT_BUF_SIZE, true,
	    virtio_net->ct_buf, virtio_net->ct_buf_p);
	if (rc != EOK)
		goto fail;

	/*
	 * Give all RX buffers to the NIC
	 */
	for (unsigned i = 0; i < RX_BUFFERS; i++) {
		/*
		 * Associtate the buffer with the descriptor, set length and
		 * flags.
		 */
		virtio_virtq_set_desc(vdev, RX_QUEUE_1, i,
		    virtio_net->rx_buf_p[i], RX_BUF_SIZE, VIRTQ_DESC_F_WRITE,
		    0);
		/*
		 * Put the set descriptor into the available ring of the RX
		 * queue.
		 */
		virtio_virtq_produce_available(vdev, RX_QUEUE_1, i);
	}

	/*
	 * Read the MAC address
	 */
	nic_address_t nic_addr;
	for (unsigned i = 0; i < 6; i++)
		nic_addr.address[i] = pio_read_8(&netcfg->mac[i]);
	rc = nic_report_address(nic_data, &nic_addr);
	if (rc != EOK)
		goto fail;

	ddf_msg(LVL_NOTE, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
	    nic_addr.address[0], nic_addr.address[1], nic_addr.address[2],
	    nic_addr.address[3], nic_addr.address[4], nic_addr.address[5]);

	/* Go live */
	virtio_device_setup_finalize(vdev);

	return EOK;

fail:
	virtio_net_teardown_bufs(virtio_net->rx_buf);
	virtio_net_teardown_bufs(virtio_net->tx_buf);
	virtio_net_teardown_bufs(virtio_net->ct_buf);

	virtio_device_setup_fail(vdev);
	virtio_pci_dev_cleanup(vdev);
	return rc;
}

static errno_t virtio_net_dev_add(ddf_dev_t *dev)
{
	ddf_msg(LVL_NOTE, "%s %s (handle = %zu)", __func__,
	    ddf_dev_get_name(dev), ddf_dev_get_handle(dev));

	errno_t rc = virtio_net_initialize(dev);
	if (rc != EOK)
		return rc;

	return ENOTSUP;
}

static ddf_dev_ops_t virtio_net_dev_ops;

static driver_ops_t virtio_net_driver_ops = {
	.dev_add = virtio_net_dev_add
};

static driver_t virtio_net_driver = {
	.name = NAME,
	.driver_ops = &virtio_net_driver_ops
};

static nic_iface_t virtio_net_nic_iface;

int main(void)
{
	printf("%s: HelenOS virtio-net driver\n", NAME);

	if (nic_driver_init(NAME) != EOK)
		return 1;

	nic_driver_implement(&virtio_net_driver_ops, &virtio_net_dev_ops,
	    &virtio_net_nic_iface);

	(void) ddf_log_init(NAME);
	return ddf_driver_main(&virtio_net_driver);
}
