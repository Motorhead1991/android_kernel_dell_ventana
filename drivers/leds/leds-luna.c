







#define NV_DEBUG 0

#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/debugfs.h>
#include <linux/earlysuspend.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>

#include <mach/luna_hwid.h>

#include "nvodm_services.h"
#include "nvodm_query_discovery.h"




static int led_log_on1  = 0;
static int led_log_on2  = 1;


#define MSG(format, arg...)   {if(led_log_on1)  printk(KERN_INFO "[LED]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(led_log_on2)  printk(KERN_INFO "[LED]" format "\n", ## arg);}



#define LUNA_LED_GUID NV_ODM_GUID('l','u','n','a','_','l','e','d')  

struct luna_led_data {
  const NvOdmPeripheralConnectivity *pConn;
	NvOdmServicesPmuHandle hPmu;
	NvU32 vdd_red;
	NvU32 vdd_green;
	NvU32 vdd_blink;
  
  
} luna_led;




static void luna_led_red_set(struct led_classdev *led_cdev, enum led_brightness value)
{
  NvU32 data;
  NvU32 SettlingTime = 0;
  MSG("%s = %d", __func__,value);
  data = value >> 3;
  if((!data) && (value))
    data = 1;
  NvOdmServicesPmuSetVoltage(luna_led.hPmu, luna_led.vdd_red, data, &SettlingTime);
}
static void luna_led_green_set(struct led_classdev *led_cdev, enum led_brightness value)
{
  NvU32 data;
  NvU32 SettlingTime = 0;
  MSG("%s = %d", __func__,value);
  data = value >> 3;
  if((!data) && (value))
    data = 1;
  NvOdmServicesPmuSetVoltage(luna_led.hPmu, luna_led.vdd_green, data, &SettlingTime);
}


static ssize_t luna_led_blink_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  NvU32 data;
  NvOdmServicesPmuGetVoltage(luna_led.hPmu, luna_led.vdd_blink, &data);
  return sprintf(buf, "%u\n", data);
}
static ssize_t luna_led_blink_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  char *after;
  unsigned long state = simple_strtoul(buf, &after, 10);
  NvU32 SettlingTime = 0;
  NvU32 data;

  MSG("%s = %d", __func__,(unsigned int)state);
  
  
  
  if(state <= 0x7F)
    data = (unsigned int)state;
  else
    data = 0x7F;

  
  
  NvOdmServicesPmuSetVoltage(luna_led.hPmu, luna_led.vdd_blink, data, &SettlingTime);

  return count;
}
static struct device_attribute luna_led_ctrl_attrs[] = {
  __ATTR(blink, 0666, luna_led_blink_show,  luna_led_blink_store),
  
  
};




static struct led_classdev luna_led_red = {
  .name           = "red",
  .brightness     = LED_OFF,
  .brightness_set = luna_led_red_set,
  
};
static struct led_classdev luna_led_green = {
  .name           = "green",
  .brightness     = LED_OFF,
  .brightness_set = luna_led_green_set,
};




static void luna_led_shutdown(struct platform_device *pdev)
{
  NvU32 data;
  NvU32 SettlingTime = 0;
  MSG("%s",__func__);
  
  data = 0;
  NvOdmServicesPmuSetVoltage(luna_led.hPmu, luna_led.vdd_red, data, &SettlingTime);
  NvOdmServicesPmuSetVoltage(luna_led.hPmu, luna_led.vdd_green, data, &SettlingTime);
  
  data = 0x7F;
  NvOdmServicesPmuSetVoltage(luna_led.hPmu, luna_led.vdd_blink, data, &SettlingTime);
}

static int luna_led_probe(struct platform_device *pdev)
{
  int ret=-EINVAL, fail=0, i;
  MSG("%s+", __func__);

  
  
  luna_led.pConn = NvOdmPeripheralGetGuid(LUNA_LED_GUID);
  if(!luna_led.pConn)
	{
		MSG2("%s, pConn get fail!", __func__);
		fail = 1;
		goto error_exit;
	}

  
  
  luna_led.hPmu = NvOdmServicesPmuOpen();
	if(!luna_led.hPmu)
	{
		MSG2("%s, hPmu get fail!", __func__);
		fail = 1;
		goto error_exit;
  }

  
  
  if(luna_led.pConn->NumAddress != 3)
  {
    MSG2("%s, NumAddress not 3!", __func__);
    fail = 1;
    goto error_exit;
  }
  else
  {
    luna_led.vdd_red    = luna_led.pConn->AddressList[0].Address; 
    luna_led.vdd_green  = luna_led.pConn->AddressList[1].Address; 
    luna_led.vdd_blink  = luna_led.pConn->AddressList[2].Address; 
    MSG2("%s, Vdd: Red=%d, Green=%d, Flash=%d",__func__,luna_led.vdd_red,luna_led.vdd_green,luna_led.vdd_blink);
  }

  
  
  ret = led_classdev_register(&pdev->dev, &luna_led_red);
  if(ret < 0) {fail = 2;  goto error_exit;}
  ret = led_classdev_register(&pdev->dev, &luna_led_green);
  if(ret < 0) {fail = 3;  goto error_exit;}

  
  
  for(i=0; i<ARRAY_SIZE(luna_led_ctrl_attrs); i++)
  {
    ret = device_create_file(luna_led_red.dev, &luna_led_ctrl_attrs[i]);
    if(ret) MSG2("%s: create FAIL, ret=%d",luna_led_ctrl_attrs[i].attr.name,ret);
  }

  
  #if defined(CONFIG_TINY_ANDROID)
    luna_led_green_set(&luna_led_green,LED_FULL);
  #endif

  MSG("%s- PASS, ret=%d", __func__,ret);
  return ret;

error_exit:
  if(fail > 2)
    led_classdev_unregister(&luna_led_green);
  if(fail > 1)
    led_classdev_unregister(&luna_led_red);
  MSG("%s- FAIL, ret=%d!", __func__,ret);
  return ret;
}



static struct platform_driver luna_led_driver = {
  
  
	.shutdown = luna_led_shutdown,
  .probe    = luna_led_probe,
  
  .driver   = {
    .name   = "luna_led",
    .owner    = THIS_MODULE,
  },
};


static int __init luna_led_init(void)
{
  int ret;
  printk("BootLog, +%s\n", __func__);
  ret = platform_driver_register(&luna_led_driver);
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}



module_init(luna_led_init);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Eric Liu, Qisda Incorporated");
MODULE_DESCRIPTION("Luna LED driver");


