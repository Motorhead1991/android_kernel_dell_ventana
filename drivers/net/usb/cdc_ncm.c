/*
 * cdc_ncm.c
 *
 * Copyright (C) ST-Ericsson 2010
 * Contact: Alexey Orishko <alexey.orishko@stericsson.com>
 * Original author: Hans Petter Selasky <hans.petter.selasky@stericsson.com>
 *
 * USB Host Driver for Network Control Model (NCM)
 * http:
 *
 * The NCM encoding, decoding and initialization logic
 * derives from FreeBSD 8.x. if_cdce.c and if_cdcereg.h
 *
 * This software is available to you under a choice of one of two
 * licenses. You may choose this file to be licensed under the terms
 * of the GNU General Public License (GPL) Version 2 or the 2-clause
 * BSD license listed below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/ctype.h>
#include <linux/ethtool.h>
#include <linux/workqueue.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/usb.h>
#include <linux/version.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
#include <linux/atomic.h>
#endif
#include <linux/usb/usbnet.h>
#include <linux/usb/cdc.h>

#define	DRIVER_VERSION				"30-Nov-2010"


#define USB_CDC_NCM_NDP16_LENGTH_MIN		0x10


#define	CDC_NCM_NTB_MAX_SIZE_TX			16384	
#define	CDC_NCM_NTB_MAX_SIZE_RX			16384	


#define	CDC_NCM_MIN_DATAGRAM_SIZE		1514	

#define	CDC_NCM_MIN_TX_PKT			512	


#define	CDC_NCM_MAX_DATAGRAM_SIZE		2048	


#define	CDC_NCM_DPT_DATAGRAMS_MAX		32


#define	CDC_NCM_RESTART_TIMER_DATAGRAM_CNT	3


#define	CDC_NCM_MIN_HDR_SIZE \
	(sizeof(struct usb_cdc_ncm_nth16) + sizeof(struct usb_cdc_ncm_ndp16) + \
	(CDC_NCM_DPT_DATAGRAMS_MAX + 1) * sizeof(struct usb_cdc_ncm_dpe16))

struct connection_speed_change {
	__le32	USBitRate; 
	__le32	DSBitRate; 
} __attribute__ ((packed));

struct cdc_ncm_data {
	struct usb_cdc_ncm_nth16 nth16;
	struct usb_cdc_ncm_ndp16 ndp16;
	struct usb_cdc_ncm_dpe16 dpe16[CDC_NCM_DPT_DATAGRAMS_MAX + 1];
};

struct cdc_ncm_ctx {
	struct cdc_ncm_data rx_ncm;
	struct cdc_ncm_data tx_ncm;
	struct usb_cdc_ncm_ntb_parameters ncm_parm;
	struct timer_list tx_timer;

	const struct usb_cdc_ncm_desc *func_desc;
	const struct usb_cdc_header_desc *header_desc;
	const struct usb_cdc_union_desc *union_desc;
	const struct usb_cdc_ether_desc *ether_desc;

	struct net_device *netdev;
	struct usb_device *udev;
	struct usb_host_endpoint *in_ep;
	struct usb_host_endpoint *out_ep;
	struct usb_host_endpoint *status_ep;
	struct usb_interface *intf;
	struct usb_interface *control;
	struct usb_interface *data;

	struct sk_buff *tx_curr_skb;
	struct sk_buff *tx_rem_skb;

	spinlock_t mtx;

	u32 tx_timer_pending;
	u32 tx_curr_offset;
	u32 tx_curr_last_offset;
	u32 tx_curr_frame_num;
	u32 rx_speed;
	u32 tx_speed;
	u32 rx_max;
	u32 tx_max;
	u32 max_datagram_size;
	u16 tx_max_datagrams;
	u16 tx_remainder;
	u16 tx_modulus;
	u16 tx_ndp_modulus;
	u16 tx_seq;
	u16 connected;
	u8 data_claimed;
	u8 control_claimed;
};

static void cdc_ncm_tx_timeout(unsigned long arg);
static const struct driver_info cdc_ncm_info;
static struct usb_driver cdc_ncm_driver;
static struct ethtool_ops cdc_ncm_ethtool_ops;

static const struct usb_device_id cdc_devs[] = {
	{ USB_INTERFACE_INFO(USB_CLASS_COMM,
		USB_CDC_SUBCLASS_NCM, USB_CDC_PROTO_NONE),
		.driver_info = (unsigned long)&cdc_ncm_info,
	},
	{
	},
};

MODULE_DEVICE_TABLE(usb, cdc_devs);

static void
cdc_ncm_get_drvinfo(struct net_device *net, struct ethtool_drvinfo *info)
{
	struct usbnet *dev = netdev_priv(net);

	strncpy(info->driver, dev->driver_name, sizeof(info->driver));
	strncpy(info->version, DRIVER_VERSION, sizeof(info->version));
	strncpy(info->fw_version, dev->driver_info->description,
		sizeof(info->fw_version));
	usb_make_path(dev->udev, info->bus_info, sizeof(info->bus_info));
}

static int
cdc_ncm_do_request(struct cdc_ncm_ctx *ctx, struct usb_cdc_notification *req,
		   void *data, u16 flags, u16 *actlen, u16 timeout)
{
	int err;

	err = usb_control_msg(ctx->udev, (req->bmRequestType & USB_DIR_IN) ?
				usb_rcvctrlpipe(ctx->udev, 0) :
				usb_sndctrlpipe(ctx->udev, 0),
				req->bNotificationType, req->bmRequestType,
				req->wValue,
				req->wIndex, data,
				req->wLength, timeout);

	if (err < 0) {
		if (actlen)
			*actlen = 0;
		return err;
	}

	if (actlen)
		*actlen = err;

	return 0;
}

static u8 cdc_ncm_setup(struct cdc_ncm_ctx *ctx)
{
	struct usb_cdc_notification req;
	u32 val;
	__le16 max_datagram_size;
	u8 flags;
	u8 iface_no;
	int err;

	iface_no = ctx->control->cur_altsetting->desc.bInterfaceNumber;

	req.bmRequestType = USB_TYPE_CLASS | USB_DIR_IN | USB_RECIP_INTERFACE;
	req.bNotificationType = USB_CDC_GET_NTB_PARAMETERS;
	req.wValue = 0;
	req.wIndex = cpu_to_le16(iface_no);
	req.wLength = cpu_to_le16(sizeof(ctx->ncm_parm));

	err = cdc_ncm_do_request(ctx, &req, &ctx->ncm_parm, 0, NULL, 1000);
	if (err) {
		pr_debug("failed GET_NTB_PARAMETERS\n");
		return 1;
	}

	
	ctx->rx_max = le32_to_cpu(ctx->ncm_parm.dwNtbInMaxSize);
	ctx->tx_max = le32_to_cpu(ctx->ncm_parm.dwNtbOutMaxSize);
	ctx->tx_remainder = le16_to_cpu(ctx->ncm_parm.wNdpOutPayloadRemainder);
	ctx->tx_modulus = le16_to_cpu(ctx->ncm_parm.wNdpOutDivisor);
	ctx->tx_ndp_modulus = le16_to_cpu(ctx->ncm_parm.wNdpOutAlignment);

	if (ctx->func_desc != NULL)
		flags = ctx->func_desc->bmNetworkCapabilities;
	else
		flags = 0;

	pr_debug("dwNtbInMaxSize=%u dwNtbOutMaxSize=%u "
		 "wNdpOutPayloadRemainder=%u wNdpOutDivisor=%u "
		 "wNdpOutAlignment=%u flags=0x%x\n",
		 ctx->rx_max, ctx->tx_max, ctx->tx_remainder, ctx->tx_modulus,
		 ctx->tx_ndp_modulus, flags);

	
	ctx->tx_max_datagrams = CDC_NCM_DPT_DATAGRAMS_MAX;

	
	if ((ctx->rx_max <
	    (CDC_NCM_MIN_HDR_SIZE + CDC_NCM_MIN_DATAGRAM_SIZE)) ||
	    (ctx->rx_max > CDC_NCM_NTB_MAX_SIZE_RX)) {
		pr_debug("Using default maximum receive length=%d\n",
						CDC_NCM_NTB_MAX_SIZE_RX);
		ctx->rx_max = CDC_NCM_NTB_MAX_SIZE_RX;
	}

	
	if ((ctx->tx_max <
	    (CDC_NCM_MIN_HDR_SIZE + CDC_NCM_MIN_DATAGRAM_SIZE)) ||
	    (ctx->tx_max > CDC_NCM_NTB_MAX_SIZE_TX)) {
		pr_debug("Using default maximum transmit length=%d\n",
						CDC_NCM_NTB_MAX_SIZE_TX);
		ctx->tx_max = CDC_NCM_NTB_MAX_SIZE_TX;
	}

	
	val = ctx->tx_ndp_modulus;

	if ((val < USB_CDC_NCM_NDP_ALIGN_MIN_SIZE) ||
	    (val != ((-val) & val)) || (val >= ctx->tx_max)) {
		pr_debug("Using default alignment: 4 bytes\n");
		ctx->tx_ndp_modulus = USB_CDC_NCM_NDP_ALIGN_MIN_SIZE;
	}

	
	val = ctx->tx_modulus;

	if ((val < USB_CDC_NCM_NDP_ALIGN_MIN_SIZE) ||
	    (val != ((-val) & val)) || (val >= ctx->tx_max)) {
		pr_debug("Using default transmit modulus: 4 bytes\n");
		ctx->tx_modulus = USB_CDC_NCM_NDP_ALIGN_MIN_SIZE;
	}

	
	if (ctx->tx_remainder >= ctx->tx_modulus) {
		pr_debug("Using default transmit remainder: 0 bytes\n");
		ctx->tx_remainder = 0;
	}

	
	ctx->tx_remainder = ((ctx->tx_remainder - ETH_HLEN) &
						(ctx->tx_modulus - 1));

	

	
	req.bmRequestType = USB_TYPE_CLASS | USB_DIR_OUT | USB_RECIP_INTERFACE;
	req.bNotificationType = USB_CDC_SET_CRC_MODE;
	req.wValue = cpu_to_le16(USB_CDC_NCM_CRC_NOT_APPENDED);
	req.wIndex = cpu_to_le16(iface_no);
	req.wLength = 0;

	err = cdc_ncm_do_request(ctx, &req, NULL, 0, NULL, 1000);
	if (err)
		pr_debug("Setting CRC mode off failed\n");

	
	req.bmRequestType = USB_TYPE_CLASS | USB_DIR_OUT | USB_RECIP_INTERFACE;
	req.bNotificationType = USB_CDC_SET_NTB_FORMAT;
	req.wValue = cpu_to_le16(USB_CDC_NCM_NTB16_FORMAT);
	req.wIndex = cpu_to_le16(iface_no);
	req.wLength = 0;

	err = cdc_ncm_do_request(ctx, &req, NULL, 0, NULL, 1000);
	if (err)
		pr_debug("Setting NTB format to 16-bit failed\n");

	
	req.bmRequestType = USB_TYPE_CLASS | USB_DIR_IN | USB_RECIP_INTERFACE;
	req.bNotificationType = USB_CDC_GET_MAX_DATAGRAM_SIZE;
	req.wValue = 0;
	req.wIndex = cpu_to_le16(iface_no);
	req.wLength = cpu_to_le16(2);

	err = cdc_ncm_do_request(ctx, &req, &max_datagram_size, 0, NULL, 1000);
	if (err) {
		pr_debug(" GET_MAX_DATAGRAM_SIZE failed, using size=%u\n",
			 CDC_NCM_MIN_DATAGRAM_SIZE);
		
		ctx->max_datagram_size = CDC_NCM_MIN_DATAGRAM_SIZE;
	} else {
		ctx->max_datagram_size = le16_to_cpu(max_datagram_size);

		if (ctx->max_datagram_size < CDC_NCM_MIN_DATAGRAM_SIZE)
			ctx->max_datagram_size = CDC_NCM_MIN_DATAGRAM_SIZE;
		else if (ctx->max_datagram_size > CDC_NCM_MAX_DATAGRAM_SIZE)
			ctx->max_datagram_size = CDC_NCM_MAX_DATAGRAM_SIZE;
	}

	if (ctx->netdev->mtu != (ctx->max_datagram_size - ETH_HLEN))
		ctx->netdev->mtu = ctx->max_datagram_size - ETH_HLEN;

	return 0;
}

static void
cdc_ncm_find_endpoints(struct cdc_ncm_ctx *ctx, struct usb_interface *intf)
{
	struct usb_host_endpoint *e;
	u8 ep;

	for (ep = 0; ep < intf->cur_altsetting->desc.bNumEndpoints; ep++) {

		e = intf->cur_altsetting->endpoint + ep;
		switch (e->desc.bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) {
		case USB_ENDPOINT_XFER_INT:
			if (usb_endpoint_dir_in(&e->desc)) {
				if (ctx->status_ep == NULL)
					ctx->status_ep = e;
			}
			break;

		case USB_ENDPOINT_XFER_BULK:
			if (usb_endpoint_dir_in(&e->desc)) {
				if (ctx->in_ep == NULL)
					ctx->in_ep = e;
			} else {
				if (ctx->out_ep == NULL)
					ctx->out_ep = e;
			}
			break;

		default:
			break;
		}
	}
}

static void cdc_ncm_free(struct cdc_ncm_ctx *ctx)
{
	if (ctx == NULL)
		return;

	del_timer_sync(&ctx->tx_timer);

	if (ctx->data_claimed) {
		usb_set_intfdata(ctx->data, NULL);
		usb_driver_release_interface(driver_of(ctx->intf), ctx->data);
	}

	if (ctx->control_claimed) {
		usb_set_intfdata(ctx->control, NULL);
		usb_driver_release_interface(driver_of(ctx->intf),
								ctx->control);
	}

	if (ctx->tx_rem_skb != NULL) {
		dev_kfree_skb_any(ctx->tx_rem_skb);
		ctx->tx_rem_skb = NULL;
	}

	if (ctx->tx_curr_skb != NULL) {
		dev_kfree_skb_any(ctx->tx_curr_skb);
		ctx->tx_curr_skb = NULL;
	}

	kfree(ctx);
}

static int cdc_ncm_bind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_ncm_ctx *ctx;
	struct usb_driver *driver;
	u8 *buf;
	int len;
	int temp;
	u8 iface_no;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (ctx == NULL)
		goto error;

	memset(ctx, 0, sizeof(*ctx));

	init_timer(&ctx->tx_timer);
	spin_lock_init(&ctx->mtx);
	ctx->netdev = dev->net;

	
	dev->data[0] = (unsigned long)ctx;

	
	driver = driver_of(intf);
	buf = intf->cur_altsetting->extra;
	len = intf->cur_altsetting->extralen;

	ctx->udev = dev->udev;
	ctx->intf = intf;

	
	while ((len > 0) && (buf[0] > 2) && (buf[0] <= len)) {

		if (buf[1] != USB_DT_CS_INTERFACE)
			goto advance;

		switch (buf[2]) {
		case USB_CDC_UNION_TYPE:
			if (buf[0] < sizeof(*(ctx->union_desc)))
				break;

			ctx->union_desc =
					(const struct usb_cdc_union_desc *)buf;

			ctx->control = usb_ifnum_to_if(dev->udev,
					ctx->union_desc->bMasterInterface0);
			ctx->data = usb_ifnum_to_if(dev->udev,
					ctx->union_desc->bSlaveInterface0);
			break;

		case USB_CDC_ETHERNET_TYPE:
			if (buf[0] < sizeof(*(ctx->ether_desc)))
				break;

			ctx->ether_desc =
					(const struct usb_cdc_ether_desc *)buf;

			dev->hard_mtu =
				le16_to_cpu(ctx->ether_desc->wMaxSegmentSize);

			if (dev->hard_mtu <
			    (CDC_NCM_MIN_DATAGRAM_SIZE - ETH_HLEN))
				dev->hard_mtu =
					CDC_NCM_MIN_DATAGRAM_SIZE - ETH_HLEN;

			else if (dev->hard_mtu >
				 (CDC_NCM_MAX_DATAGRAM_SIZE - ETH_HLEN))
				dev->hard_mtu =
					CDC_NCM_MAX_DATAGRAM_SIZE - ETH_HLEN;
			break;

		case USB_CDC_NCM_TYPE:
			if (buf[0] < sizeof(*(ctx->func_desc)))
				break;

			ctx->func_desc = (const struct usb_cdc_ncm_desc *)buf;
			break;

		default:
			break;
		}
advance:
		
		temp = buf[0];
		buf += temp;
		len -= temp;
	}

	
	if ((ctx->control == NULL) || (ctx->data == NULL) ||
	    (ctx->ether_desc == NULL))
		goto error;

	
	if (ctx->data != intf) {
		temp = usb_driver_claim_interface(driver, ctx->data, dev);
		if (temp)
			goto error;
		ctx->data_claimed = 1;
	}

	if (ctx->control != intf) {
		temp = usb_driver_claim_interface(driver, ctx->control, dev);
		if (temp)
			goto error;
		ctx->control_claimed = 1;
	}

	iface_no = ctx->data->cur_altsetting->desc.bInterfaceNumber;

	
	temp = usb_set_interface(dev->udev, iface_no, 0);
	if (temp)
		goto error;

	
	if (cdc_ncm_setup(ctx))
		goto error;

	
	temp = usb_set_interface(dev->udev, iface_no, 1);
	if (temp)
		goto error;

	cdc_ncm_find_endpoints(ctx, ctx->data);
	cdc_ncm_find_endpoints(ctx, ctx->control);

	if ((ctx->in_ep == NULL) || (ctx->out_ep == NULL) ||
	    (ctx->status_ep == NULL))
		goto error;

	dev->net->ethtool_ops = &cdc_ncm_ethtool_ops;

	usb_set_intfdata(ctx->data, dev);
	usb_set_intfdata(ctx->control, dev);
	usb_set_intfdata(ctx->intf, dev);

	temp = usbnet_get_ethernet_addr(dev, ctx->ether_desc->iMACAddress);
	if (temp)
		goto error;

	dev_info(&dev->udev->dev, "MAC-Address: "
				"0x%02x:0x%02x:0x%02x:0x%02x:0x%02x:0x%02x\n",
				dev->net->dev_addr[0], dev->net->dev_addr[1],
				dev->net->dev_addr[2], dev->net->dev_addr[3],
				dev->net->dev_addr[4], dev->net->dev_addr[5]);

	dev->in = usb_rcvbulkpipe(dev->udev,
		ctx->in_ep->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->out = usb_sndbulkpipe(dev->udev,
		ctx->out_ep->desc.bEndpointAddress & USB_ENDPOINT_NUMBER_MASK);
	dev->status = ctx->status_ep;
	dev->rx_urb_size = ctx->rx_max;

	
	netif_carrier_off(dev->net);
	ctx->tx_speed = ctx->rx_speed = 0;
	return 0;

error:
	cdc_ncm_free((struct cdc_ncm_ctx *)dev->data[0]);
	dev->data[0] = 0;
	dev_info(&dev->udev->dev, "Descriptor failure\n");
	return -ENODEV;
}

static void cdc_ncm_unbind(struct usbnet *dev, struct usb_interface *intf)
{
	struct cdc_ncm_ctx *ctx = (struct cdc_ncm_ctx *)dev->data[0];
	struct usb_driver *driver;

	if (ctx == NULL)
		return;		

	driver = driver_of(intf);

	usb_set_intfdata(ctx->data, NULL);
	usb_set_intfdata(ctx->control, NULL);
	usb_set_intfdata(ctx->intf, NULL);

	
	if (ctx->data_claimed) {
		usb_driver_release_interface(driver, ctx->data);
		ctx->data_claimed = 0;
	}

	if (ctx->control_claimed) {
		usb_driver_release_interface(driver, ctx->control);
		ctx->control_claimed = 0;
	}

	cdc_ncm_free(ctx);
}

static void cdc_ncm_zero_fill(u8 *ptr, u32 first, u32 end, u32 max)
{
	if (first >= max)
		return;
	if (first >= end)
		return;
	if (end > max)
		end = max;
	memset(ptr + first, 0, end - first);
}

static struct sk_buff *
cdc_ncm_fill_tx_frame(struct cdc_ncm_ctx *ctx, struct sk_buff *skb)
{
	struct sk_buff *skb_out;
	u32 rem;
	u32 offset;
	u32 last_offset;
	u16 n = 0;
	u8 timeout = 0;

	
	if (skb != NULL)
		swap(skb, ctx->tx_rem_skb);
	else
		timeout = 1;

	

	
	if (ctx->tx_curr_skb != NULL) {
		
		skb_out = ctx->tx_curr_skb;
		offset = ctx->tx_curr_offset;
		last_offset = ctx->tx_curr_last_offset;
		n = ctx->tx_curr_frame_num;

	} else {
		
		skb_out = alloc_skb(ctx->tx_max, GFP_ATOMIC);
		if (skb_out == NULL) {
			if (skb != NULL) {
				dev_kfree_skb_any(skb);
				ctx->netdev->stats.tx_dropped++;
			}
			goto exit_no_skb;
		}

		
		offset = ALIGN(sizeof(struct usb_cdc_ncm_nth16),
					ctx->tx_ndp_modulus) +
					sizeof(struct usb_cdc_ncm_ndp16) +
					(ctx->tx_max_datagrams + 1) *
					sizeof(struct usb_cdc_ncm_dpe16);

		
		last_offset = offset;
		
		offset = ALIGN(offset, ctx->tx_modulus) + ctx->tx_remainder;
		
		cdc_ncm_zero_fill(skb_out->data, 0, offset, offset);
		n = 0;
		ctx->tx_curr_frame_num = 0;
	}

	for (; n < ctx->tx_max_datagrams; n++) {
		
		if (offset >= ctx->tx_max)
			break;

		
		rem = ctx->tx_max - offset;

		if (skb == NULL) {
			skb = ctx->tx_rem_skb;
			ctx->tx_rem_skb = NULL;

			
			if (skb == NULL)
				break;
		}

		if (skb->len > rem) {
			if (n == 0) {
				
				dev_kfree_skb_any(skb);
				skb = NULL;
				ctx->netdev->stats.tx_dropped++;
			} else {
				
				if (ctx->tx_rem_skb != NULL) {
					dev_kfree_skb_any(ctx->tx_rem_skb);
					ctx->netdev->stats.tx_dropped++;
				}
				ctx->tx_rem_skb = skb;
				skb = NULL;

				
				timeout = 1;
			}
			break;
		}

		memcpy(((u8 *)skb_out->data) + offset, skb->data, skb->len);

		ctx->tx_ncm.dpe16[n].wDatagramLength = cpu_to_le16(skb->len);
		ctx->tx_ncm.dpe16[n].wDatagramIndex = cpu_to_le16(offset);

		
		offset += skb->len;

		
		last_offset = offset;

		
		offset = ALIGN(offset, ctx->tx_modulus) + ctx->tx_remainder;

		
		cdc_ncm_zero_fill(skb_out->data, last_offset, offset,
								ctx->tx_max);
		dev_kfree_skb_any(skb);
		skb = NULL;
	}

	
	if (skb != NULL) {
		dev_kfree_skb_any(skb);
		skb = NULL;
		ctx->netdev->stats.tx_dropped++;
	}

	ctx->tx_curr_frame_num = n;

	if (n == 0) {
		
		
		ctx->tx_curr_skb = skb_out;
		ctx->tx_curr_offset = offset;
		ctx->tx_curr_last_offset = last_offset;
		goto exit_no_skb;

	} else if ((n < ctx->tx_max_datagrams) && (timeout == 0)) {
		
		
		ctx->tx_curr_skb = skb_out;
		ctx->tx_curr_offset = offset;
		ctx->tx_curr_last_offset = last_offset;
		
		if (n < CDC_NCM_RESTART_TIMER_DATAGRAM_CNT)
			ctx->tx_timer_pending = 2;
		goto exit_no_skb;

	} else {
		
		
	}

	
	if (last_offset > ctx->tx_max)
		last_offset = ctx->tx_max;

	
	offset = last_offset;

	
	if (offset > CDC_NCM_MIN_TX_PKT)
		offset = ctx->tx_max;

	
	cdc_ncm_zero_fill(skb_out->data, last_offset, offset, ctx->tx_max);

	
	last_offset = offset;

	if ((last_offset < ctx->tx_max) && ((last_offset %
			le16_to_cpu(ctx->out_ep->desc.wMaxPacketSize)) == 0)) {
		
		*(((u8 *)skb_out->data) + last_offset) = 0;
		last_offset++;
	}

	
	for (; n <= CDC_NCM_DPT_DATAGRAMS_MAX; n++) {
		ctx->tx_ncm.dpe16[n].wDatagramLength = 0;
		ctx->tx_ncm.dpe16[n].wDatagramIndex = 0;
	}

	
	ctx->tx_ncm.nth16.dwSignature = cpu_to_le32(USB_CDC_NCM_NTH16_SIGN);
	ctx->tx_ncm.nth16.wHeaderLength =
					cpu_to_le16(sizeof(ctx->tx_ncm.nth16));
	ctx->tx_ncm.nth16.wSequence = cpu_to_le16(ctx->tx_seq);
	ctx->tx_ncm.nth16.wBlockLength = cpu_to_le16(last_offset);
	ctx->tx_ncm.nth16.wFpIndex = ALIGN(sizeof(struct usb_cdc_ncm_nth16),
							ctx->tx_ndp_modulus);

	memcpy(skb_out->data, &(ctx->tx_ncm.nth16), sizeof(ctx->tx_ncm.nth16));
	ctx->tx_seq++;

	
	ctx->tx_ncm.ndp16.dwSignature =
				cpu_to_le32(USB_CDC_NCM_NDP16_NOCRC_SIGN);
	rem = sizeof(ctx->tx_ncm.ndp16) + ((ctx->tx_curr_frame_num + 1) *
					sizeof(struct usb_cdc_ncm_dpe16));
	ctx->tx_ncm.ndp16.wLength = cpu_to_le16(rem);
	ctx->tx_ncm.ndp16.wNextFpIndex = 0; 

	memcpy(((u8 *)skb_out->data) + ctx->tx_ncm.nth16.wFpIndex,
						&(ctx->tx_ncm.ndp16),
						sizeof(ctx->tx_ncm.ndp16));

	memcpy(((u8 *)skb_out->data) + ctx->tx_ncm.nth16.wFpIndex +
					sizeof(ctx->tx_ncm.ndp16),
					&(ctx->tx_ncm.dpe16),
					(ctx->tx_curr_frame_num + 1) *
					sizeof(struct usb_cdc_ncm_dpe16));

	
	skb_put(skb_out, last_offset);

	
	ctx->tx_curr_skb = NULL;
	return skb_out;

exit_no_skb:
	return NULL;
}

static void cdc_ncm_tx_timeout_start(struct cdc_ncm_ctx *ctx)
{
	
	if (timer_pending(&ctx->tx_timer) == 0) {
		ctx->tx_timer.function = &cdc_ncm_tx_timeout;
		ctx->tx_timer.data = (unsigned long)ctx;
		ctx->tx_timer.expires = jiffies + ((HZ + 999) / 1000);
		add_timer(&ctx->tx_timer);
	}
}

static void cdc_ncm_tx_timeout(unsigned long arg)
{
	struct cdc_ncm_ctx *ctx = (struct cdc_ncm_ctx *)arg;
	u8 restart;

	spin_lock(&ctx->mtx);
	if (ctx->tx_timer_pending != 0) {
		ctx->tx_timer_pending--;
		restart = 1;
	} else
		restart = 0;

	spin_unlock(&ctx->mtx);

	if (restart)
		cdc_ncm_tx_timeout_start(ctx);
	else if (ctx->netdev != NULL)
		usbnet_start_xmit(NULL, ctx->netdev);
}

static struct sk_buff *
cdc_ncm_tx_fixup(struct usbnet *dev, struct sk_buff *skb, gfp_t flags)
{
	struct sk_buff *skb_out;
	struct cdc_ncm_ctx *ctx = (struct cdc_ncm_ctx *)dev->data[0];
	u8 need_timer = 0;

	
	if (ctx == NULL)
		goto error;

	spin_lock(&ctx->mtx);
	skb_out = cdc_ncm_fill_tx_frame(ctx, skb);
	if (ctx->tx_curr_skb != NULL)
		need_timer = 1;
	spin_unlock(&ctx->mtx);

	
	if (need_timer)
		cdc_ncm_tx_timeout_start(ctx);

	if (skb_out)
		dev->net->stats.tx_packets += ctx->tx_curr_frame_num;
	return skb_out;

error:
	if (skb != NULL)
		dev_kfree_skb_any(skb);

	return NULL;
}

static int cdc_ncm_rx_fixup(struct usbnet *dev, struct sk_buff *skb_in)
{
	struct sk_buff *skb;
	struct cdc_ncm_ctx *ctx;
	int sumlen;
	int actlen;
	int temp;
	int nframes;
	int x;
	int offset;

	ctx = (struct cdc_ncm_ctx *)dev->data[0];
	if (ctx == NULL)
		goto error;

	actlen = skb_in->len;
	sumlen = CDC_NCM_NTB_MAX_SIZE_RX;

	if (actlen < (sizeof(ctx->rx_ncm.nth16) + sizeof(ctx->rx_ncm.ndp16))) {
		pr_debug("frame too short\n");
		goto error;
	}

	memcpy(&(ctx->rx_ncm.nth16), ((u8 *)skb_in->data),
						sizeof(ctx->rx_ncm.nth16));

	if (le32_to_cpu(ctx->rx_ncm.nth16.dwSignature) !=
	    USB_CDC_NCM_NTH16_SIGN) {
		pr_debug("invalid NTH16 signature <%u>\n",
			 le32_to_cpu(ctx->rx_ncm.nth16.dwSignature));
		goto error;
	}

	temp = le16_to_cpu(ctx->rx_ncm.nth16.wBlockLength);
	if (temp > sumlen) {
		pr_debug("unsupported NTB block length %u/%u\n", temp, sumlen);
		goto error;
	}

	temp = le16_to_cpu(ctx->rx_ncm.nth16.wFpIndex);
	if ((temp + sizeof(ctx->rx_ncm.ndp16)) > actlen) {
		pr_debug("invalid DPT16 index\n");
		goto error;
	}

	memcpy(&(ctx->rx_ncm.ndp16), ((u8 *)skb_in->data) + temp,
						sizeof(ctx->rx_ncm.ndp16));

	if (le32_to_cpu(ctx->rx_ncm.ndp16.dwSignature) !=
	    USB_CDC_NCM_NDP16_NOCRC_SIGN) {
		pr_debug("invalid DPT16 signature <%u>\n",
			 le32_to_cpu(ctx->rx_ncm.ndp16.dwSignature));
		goto error;
	}

	if (le16_to_cpu(ctx->rx_ncm.ndp16.wLength) <
	    USB_CDC_NCM_NDP16_LENGTH_MIN) {
		pr_debug("invalid DPT16 length <%u>\n",
			 le32_to_cpu(ctx->rx_ncm.ndp16.dwSignature));
		goto error;
	}

	nframes = ((le16_to_cpu(ctx->rx_ncm.ndp16.wLength) -
					sizeof(struct usb_cdc_ncm_ndp16)) /
					sizeof(struct usb_cdc_ncm_dpe16));
	nframes--; 

	pr_debug("nframes = %u\n", nframes);

	temp += sizeof(ctx->rx_ncm.ndp16);

	if ((temp + nframes * (sizeof(struct usb_cdc_ncm_dpe16))) > actlen) {
		pr_debug("Invalid nframes = %d\n", nframes);
		goto error;
	}

	if (nframes > CDC_NCM_DPT_DATAGRAMS_MAX) {
		pr_debug("Truncating number of frames from %u to %u\n",
					nframes, CDC_NCM_DPT_DATAGRAMS_MAX);
		nframes = CDC_NCM_DPT_DATAGRAMS_MAX;
	}

	memcpy(&(ctx->rx_ncm.dpe16), ((u8 *)skb_in->data) + temp,
				nframes * (sizeof(struct usb_cdc_ncm_dpe16)));

	for (x = 0; x < nframes; x++) {
		offset = le16_to_cpu(ctx->rx_ncm.dpe16[x].wDatagramIndex);
		temp = le16_to_cpu(ctx->rx_ncm.dpe16[x].wDatagramLength);

		
		if ((offset == 0) || (temp == 0)) {
			if (!x)
				goto error; 
			break;
		}

		
		if (((offset + temp) > actlen) ||
		    (temp > CDC_NCM_MAX_DATAGRAM_SIZE) || (temp < ETH_HLEN)) {
			pr_debug("invalid frame detected (ignored)"
				"offset[%u]=%u, length=%u, skb=%p\n",
							x, offset, temp, skb);
			if (!x)
				goto error;
			break;

		} else {
			skb = skb_clone(skb_in, GFP_ATOMIC);
			skb->len = temp;
			skb->data = ((u8 *)skb_in->data) + offset;
			skb_set_tail_pointer(skb, temp);
			usbnet_skb_return(dev, skb);
		}
	}
	return 1;
error:
	return 0;
}

static void
cdc_ncm_speed_change(struct cdc_ncm_ctx *ctx,
		     struct connection_speed_change *data)
{
	uint32_t rx_speed = le32_to_cpu(data->USBitRate);
	uint32_t tx_speed = le32_to_cpu(data->DSBitRate);

	
	if ((tx_speed != ctx->tx_speed) || (rx_speed != ctx->rx_speed)) {
		ctx->tx_speed = tx_speed;
		ctx->rx_speed = rx_speed;

		if ((tx_speed > 1000000) && (rx_speed > 1000000)) {
			printk(KERN_INFO KBUILD_MODNAME
				": %s: %u mbit/s downlink "
				"%u mbit/s uplink\n",
				ctx->netdev->name,
				(unsigned int)(rx_speed / 1000000U),
				(unsigned int)(tx_speed / 1000000U));
		} else {
			printk(KERN_INFO KBUILD_MODNAME
				": %s: %u kbit/s downlink "
				"%u kbit/s uplink\n",
				ctx->netdev->name,
				(unsigned int)(rx_speed / 1000U),
				(unsigned int)(tx_speed / 1000U));
		}
	}
}

static void cdc_ncm_status(struct usbnet *dev, struct urb *urb)
{
	struct cdc_ncm_ctx *ctx;
	struct usb_cdc_notification *event;

	ctx = (struct cdc_ncm_ctx *)dev->data[0];

	if (urb->actual_length < sizeof(*event))
		return;

	
	if (test_and_clear_bit(EVENT_STS_SPLIT, &dev->flags)) {
		cdc_ncm_speed_change(ctx,
		      (struct connection_speed_change *)urb->transfer_buffer);
		return;
	}

	event = urb->transfer_buffer;

	switch (event->bNotificationType) {
	case USB_CDC_NOTIFY_NETWORK_CONNECTION:
		
		ctx->connected = event->wValue;

		printk(KERN_INFO KBUILD_MODNAME ": %s: network connection:"
			" %sconnected\n",
			ctx->netdev->name, ctx->connected ? "" : "dis");

		if (ctx->connected)
			netif_carrier_on(dev->net);
		else {
			netif_carrier_off(dev->net);
			ctx->tx_speed = ctx->rx_speed = 0;
		}
		break;

	case USB_CDC_NOTIFY_SPEED_CHANGE:
		if (urb->actual_length <
		    (sizeof(*event) + sizeof(struct connection_speed_change)))
			set_bit(EVENT_STS_SPLIT, &dev->flags);
		else
			cdc_ncm_speed_change(ctx,
				(struct connection_speed_change *) &event[1]);
		break;

	default:
		dev_err(&dev->udev->dev, "NCM: unexpected "
			"notification 0x%02x!\n", event->bNotificationType);
		break;
	}
}

static int cdc_ncm_check_connect(struct usbnet *dev)
{
	struct cdc_ncm_ctx *ctx;

	ctx = (struct cdc_ncm_ctx *)dev->data[0];
	if (ctx == NULL)
		return 1;	

	return !ctx->connected;
}

cdc_ncm_probe(struct usb_interface *udev, const struct usb_device_id *prod)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
	udev->needs_remote_wakeup = 1;
#endif
	return usbnet_probe(udev, prod);
}

static void cdc_ncm_disconnect(struct usb_interface *intf)
{
	struct usbnet *dev = usb_get_intfdata(intf);

	if (dev == NULL)
		return;		

	usbnet_disconnect(intf);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
static int cdc_ncm_manage_power(struct usbnet *dev, int status)
{
	dev->intf->needs_remote_wakeup = status;
	return 0;
}
#endif

static const struct driver_info cdc_ncm_info = {
	.description = "CDC NCM",
	.flags = FLAG_NO_SETINT | FLAG_MULTI_PACKET,
	.bind = cdc_ncm_bind,
	.unbind = cdc_ncm_unbind,
	.check_connect = cdc_ncm_check_connect,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33))
	.manage_power = cdc_ncm_manage_power,
#endif
	.status = cdc_ncm_status,
	.rx_fixup = cdc_ncm_rx_fixup,
	.tx_fixup = cdc_ncm_tx_fixup,
};

static struct usb_driver cdc_ncm_driver = {
	.name = "cdc_ncm",
	.id_table = cdc_devs,
	.probe = cdc_ncm_probe,
	.disconnect = cdc_ncm_disconnect,
	.suspend = usbnet_suspend,
	.resume = usbnet_resume,
	.supports_autosuspend = 1,
};

static struct ethtool_ops cdc_ncm_ethtool_ops = {
	.get_drvinfo = cdc_ncm_get_drvinfo,
	.get_link = usbnet_get_link,
	.get_msglevel = usbnet_get_msglevel,
	.set_msglevel = usbnet_set_msglevel,
	.get_settings = usbnet_get_settings,
	.set_settings = usbnet_set_settings,
	.nway_reset = usbnet_nway_reset,
};

static int __init cdc_ncm_init(void)
{
	printk(KERN_INFO KBUILD_MODNAME ": " DRIVER_VERSION "\n");
	return usb_register(&cdc_ncm_driver);
}

module_init(cdc_ncm_init);

static void __exit cdc_ncm_exit(void)
{
	usb_deregister(&cdc_ncm_driver);
}

module_exit(cdc_ncm_exit);

MODULE_AUTHOR("Hans Petter Selasky");
MODULE_DESCRIPTION("USB CDC NCM host driver");
MODULE_LICENSE("Dual BSD/GPL");
