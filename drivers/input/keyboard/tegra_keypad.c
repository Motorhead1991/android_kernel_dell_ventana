/*
 * drivers/input/keyboard/tegra_keypad.c
 *
 * Keyboard class input driver for the NVIDIA Tegra SoC internal matrix
 * keyboard controller
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






#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/i2c.h>
#include <linux/semaphore.h>
#include <linux/delay.h>
#include <mach/gpio.h>
#include "mach/nvrm_linux.h"
#include "nvodm_services.h"
#include "nvodm_query_discovery.h"
#include <linux/earlysuspend.h>
#include <mach/luna_hwid.h>
#include <linux/wakelock.h>
#include <linux/reboot.h>
#include <linux/switch.h>

#define KEYPAD_DRIVER_NAME "tegra_keypad"
#define KEY_DRIVER_NAME "tegra_keypad_key"
#define TCH_DBG(fmt, args...) printk(KERN_INFO "KEYPAD: " fmt, ##args)
#define Tegra_Keypad_DEVICE_GUID NV_ODM_GUID('k','e','y','b','o','a','r','d')

static int DLL=0;
module_param_named( 
	DLL, DLL,
 	int, S_IRUGO | S_IWUSR | S_IWGRP
)
#define INFO_LEVEL  1
#define ERR_LEVEL   2
#define MY_INFO_PRINTK(level, fmt, args...) if(level <= DLL) printk( fmt, ##args);
#define PRINT_IN MY_INFO_PRINTK(4,"KEYPAD:+++++%s++++ %d\n",__func__,__LINE__);
#define PRINT_OUT MY_INFO_PRINTK(4,"KEYPAD:----%s---- %d\n",__func__,__LINE__);
#define NUM_OF_1A_KEY_SIZE 6 
#define NUM_OF_1B_KEY_SIZE 3 

#define NUM_OF_2_1_KEY_SIZE 4 
#define MAX_SUPPORT_KEYS 7
#define ECHOSTR_SIZE        20

struct key_t
{
	NvOdmServicesGpioIntrHandle hGpioIntr; 
	NvOdmGpioPinHandle hPin; 
	int key_code;
	struct delayed_work      key_work;
	NvU32 state;
};

enum{
CapSensor_Detectable,
CapSensor_PowerOff,
CapSensor_PowerOn
};

struct keypad_t {
	struct input_dev         *keyarray_input;
    int                      open_count;
	int                      key_size; 
	int						 keypad_suspended; 
	int                      misc_open_count; 
    struct workqueue_struct  *key_wqueue;
	struct key_t             keys[MAX_SUPPORT_KEYS];
    NvOdmServicesGpioHandle hGpio;
	struct early_suspend     key_early_suspend; 
	struct  mutex	mutex;
	struct wake_lock wake_lock;
	struct wake_lock pwr_key_keep_1s_awake_wake_lock;
	struct delayed_work      pwrkey_work;
	struct workqueue_struct *pwrkey_wqueue;
    struct wake_lock capsensor_wakelock;
    NvOdmServicesPmuHandle hPmu;
	NvOdmGpioPinHandle SARPwr_hPin; 
	NvU32 VddId;
	NvBool PowerOn;
	struct switch_dev sdev;
	int chk_SAR_state;
	int fvs_mode;
	struct delayed_work capsensor_work;
	struct workqueue_struct *capsensor_wqueue;
	int detect_state;
	uint32_t capsensor_detect_count;
	uint32_t capsensor_detect_count_fvs;
	struct key_t *capsensor_key;
	int fvs_mode_sar;
	NvU32 capsensor_powercut;
};


static struct keypad_t         *g_kp;
static int __init keypad_probe(struct platform_device *pdev);
static void keypad_irqHandler(void *arg);
static int keypad_release_gpio( struct keypad_t *g_kp );

static int query_keypad_pin_info(struct keypad_t *g_kp)
{
    NvU32 i;
    NvU32 IntGpioPin[MAX_SUPPORT_KEYS];
	NvU32 IntGpioPort[MAX_SUPPORT_KEYS];
	NvU32 SARPwr_GpioPin;
	NvU32 SARPwr_GpioPort;
	int result = 0;

	const NvOdmPeripheralConnectivity *pConnectivity = NULL;
    
	PRINT_IN
	
	if ( system_rev == EVT1A)
	{
		g_kp->key_size = NUM_OF_1A_KEY_SIZE;
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""key_size : %d for EVT1A\n",g_kp->key_size);
	}
	else if (system_rev == EVT1B || system_rev == EVT1_3)
	{
		g_kp->key_size = NUM_OF_1B_KEY_SIZE;
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""key_size : %d for EVT1B\n",g_kp->key_size);
	}
	else
	{
		g_kp->key_size = NUM_OF_2_1_KEY_SIZE;
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""key_size : %d for EVT2_1\n",g_kp->key_size);
	}
    pConnectivity = NvOdmPeripheralGetGuid(Tegra_Keypad_DEVICE_GUID);
    if (!pConnectivity)
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "NvOdm Keypad : pConnectivity is NULL Error \n");
        goto fail;
    }

    if (pConnectivity->Class != NvOdmPeripheralClass_HCI)
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "NvOdm Touch : didn't find any periperal in discovery query for touch device Error \n");
        goto fail;
    }

	for (i = 0; i < pConnectivity->NumAddress; i++)
    {
        switch (pConnectivity->AddressList[i].Interface)
        {
			case NvOdmIoModule_Gpio:
				if (i == 7 && system_rev >= EVT3)
				{
					SARPwr_GpioPort = pConnectivity->AddressList[i].Instance;
					SARPwr_GpioPin = pConnectivity->AddressList[i].Address;
				}
				else if(i != 7)
				{
					IntGpioPort[i] = pConnectivity->AddressList[i].Instance;
					IntGpioPin[i] = pConnectivity->AddressList[i].Address;
				}
                break;
			case NvOdmIoModule_Vdd:
				g_kp->VddId = pConnectivity->AddressList[i].Address;	
                break;
            default:
                break;
        }
    }
	
    g_kp->hGpio = NvOdmGpioOpen();
    if (!g_kp->hGpio)
    {        
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "NvOdm Keypad : NvOdmGpioOpen Error \n");
        goto fail;
    }
	
	for ( i = 0 ; i < g_kp->key_size ; i++ )
	{
		if (system_rev <= EVT1_3)
		{
			g_kp->keys[i].hPin = NvOdmGpioAcquirePinHandle(g_kp->hGpio, IntGpioPort[i], IntGpioPin[i]);
			MY_INFO_PRINTK( 1,"INFO_LEVEL:" "NvOdm Keypad : get GPIO Int_port %d GPIO Int_pin %d \n",IntGpioPort[i], IntGpioPin[i]);
			if (!g_kp->keys[i].hPin)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "NvOdm Keypad : Couldn't get GPIO pin \n");
				goto fail;
			}
		}
		else
		{
			if (i==3)
			{
				g_kp->keys[i].hPin = NvOdmGpioAcquirePinHandle(g_kp->hGpio, IntGpioPort[6], IntGpioPin[6]);
				MY_INFO_PRINTK( 4,"INFO_LEVEL:" "NvOdm Keypad : get GPIO Int_port %d GPIO Int_pin %d \n",IntGpioPort[6], IntGpioPin[6]);
				if (!g_kp->keys[i].hPin)
				{
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "NvOdm Keypad : Couldn't get GPIO pin \n");
					goto fail;
				}
			}
			else
			{
				g_kp->keys[i].hPin = NvOdmGpioAcquirePinHandle(g_kp->hGpio, IntGpioPort[i], IntGpioPin[i]);
				MY_INFO_PRINTK( 4,"INFO_LEVEL:" "NvOdm Keypad : get GPIO Int_port %d GPIO Int_pin %d \n",IntGpioPort[i], IntGpioPin[i]);
				if (!g_kp->keys[i].hPin)
				{
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "NvOdm Keypad : Couldn't get GPIO pin \n");
					goto fail;
				}
			}
		}
	}
	
	if ( system_rev >= EVT3)
	{
		g_kp->SARPwr_hPin = NvOdmGpioAcquirePinHandle(g_kp->hGpio, SARPwr_GpioPort, SARPwr_GpioPin);
		MY_INFO_PRINTK( 1, "INFO_LEVEL:""NvOdm Keypad : get GPIO SARPwr_port %d GPIO SARPwr_pin %d \n",SARPwr_GpioPort, SARPwr_GpioPin);
		if (!g_kp->SARPwr_hPin)
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:""NvOdm Keypad : Couldn't get SAR Pwr GPIO pin \n");
			goto fail;
		}
		printk("NvOdmGpioAcquirePinHandle sarpwr pin\n");
	}
	PRINT_OUT
    return result;
	
fail:
	keypad_release_gpio(g_kp);
	PRINT_OUT
	result = -EFAULT;
	return result;
}

extern void Tps6586x_set_EXITSLREQ_clear_SYSINEN(void);
extern void Tps6586x_clear_EXITSLREQ_set_SYSINEN(void);
static ssize_t switch_cap_print_state(struct switch_dev *sdev, char *buf)
{

	NvU32 pinValue;
	NvU32 sar_rek_value;
	PRINT_IN
	switch (switch_get_state(&g_kp->sdev)) {
	case 0:
		pinValue = 1;
		MY_INFO_PRINTK(1,"INFO_LEVEL:""BodySAR is press p[%x]\n",pinValue);
	PRINT_OUT
		return sprintf(buf, "%x\n",pinValue);
	case 1:
		pinValue = 0;
		MY_INFO_PRINTK(1,"INFO_LEVEL:""BodySAR is release p[%x]\n",pinValue);
	PRINT_OUT
		return sprintf(buf,"%x\n",pinValue);
	case 2:
		sar_rek_value = 2;
		MY_INFO_PRINTK(1,"INFO_LEVEL:""BodySAR re k p[%x]\n",sar_rek_value);
	PRINT_OUT
		return sprintf(buf,"%x\n",sar_rek_value);
	}
    
	PRINT_OUT
	return -EINVAL;
}

static void keypad_irqWorkHandler( struct work_struct *work )
{
	NvU32 pinValue;
	struct key_t *key = container_of(work,struct key_t,key_work.work);
	struct sched_param s = { .sched_priority = 1 }; 
 	NvU32 send_key_event_flag = 0;
 	
	PRINT_IN
	if(!rt_task(current))
    {
 		if(sched_setscheduler_nocheck(current, SCHED_FIFO, &s)!=0)
		{
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "fail to set rt pri...\n" );
		}
		else
		{
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "set rt pri...\n" );
		}
    }
	
	mutex_lock(&g_kp->mutex);
	if(key->key_code == KEY_POWER)
	{
		wake_lock_timeout(&g_kp->pwr_key_keep_1s_awake_wake_lock,2*HZ);
	}
	msleep(5); 
	for(;;)
	{
		NvOdmGpioGetState(g_kp->hGpio,key->hPin, &pinValue );
		if(pinValue != key->state)
		{
			if( 0 == pinValue )
			{
				printk("keycode[%d] is pressed\n",key->key_code);
				input_report_key(g_kp->keyarray_input, key->key_code, 1);
				input_sync(g_kp->keyarray_input);
				send_key_event_flag = 1;
				if(key->key_code == KEY_POWER)
				{
					wake_lock(&g_kp->wake_lock);					
					Tps6586x_set_EXITSLREQ_clear_SYSINEN();
					queue_delayed_work(g_kp->pwrkey_wqueue, &g_kp->pwrkey_work, 8*HZ);
				}
				#if 0
				else if(key->key_code == KEY_F1)
				{
					MY_INFO_PRINTK( 1,"INFO_LEVEL:"" cap sensor is pressed p[%x]\n",pinValue);
					wake_lock_timeout(&g_kp->capsensor_wakelock,5*HZ);
					switch_set_state(&g_kp->sdev, pinValue);
				}
				#endif
				NvOdmGpioConfig(g_kp->hGpio,key->hPin,NvOdmGpioPinMode_InputInterruptHigh);
			}
			else
			{
				printk("keycode[%d] is released\n",key->key_code);
				input_report_key(g_kp->keyarray_input, key->key_code, 0);
				input_sync(g_kp->keyarray_input);
				send_key_event_flag = 1;
				if(key->key_code == KEY_POWER)
				{	
					Tps6586x_clear_EXITSLREQ_set_SYSINEN();
					while(cancel_delayed_work_sync(&g_kp->pwrkey_work));
					wake_unlock(&g_kp->wake_lock);
				}
				#if 0
				else if(key->key_code == KEY_F1)
				{
					MY_INFO_PRINTK( 1,"INFO_LEVEL:"" cap sensor is released p[%x]\n",pinValue);
					switch_set_state(&g_kp->sdev, pinValue);
					wake_lock_timeout(&g_kp->capsensor_wakelock,5*HZ);
				}
				#endif
				NvOdmGpioConfig(g_kp->hGpio,key->hPin,NvOdmGpioPinMode_InputInterruptLow);
			}
			key->state = pinValue;
		}
		else
		{
			
			break;
		}
	}
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "before enable_irq()\n");
	NvOdmGpioInterruptDone(key->hGpioIntr); 

	if(send_key_event_flag == 0)
	{
		input_report_key(g_kp->keyarray_input, key->key_code, key->state?1:0);
		input_sync(g_kp->keyarray_input);
		input_report_key(g_kp->keyarray_input, key->key_code, key->state?0:1);
		input_sync(g_kp->keyarray_input);
	}

	mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return;
}

static void keypad_irqHandler(void *arg)
{
    struct key_t *key = (struct key_t *)arg;
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "before disable_irq()\n" );
    queue_delayed_work(g_kp->key_wqueue, &key->key_work, 0);
}
static ssize_t kp_misc_write( struct file *fp,
                              const char __user *buffer,
                              size_t count,
                              loff_t *ppos )
{
	char echostr[ECHOSTR_SIZE];
    
	PRINT_IN
	if ( count > ECHOSTR_SIZE )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "ts_misc_write: invalid count %d\n", count );
        return -EINVAL;
    }
    
	if ( copy_from_user(echostr,buffer,count) )
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "copy Echo String from user failed: %s\n",echostr);
		return -EINVAL;
	}
	mutex_lock(&g_kp->mutex);
	echostr[count-1]='\0';
	if ( strcmp(echostr,"enter fvs" ) == 0 )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		g_kp->keys[1].key_code = KEY_F8;
		g_kp->keys[2].key_code = KEY_F9;
		g_kp->fvs_mode = 1;
		g_kp->fvs_mode_sar = 1;
	}
	else if ( strcmp(echostr,"exit fvs" ) == 0 )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		g_kp->keys[1].key_code = KEY_VOLUMEDOWN;
		g_kp->keys[2].key_code = KEY_VOLUMEUP;
		g_kp->fvs_mode = 0;
		g_kp->fvs_mode_sar = 0;
	}
	else if( strcmp(echostr,"enable SAR" ) == 0  )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		
		g_kp->chk_SAR_state = 1;
	}
	else if( strcmp(echostr,"disable SAR" ) == 0  )
	{
		MY_INFO_PRINTK( 1, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
		
		g_kp->chk_SAR_state = 0;
	}
	else
	{
		MY_INFO_PRINTK(2, "ERROR_LEVEL:" "capkey in deep sleep mode and can't enter FVS mode\n");
	}
	mutex_unlock(&g_kp->mutex);
	PRINT_OUT
    return count;    
}

static int keypad_release_gpio( struct keypad_t *g_kp )
{
    int i;
	
	PRINT_IN
	MY_INFO_PRINTK( 4,"INFO_LEVEL:" "releasing gpio IntPin \n" );
	if(g_kp->hGpio)
	{
		for ( i = 0 ; i < g_kp->key_size; i++ )
		{
			if(g_kp->keys[i].hPin) NvOdmGpioReleasePinHandle(g_kp->hGpio, g_kp->keys[i].hPin);
		}
		NvOdmGpioClose(g_kp->hGpio);
	}
	PRINT_OUT
    return 0;
}

static int keypad_poweron_device(struct keypad_t *g_kp, NvBool OnOff)
{
    int     rc = 0;

    PRINT_IN
    MY_INFO_PRINTK( 4,"INFO_LEVEL:""power switch %d\n", OnOff );
	g_kp->hPmu = NvOdmServicesPmuOpen();
	
	if (!g_kp->hPmu)
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:""NvOdmServicesPmuOpen Error \n");
        return -EFAULT;
    }
    
    if (OnOff != g_kp->PowerOn)
    {
        NvOdmServicesPmuVddRailCapabilities vddrailcap;
        NvU32 settletime;
		
        NvOdmServicesPmuGetCapabilities( g_kp->hPmu, g_kp->VddId, &vddrailcap);
		
        if(OnOff)
		{
            NvOdmServicesPmuSetVoltage( g_kp->hPmu, g_kp->VddId, vddrailcap.requestMilliVolts, &settletime);
			MY_INFO_PRINTK( 4,"INFO_LEVEL:""power_switch_VDD(%d)\n", g_kp->VddId);
			MY_INFO_PRINTK( 4,"INFO_LEVEL:""power_switch_VDD_Voltage(%d)\n", vddrailcap.requestMilliVolts);
			
		}
        else
		{
            NvOdmServicesPmuSetVoltage( g_kp->hPmu, g_kp->VddId, NVODM_VOLTAGE_OFF, &settletime);
		}
		
        if (settletime)
            NvOdmOsWaitUS(settletime); 
        g_kp->PowerOn = OnOff;
		
		#if 0
        if(OnOff)
            NvOdmOsSleepMS(ATA_POR_DELAY);
		#endif	
    }

    NvOdmServicesPmuClose(g_kp->hPmu);
    return rc;
}


static int keypad_setup_gpio( struct keypad_t *g_kp )
{
    int i;
	
    PRINT_IN
	MY_INFO_PRINTK( 4,"INFO_LEVEL:" "setup gpio Input pin \n");
    
	for( i = 0 ; i < g_kp->key_size; i++ )
	{
		NvOdmGpioConfig(g_kp->hGpio,g_kp->keys[i].hPin,NvOdmGpioPinMode_InputInterruptLow);
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""keypad_setup_gpio: setup gpio_keyarray \n" );
	}
	
	if(system_rev >= EVT3)
	{
		NvOdmGpioConfig(g_kp->hGpio,g_kp->SARPwr_hPin,NvOdmGpioPinMode_Output);
		MY_INFO_PRINTK( 4, "INFO_LEVEL:""keypad_setup_gpio: config SAR_PWR gpio : output \n");
		
		NvOdmGpioSetState(g_kp->hGpio, g_kp->SARPwr_hPin, 1);
		MY_INFO_PRINTK( 4,"INFO_LEVEL:""keypad_setup_gpio: setup SAR_PWR state : high\n");
		printk("config SAR_PWR gpio : output high\n");
	}
	PRINT_OUT
    return 0;
}

static int keypad_keyarray_event(struct input_dev *dev, unsigned int type,
             unsigned int code, int value)
{
  return 0;
}

static int read_BodySAR_thread(void *key)
{
	int ret = 0;
	NvU32 pinValue;
	struct key_t *capsensor = (struct key_t *)key;
 	struct task_struct *tsk = current;
 	ignore_signals(tsk);
 	set_cpus_allowed_ptr(tsk,  cpu_all_mask);
 	current->flags |= PF_NOFREEZE | PF_FREEZER_NOSIG;
	PRINT_IN
	
	for(;;)
	{
		msleep(1000);
		mutex_lock(&g_kp->mutex);
		if(g_kp->keypad_suspended == 0 && g_kp->chk_SAR_state == 1)
		{
			NvOdmGpioGetState(g_kp->hGpio,capsensor->hPin, &pinValue );
			switch_set_state(&g_kp->sdev, pinValue);
			capsensor->state = pinValue;
			
        }
		mutex_unlock(&g_kp->mutex);	
	}
	PRINT_OUT
	return ret;
}


void luna_bodysar_callback(int up)
{
	NvU32 pinValue;
	NvU32 sar_rek_value;
	
	printk("%s %d\n",__func__,__LINE__);
	mutex_lock(&g_kp->mutex);
	if(up == 0) 
	{
		g_kp->capsensor_powercut = 1; 
		if(g_kp->keypad_suspended == 0)
		{	
			if(system_rev >= EVT3)
			{
				pinValue = 1;
				switch_set_state(&g_kp->sdev, pinValue);
				g_kp->capsensor_key->state = pinValue;
				printk("%s BodySAR p[%x]\n",__func__,pinValue);
				mutex_unlock(&g_kp->mutex);
				cancel_delayed_work_sync(&g_kp->capsensor_work);
				mutex_lock(&g_kp->mutex);
				NvOdmGpioSetState(g_kp->hGpio, g_kp->SARPwr_hPin, 0);
				printk("config SAR_PWR gpio : output low\n");
			}
		}
	}
	else 
	{
		g_kp->capsensor_powercut = 0; 
		if(g_kp->keypad_suspended == 0)
		{
			if(system_rev >= EVT3)
			{
				NvOdmGpioSetState(g_kp->hGpio, g_kp->SARPwr_hPin, 1);
				g_kp->detect_state = CapSensor_Detectable;
				g_kp->capsensor_detect_count = 0;
				g_kp->capsensor_detect_count_fvs = 0;
				sar_rek_value = 2;
				switch_set_state(&g_kp->sdev, sar_rek_value);
				queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
				printk("config SAR_PWR gpio : output high\n");
			}
		}
		else
		{
			
		}
	}
	printk("%s %d\n",__func__,__LINE__);
	mutex_unlock(&g_kp->mutex);
}
EXPORT_SYMBOL(luna_bodysar_callback);
static void detect_capsensor( struct work_struct *work )
{
	NvU32 pinValue;
    NvU32 sar_rek_value;
	
	mutex_lock(&g_kp->mutex);
	if(g_kp->keypad_suspended == 1)
	{
		
		mutex_unlock(&g_kp->mutex);
		return;
	}
	switch(g_kp->detect_state)
	{
		case CapSensor_Detectable:
			if(g_kp->keypad_suspended == 0 && g_kp->chk_SAR_state == 1)
			{
				NvOdmGpioGetState(g_kp->hGpio,g_kp->capsensor_key->hPin, &pinValue );
				switch_set_state(&g_kp->sdev, pinValue);
				g_kp->capsensor_key->state = pinValue;
				printk("%s BodySAR p[%x]\n",__func__,pinValue);
			}
			g_kp->capsensor_detect_count++;
			if(g_kp->fvs_mode_sar == 1)
			{
				g_kp->capsensor_detect_count_fvs++;
				printk("%s enter fvs mode\n",__func__);
				if(g_kp->capsensor_detect_count_fvs >= 9)
				{
					
					g_kp->detect_state = CapSensor_PowerOff;
				}	
			}
			else
			{
				if(g_kp->capsensor_detect_count >= 29)
				{
					g_kp->detect_state = CapSensor_PowerOff;
				}	
			}
			
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 1*HZ);
			
		break;
		
		case CapSensor_PowerOff:
			NvOdmGpioSetState(g_kp->hGpio, g_kp->SARPwr_hPin, 0);
			
			g_kp->detect_state = CapSensor_PowerOn;
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 1*HZ);
			
		break;
		
		case CapSensor_PowerOn:
			NvOdmGpioSetState(g_kp->hGpio, g_kp->SARPwr_hPin, 1);
			
			g_kp->detect_state = CapSensor_Detectable;
			if (g_kp->fvs_mode_sar == 1)
			{
				g_kp->capsensor_detect_count_fvs = 0;
			}
			else
			{
				g_kp->capsensor_detect_count = 0;
			}
			sar_rek_value = 2;
			switch_set_state(&g_kp->sdev, sar_rek_value);
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
			
		break;
		
		default:
			
		break;
	}
	mutex_unlock(&g_kp->mutex);
	
}

static int keypad_keyarray_open(struct input_dev *dev)
{
    int rc = 0;
    int i;
	
    PRINT_IN
    mutex_lock(&g_kp->mutex);
    
    if(g_kp->open_count == 0)
    {	
		for ( i = 0 ; i < g_kp->key_size ; i++ )
		{
			if(g_kp->keys[i].key_code != KEY_F1)
			{
				if (NvOdmGpioInterruptRegister(g_kp->hGpio, &g_kp->keys[i].hGpioIntr,
					g_kp->keys[i].hPin, NvOdmGpioPinMode_InputInterruptLow, keypad_irqHandler,
					(void*)(&g_kp->keys[i]), 0) == NV_FALSE)
				{
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "irq %p requested failed\n", g_kp->keys[i].hGpioIntr );
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "gpio pin %p requested failed\n", g_kp->keys[i].hPin );
					rc = -EFAULT;
				}
				else
				{
					MY_INFO_PRINTK( 4,"INFO_LEVEL:" "irq %p requested successfully\n", g_kp->keys[i].hGpioIntr );
					MY_INFO_PRINTK( 4,"INFO_LEVEL:" "gpio pin %p requested successfully\n", g_kp->keys[i].hPin );
				}
			}
			else
			{
				if(system_rev >= EVT2 && system_rev < EVT3)
				{
					kernel_thread(read_BodySAR_thread, &g_kp->keys[i], CLONE_FS | CLONE_FILES);
				}
				else if(system_rev >= EVT3)
				{
					g_kp->capsensor_key = &g_kp->keys[i];
					INIT_DELAYED_WORK( &g_kp->capsensor_work, detect_capsensor );
					g_kp->capsensor_wqueue = create_singlethread_workqueue("detect_capsensor");
					g_kp->detect_state = CapSensor_Detectable;
					g_kp->capsensor_detect_count = 0;
					g_kp->capsensor_detect_count_fvs = 0;
					queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
					printk("int work and work Q for sar\n");			
				}
			}
        }
		g_kp->open_count++; 
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "open count : %d\n",g_kp->open_count );
    }
	else
	{
        rc = -EFAULT;
        MY_INFO_PRINTK( 4,"INFO_LEVEL:" "opened %d times previously\n", g_kp->open_count);
    }
    mutex_unlock(&g_kp->mutex);
    
	PRINT_OUT
    return rc;
}

static void keypad_keyarray_close(struct input_dev *dev)
{
	int i;
	
	PRINT_IN
	mutex_lock(&g_kp->mutex);
	if(  g_kp->open_count > 0 )
    {
         g_kp->open_count--;
        MY_INFO_PRINTK( 4,"INFO_LEVEL:" "still opened %d times\n",  g_kp->open_count );
        for ( i = 0 ; i < g_kp->key_size; i++ )
		{		
			if(g_kp->keys[i].key_code != KEY_F1)
			{
				MY_INFO_PRINTK( 4,"INFO_LEVEL:" "irq %p will be freed\n",  g_kp->keys[i].hGpioIntr );	
				if (g_kp->keys[i].hGpioIntr)
					NvOdmGpioInterruptUnregister(g_kp->hGpio, g_kp->keys[i].hPin, g_kp->keys[i].hGpioIntr );
			}
		}
    }
	mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return;
}

static int kp_misc_release(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_kp->mutex);
    if( g_kp->misc_open_count )
    {
        g_kp->misc_open_count--;      
    }
    mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return result;
}
static int kp_misc_open(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_kp->mutex);
    if( g_kp->misc_open_count ==0 )
    {
        g_kp->misc_open_count++;
        MY_INFO_PRINTK( 4, "INFO_LEVEL:" "misc open count : %d\n",g_kp->misc_open_count );          
    }	
    else
    { 
		result = -EFAULT;
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to open misc count : %d\n",g_kp->misc_open_count );  
    }
    mutex_unlock(&g_kp->mutex);
    PRINT_OUT
    return result;
}
static struct file_operations kp_misc_fops = {
	.owner 	= THIS_MODULE,
	.open 	= kp_misc_open,
	.release = kp_misc_release,
	.write = kp_misc_write,
	
    
};
static struct miscdevice kp_misc_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= "misc_keypad",
	.fops 	= &kp_misc_fops,
};

static void keypad_suspend(struct early_suspend *h)
{
    int ret = 0;
	NvU32 pinValue;
    struct keypad_t *g_kp = container_of( h,struct keypad_t,key_early_suspend);
	
	int i;
	
	PRINT_IN
    MY_INFO_PRINTK( 1, "INFO_LEVEL:" "keypad_suspend : E\n" );
	mutex_lock(&g_kp->mutex);
	if( g_kp->keypad_suspended )
	{
		mutex_unlock(&g_kp->mutex);
		PRINT_OUT
		return;
	}
		g_kp->keypad_suspended = 1;
	
	if(g_kp->capsensor_powercut != 1)
	{
		if(system_rev >= EVT3)
		{
			pinValue = 1;
			switch_set_state(&g_kp->sdev, pinValue);
			g_kp->capsensor_key->state = pinValue;
			printk("%s BodySAR p[%x]\n",__func__,pinValue);
			mutex_unlock(&g_kp->mutex);
			cancel_delayed_work_sync(&g_kp->capsensor_work);
			mutex_lock(&g_kp->mutex);
			NvOdmGpioSetState(g_kp->hGpio, g_kp->SARPwr_hPin, 0);
			printk("config SAR_PWR gpio : output low\n");
		}
	}
    
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		if(g_kp->keys[i].key_code != KEY_POWER && g_kp->keys[i].key_code != KEY_VOLUMEDOWN && g_kp->keys[i].key_code != KEY_VOLUMEUP && g_kp->keys[i].key_code != KEY_F1)
		{
			NvOdmGpioInterruptMask(g_kp->keys[i].hGpioIntr, 1);
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "disable irq %p\n",g_kp->keys[i].hGpioIntr );
		}
		else
		{
			
		}
	}	
    
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		if(g_kp->keys[i].key_code != KEY_POWER && g_kp->keys[i].key_code != KEY_VOLUMEDOWN && g_kp->keys[i].key_code != KEY_VOLUMEUP && g_kp->keys[i].key_code != KEY_F1)
		{
			mutex_unlock(&g_kp->mutex);
			ret = cancel_work_sync(&g_kp->keys[i].key_work.work);
			mutex_lock(&g_kp->mutex);
			if (ret) 
			{
				NvOdmGpioInterruptMask(g_kp->keys[i].hGpioIntr, 0);
				MY_INFO_PRINTK( 1, "INFO_LEVEL:" "disalbe irq %p\n",g_kp->keys[i].hGpioIntr );
			}
		}
    }
	
	mutex_unlock(&g_kp->mutex);
    PRINT_OUT
	return;
}


static void keypad_resume(struct early_suspend *h)
{
    NvU32 pinValue;
	NvU32 sar_rek_value;
    struct keypad_t *g_kp = container_of( h,struct keypad_t,key_early_suspend);
	int i;
	
    PRINT_IN
	mutex_lock(&g_kp->mutex);
    if( 0 == g_kp->keypad_suspended )
	{
		mutex_unlock(&g_kp->mutex);
		PRINT_OUT
		return;
	} 
        
    
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		NvOdmGpioGetState(g_kp->hGpio,g_kp->keys[i].hPin, &pinValue );	
		if( 0 == pinValue )
		{
			NvOdmGpioConfig(g_kp->hGpio,g_kp->keys[i].hPin,NvOdmGpioPinMode_InputInterruptHigh);
		}
		else
		{
			NvOdmGpioConfig(g_kp->hGpio,g_kp->keys[i].hPin,NvOdmGpioPinMode_InputInterruptLow);
		}
		g_kp->keys[i].state = pinValue;
	}
	
	
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		if(g_kp->keys[i].key_code != KEY_POWER && g_kp->keys[i].key_code != KEY_VOLUMEDOWN && g_kp->keys[i].key_code != KEY_VOLUMEUP && g_kp->keys[i].key_code != KEY_F1)
		{
			NvOdmGpioInterruptMask(g_kp->keys[i].hGpioIntr, 0);
			MY_INFO_PRINTK( 1, "INFO_LEVEL:" "enable irq %p\n",g_kp->keys[i].hGpioIntr );
		}
	}	
	g_kp->keypad_suspended = 0;
	
	if(g_kp->capsensor_powercut != 1)
	{
		if(system_rev >= EVT3)
		{
			NvOdmGpioSetState(g_kp->hGpio, g_kp->SARPwr_hPin, 1);
			g_kp->detect_state = CapSensor_Detectable;
			g_kp->capsensor_detect_count = 0;
			g_kp->capsensor_detect_count_fvs = 0;
			sar_rek_value = 2;
			switch_set_state(&g_kp->sdev, sar_rek_value);
			queue_delayed_work(g_kp->capsensor_wqueue, &g_kp->capsensor_work, 3*HZ);
			printk("config SAR_PWR gpio : output high\n");
		}
	}
    mutex_unlock(&g_kp->mutex);
	PRINT_OUT
	return;
}

static int keyarray_register_input( struct input_dev **input,
                              struct platform_device *pdev )
{
  int rc = 0;
  struct input_dev *input_dev;
  int i;
  
  input_dev = input_allocate_device();
  if ( !input_dev )
  {
    rc = -ENOMEM;
    return rc;
  }

  input_dev->name = KEY_DRIVER_NAME;
  input_dev->phys = "tegra_keypad_key/event0";
  input_dev->id.bustype = BUS_I2C;
  input_dev->id.vendor = 0x0001;
  input_dev->id.product = 0x0002;
  input_dev->id.version = 0x0100;

  input_dev->open = keypad_keyarray_open;
  input_dev->close = keypad_keyarray_close;
  input_dev->event = keypad_keyarray_event;

  input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
  
	set_bit(KEY_POWER, input_dev->keybit);
	set_bit(KEY_VOLUMEDOWN, input_dev->keybit);
	set_bit(KEY_VOLUMEUP, input_dev->keybit);
	if (system_rev == EVT1A)
	{
		set_bit(KEY_MENU, input_dev->keybit);
		set_bit(KEY_BACK, input_dev->keybit);
		set_bit(KEY_HOME, input_dev->keybit);
	}
	else if (system_rev >= EVT2)
	{
		set_bit(KEY_F1, input_dev->keybit);
	}
	
	set_bit(KEY_F8, input_dev->keybit);
	set_bit(KEY_F9, input_dev->keybit);
	g_kp->keys[0].key_code = KEY_POWER;
	g_kp->keys[1].key_code = KEY_VOLUMEDOWN;
	g_kp->keys[2].key_code = KEY_VOLUMEUP;
	if (system_rev == EVT1A)
	{
		g_kp->keys[3].key_code = KEY_MENU;
		g_kp->keys[4].key_code = KEY_BACK;
		g_kp->keys[5].key_code = KEY_HOME;
	}
    else if (system_rev >= EVT2)
	{
		g_kp->keys[3].key_code = KEY_F1;
	}
	for(i = 0;i< g_kp->key_size ;i++)
	{
		g_kp->keys[i].state = 1;
	}
  
	rc = input_register_device( input_dev );
	if ( rc )
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to register keyarray input device\\n");
		input_free_device( input_dev );
	}
	else
	{
		*input = input_dev;
	}
	return rc;
}

static struct platform_driver tegra_keypad_driver = {
	.driver	 = {
		.name   = KEYPAD_DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	  = keypad_probe,
	
	
};

static void pwrkey_work_func(struct work_struct *work)
{
  printk("[KEY]## %s+\n",__func__);
  msleep(1);
  Tps6586x_clear_EXITSLREQ_set_SYSINEN();
  kernel_power_off("Power Key Long Press");
  printk("[KEY]## %s-\n",__func__);
}

static int __init keypad_probe(struct platform_device *pdev)
{
	int    result;
	int i;
 
    PRINT_IN
	
    g_kp = kzalloc( sizeof(struct keypad_t), GFP_KERNEL );
    if( !g_kp )
    {
        result = -ENOMEM;
        return result;
    }
	
	
	result = query_keypad_pin_info(g_kp);
	if( result )
	{
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to setup gpio_keyarray\n" );
        
		
        PRINT_OUT
        return result;
    }
	platform_set_drvdata(pdev, g_kp);
	mutex_init(&g_kp->mutex);
	wake_lock_init(&g_kp->wake_lock, WAKE_LOCK_SUSPEND, "power_key_lock");
	wake_lock_init(&g_kp->pwr_key_keep_1s_awake_wake_lock, WAKE_LOCK_SUSPEND, "pwr_key_keep_1s_awake_wake_lock");
    wake_lock_init(&g_kp->capsensor_wakelock, WAKE_LOCK_SUSPEND, "cap_sensor");
	INIT_DELAYED_WORK(&g_kp->pwrkey_work, pwrkey_work_func);
	g_kp->pwrkey_wqueue = create_singlethread_workqueue("pwrkey_workqueue");
	
	if(system_rev >= EVT2 && system_rev < EVT3)
	{
    	result = keypad_poweron_device(g_kp,1);
    	if(result)
		{
    		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "keypad_probe: failed to power on device\n" );
			keypad_release_gpio(g_kp);
			kfree(g_kp);
        	PRINT_OUT
        	return result;
    	}
    }
	
	
    result = keypad_setup_gpio( g_kp );
    if( result )
	{
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to setup gpio_keyarray\n" );
        keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	
	
	for ( i = 0 ; i < g_kp->key_size; i++ )
	{
		INIT_DELAYED_WORK( &g_kp->keys[i].key_work, keypad_irqWorkHandler );
	}	
	
	
    g_kp->key_wqueue = create_singlethread_workqueue(KEYPAD_DRIVER_NAME);
    if (!g_kp->key_wqueue)
	{
        switch_dev_unregister(&g_kp->sdev);
		keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	
	result = keyarray_register_input( &g_kp->keyarray_input, NULL );
	if( result )
    {
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "failed to register keyarray input\n" ); 
    	keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	
	if(system_rev >= EVT2)
	{
		g_kp->sdev.name = pdev->name;
		g_kp->sdev.print_name = switch_cap_print_state;
		result = switch_dev_register(&g_kp->sdev);
		if (result)
		{
			MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed register switch driver\n" );
			keypad_release_gpio( g_kp );
			kfree(g_kp);
			PRINT_OUT
			return result;
		}
	}
	
	result = misc_register( &kp_misc_device );
    if( result )
    {
       	MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed register misc driver\n" );
        switch_dev_unregister(&g_kp->sdev);
    	keypad_release_gpio( g_kp );
		kfree(g_kp);
        PRINT_OUT
        return result;
    }
	
	
    g_kp->key_early_suspend.level = 150; 
    g_kp->key_early_suspend.suspend = keypad_suspend;
    g_kp->key_early_suspend.resume = keypad_resume;
    register_early_suspend(&g_kp->key_early_suspend);
	
    PRINT_OUT
    return 0;
}

static int __devinit keypad_init(void)
{
	int rc = 0;
	PRINT_IN
	MY_INFO_PRINTK( 1,"INFO_LEVEL:""system_rev=0x%x\n",system_rev);
	rc = platform_driver_register(&tegra_keypad_driver);
	PRINT_OUT
	return rc;
}

static void __exit keypad_exit(void)
{
	PRINT_IN
	platform_driver_unregister(&tegra_keypad_driver);
	PRINT_OUT
}

module_init(keypad_init);
module_exit(keypad_exit);

MODULE_DESCRIPTION("Tegra keypad driver");
