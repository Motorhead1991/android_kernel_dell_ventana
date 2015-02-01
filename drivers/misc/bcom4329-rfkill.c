/*
 * RF kill device using NVIDIA Tegra ODM kit
 *
 * Copyright (c) 2009, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/rfkill.h>
#include <linux/bcom4329-rfkill.h>

#define DRIVER_NAME    "bcom4329-rfkill"
#define DRIVER_DESC    "Nvidia Tegra rfkill"




static int bcom4329_rfkill_set_power(void *data, bool blocked)
{
    struct platform_device *pdev = data;
    struct bcom4329_platform_data *plat = pdev->dev.platform_data;
#if 0
    NvU32 settletime = 0;
    NvU32 GpioLevel;
#endif

    if (!blocked)
    {
#if 1
        printk(KERN_DEBUG "bluetooth_power: Turn on chip \n");
        

        
        gpio_direction_output(plat->gpio_enable, 1);

        msleep(100);
        printk(KERN_DEBUG "bluetooth_power: Turn on chip, Sleep done\n");
        
        
        
        gpio_direction_output(plat->gpio_reset, 1);

        printk(KERN_DEBUG "bluetooth_power: Pull high for BT_WAKEUP\n");
        
        gpio_direction_output(plat->gpio_wakepin, 1);
        printk(KERN_INFO "Bluetooth power ON Done\n");
#else
        printk(KERN_DEBUG "bluetooth_power: Turn on chip \n");
        

        GpioLevel = 1;
        NvRmGpioWritePins(hGpio, &hBlueToothEnPin, &GpioLevel, 1);

        
        NvRmGpioConfigPins(hGpio, &hBlueToothEnPin, 1,
            NvRmGpioPinMode_Output);

        
        NvOdmOsSleepMS(100);
        printk(KERN_DEBUG "bluetooth_power: Turn on chip, Sleep done\n");
        
        
        GpioLevel = 1;
        NvRmGpioWritePins(hGpio, &hBlueToothResetPin, &GpioLevel, 1);

        
        NvRmGpioConfigPins(hGpio, &hBlueToothResetPin, 1,
            NvRmGpioPinMode_Output);

        printk(KERN_DEBUG "bluetooth_power: Pull low for BT_WAKEUP\n");

        GpioLevel = 0;
        NvRmGpioWritePins(hGpio, &hBlueToothWakePin, &GpioLevel, 1);

        
        NvRmGpioConfigPins(hGpio, &hBlueToothWakePin, 1,
            NvRmGpioPinMode_Output);


        printk(KERN_INFO "Bluetooth power ON Done\n");
#endif
    }
    else
    {
#if 1
        gpio_set_value(plat->gpio_reset, 0);
        gpio_set_value(plat->gpio_enable, 0);

        printk(KERN_INFO "Bluetooth power OFF\n");
#else
        
        GpioLevel = 0;
        NvRmGpioWritePins(hGpio, &hBlueToothResetPin, &GpioLevel, 1);

        
        NvRmGpioConfigPins(hGpio, &hBlueToothResetPin, 1,
            NvRmGpioPinMode_Output);

        
        NvRmGpioWritePins(hGpio, &hBlueToothEnPin, &GpioLevel, 1);

        
        NvRmGpioConfigPins(hGpio, &hBlueToothEnPin, 1,
            NvRmGpioPinMode_Output);


        
        
        
        
        

        printk(KERN_INFO "Bluetooth power OFF\n");
#endif
    }
    
    

    return 0;
}

static struct rfkill_ops bcom4329_rfkill_ops = {
    .set_block = bcom4329_rfkill_set_power,
};

static int __init bcom_rfkill_probe(struct platform_device *pdev)
{
    int rc;
    struct bcom4329_platform_data *plat = pdev->dev.platform_data;
    struct rfkill *rfkill = NULL;

    printk("+%s\n", __func__);
    #if 1
    if ((plat->gpio_reset == 0) || (plat->gpio_enable == 0) || (plat->gpio_wakepin == 0))
    {
        goto fail_gpio;
    }
    #else
    rc = gpio_request(plat->gpio_reset, "bcom4329_reset");
    if (rc < 0) {
        dev_err(&pdev->dev, "gpio_request failed\n");
        goto fail_gpio;
    }
    rc = gpio_request(plat->gpio_enable, "bcom4329_enable");
    if (rc < 0) {
        dev_err(&pdev->dev, "gpio_request failed\n");
        goto fail_gpio;
    }
    rc = gpio_request(plat->gpio_wakepin, "bcom4329_wakepin");
    if (rc < 0) {
        dev_err(&pdev->dev, "gpio_request failed\n");
        goto fail_gpio;
    }
    #endif

    rfkill = rfkill_alloc(DRIVER_NAME, &pdev->dev, RFKILL_TYPE_BLUETOOTH, &bcom4329_rfkill_ops, pdev);
    if (!rfkill) {
        rc = -ENOMEM;
        goto fail;
    }

    platform_set_drvdata(pdev, rfkill);

    rc = rfkill_register(rfkill);
    if (rc)
        goto fail;

    return rc;

fail:
    rfkill_destroy(rfkill);
fail_gpio:
    printk(KERN_ERR "gpio failed\n");
    gpio_free(plat->gpio_reset);
    gpio_free(plat->gpio_enable);
    gpio_free(plat->gpio_wakepin);

    return rc;
}

static int __init bcom_rfkill_remove(struct platform_device *pdev)
{
    struct bcom4329_platform_data *plat = pdev->dev.platform_data;
    struct rfkill *rfkill = platform_get_drvdata(pdev);

    rfkill_unregister(rfkill);
    rfkill_destroy(rfkill);
    gpio_free(plat->gpio_reset);
    gpio_free(plat->gpio_enable);
    gpio_free(plat->gpio_wakepin);

    return 0;
}

static struct platform_driver bcom_rfkill_driver = {
    .probe      = bcom_rfkill_probe,
    .remove     = bcom_rfkill_remove,
    .driver     =
    {
        .name   = DRIVER_NAME,
        .owner  = THIS_MODULE,
    },
};

static int __init bcom_rfkill_init(void)
{
    printk("++bcom_rfkill_init++");
    return platform_driver_register(&bcom_rfkill_driver);
}

static void __exit bcom_rfkill_exit(void)
{
    platform_driver_unregister(&bcom_rfkill_driver);
}

module_init(bcom_rfkill_init);
module_exit(bcom_rfkill_exit);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
