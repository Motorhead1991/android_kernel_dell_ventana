/*
* Customer code to add GPIO control during WLAN start/stop
* Copyright (C) 1999-2010, Broadcom Corporation
* 
*      Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2 (the "GPL"),
* available at http:
* following added to such license:
* 
*      As a special exception, the copyright holders of this software give you
* permission to link this software with independent modules, and to copy and
* distribute the resulting executable under terms of your choice, provided that
* you also meet, for each linked independent module, the terms and conditions of
* the license of that module.  An independent module is a module which is not
* derived from this software.  The special exception does not apply to any
* modifications of the software.
* 
*      Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*
* $Id: dhd_custom_gpio.c,v 1.1.4.8.4.1 2010/09/02 23:13:16 Exp $
*/



#include <linux/proc_fs.h>
#include <linux/mmc/host.h>

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>

#include <dngl_stats.h>
#include <dhd.h>

#include <wlioctl.h>
#include <wl_iw.h>

#define WL_ERROR(x) printf x
#define WL_TRACE(x)


struct proc_dir_entry *entry;
int q_wlan_flag = 0;
EXPORT_SYMBOL(q_wlan_flag);
const NvOdmPeripheralConnectivity *bcmOdmConn=NULL;
NvOdmServicesGpioHandle bcmOdmGpio;
NvOdmGpioPinHandle bcmWlanPWGpioPin;
#ifdef CUSTOMER_HW
extern  void bcm_wlan_power_off(int);
extern  void bcm_wlan_power_on(int);
#endif 
#ifdef CUSTOMER_HW2
int wifi_set_carddetect(int on);
int wifi_set_power(int on, unsigned long msec);
int wifi_get_irq_number(unsigned long *irq_flags_ptr);
#endif

#if defined(OOB_INTR_ONLY)

#if defined(BCMLXSDMMC)
extern int sdioh_mmc_irq(int irq);
#endif 

#ifdef CUSTOMER_HW3
#include <mach/gpio.h>
#endif


static int dhd_oob_gpio_num = -1; 

module_param(dhd_oob_gpio_num, int, 0644);
MODULE_PARM_DESC(dhd_oob_gpio_num, "DHD oob gpio number");

int dhd_customer_oob_irq_map(unsigned long *irq_flags_ptr)
{
	int  host_oob_irq = 0;

#ifdef CUSTOMER_HW2
	host_oob_irq = wifi_get_irq_number(irq_flags_ptr);

#else 
#if defined(CUSTOM_OOB_GPIO_NUM)
	if (dhd_oob_gpio_num < 0) {
		dhd_oob_gpio_num = CUSTOM_OOB_GPIO_NUM;
	}
#endif

	if (dhd_oob_gpio_num < 0) {
		WL_ERROR(("%s: ERROR customer specific Host GPIO is NOT defined \n",
			__FUNCTION__));
		return (dhd_oob_gpio_num);
	}

	WL_ERROR(("%s: customer specific Host GPIO number is (%d)\n",
	         __FUNCTION__, dhd_oob_gpio_num));

#if defined CUSTOMER_HW
	host_oob_irq = MSM_GPIO_TO_INT(dhd_oob_gpio_num);
#elif defined CUSTOMER_HW3
	gpio_request(dhd_oob_gpio_num, "oob irq");
	host_oob_irq = gpio_to_irq(dhd_oob_gpio_num);
	gpio_direction_input(dhd_oob_gpio_num);
#endif 
#endif 

	return (host_oob_irq);
}
#endif 



static int q_proc_call(char *buf, char **start, off_t off,
                                        int count, int *eof, void *data)
{
    int len = 0;
    if(q_wlan_flag == 0)
        len = sprintf(buf + len, "%s", "0\n");
    else if(q_wlan_flag == 1)
        len = sprintf(buf + len, "%s", "1\n");
    else
        len = sprintf(buf + len, "%s", "0\n");

    return len;
}


void
dhd_customer_gpio_wlan_ctrl(int onoff)
{
	switch (onoff) {
		case WLAN_RESET_OFF:
			WL_TRACE(("%s: call customer specific GPIO to insert WLAN RESET\n",
				__FUNCTION__));
			WL_ERROR(("=========== WLAN placed in RESET ========\n"));
                        
                        mdelay(1000);
                        NvOdmGpioSetState(bcmOdmGpio, bcmWlanPWGpioPin, 0);
                        mdelay(100);
                        NvOdmGpioReleasePinHandle(bcmOdmGpio, bcmWlanPWGpioPin);

		break;

		case WLAN_RESET_ON:
			WL_TRACE(("%s: callc customer specific GPIO to remove WLAN RESET\n",
				__FUNCTION__));
			WL_ERROR(("=========== WLAN going back to live  ========\n"));
                        
                        mdelay(1000);
                        
                        
                        bcmOdmConn = NvOdmPeripheralGetGuid(WLAN_GUID);
                        if (!bcmOdmConn)
                             printk("NvOdm Wlan : didn't find any periperal in discovery query for Wlan device Error \n");
                        bcmOdmGpio=NvOdmGpioOpen();
                        bcmWlanPWGpioPin=NvOdmGpioAcquirePinHandle(bcmOdmGpio, 
			                                           bcmOdmConn->AddressList[0].Instance, 
                                                                   bcmOdmConn->AddressList[0].Address);
                        NvOdmGpioConfig(bcmOdmGpio, bcmWlanPWGpioPin, NvOdmGpioPinMode_Output);
                        NvOdmGpioSetState(bcmOdmGpio, bcmWlanPWGpioPin, 1);
                        mdelay(100);

		break;

		case WLAN_POWER_OFF:
			WL_TRACE(("%s: call customer specific GPIO to turn off WL_REG_ON\n",
				__FUNCTION__));
                        
                        mdelay(1000);
                        NvOdmGpioSetState(bcmOdmGpio, bcmWlanPWGpioPin, 0);
                        mdelay(100);
                        NvOdmGpioReleasePinHandle(bcmOdmGpio, bcmWlanPWGpioPin);
                        
                        
                        mmc_detect_change(tegra_mmcptr, 0);
                        remove_proc_entry("q_wlan", NULL);  
		break;

		case WLAN_POWER_ON:
			WL_TRACE(("%s: call customer specific GPIO to turn on WL_REG_ON\n",
				__FUNCTION__));
                        
                        
                        q_wlan_flag = 0;  
                        entry = create_proc_read_entry("q_wlan", 0, NULL, q_proc_call, NULL);
                        if (!entry)
                              printk("cl:unable to create proc file\n");
                
                        
                        
                        bcmOdmConn = NvOdmPeripheralGetGuid(WLAN_GUID);
                        if (!bcmOdmConn)
                             printk("NvOdm Wlan : didn't find any periperal in discovery query for Wlan device Error \n");
                        bcmOdmGpio=NvOdmGpioOpen();
                        bcmWlanPWGpioPin=NvOdmGpioAcquirePinHandle(bcmOdmGpio, 
			                                           bcmOdmConn->AddressList[0].Instance, 
                                                                   bcmOdmConn->AddressList[0].Address);
                        NvOdmGpioConfig(bcmOdmGpio, bcmWlanPWGpioPin, NvOdmGpioPinMode_Output);
                        NvOdmGpioSetState(bcmOdmGpio, bcmWlanPWGpioPin, 1);
                        mdelay(100);
                        
                        
                        mmc_detect_change(tegra_mmcptr, 0);
			
			OSL_DELAY(200);
		break;
	}
}

#ifdef GET_CUSTOM_MAC_ENABLE

int
dhd_custom_get_mac_address(unsigned char *buf)
{
	WL_TRACE(("%s Enter\n", __FUNCTION__));
	if (!buf)
		return -EINVAL;

	

#ifdef EXAMPLE_GET_MAC
	
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif 

	return 0;
}
#endif 
