#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include "nvos.h"
#include "nvodm_services.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query.h"
#include "nvodm_query_gpio.h"

#define GPIOCTL_DEBUG_LOG_ENABLE 1
#define GPIOCTL_PRINTKE_ENABLE 1
#define GPIOCTL_PRINTKD_ENABLE 1
#define GPIOCTL_PRINTKW_ENABLE 1
#define GPIOCTL_PRINTKI_ENABLE 1

#ifdef CONFIG_TINY_ANDROID
#define USE_TINY_OS 1
#else
#define USE_TINY_OS 0
#endif

#if GPIOCTL_DEBUG_LOG_ENABLE
	#if GPIOCTL_PRINTKE_ENABLE
		#define GPIOCTL_PRINTKE printk
	#else
		#define GPIOCTL_PRINTKE(a...)
	#endif

	#if GPIOCTL_PRINTKD_ENABLE
		#define GPIOCTL_PRINTKD printk
	#else
		#define GPIOCTL_PRINTKD(a...)
	#endif

	#if GPIOCTL_PRINTKW_ENABLE
		#define GPIOCTL_PRINTKW printk
	#else
		#define GPIOCTL_PRINTKW(a...)
	#endif

	#if GPIOCTL_PRINTKI_ENABLE
		#define GPIOCTL_PRINTKI printk
	#else
		#define GPIOCTL_PRINTKI(a...)
	#endif
#else
	#define GPIOCTL_PRINTKE(a...)
	#define GPIOCTL_PRINTKD(a...)
	#define GPIOCTL_PRINTKW(a...)
	#define GPIOCTL_PRINTKI(a...)
#endif

#define GPIOCTL_NAME "gpioctl"


struct gpioctl_driver_data_t
{
	NvOdmServicesGpioHandle hGpio;
	NvOdmGpioPinHandle hResetGpioPin;
	NvOdmGpioPinHandle hStandByGpioPin;
	#if USE_TINY_OS
	NvOdmGpioPinHandle hBackLightEnableGpioPin;
	#endif
	int mResetGpioState;
	int mStandByGpioState;
	#if USE_TINY_OS
	int mBackLightEnableGpioState;
	#endif
};



static struct gpioctl_driver_data_t gpioctl_driver_data;

#if 0
static ssize_t gpioctl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);

	for (i = 0; i < ARRAY_SIZE(gpio_pin_name); i++)
	{
		if (!strncmp(*(attr->attr).name, driver_data->gpioctl_attrs[i].attr.name, strlen(driver_data->gpioctl_attrs[i].attr.name)))
		{
			break;
		}
	}
	if (i >= ARRAY_SIZE(gpio_pin_name))
	{
		GPIOCTL_PRINTKW(KERN_WARNING "[GPIOCTL] %s:: i> ARRAY_SIZE(gpio_pin_name)\n", __func__);
		return sprintf(buf, "%d\n", driver_data->mGpioState[0]);
	}

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return sprintf(buf, "%d\n", driver_data->mGpioState[i]);
}

static ssize_t gpioctl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int i;
	int value;
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);

	for (i = 0; i < ARRAY_SIZE(gpio_pin_name); i++)
	{
		if (!strncmp(*(attr->attr).name, driver_data->gpioctl_attrs[i].attr.name, strlen(driver_data->gpioctl_attrs[i].attr.name)))
		{
			break;
		}
	}
	if (i >= ARRAY_SIZE(gpio_pin_name))
	{
		GPIOCTL_PRINTKW(KERN_WARNING "[GPIOCTL] %s:: i> ARRAY_SIZE(gpio_pin_name)\n", __func__);
		return count;
	}

	sscanf(buf, "%d", &value);

	if (value == 0 || value == 1)
	{
		NvOdmGpioSetState(driver_data->hGpio, driver_data->hGpioPin[i], value);
		driver_data->mGpioState[i] = value;
	}
	else
	{
		GPIOCTL_PRINTKW(KERN_WARNING "[GPIOCTL] %s:: value=%d is invaild\n", __func__, value);
		return count;
	}

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return count;
}
#endif

static ssize_t gpioctl_reset_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);
	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::state=%d\n", __func__, driver_data->mResetGpioState);
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return sprintf(buf, "%d\n", driver_data->mResetGpioState);
}

static ssize_t gpioctl_reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value;
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);

	sscanf(buf, "%d\n", &value);
	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::value=%d\n", __func__, value);

	if (value == 0 || value == 1)
	{
		NvOdmGpioSetState(driver_data->hGpio, driver_data->hResetGpioPin, value);
		driver_data->mResetGpioState = value;
	}
	else
	{
		GPIOCTL_PRINTKW(KERN_WARNING "[GPIOCTL] %s:: value=%d is invaild\n", __func__, value);
		return count;
	}

	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::state=%d\n", __func__, driver_data->mResetGpioState);
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return count;
}

static ssize_t gpioctl_standby_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);
	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::state=%d\n", __func__, driver_data->mStandByGpioState);
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return sprintf(buf, "%d\n", driver_data->mStandByGpioState);
}

static ssize_t gpioctl_standby_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value;
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);

	sscanf(buf, "%d\n", &value);
	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::value=%d\n", __func__, value);

	if (value == 0 || value == 1)
	{
		NvOdmGpioSetState(driver_data->hGpio, driver_data->hStandByGpioPin, value);
		driver_data->mStandByGpioState = value;
	}
	else
	{
		GPIOCTL_PRINTKW(KERN_WARNING "[GPIOCTL] %s:: value=%d is invaild\n", __func__, value);
		return count;
	}

	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::state=%d\n", __func__, driver_data->mStandByGpioState);
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return count;
}

#if USE_TINY_OS
static ssize_t gpioctl_backlight_enable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);
	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::state=%d\n", __func__, driver_data->mBackLightEnableGpioState);
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return sprintf(buf, "%d\n", driver_data->mBackLightEnableGpioState);
}
static ssize_t gpioctl_backlight_enable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int value;
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(dev);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);

	sscanf(buf, "%d\n", &value);
	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::value=%d\n", __func__, value);

	if (value == 0 || value == 1)
	{
		NvOdmGpioSetState(driver_data->hGpio, driver_data->hBackLightEnableGpioPin, value);
		driver_data->mBackLightEnableGpioState = value;
	}
	else
	{
		GPIOCTL_PRINTKW(KERN_WARNING "[GPIOCTL] %s:: value=%d is invaild\n", __func__, value);
		return count;
	}

	GPIOCTL_PRINTKI(KERN_INFO "[GPIOCTL] %s::state=%d\n", __func__, driver_data->mBackLightEnableGpioState);
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return count;
}
#endif

static struct device_attribute gpioctl_attrs[] = {
	__ATTR(reset, 0666, gpioctl_reset_show, gpioctl_reset_store),
	__ATTR(standby, 0666, gpioctl_standby_show, gpioctl_standby_store),
	#if USE_TINY_OS
	__ATTR(backlight_enable, 0666, gpioctl_backlight_enable_show, gpioctl_backlight_enable_store),
	#endif
};

static int gpioctl_probe(struct platform_device *pdev)
{
	int i;
	int ret = 0;

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);

	NvOsMemset(&gpioctl_driver_data, 0, sizeof(struct gpioctl_driver_data_t));
	dev_set_drvdata(&pdev->dev, &gpioctl_driver_data);

	gpioctl_driver_data.hGpio = NvOdmGpioOpen();
	if (!gpioctl_driver_data.hGpio)
	{
		ret = -1;
		GPIOCTL_PRINTKE(KERN_ERR "[GPIOCTL] %s:: NvOdmGpioOpen fail err\n", __func__);
		goto gpioctl_probe_NvOdmGpioOpen_err;
	}

	gpioctl_driver_data.hResetGpioPin = NvOdmGpioAcquirePinHandle(gpioctl_driver_data.hGpio, 'o' - 'a', 7);	
	if (!gpioctl_driver_data.hResetGpioPin)
	{
		ret = -1;
		GPIOCTL_PRINTKE(KERN_ERR "[GPIOCTL] %s:: NvOdmGpioAcquirePinHandle ResetGpio fail err\n", __func__);
		goto gpioctl_probe_NvOdmGpioAcquirePinHandle_ResetGpio_err;
	}

	gpioctl_driver_data.hStandByGpioPin = NvOdmGpioAcquirePinHandle(gpioctl_driver_data.hGpio, 'p' - 'a', 3);  
	if (!gpioctl_driver_data.hStandByGpioPin)
	{
		ret = -1;
		GPIOCTL_PRINTKE(KERN_ERR "[GPIOCTL] %s:: NvOdmGpioAcquirePinHandle StandByGpio fail err\n", __func__);
		goto gpioctl_probe_NvOdmGpioAcquirePinHandle_StandByGpio_err;
	}

	#if USE_TINY_OS
	gpioctl_driver_data.hBackLightEnableGpioPin = NvOdmGpioAcquirePinHandle(gpioctl_driver_data.hGpio, 'r' - 'a', 7);
	if (!gpioctl_driver_data.hBackLightEnableGpioPin)
	{
		ret = -1;
		GPIOCTL_PRINTKE(KERN_ERR "[GPIOCTL] %s:: NvOdmGpioAcquirePinHandle BackLightGpio fail err\n", __func__);
		goto gpioctl_probe_NvOdmGpioAcquirePinHandle_BackLightGpio_err;
	}
	#endif

	NvOdmGpioConfig(gpioctl_driver_data.hGpio, gpioctl_driver_data.hResetGpioPin, NvOdmGpioPinMode_Output);
	NvOdmGpioConfig(gpioctl_driver_data.hGpio, gpioctl_driver_data.hStandByGpioPin, NvOdmGpioPinMode_Output);
	#if USE_TINY_OS
	NvOdmGpioConfig(gpioctl_driver_data.hGpio, gpioctl_driver_data.hBackLightEnableGpioPin, NvOdmGpioPinMode_Output);
	#endif

	NvOdmGpioSetState(gpioctl_driver_data.hGpio, gpioctl_driver_data.hResetGpioPin, 1);
	gpioctl_driver_data.mResetGpioState = 1;

	for (i = 0; i < ARRAY_SIZE(gpioctl_attrs); i++)
	{
		ret = device_create_file(&pdev->dev, &gpioctl_attrs[i]);
		if (ret)
		{
			GPIOCTL_PRINTKE(KERN_ERR "[GPIOCTL] %s:: device_create_file fail\n", __func__);
			goto gpioctl_probe_device_create_file_err;
		}
	}

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return 0;

gpioctl_probe_device_create_file_err:
	for (i -= 1; i >= 0; i--)
	{
		device_remove_file(&pdev->dev, &gpioctl_attrs[i]);
	}

	#if USE_TINY_OS
	if (gpioctl_driver_data.hBackLightEnableGpioPin)
	{
		NvOdmGpioReleasePinHandle(gpioctl_driver_data.hGpio, gpioctl_driver_data.hBackLightEnableGpioPin);
		gpioctl_driver_data.hBackLightEnableGpioPin = NULL;
	}

gpioctl_probe_NvOdmGpioAcquirePinHandle_BackLightGpio_err:
	#endif
	if (gpioctl_driver_data.hStandByGpioPin)
	{
		NvOdmGpioReleasePinHandle(gpioctl_driver_data.hGpio, gpioctl_driver_data.hStandByGpioPin);
		gpioctl_driver_data.hStandByGpioPin = NULL;
	}
gpioctl_probe_NvOdmGpioAcquirePinHandle_StandByGpio_err:
	if (gpioctl_driver_data.hResetGpioPin)
	{
		NvOdmGpioReleasePinHandle(gpioctl_driver_data.hGpio, gpioctl_driver_data.hResetGpioPin);
		gpioctl_driver_data.hResetGpioPin = NULL;
	}
gpioctl_probe_NvOdmGpioAcquirePinHandle_ResetGpio_err:
	if (gpioctl_driver_data.hGpio)
	{
		NvOdmGpioClose(gpioctl_driver_data.hGpio);
		gpioctl_driver_data.hGpio = NULL;
	}
gpioctl_probe_NvOdmGpioOpen_err:
	dev_set_drvdata(&(pdev->dev), NULL);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return ret;
}

static int gpioctl_remove(struct platform_device *pdev)
{
	int i;
	int ret = 0;
	struct gpioctl_driver_data_t *driver_data = dev_get_drvdata(&(pdev->dev));

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);

	for (i = 0; i < ARRAY_SIZE(gpioctl_attrs); i++)
	{
		device_remove_file(&pdev->dev, &gpioctl_attrs[i]);
	}

	#if USE_TINY_OS
	if (driver_data->hBackLightEnableGpioPin)
	{
		NvOdmGpioReleasePinHandle(driver_data->hGpio, driver_data->hBackLightEnableGpioPin);
		driver_data->hBackLightEnableGpioPin = NULL;
	}
	#endif

	if (driver_data->hStandByGpioPin)
	{
		NvOdmGpioReleasePinHandle(driver_data->hGpio, driver_data->hStandByGpioPin);
		driver_data->hStandByGpioPin = NULL;
	}

	if (driver_data->hResetGpioPin)
	{
		NvOdmGpioReleasePinHandle(driver_data->hGpio, driver_data->hResetGpioPin);
		driver_data->hResetGpioPin = NULL;
	}

	if (driver_data->hGpio)
	{
		NvOdmGpioClose(driver_data->hGpio);
		driver_data->hGpio = NULL;
	}

	dev_set_drvdata(&pdev->dev, NULL);

	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
	return ret;
}

static struct platform_driver gpioctl_platform_driver = {
	.driver = { .name = GPIOCTL_NAME,
	                 .owner = THIS_MODULE,},
	.probe = gpioctl_probe,
	.remove = gpioctl_remove,
};

static int __init gpioctl_init(void)
{
	int ret = 0;

	printk("BootLog, +%s+\n", __func__);

	
	ret = platform_driver_register(&gpioctl_platform_driver);
	if (ret)
	{
		printk("BootLog, -%s-, misc_register error, ret=%d\n", __func__, ret);
		return ret;
	}

	printk("BootLog, -%s-, ret=%d\n", __func__, ret);
	return ret;
}
static void __exit gpioctl_exit(void)
{
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] +%s+\n", __func__);
	platform_driver_unregister(&gpioctl_platform_driver);
	GPIOCTL_PRINTKD(KERN_DEBUG "[GPIOCTL] -%s-\n", __func__);
}
module_init(gpioctl_init);
module_exit(gpioctl_exit);

MODULE_DESCRIPTION("GPIO CONTROL");
MODULE_LICENSE("GPL");

