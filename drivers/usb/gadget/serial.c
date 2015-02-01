/*
 * serial.c
  *
 * Copyright (C) 2010 by Franko Fang <huananhu@huawei.com> (Huawei Technologies Co., Ltd.)
  *
 * Released under the GPLv2.
  *
  *
 * History:
  *   2011-02-09: optimize the declarations of the endpoints for the interfaces. (Franko Fang <huananhu@huawei.com>)
 *  	Update the driver version to be v1.00.01.00 from v1.00.00.00.
 */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/device.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/usb.h>
#include <linux/usb/serial.h>
#include <linux/usb/ch9.h>
#include <linux/usb/cdc.h>
#include <linux/usb/gadget.h>

#include "gadget_chips.h"

#include "../core/gadgetutil.h"







MODULE_DESCRIPTION("Bridge driver for the communication between PC and Huawei modules");	
MODULE_AUTHOR("Franko Fang <huananhu@huawei.com>");
MODULE_VERSION("v1.00.03.00");
MODULE_LICENSE("GPL");



#define GS_MAJOR				127
#define GS_MINOR_START			0

#define GS_NUM_PORTS			16
#define GS_MAX_EP_NUM 			32

#define GS_NUM_CONFIGS			1
#define GS_NO_CONFIG_ID			0
#define GS_BULK_CONFIG_ID		1
#define GS_ACM_CONFIG_ID		2

#define GS_MAX_NUM_INTERFACES		2
#define GS_BULK_INTERFACE_ID		0
#define GS_CONTROL_INTERFACE_ID		0
#define GS_DATA_INTERFACE_ID		1



#define GS_MAX_DESC_LEN			512	
#define GS_LOG2_NOTIFY_INTERVAL		5	
#define GS_NOTIFY_MAXPACKET		8

#define GS_DEFAULT_READ_Q_SIZE		32
#define GS_DEFAULT_WRITE_Q_SIZE		32

#define GS_DEFAULT_WRITE_BUF_SIZE	8192
#define GS_TMP_BUF_SIZE			8192

#define GS_CLOSE_TIMEOUT		15

#define GS_DEFAULT_USE_ACM		0


#define GS_BULK_ORIGIN			0

#define GS_DEFAULT_DTE_RATE		9600
#define GS_DEFAULT_DATA_BITS		8
#define GS_DEFAULT_PARITY		USB_CDC_NO_PARITY
#define GS_DEFAULT_CHAR_FORMAT		USB_CDC_1_STOP_BITS


#define GS_VENDOR_ID                    0x12D1  
#define GS_PRODUCT_ID                   0x1001  
#define GS_MAX_INTF_NUM					3		

#define GS_PRODUCT_NAME 		"USB Mobile BroadBand"
#define GS_DRIVER_NAME			"Gadget Serial"
#define GS_MODEM_NAME			"HUAWEI Mobile Connect - 3G Modem"
#define GS_PCUI_NAME            "HUAWEI Mobile Connect - 3G PC UI Interface"
#define GS_DIAG_NAME			"HUAWEI Mobile Connect - 3G Application Interface"
#define GS_SHORT_DRIVER_NAME	"g_serial"
#define GS_SHORT_MODEM_NAME		"g_serial_modem"
#define GS_SHORT_DIAG_NAME		"g_serial_diag"
#define GS_SHORT_PCUI_NAME		"g_serial_pcui"


#define GS_MANUFACTURER_STR_ID	1
#define GS_PRODUCT_STR_ID		2
#define GS_SERIAL_STR_ID		3
#define GS_BULK_CONFIG_STR_ID	4
#define GS_ACM_CONFIG_STR_ID	5
#define GS_CONTROL_STR_ID		6
#define GS_DATA_STR_ID			7
#define GS_MODEM_STR_ID         8
#define GS_DIAG_STR_ID			9
#define GS_PCUI_STR_ID         	10


static char manufacturer[50];
static struct usb_string gs_strings[] = {
        { GS_MANUFACTURER_STR_ID, manufacturer },
        { GS_PRODUCT_STR_ID, GS_PRODUCT_NAME },
        { GS_SERIAL_STR_ID, "0xFFFFFFFF" },
        { GS_BULK_CONFIG_STR_ID, "Gadget Serial Bulk" },
        { GS_ACM_CONFIG_STR_ID, "Gadget Serial CDC ACM" },
        { GS_CONTROL_STR_ID, "Gadget Serial Control" },
        { GS_DATA_STR_ID, "Gadget Serial Data" },
		{ GS_MODEM_STR_ID,  GS_MODEM_NAME },
		{ GS_DIAG_STR_ID, GS_DIAG_NAME },
		{ GS_PCUI_STR_ID,  GS_PCUI_NAME },
        {  } /* end of list */
};
static struct usb_gadget_strings gs_string_table = {
        .language =             0x0409, /* en-us */
        .strings =              gs_strings,
};


#define GS_DEVICE_NUM 0x0202
static struct usb_device_descriptor gs_device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		__constant_cpu_to_le16(0x0200),
	.bDeviceClass = 	0,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	.idVendor =		__constant_cpu_to_le16(GS_VENDOR_ID),
	.idProduct =		__constant_cpu_to_le16(GS_PRODUCT_ID),
	.iManufacturer =	GS_MANUFACTURER_STR_ID,
	.iProduct =		GS_PRODUCT_STR_ID,
	.iSerialNumber =	GS_SERIAL_STR_ID,
	.bNumConfigurations =	GS_NUM_CONFIGS,
};

static struct usb_otg_descriptor gs_otg_descriptor = {
	.bLength =		sizeof(gs_otg_descriptor),
	.bDescriptorType =	USB_DT_OTG,
	.bmAttributes =		USB_OTG_SRP,
};

static struct usb_config_descriptor gs_bulk_config_desc = {
	.bLength =		USB_DT_CONFIG_SIZE,
	.bDescriptorType =	USB_DT_CONFIG,
	
	.bNumInterfaces =       GS_MAX_INTF_NUM,	
	.bConfigurationValue =	GS_BULK_CONFIG_ID,
	.iConfiguration =	GS_BULK_CONFIG_STR_ID,
	.bmAttributes =		USB_CONFIG_ATT_ONE | USB_CONFIG_ATT_SELFPOWER,
	.bMaxPower =		0xFA,
};

#define HW_VENDOR_INTERFACE 	0xFF
#define GS_MODEM_INTERFACE_ID 	0
#define GS_DIAG_INTERFACE_ID	1
#define GS_PCUI_INTERFACE_ID	2
static const struct usb_interface_descriptor gs_modem_interface_desc = {
	.bLength =				USB_DT_INTERFACE_SIZE,
	.bDescriptorType =		USB_DT_INTERFACE,
	.bInterfaceNumber =		GS_MODEM_INTERFACE_ID,
	.bNumEndpoints =		3,
	.bInterfaceClass =		HW_VENDOR_INTERFACE,
	.bInterfaceSubClass =	HW_VENDOR_INTERFACE,
	.bInterfaceProtocol =	HW_VENDOR_INTERFACE,
	.iInterface =			GS_MODEM_STR_ID,
};
static const struct usb_interface_descriptor gs_diag_interface_desc = {
    .bLength =              USB_DT_INTERFACE_SIZE,
    .bDescriptorType =      USB_DT_INTERFACE,
    .bInterfaceNumber =     GS_DIAG_INTERFACE_ID,
    .bNumEndpoints =        2,
    .bInterfaceClass =      HW_VENDOR_INTERFACE,
    .bInterfaceSubClass =   HW_VENDOR_INTERFACE,
    .bInterfaceProtocol =   HW_VENDOR_INTERFACE,
    .iInterface =           GS_DIAG_STR_ID,
};

static const struct usb_interface_descriptor gs_pcui_interface_desc = {
    .bLength =              USB_DT_INTERFACE_SIZE,
    .bDescriptorType =      USB_DT_INTERFACE,
    .bInterfaceNumber =     GS_PCUI_INTERFACE_ID,
    .bNumEndpoints =        2,
    .bInterfaceClass =      HW_VENDOR_INTERFACE,
    .bInterfaceSubClass =   HW_VENDOR_INTERFACE,
    .bInterfaceProtocol =   HW_VENDOR_INTERFACE,
    .iInterface =           GS_PCUI_STR_ID,
};


static struct usb_endpoint_descriptor gs_fullspeed_int_modem_desc = {
        .bLength =              USB_DT_ENDPOINT_SIZE,
        .bDescriptorType =      USB_DT_ENDPOINT,
        .bEndpointAddress =     0x1|USB_DIR_IN,
        .bmAttributes =         USB_ENDPOINT_XFER_INT,
        .wMaxPacketSize =       __constant_cpu_to_le16(GS_NOTIFY_MAXPACKET),
        .bInterval =            1 << GS_LOG2_NOTIFY_INTERVAL,
};
static struct usb_endpoint_descriptor gs_fullspeed_in_modem_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
    .bEndpointAddress =     0x2|USB_DIR_IN,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =		__constant_cpu_to_le16(64),
	.bInterval =            0x20,
};
static struct usb_endpoint_descriptor gs_fullspeed_out_modem_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
    .bEndpointAddress =     0x1|USB_DIR_OUT,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =		__constant_cpu_to_le16(64),
	.bInterval =            0x20,
};

static struct usb_endpoint_descriptor gs_fullspeed_in_diag_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
    .bEndpointAddress =     0x3|USB_DIR_IN,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =		__constant_cpu_to_le16(64),
	.bInterval =            0x20,
};
static struct usb_endpoint_descriptor gs_fullspeed_out_diag_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
    .bEndpointAddress =     0x2|USB_DIR_OUT,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =		__constant_cpu_to_le16(64),
	.bInterval =            0x20,
};

static struct usb_endpoint_descriptor gs_fullspeed_in_pcui_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
    .bEndpointAddress =     0x4|USB_DIR_IN,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =		__constant_cpu_to_le16(64),
	.bInterval =            0x20,
};
static struct usb_endpoint_descriptor gs_fullspeed_out_pcui_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
    .bEndpointAddress =     0x3|USB_DIR_OUT,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =		__constant_cpu_to_le16(64),
	.bInterval =            0x20,
};

static const struct usb_descriptor_header *gs_bulk_fullspeed_function[] = {
	(struct usb_descriptor_header *) &gs_modem_interface_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_int_modem_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_in_modem_desc,
	(struct usb_descriptor_header *) &gs_fullspeed_out_modem_desc,
    (struct usb_descriptor_header *) &gs_diag_interface_desc,
    (struct usb_descriptor_header *) &gs_fullspeed_in_diag_desc,
    (struct usb_descriptor_header *) &gs_fullspeed_out_diag_desc,
    (struct usb_descriptor_header *) &gs_pcui_interface_desc,
    (struct usb_descriptor_header *) &gs_fullspeed_in_pcui_desc,
    (struct usb_descriptor_header *) &gs_fullspeed_out_pcui_desc,
    NULL,
};
static const struct usb_endpoint_descriptor *gs_fullspeed_endpoints_pid1001[] = {
	&gs_fullspeed_int_modem_desc,
	&gs_fullspeed_in_modem_desc,
	&gs_fullspeed_out_modem_desc,
    &gs_fullspeed_in_diag_desc,
    &gs_fullspeed_out_diag_desc,
    &gs_fullspeed_in_pcui_desc,
    &gs_fullspeed_out_pcui_desc,
    NULL,
};

static struct usb_endpoint_descriptor gs_highspeed_int_modem_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
    .bEndpointAddress =     0x1|USB_DIR_IN,
    .bmAttributes =         USB_ENDPOINT_XFER_INT,
    .wMaxPacketSize =       __constant_cpu_to_le16(64),
    .bInterval =            GS_LOG2_NOTIFY_INTERVAL,
};
static struct usb_endpoint_descriptor gs_highspeed_in_modem_desc = {
	.bLength =				USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =		USB_DT_ENDPOINT,
	.bEndpointAddress = 	0x2|USB_DIR_IN,
	.bmAttributes =			USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =		__constant_cpu_to_le16(512),
	.bInterval =            0x20,
};
static struct usb_endpoint_descriptor gs_highspeed_out_modem_desc = {
	.bLength =				USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =		USB_DT_ENDPOINT,
	.bEndpointAddress =     0x1|USB_DIR_OUT,
	.bmAttributes =			USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =		__constant_cpu_to_le16(512),
	.bInterval =            0x20,
};

static struct usb_endpoint_descriptor gs_highspeed_in_diag_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
	.bEndpointAddress =     0x3|USB_DIR_IN,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =       __constant_cpu_to_le16(512),
	.bInterval =            0x20,
};
static struct usb_endpoint_descriptor gs_highspeed_out_diag_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
	.bEndpointAddress =     0x2|USB_DIR_OUT,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =       __constant_cpu_to_le16(512),
	.bInterval =            0x20,
};

static struct usb_endpoint_descriptor gs_highspeed_in_pcui_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
	.bEndpointAddress =     0x4|USB_DIR_IN,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =       __constant_cpu_to_le16(512),
	.bInterval =            0x20,
};
static struct usb_endpoint_descriptor gs_highspeed_out_pcui_desc = {
    .bLength =              USB_DT_ENDPOINT_SIZE,
    .bDescriptorType =      USB_DT_ENDPOINT,
	.bEndpointAddress =     0x3|USB_DIR_OUT,
    .bmAttributes =         USB_ENDPOINT_XFER_BULK,
    .wMaxPacketSize =       __constant_cpu_to_le16(512),
	.bInterval =            0x20,
};

static const struct usb_descriptor_header *gs_bulk_highspeed_function[] = {	
    (struct usb_descriptor_header *) &gs_modem_interface_desc,
	(struct usb_descriptor_header *) &gs_highspeed_int_modem_desc,
 	(struct usb_descriptor_header *) &gs_highspeed_in_modem_desc,
    (struct usb_descriptor_header *) &gs_highspeed_out_modem_desc,
    (struct usb_descriptor_header *) &gs_diag_interface_desc,
	(struct usb_descriptor_header *) &gs_highspeed_in_diag_desc,
    (struct usb_descriptor_header *) &gs_highspeed_out_diag_desc,
    (struct usb_descriptor_header *) &gs_pcui_interface_desc,
 	(struct usb_descriptor_header *) &gs_highspeed_in_pcui_desc,
    (struct usb_descriptor_header *) &gs_highspeed_out_pcui_desc,
	NULL,
};
static struct usb_endpoint_descriptor *gs_highspeed_endpoints_pid1001[] = {
	&gs_highspeed_int_modem_desc,
	&gs_highspeed_in_modem_desc,
	&gs_highspeed_out_modem_desc,
    &gs_highspeed_in_diag_desc,
    &gs_highspeed_out_diag_desc,
    &gs_highspeed_in_pcui_desc,
    &gs_highspeed_out_pcui_desc,
    NULL,
};


static struct usb_qualifier_descriptor gs_qualifier_desc = {
	.bLength =		sizeof(struct usb_qualifier_descriptor),
	.bDescriptorType =	USB_DT_DEVICE_QUALIFIER,
	.bcdUSB =		__constant_cpu_to_le16 (0x0200),
	
	.bNumConfigurations =	GS_NUM_CONFIGS,
};





struct gs_list_head {
	struct list_head ep_req_list;
	struct usb_ep *ep;
};

struct gs_buf {
	unsigned int		buf_size;
	char			*buf_buf;
	char			*buf_get;
	char			*buf_put;
};


struct gs_req_entry {
	struct list_head	re_entry;
	struct usb_request	*re_req;
};


struct gs_dev {
	struct usb_gadget				*gadget;		
	int								config;			
	spinlock_t						splock;			
	spinlock_t 						copy_lock;		
	spinlock_t						send_lock;
	spinlock_t						list_lock;
	struct work_struct 				bridge_work;	
	
	struct usb_ep					*cur_ep_list[GS_MAX_EP_NUM];		
	struct usb_endpoint_descriptor 	*cur_ep_desc_list[GS_MAX_EP_NUM];	;
	char 							*ep_names[GS_MAX_EP_NUM];			
	struct usb_request				*ctrl_req;							
	struct usb_request      		*int_req;  							
	struct gs_list_head        		modem_int_req_list;    
    struct gs_list_head        		*intf_req_list[GS_MAX_INTF_NUM];
	struct usb_cdc_line_coding      ports_line_coding[GS_MAX_INTF_NUM];	
	int 							intf_num;
};

static struct __bridge_buffer
{
	unsigned int port_index;
	char szbuf[GS_DEFAULT_WRITE_BUF_SIZE];
	unsigned int len;
} gs_bridge_buffer[32];

static struct gs_dev *gs_device;


#define gs_debug(format, arg...) \
	do { printk(KERN_DEBUG  format, ##arg); } while (0)
	

#define gs_info(format, arg...) \
	do {printk(KERN_INFO  format, ##arg);} while (0)

#define gs_err(format, arg...) \
		do {printk(KERN_ERR  format, ##arg);} while (0)



static int gs_bind(struct usb_gadget * gadget);
static void gs_unbind(struct usb_gadget *gadget);
static int gs_setup(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl);
static int gs_setup_standard(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl);
static int gs_setup_class(struct usb_gadget *gadget,
	const struct usb_ctrlrequest *ctrl);
static void gs_setup_complete(struct usb_ep *ep, struct usb_request *req);
static void gs_disconnect(struct usb_gadget *gadget);
static int gs_set_config(struct gs_dev *dev, unsigned config);
static void gs_reset_config(struct gs_dev *dev);
static int gs_build_config_buf(struct usb_gadget *g, u8 *buf, 
		u8 type, unsigned int index, int is_otg);
static int gs_bridge_copyto_gs (unsigned int port_index, char *packet, unsigned int size);
static void gs_bridge_work(struct work_struct *work);
extern int net2280_set_fifo_mode(struct usb_gadget *gadget, int mode);


static struct usb_gadget_driver gs_gadget_driver = {
#ifdef CONFIG_USB_GADGET_DUALSPEED
	.speed =		USB_SPEED_HIGH,
#else
	.speed =		USB_SPEED_FULL,
#endif 
	.function =		GS_PRODUCT_NAME,	
	.bind =			gs_bind,
	.unbind =		gs_unbind,
	.setup =		gs_setup,
	.disconnect =		gs_disconnect,
	.driver = {
		.name =		GS_SHORT_DRIVER_NAME,
	},
};



static struct usb_request *
gs_alloc_req(struct usb_ep *ep, unsigned int len, gfp_t kmalloc_flags)
{
	struct usb_request *req;

	if (ep == NULL)
		return NULL;

	req = usb_ep_alloc_request(ep, kmalloc_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = kmalloc(len, kmalloc_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}


static void gs_free_req(struct usb_ep *ep, struct usb_request *req)
{
	if (ep != NULL && req != NULL) {
		if (req->buf) {
			kfree(req->buf);
			req->buf = NULL;
		}
		usb_ep_free_request(ep, req);
	}
}


static struct gs_buf *gs_buf_alloc(unsigned int size, gfp_t kmalloc_flags)
{
	struct gs_buf *gb;

	if (size == 0)
		return NULL;

	gb = kmalloc(sizeof(struct gs_buf), kmalloc_flags);
	if (gb == NULL)
		return NULL;

	gb->buf_buf = kmalloc(size, kmalloc_flags);
	if (gb->buf_buf == NULL) {
		kfree(gb);
		return NULL;
	}

	gb->buf_size = size;
	gb->buf_get = gb->buf_put = gb->buf_buf;

	return gb;
}

/*
 * gs_buf_free
 *
 * Free the buffer and all associated memory.
 */
static void gs_buf_free(struct gs_buf *gb)
{
	if (gb) {
		kfree(gb->buf_buf);
		kfree(gb);
	}
}


static void gs_buf_clear(struct gs_buf *gb)
{
	if (gb != NULL)
		gb->buf_get = gb->buf_put;
		
}


static unsigned int gs_buf_data_avail(struct gs_buf *gb)
{
	if (gb != NULL)
		return (gb->buf_size + gb->buf_put - gb->buf_get) % gb->buf_size;
	else
		return 0;
}


static unsigned int gs_buf_space_avail(struct gs_buf *gb)
{
	if (gb != NULL)
		return (gb->buf_size + gb->buf_get - gb->buf_put - 1) % gb->buf_size;
	else
		return 0;
}


static unsigned int
gs_buf_put(struct gs_buf *gb, const char *buf, unsigned int count)
{
	unsigned int len;

	if (gb == NULL)
		return 0;

	len  = gs_buf_space_avail(gb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = gb->buf_buf + gb->buf_size - gb->buf_put;
	if (count > len) {
		memcpy(gb->buf_put, buf, len);
		memcpy(gb->buf_buf, buf+len, count - len);
		gb->buf_put = gb->buf_buf + count - len;
	} else {
		memcpy(gb->buf_put, buf, count);
		if (count < len)
			gb->buf_put += count;
		else 
			gb->buf_put = gb->buf_buf;
	}

	return count;
}


static unsigned int
gs_buf_get(struct gs_buf *gb, char *buf, unsigned int count)
{
	unsigned int len;

	if (gb == NULL)
		return 0;

	len = gs_buf_data_avail(gb);
	if (count > len)
		count = len;

	if (count == 0)
		return 0;

	len = gb->buf_buf + gb->buf_size - gb->buf_get;
	if (count > len) {
		memcpy(buf, gb->buf_get, len);
		memcpy(buf+len, gb->buf_buf, count - len);
		gb->buf_get = gb->buf_buf + count - len;
	} else {
		memcpy(buf, gb->buf_get, count);
		if (count < len)
			gb->buf_get += count;
		else 
			gb->buf_get = gb->buf_buf;
	}

	return count;
}

static int gs_bind(struct usb_gadget * gadget)
{
	int gctype;
	int index = 0;
	struct usb_ep *ep = NULL;
	
	gctype = usb_gadget_controller_number(gadget);
	if (0 <= gctype) {
		gs_device_desc.bcdDevice = cpu_to_le16(GS_DEVICE_NUM | gctype);
	} else {
		gs_device_desc.bcdDevice = cpu_to_le16(GS_DEVICE_NUM | 0x0099);
	}
	gs_device_desc.bDeviceClass = 0;
	gs_device_desc.bMaxPacketSize0 = gadget->ep0->maxpacket;
	if (gadget_is_dualspeed(gadget)) {
		gs_qualifier_desc.bDeviceClass = USB_CLASS_VENDOR_SPEC;
		gs_qualifier_desc.bMaxPacketSize0 = gs_device_desc.bMaxPacketSize0;
		while (NULL != gs_fullspeed_endpoints_pid1001[index]) {
			gs_highspeed_endpoints_pid1001[index]->bEndpointAddress =
				gs_fullspeed_endpoints_pid1001[index]->bEndpointAddress;
			index++;
		}
	}
	usb_gadget_set_selfpowered(gadget);
	if (gadget_is_otg(gadget)) {
		gs_otg_descriptor.bmAttributes |= USB_OTG_HNP;
		gs_bulk_config_desc.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}
	snprintf(manufacturer, sizeof(manufacturer), "%s %s with %s",
		init_utsname()->sysname, init_utsname()->release,
		gadget->name);

	gs_device = kzalloc(sizeof(struct gs_dev), GFP_KERNEL);
	if (NULL == gs_device) {
		return -ENOMEM;
	}
	index = 0;
	usb_ep_autoconfig_reset(gadget);
	while (NULL != gs_fullspeed_endpoints_pid1001[index]) {
		ep = usb_ep_autoconfig(gadget, gs_fullspeed_endpoints_pid1001[index]);
			if (NULL == ep || GS_MAX_EP_NUM <= index) {
				gs_debug("fxz-%s: get null ep\n", __func__);
				goto ep_config_failed;
			}
			gs_device->ep_names[index++] = ep->name;
			ep->driver_data = ep;
	}
	gs_device->ep_names[index] = NULL;
	gs_device->intf_num = gs_bulk_config_desc.bNumInterfaces;
	gs_device->gadget = gadget;
	spin_lock_init(&gs_device->splock);
	spin_lock_init(&gs_device->copy_lock); 
	spin_lock_init(&gs_device->send_lock); 
	spin_lock_init(&gs_device->list_lock); 
	INIT_WORK(&gs_device->bridge_work, gs_bridge_work);
	
	for (index = 0; index < gs_device->intf_num; index++) {
		gs_device->intf_req_list[index] = kzalloc(sizeof (struct gs_list_head), GFP_KERNEL);
		if (NULL == gs_device->intf_req_list[index]) {
			goto bind_failed;
		}
		gs_device->intf_req_list[index]->ep = NULL;
		INIT_LIST_HEAD(&gs_device->intf_req_list[index]->ep_req_list);
	}
	gs_device->modem_int_req_list.ep = NULL;
	INIT_LIST_HEAD(&gs_device->modem_int_req_list.ep_req_list);
	
	
	gs_device->ctrl_req = gs_alloc_req(gadget->ep0, GS_MAX_DESC_LEN,
		GFP_KERNEL); 
	
	if (NULL == gs_device->ctrl_req ) {
		goto bind_failed;
	}
	gs_device->ctrl_req->complete = gs_setup_complete;

	gadget->ep0->driver_data = gs_device;
	set_gadget_data(gadget, gs_device);

	memset(gs_bridge_buffer, 0, sizeof (gs_bridge_buffer));
	bridge_mode = 0;
	gadgetutil.bridge_copyto_gs = gs_bridge_copyto_gs;
	return 0;
bind_failed:
	if (NULL != gs_device->ctrl_req){
		kfree(gs_device->ctrl_req);
	}
	for (index = 0; index < gs_device->intf_num; index++) {
		if (NULL != gs_device->intf_req_list[index]){
			kfree(gs_device->intf_req_list[index]);
		}
	}
ep_config_failed:
	usb_ep_autoconfig_reset(gadget);
	kfree(gs_device);
	printk(KERN_ERR"fxz-%s: failed\n", __func__);
	return -ENODEV;
}
static void gs_unbind(struct usb_gadget * gadget)
{
	int index = 0;
	struct gs_dev *dev = get_gadget_data(gadget);

	bridge_mode = 0;
	gadgetutil.bridge_copyto_gs = NULL;
	gs_device = NULL;

	
	if (NULL != dev) {
		if (NULL != dev->ctrl_req) {
			gs_free_req(gadget->ep0, dev->ctrl_req);
			dev->ctrl_req = NULL;
		}
		for (index = 0; index < GS_MAX_EP_NUM; index++) {
			if (NULL != dev->cur_ep_list[index]) {
				usb_ep_disable(dev->cur_ep_list[index]);
			}
		}
		for (index = 0; index < dev->intf_num; index++) {
			if (NULL != dev->intf_req_list[index]){
				kfree(dev->intf_req_list[index]);
			}
		}
		set_gadget_data(gadget, NULL);
		kfree(dev);
	}
}

/*
 * gs_disconnect
 *
 * Called when the device is disconnected.  Frees the closed
 * ports and disconnects open ports.  Open ports will be freed
 * on close.  Then reallocates the ports for the next connection.
 */
static void gs_disconnect(struct usb_gadget *gadget)
{
	unsigned long flags;
	struct gs_dev *dev = get_gadget_data(gadget);

	printk(KERN_ERR"fxz-%s: called\n", __func__);
	spin_lock_irqsave(&dev->splock, flags);
	gs_reset_config(dev);
	spin_unlock_irqrestore(&dev->splock, flags);
}

static int gs_build_config_buf(struct usb_gadget *gadget, u8 *buf, u8 type, unsigned int index, int is_otg)
{
	int len;
	int high_speed = 0;
	const struct usb_config_descriptor *config_desc;
	const struct usb_descriptor_header **function;

	if (index >= gs_device_desc.bNumConfigurations)
		return -EINVAL;

	
	if (gadget_is_dualspeed(gadget)) {
		high_speed = (USB_SPEED_HIGH == gadget->speed);
		if (USB_DT_OTHER_SPEED_CONFIG == type){
			high_speed = !high_speed;
		}
	}

	config_desc = &gs_bulk_config_desc;
	function = high_speed ? gs_bulk_highspeed_function : gs_bulk_fullspeed_function;
	
	len = usb_gadget_config_buf(config_desc, buf, GS_MAX_DESC_LEN, function); 
	if (len < 0)
		return len;

	((struct usb_config_descriptor *)buf)->bDescriptorType = type;

	return len;
}



static struct gs_req_entry *
gs_alloc_req_entry(struct usb_ep *ep, unsigned len, gfp_t kmalloc_flags)
{
	struct gs_req_entry	*req;

	req = kmalloc(sizeof(struct gs_req_entry), kmalloc_flags);
	if (req == NULL)
		return NULL;

	req->re_req = gs_alloc_req(ep, len, kmalloc_flags);
	if (req->re_req == NULL) {
		kfree(req);
		return NULL;
	}

	req->re_req->context = req;

	return req;
}


static void gs_free_req_entry(struct usb_ep *ep, struct gs_req_entry *req)
{
	if (ep != NULL && req != NULL) {
		if (req->re_req != NULL)
			gs_free_req(ep, req->re_req);
		kfree(req);
	}
}

static void gs_reset_config(struct gs_dev *dev)
{
	struct gs_req_entry *req_entry;
	int index;

	printk(KERN_ERR"fxz-%s: called\n", __func__);
	if (NULL == dev || GS_NO_CONFIG_ID == dev->config) {
		return;
	}
	dev->config = GS_NO_CONFIG_ID;

	while (!list_empty(&dev->modem_int_req_list.ep_req_list)) {
		req_entry = list_entry(dev->modem_int_req_list.ep_req_list.next,
						struct gs_req_entry, re_entry);
		list_del(&req_entry->re_entry);
		gs_free_req_entry(dev->modem_int_req_list.ep, req_entry);
	}

	for (index = 0; index < dev->intf_num; index++) {
		while (!list_empty(&dev->intf_req_list[index]->ep_req_list)) {
			req_entry = list_entry(dev->intf_req_list[index]->ep_req_list.next,
							struct gs_req_entry, re_entry);
			list_del(&req_entry->re_entry);
			gs_free_req_entry(dev->intf_req_list[index]->ep, req_entry);
		}
	}

	for (index = 0; index < GS_MAX_EP_NUM; index++) {
		if (NULL != dev->cur_ep_list[index]) {
			usb_ep_disable(dev->cur_ep_list[index]);
			dev->cur_ep_list[index] = NULL;
		}
	}
	
	for (index = 0; index < GS_MAX_INTF_NUM; index++) {
		memset(&dev->ports_line_coding[index], 0, sizeof(struct usb_cdc_line_coding));
	}
}
static void gs_setup_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (req->status || req->actual != req->length) {
		gs_err("fxz-%s: status error, status=%d, "
			"actual=%d, length=%d\n", __func__, 
			req->status, req->actual, req->length);
	}
}
static int gs_get_intfnum_of_out_ep(struct usb_ep *out_ep)
{
	int index = 0;
	int idx;
	struct gs_dev *dev = out_ep->driver_data;
	if (NULL == dev) {
		gs_debug("fxz-%s: ep is not bind\n", __func__);
		return -1;
	}
	for (index = 0; index < GS_MAX_EP_NUM; index++) {
		if (0 == strcmp(out_ep->name, dev->cur_ep_list[index]->name)) {
			break;
		}
	}

	if (GS_MAX_EP_NUM <= index || 0 == index) {
		gs_debug("fxz-%s: cannot fount the ep\n", __func__);
		return -1;
	}
	for (idx = 0; idx < dev->intf_num; idx++){
		if (0 == strcmp(dev->intf_req_list[idx]->ep->name, dev->cur_ep_list[index - 1]->name)) {
			break;
		}
	}

	if (idx >= dev->intf_num) {
		gs_debug("fxz-%s: cannot fount the interface index\n", __func__);
		return -1;
	}
	return idx;
}


static int gs_recv_packet(struct usb_ep *ep, char *packet, unsigned int size)
{
	int intf_idx = 0;
	struct gs_dev *dev = ep->driver_data;
	int ret = 0;
	int index = 0;
	
	if (NULL == dev) {
		gs_debug("fxz-%s: ep is not bind\n", __func__);
		return -EIO;
	}
	bridge_mode = 1;
	intf_idx = gs_get_intfnum_of_out_ep(ep);
	if (0 > intf_idx || 0x0 != intf_idx || intf_idx >= dev->intf_num) {
	
		printk(KERN_ERR"fxz-%s: ep out of the interface[%d]\n", __func__, intf_idx);
		return -EIO;
	}

	
	index = gadget_get_map_index_bridge_to_device(product_id, intf_idx);
	
	if (index >= 0xf || NULL == gadgetutil.bridge_copyto_us) {
		return -ENODEV;
	}
	printk(KERN_ERR"fxz-%s: send packet\n", __func__);
	ret = gadgetutil.bridge_copyto_us(index, packet, size);
	if(ret == -100) {
		gs_debug("fxz-%s:can't open port[0]\n", __func__);	
	}
	return ret;
}


static void gs_read_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (!req || !req->buf) {
		printk(KERN_ERR"fxz-%s: request invalid\n", __func__);
		return;
	}
	switch(req->status) {
	case 0:
		
		gs_recv_packet(ep, req->buf, req->actual);
requeue:
		req->length = ep->maxpacket;
		if (usb_ep_queue(ep, req, GFP_ATOMIC)) {
			printk(KERN_ERR"fxz-%s: cannot queue read request\n", __func__);
		}
		break;

	case -ESHUTDOWN:
		
		printk(KERN_ERR"fxz-%s: shutdown\n", __func__);
		gs_free_req(ep, req);
		break;

	default:
		
		printk(KERN_ERR"fxz-%s: unexpected status error, status=%d\n", __func__, req->status);
		goto requeue;
		break;
	}

}
static void gs_write_complete(struct usb_ep *ep, struct usb_request *req)
{

	
	gs_free_req(ep, req);

}

static inline struct usb_endpoint_descriptor *
choose_ep_desc(struct usb_gadget *g, struct usb_endpoint_descriptor *hs,
		struct usb_endpoint_descriptor *fs)
{
	if (gadget_is_dualspeed(g) && USB_SPEED_HIGH == g->speed)
		return hs;
	return fs;
}


static int gs_set_config(struct gs_dev *dev, unsigned config)
{
	int ret;
	int index;
	struct usb_ep *ep;
	struct usb_gadget *gadget;
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_request *req;
	struct gs_list_head **req_head;

	if (NULL == dev || config == dev->config) {
		return 0;
	}

	gadget = dev->gadget;
	gs_reset_config(dev);

	switch (config) {
		case GS_NO_CONFIG_ID:
			return 0;
		case GS_BULK_CONFIG_ID:
			if (gadget_is_net2280(gadget)) {
				net2280_set_fifo_mode(gadget, 1);
			}
			break;
		case GS_ACM_CONFIG_ID:
			return 0;
		default:
			return -EINVAL;
	}
	dev->config = config;

	gadget_for_each_ep(ep, gadget) {
		for (index = 0; index < GS_MAX_EP_NUM; index++) {
			if (NULL == dev->ep_names[index]) {
				break;
			}
			if (0 == strcmp(ep->name, dev->ep_names[index])) {
				ep_desc = choose_ep_desc(gadget,
							gs_highspeed_endpoints_pid1001[index],
							gs_fullspeed_endpoints_pid1001[index]);
				ret = usb_ep_enable(ep, ep_desc);
				if (0 == ret) {
					ep->driver_data = dev;
					dev->cur_ep_list[index] = ep;
					dev->cur_ep_desc_list[index] = ep_desc;
				} else {
					goto exit_and_reset_config;
				}
				break;
			}
		}
	}
	for (index = 0; ; index++) {
		ep_desc = choose_ep_desc(gadget,
							gs_highspeed_endpoints_pid1001[index],
							gs_fullspeed_endpoints_pid1001[index]);
		if (NULL == ep_desc){
			break;
		}
		if (ep_desc != dev->cur_ep_desc_list[index]) {
			gs_err("fxz-%s: desc not equal\n", __func__);
			ret = -EIO;
			goto exit_and_reset_config;
		}
	}

	req_head = dev->intf_req_list;
	for (index = 0; index < GS_MAX_EP_NUM; index++){
		if (NULL == dev->cur_ep_desc_list[index]) {
			break;
		}
		if (USB_ENDPOINT_XFER_INT == dev->cur_ep_desc_list[index]->bmAttributes
			&& (USB_DIR_IN & dev->cur_ep_desc_list[index]->bEndpointAddress)) {
			int idx = 0;
			struct gs_req_entry *req_entry;
			ep = dev->cur_ep_list[index];
			dev->modem_int_req_list.ep = ep;
			
			for (idx = 0; idx < GS_DEFAULT_WRITE_Q_SIZE; idx++) {
				if ((req_entry = gs_alloc_req_entry(ep, ep->maxpacket, GFP_KERNEL))) {
						req_entry->re_req->complete = gs_write_complete;
						list_add(&req_entry->re_entry, &dev->modem_int_req_list.ep_req_list);
				} else {
						gs_err("fxz-%s: cannot allocate write requests\n", __func__);
						ret = -ENOMEM;
						goto exit_and_reset_config;
				}
			}
			
		}
		if (USB_ENDPOINT_XFER_BULK == dev->cur_ep_desc_list[index]->bmAttributes
			&& (USB_DIR_IN & dev->cur_ep_desc_list[index]->bEndpointAddress)) {
			int idx = 0;			
			struct gs_req_entry *req_entry;
			ep = dev->cur_ep_list[index];
			req_head[0]->ep = ep;
			
			for (idx = 0; idx < GS_DEFAULT_WRITE_Q_SIZE; idx++) {
				if ((req_entry = gs_alloc_req_entry(ep, ep->maxpacket, GFP_KERNEL))) {
						req_entry->re_req->complete = gs_write_complete;
						list_add(&req_entry->re_entry, &req_head[0]->ep_req_list);
				} else {
						 gs_err("fxz-%s: cannot allocate write requests\n", __func__);
						ret = -ENOMEM;
						goto exit_and_reset_config;
				}
			}
			req_head++;
		} else {
			gs_debug("fxz-%s: the [%d] ep name[%s]\n", __func__, index, dev->cur_ep_list[index]->name);
		if (USB_ENDPOINT_XFER_BULK == dev->cur_ep_desc_list[index]->bmAttributes
			&& !(USB_DIR_IN & dev->cur_ep_desc_list[index]->bEndpointAddress)) {
			int idx = 0;
			ep = dev->cur_ep_list[index];
			
			for (idx =0; idx < GS_DEFAULT_READ_Q_SIZE && 0 == ret; idx++) {
				if ((req = gs_alloc_req(ep, ep->maxpacket, GFP_KERNEL))) {
						req->complete = gs_read_complete;
						if ((ret = usb_ep_queue(ep, req, GFP_ATOMIC))) {
								gs_err("fxz-%s: cannot queue read request, ret=%d\n", __func__, ret);
						}
				} else {
						gs_err("fxz-%s: cannot allocate read requests\n", __func__);
						ret = -ENOMEM;
						goto exit_and_reset_config;
				}
			}
		}
		}
	}
	return 0;
exit_and_reset_config:
	gs_reset_config(dev);
	return ret;
}

static int gs_setup_standard(struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{
	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->ctrl_req;
	u16 wIndex = le16_to_cpu(ctrl->wIndex);
	u16 wValue = le16_to_cpu(ctrl->wValue);
	u16 wLength = le16_to_cpu(ctrl->wLength);

	switch (ctrl->bRequest) {
		case USB_REQ_GET_DESCRIPTOR:
			if (USB_DIR_IN != ctrl->bRequestType) {
				break;
			}

			switch (wValue >> 8) {
				case USB_DT_DEVICE:
					ret = min(wLength, (u16)sizeof(struct usb_device_descriptor));
					memcpy(req->buf, &gs_device_desc, ret);
					break;
				case USB_DT_DEVICE_QUALIFIER:
					if (0 == gadget_is_dualspeed(gadget)) {
						break;
					}
					ret = min(wLength, (u16)sizeof(struct usb_qualifier_descriptor));
					memcpy(req->buf, &gs_qualifier_desc, ret);
					break;
				case USB_DT_OTHER_SPEED_CONFIG: 
					if (0 == gadget_is_dualspeed(gadget)) {
						break;
					}
				case USB_DT_CONFIG:
					ret = gs_build_config_buf(gadget, req->buf, 
							wValue >> 8, wValue & 0xff,
							gadget_is_otg(gadget));
					if (0 <= ret) {
						ret = min(wLength, (u16)ret);
					}
					break;
				case USB_DT_STRING:
					ret = usb_gadget_get_string(&gs_string_table, wValue & 0xff, req->buf);
					if (0 <= ret) {
						ret = min(wLength, (u16)ret);
					}
					break;
				default:
					break;
			}
			break;
		case USB_REQ_GET_CONFIGURATION:
			if (USB_DIR_IN != ctrl->bRequestType) { 
				break;
			}
			*(u8 *)req->buf = dev->config;
			ret = min(wLength, (u16)1);
			break;
		case USB_REQ_SET_CONFIGURATION:
			if (USB_DIR_OUT != ctrl->bRequestType) { 
				break;
			}
			spin_lock(&dev->splock);
			ret = gs_set_config(dev, GS_BULK_CONFIG_ID);
			spin_unlock(&dev->splock);
			break;
		case USB_REQ_GET_INTERFACE:
			if ((USB_DIR_IN | USB_RECIP_INTERFACE) != ctrl->bRequestType
				|| GS_NO_CONFIG_ID == dev->config) {
				break;
			}
			if (GS_MAX_NUM_INTERFACES <= wIndex
				|| (GS_BULK_CONFIG_ID == dev->config && GS_BULK_INTERFACE_ID != wIndex)) {
				
				break;
			}
			*(u8 *)req->buf = 0;
			ret = min(wLength, (u16)1);
			break;
		case USB_REQ_SET_INTERFACE:
			if (USB_RECIP_INTERFACE != ctrl->bRequestType
				|| 0 == dev->config
				|| GS_MAX_NUM_INTERFACES <= wIndex) {
				break;
			}
			if (GS_BULK_CONFIG_ID == dev->config
				&& GS_BULK_INTERFACE_ID != wIndex) {
				break;
			}
			if (0 != wValue) {
				break;
			}
			spin_lock(&dev->splock);
			if (gadget_is_pxa(gadget)){
				ret = gs_set_config(dev, GS_BULK_CONFIG_ID);
			} else {
				int index;
				for (index = 0; index < GS_MAX_EP_NUM; index++) {
					if (NULL != dev->cur_ep_list[index]) {
						usb_ep_disable(dev->cur_ep_list[index]);
						usb_ep_enable(dev->cur_ep_list[index], dev->cur_ep_desc_list[index]);
					}
				}
				ret = 0;
			}
			spin_unlock(&dev->splock);
			break;
		default:
			break;
	}
	return ret;
}
static int cur_intf_line_coding = -1;
static void gs_set_class_complete(struct usb_ep *ep, struct usb_request *req) 
{
	struct gs_dev *dev = ep->driver_data;
	
	if (req->status != 0) {
		printk(KERN_ERR"fxz-%s: err %d\n", __func__, req->status);
		return;
	}

	
	 if (req->actual != req->length){
		printk(KERN_ERR"fxz-%s: err actual[%d], length[%d]\n", __func__, req->actual, req->length);
		usb_ep_set_halt(ep);
	} else if (req->actual == sizeof(struct usb_cdc_line_coding)){
		struct usb_cdc_line_coding	*value = req->buf;
		dev->ports_line_coding[cur_intf_line_coding] = *value;
	}
}

static int gs_setup_class(struct usb_gadget *gadget, const struct usb_ctrlrequest *ctrl)
{

	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->ctrl_req;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {
		case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_LINE_CODING:
			if (w_length != sizeof(struct usb_cdc_line_coding)
				|| GS_MAX_INTF_NUM <= w_index) {
				goto invalid;
			}
			ret = w_length;
			cur_intf_line_coding = w_index;
			req->complete = gs_set_class_complete; 
			break;
		case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_GET_LINE_CODING:
			if (GS_MAX_INTF_NUM <= w_index) {
				goto invalid;
			}
			ret =  min_t(unsigned, w_length, sizeof(struct usb_cdc_line_coding));
			memcpy(req->buf, &dev->ports_line_coding[w_index], ret);
			break;
		case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_CONTROL_LINE_STATE:
			if (GS_MAX_INTF_NUM <= w_index) {
				goto invalid;
			}
			ret = 0;
			break;
		default:
			
			break;
	}
invalid:
	return ret;
}

static int gs_setup(struct usb_gadget * gadget, const struct usb_ctrlrequest * ctrl)
{
	int ret = -EOPNOTSUPP;
	struct gs_dev *dev = get_gadget_data(gadget);
	struct usb_request *req = dev->ctrl_req;
	req->complete = gs_setup_complete;

	switch (ctrl->bRequestType & USB_TYPE_MASK) {
		case USB_TYPE_STANDARD:
			ret = gs_setup_standard(gadget, ctrl);
			break;
		case USB_TYPE_CLASS:
			ret = gs_setup_class(gadget, ctrl);
			
			break;
		default:
			break;
	}

	if (0 <= ret) {
		req->length = ret;
		
		
		req->zero = ((ret >= le16_to_cpu(ctrl->wLength)) && (0 == ret % gadget->ep0->maxpacket));
		ret = usb_ep_queue(gadget->ep0, req, GFP_ATOMIC);
		if (0 > ret) {
			printk(KERN_ERR"fxz-%s: submit failed\n", __func__);
			req->status = 0;
			gs_setup_complete(gadget->ep0, req);
		}
	} 
	return ret;
}


static int gs_bridge_copyto_gs (unsigned int port_index, char *packet, unsigned int size)
{

	struct gs_dev *dev = gs_device;
	unsigned long flags;
	int index = 0;
	if (3 <= port_index){
		return 0;
	}
	
	for (index = 0; index < 32; index++) {
		if (0 == gs_bridge_buffer[index].len && GS_DEFAULT_WRITE_BUF_SIZE > size) {
			gs_bridge_buffer[index].port_index = port_index;
			memcpy(gs_bridge_buffer[index].szbuf, packet, size);
			gs_bridge_buffer[index].len = size;
			break;
		}
	}
	schedule_work(&dev->bridge_work);
	return 0;
}
static int gs_copyto_gs (unsigned int port_index, char *packet, unsigned int size)
{
	struct gs_dev *dev = gs_device;
	struct usb_request *req;
	int writing_size = 0;
	int index = 0;
	int need_zero_packet = 0;
	static int idx = 0;
	
	if (0 == size % dev->intf_req_list[port_index]->ep->maxpacket) {
		need_zero_packet = 1;
	}

repeat:
	req = gs_alloc_req(dev->intf_req_list[port_index]->ep, 
								dev->intf_req_list[port_index]->ep->maxpacket, GFP_KERNEL);
	if (NULL == req)
		return -1;
	req->complete = gs_write_complete;
	if (size > dev->intf_req_list[port_index]->ep->maxpacket){
		writing_size = dev->intf_req_list[port_index]->ep->maxpacket;
	} else  if (size == dev->intf_req_list[port_index]->ep->maxpacket){
		writing_size = size;
		req->zero = size == dev->intf_req_list[port_index]->ep->maxpacket;
		
	} else {
		writing_size = size;
	}
	memcpy(req->buf, packet, writing_size);
	req->length = writing_size;
	
	if (usb_ep_queue(dev->intf_req_list[port_index]->ep, req, GFP_ATOMIC)) {
		gs_free_req(dev->intf_req_list[port_index]->ep, req);
		
		
		return -1;
	}
	if (size > writing_size) {
		size -= writing_size;
		packet += writing_size;
		goto repeat;
	}
	return 0;
}
static void gs_bridge_work(struct work_struct *work) 
{
	int index = 0;
	static int mutex = 0;
	if (1 == mutex) {
		return;
	}
	mutex = 1;
	for (index = 0; index < 32; index++) {
		if (0 < gs_bridge_buffer[index].len) {
			(void)gs_copyto_gs(gs_bridge_buffer[index].port_index, gs_bridge_buffer[index].szbuf, gs_bridge_buffer[index].len);
			gs_bridge_buffer[index].len = 0;
		}
	}
	mutex = 0;
}



static int __init gs_module_init(void)
{	
	if (!bridge_enabled)
	{
		printk(KERN_INFO "%s: not registered\n", __func__);
		return 0;
	}
	return usb_gadget_register_driver(&gs_gadget_driver);
}


static void __exit gs_module_exit(void)
{
	if (!bridge_enabled)
	{
		printk(KERN_INFO "%s: not registered\n", __func__);
		return 0;
	}
	usb_gadget_unregister_driver(&gs_gadget_driver);
}
module_init(gs_module_init);
module_exit(gs_module_exit);
