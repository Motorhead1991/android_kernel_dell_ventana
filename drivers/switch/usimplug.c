/*
 *  drivers/switch/usimplug.c
 *
 * Copyright (C) 2010 Qisda Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/switch.h>
#include <linux/delay.h>

#include "nvos.h"
#include "nvodm_services.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query.h"
#include "nvodm_query_gpio.h"


#define USIM_ERROR   0
#define USIM_DEBUG   1
#define USIM_VERBOSE 2

#define USIM_PRINT(level, format, arg...) \
   if(usim_level >= level ) printk(KERN_INFO "usimplug: "format,  ##arg)

#define USIMPLUG_NAME "usimplug"
#define USIMPLUG_GUID NV_ODM_GUID('u','s','i','m','p','l','u','g')

static uint usim_level = USIM_DEBUG;  

struct usimplug_driver_data_t
{
    struct switch_dev           sdev;
	NvOdmServicesGpioHandle     hGpio;
	NvOdmGpioPinHandle          hDetectPin;
    NvOdmServicesGpioIntrHandle hDetectIntr;
	NvU32                       intPinNum;
	NvU32                       intPinPort;
	int                         state;                      
	struct work_struct          work;
};

static struct usimplug_driver_data_t usimplug_driver_data;

static void usimplug_switch_work(struct work_struct *work)
{
    NvU32 pinValue;
    NvU32 pinValue_temp;
    struct usimplug_driver_data_t *pUsim = 
	   container_of(work, struct usimplug_driver_data_t, work);

    msleep(50);
    NvOdmGpioGetState(pUsim->hGpio, pUsim->hDetectPin, &pinValue_temp);
    msleep(50);
    NvOdmGpioGetState(pUsim->hGpio, pUsim->hDetectPin, &pinValue);
    USIM_PRINT(USIM_VERBOSE, "state check %d -> %d\n", pinValue_temp, pinValue);
    if( pinValue != pinValue_temp)
    {
        msleep(50);
        NvOdmGpioGetState(pUsim->hGpio, pUsim->hDetectPin, &pinValue);
    }

    USIM_PRINT(USIM_VERBOSE, "confirmed %d, original %d\n", pinValue, pUsim->state);
    if( pinValue != pUsim->state)
	{
	    switch_set_state(&pUsim->sdev, pinValue);
	    pUsim->state = pinValue; 
	}
}

static void usimplug_detect_handler(void *arg)
{
    struct usimplug_driver_data_t *pUsim = (struct usimplug_driver_data_t *)arg;
    NvU32 pinValue;
	
    NvOdmGpioGetState(pUsim->hGpio, pUsim->hDetectPin, &pinValue);
    USIM_PRINT(USIM_DEBUG, "%s!!!\n", 
		  pinValue? "plugged in": "unplugged");
	schedule_work(&pUsim->work);
	NvOdmGpioInterruptDone(pUsim->hDetectIntr);
}

static void usimplug_io_release(struct usimplug_driver_data_t *pUsim)
{
    if( pUsim->hDetectIntr )
	    NvOdmGpioInterruptUnregister(pUsim->hGpio, pUsim->hDetectPin, pUsim->hDetectIntr);

    if( pUsim->hDetectPin )
		NvOdmGpioReleasePinHandle(pUsim->hGpio, pUsim->hDetectPin);
	
    if( pUsim->hGpio )
		NvOdmGpioClose(pUsim->hGpio);

	NvOsMemset(pUsim, 0, sizeof(struct usimplug_driver_data_t));
};

static int usimplug_io_query(struct usimplug_driver_data_t *pUsim)
{
    const NvOdmPeripheralConnectivity *pConn = NULL;
	NvU32 i;

	pConn = NvOdmPeripheralGetGuid(USIMPLUG_GUID);
	if( !pConn )
	{
	   USIM_PRINT(USIM_ERROR, "no such peripheral\n");
	   return -ENODEV;
	}

    for(i=0; i<pConn->NumAddress; i++)
	{
	    if( NvOdmIoModule_Gpio == pConn->AddressList[i].Interface )
        {
		    if( pConn->AddressList[i].Address == 0 )
			{
		        pUsim->intPinPort = pConn->AddressList[i].Instance;
		        pUsim->intPinNum = pConn->AddressList[i].Address;
			}
		}
	}
	USIM_PRINT(USIM_DEBUG, "intPinPort=%d, intPinNum=%d\n",
		  pUsim->intPinPort, pUsim->intPinNum);

	return 0;
}

#define USIMPLUG_DEBOUNCE_TIME 250 
static int usimplug_io_setup(struct usimplug_driver_data_t *pUsim)
{
    int ret;

	if( (ret = usimplug_io_query(pUsim)) )
	    return ret;

    pUsim->hGpio = NvOdmGpioOpen();
	if( !pUsim->hGpio )
	{
	    USIM_PRINT(USIM_ERROR, "gpio open failed\n");
		return -EINVAL;
	}

	
    pUsim->hDetectPin = NvOdmGpioAcquirePinHandle(pUsim->hGpio, pUsim->intPinPort, pUsim->intPinNum);
	if( !pUsim->hDetectPin )
	{
	    USIM_PRINT(USIM_ERROR, "gpio pin handle acquire failed, port=%d, num=%d\n",
			          pUsim->intPinPort, pUsim->intPinNum);
		return -EINVAL;
	}
	
	NvOdmGpioConfig(pUsim->hGpio, pUsim->hDetectPin, NvOdmGpioPinMode_InputInterruptAny);
	NvOdmGpioGetState(pUsim->hGpio, pUsim->hDetectPin, &pUsim->state);
	pUsim->sdev.state = pUsim->state;
	USIM_PRINT(USIM_DEBUG, "default state %d\n", pUsim->state);
	if( NV_FALSE == NvOdmGpioInterruptRegister(pUsim->hGpio, &pUsim->hDetectIntr, 
	                                           pUsim->hDetectPin, NvOdmGpioPinMode_InputInterruptAny,
							                   usimplug_detect_handler, (void*)pUsim, USIMPLUG_DEBOUNCE_TIME) )
	{
	    USIM_PRINT(USIM_ERROR, "detect gpio interrupt register failed\n");
		return -EINVAL;
	}

	return 0;
}

static int usimplug_probe(struct platform_device *pdev)
{
	int ret = 0;
    struct usimplug_driver_data_t *pUsim = &usimplug_driver_data;

	USIM_PRINT(USIM_DEBUG, "%s()\n", __func__);

	NvOsMemset(&usimplug_driver_data, 0, sizeof(struct usimplug_driver_data_t));

	pUsim->sdev.name = pdev->name;
	ret = switch_dev_register(&pUsim->sdev);
	if( ret ) goto switch_register_failed;

	ret = usimplug_io_setup(pUsim);
	if( ret ) goto probe_io_failed;

	INIT_WORK(&pUsim->work, usimplug_switch_work);

	dev_set_drvdata(&pdev->dev, &usimplug_driver_data);

	return 0;
	
probe_io_failed:
	switch_dev_unregister(&pUsim->sdev);
	usimplug_io_release(pUsim);
	dev_set_drvdata(&(pdev->dev), NULL);
	
switch_register_failed:
	USIM_PRINT(USIM_DEBUG, "%s(), ret=%d\n", __func__, ret);
	return ret;
}

static int usimplug_remove(struct platform_device *pdev)
{
	int ret = 0;
	struct usimplug_driver_data_t *pUsim = dev_get_drvdata(&(pdev->dev));

	USIM_PRINT(USIM_DEBUG, "%s()\n", __func__);

    usimplug_io_release(pUsim);
	switch_dev_unregister(&pUsim->sdev);
	dev_set_drvdata(&pdev->dev, NULL);

	return ret;
}

static struct platform_driver usimplug_platform_driver = {
	.driver = { .name = USIMPLUG_NAME,
	            .owner = THIS_MODULE,},
	.probe = usimplug_probe,
	.remove = usimplug_remove,
};

static int __init usimplug_init(void)
{
	int ret = 0;

	printk("BootLog, +%s+\n", __func__);

	ret = platform_driver_register(&usimplug_platform_driver);
	if (ret)
	{
		printk("BootLog, -%s-, register failed, ret=%d\n", __func__, ret);
		return ret;
	}

	printk("BootLog, -%s-, ret=%d\n", __func__, ret);
	return ret;
}
static void __exit usimplug_exit(void)
{
	USIM_PRINT(USIM_DEBUG, "%s()\n", __func__);
	platform_driver_unregister(&usimplug_platform_driver);
}
module_init(usimplug_init);
module_exit(usimplug_exit);
module_param(usim_level, uint, 0644);

MODULE_DESCRIPTION("USIM plug detection");
MODULE_LICENSE("GPL");

