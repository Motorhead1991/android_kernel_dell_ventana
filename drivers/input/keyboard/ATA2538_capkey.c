/*  drivers/input/keyboard/ata2538_capkey.c
 *
 *  Copyright (c) 2008 QUALCOMM USA, INC.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, you can find it at http:
 *
 *  Driver for QWERTY keyboard with I/O communications via
 *  the I2C Interface. The keyboard hardware is a reference design supporting
 *  the standard XT/PS2 scan codes (sets 1&2).
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
#include "nvodm_query_pins_ap20.h"
#include "mach/nvrm_linux.h"
#include "nvodm_services.h"
#include <linux/earlysuspend.h>
#include "nvodm_query_discovery.h"
#include <ATA2538_capkey.h>
#include <mach/luna_hwid.h>
#define CAPKEY_RETRY_COUNT    5


static int DLL=0;
module_param_named( 
	DLL, DLL,
 	int, S_IRUGO | S_IWUSR | S_IWGRP
)
#define INFO_LEVEL  1
#define ERR_LEVEL   2
#define MY_INFO_PRINTK(level, fmt, args...) if(level <= DLL) printk( fmt, ##args);
#define PRINT_IN MY_INFO_PRINTK(4,"+++++%s++++ %d\n",__func__,__LINE__);
#define PRINT_OUT MY_INFO_PRINTK(4,"----%s---- %d\n",__func__,__LINE__);

#define CAPKEY_DRIVER_NAME "ATA2538_capkey"
#define ATA2538_CAPKEY_DEVICE_GUID NV_ODM_GUID('a','t','c','a','p','k','e','y')

struct capkey_t {
    struct i2c_client        *client_4_i2c;
    struct input_dev         *capkey_input;
    int                      irq; 
    int                      open_count; 
	int                      misc_open_count; 
	int						 capkey_suspended; 
	int                      fvs_mode_flag;  
    struct delayed_work      capkey_work;
    struct workqueue_struct  *capkey_wqueue;
    struct mutex             mutex;
	struct early_suspend     capkey_early_suspend; 
	NvOdmServicesI2cHandle hOdmI2c;
    NvOdmServicesGpioHandle hGpio;
    NvOdmServicesPmuHandle hPmu;
    NvOdmGpioPinHandle hIntPin;
	NvOdmGpioPinHandle hRstPin;
	NvOdmServicesGpioIntrHandle hGpioIntr;
	NvU32 DeviceAddr;
	NvU32 VddId;
	NvBool PowerOn;
	NvU32 capkey_powercut; 
};


static struct capkey_t         *g_ck;
static int __devinit capkey_probe(struct platform_device *pdev);
int  capkey_LED_power_switch(int on);

static int capkey_write_i2c( struct i2c_client *client,
                         uint8_t           regBuf,
                         uint8_t           *dataBuf,
                         uint8_t           dataLen )
{
    int     result = 0;
    uint8_t *buf = NULL;
    int     retryCnt = CAPKEY_RETRY_COUNT;
    NvOdmI2cStatus status = NvOdmI2cStatus_Success;    
    NvOdmI2cTransactionInfo TransactionInfo = {0};
	
    TransactionInfo.Address = g_ck->DeviceAddr;
    TransactionInfo.Buf = buf;
    TransactionInfo.Flags = NVODM_I2C_IS_WRITE;
    TransactionInfo.NumBytes = 0;

    buf = kzalloc( dataLen+1, GFP_KERNEL );
    if( NULL == buf )
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL¡G""capkey_write_i2c: alloc memory failed\n");
        return -EFAULT;
    }

    buf[0] = regBuf;
    memcpy( &buf[1], dataBuf, dataLen );
	TransactionInfo.Buf = buf;
    TransactionInfo.NumBytes = dataLen+1;

    while( retryCnt )
    {
		status = NvOdmI2cTransaction(g_ck->hOdmI2c, &TransactionInfo, 1, 
                        100, NV_WAIT_INFINITE);
        if( status != NvOdmI2cStatus_Success )
        {
            result = -EFAULT;
			MY_INFO_PRINTK( 2,"ERROR_LEVEL¡G""capkey_write_i2c: write %Xh %d bytes return failure, %d\n", buf[0], dataLen, result);
            if( NvOdmI2cStatus_Timeout == status ) msleep(10);
            retryCnt--;
        }else {
            result = 0;
            break;
        }
    }

    if( (result == 0) && (retryCnt < CAPKEY_RETRY_COUNT) )
        MY_INFO_PRINTK( 1, "INFO_LEVEL:""capkey_write_i2c: write %Xh %d bytes retry at %d\n", buf[0], dataLen, CAPKEY_RETRY_COUNT-retryCnt);

    kfree( buf );
    return result;
}

static int capkey_read_i2c( struct i2c_client *client,
                        uint8_t           regBuf,
                        uint8_t           *dataBuf,
                        uint8_t           dataLen )
{
    int     retryCnt = CAPKEY_RETRY_COUNT;
    NvOdmI2cStatus status = NvOdmI2cStatus_Success;    
    NvOdmI2cTransactionInfo TransactionInfo[2];

    TransactionInfo[0].Address = g_ck->DeviceAddr;
    TransactionInfo[0].Buf = &regBuf;
    TransactionInfo[0].Flags = NVODM_I2C_IS_WRITE;
    TransactionInfo[0].NumBytes = 1;    
    TransactionInfo[1].Address = ((g_ck->DeviceAddr) | 0x1);
    TransactionInfo[1].Buf = dataBuf;
    TransactionInfo[1].Flags = 0;
    TransactionInfo[1].NumBytes = dataLen;

    while( retryCnt )
    {
		status = NvOdmI2cTransaction(g_ck->hOdmI2c, &TransactionInfo[0], 2,
										100, NV_WAIT_INFINITE);
		if (status != NvOdmI2cStatus_Success)
		{
			switch (status)
			{
				case NvOdmI2cStatus_Timeout:
					MY_INFO_PRINTK( 2, "ERROR_LEVEL:""NvOdmPmuI2cRead8 Failed: Timeout\n"); 
					msleep(10);
					break;
				 case NvOdmI2cStatus_SlaveNotFound:
				 default:
					MY_INFO_PRINTK( 2, "ERROR_LEVEL:""NvOdmPmuI2cRead8 Failed: SlaveNotFound\n");
					break;             
			}
			retryCnt--;
		}         
        else {
            break;
        }
    }

    if( (retryCnt < CAPKEY_RETRY_COUNT) )
        MY_INFO_PRINTK( 4, "INFO_LEVEL:""capkey_read_i2c: read %Xh %d bytes retry at %d\n", regBuf, dataLen, CAPKEY_RETRY_COUNT-retryCnt);

	if(status != NvOdmI2cStatus_Success)
		return -EFAULT;
	else
		return 0;
}

static int capkey_config_ATA2538(struct capkey_t *g_ck)
{
    int i;
	int result;
    uint8_t value;
	uint8_t data[ALPHA_SIZE];
    uint8_t warm_reset_data;
	
    PRINT_IN
    
    value = 0xFF;
    result = capkey_write_i2c(NULL,ADDR_REG_CHECK,&value,1);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg_check 0X%Xh = 0x%X\n",ADDR_REG_CHECK, value );
        PRINT_OUT
        return result;
    }
    MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "write reg_check 0X%Xh = 0x%X\n",ADDR_REG_CHECK,value );
	
    
    for ( i = 0; i < ALPHA_SIZE ; i++ )
    {
		if (system_rev <= EVT1_3)
		{
			data[0] = init_data_alpha[i];
		}
		else if (system_rev == EVT2)
		{
			data[0] = init_data_alpha_evt2_1[i];
		}
		else if (system_rev >= EVT2_2 && system_rev <= EVT2_4)
		{
			data[0] = init_data_alpha_evt2_2[i];
		}
		else 
		{
			data[0] = init_data_alpha_evt3[i];
		}
    	result = capkey_write_i2c(NULL,i,data,1);
    	if ( result )
    	{
    		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to write reg_check 0X%Xh = 0x%x\n",i,data[0] );
        	PRINT_OUT
        	return result;	
    	}
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read 0x0%d = 0x%X\n",i, data[0] );
    }
	
    
    for ( i = ALPHA_SIZE; i < TOTAL_REG_SIZE; i++ )
    {	
    	if (system_rev <= EVT1_3)
		{
			data[0]	= init_data_burst[i-ALPHA_SIZE];
		}
		else if (system_rev == EVT2)
		{
			data[0] = init_data_burst_evt2_1[i-ALPHA_SIZE];
		}
		else if (system_rev >= EVT2_2 && system_rev <= EVT2_4)
		{
			data[0]	= init_data_burst_evt2_2[i-ALPHA_SIZE];
		}
		else 
		{
			data[0]	= init_data_burst_evt3[i-ALPHA_SIZE];
		}
		if(i != ADDR_REG_CHECK)
		{
    		result = capkey_write_i2c(NULL,i,data,1);
    		if ( result )
    		{
    			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to write reg init_data_burst 0x%Xh = 0x%x\n",i,data[0] );
        		PRINT_OUT
        		return result;
    		}	
        }
    }
    
    
    msleep(1);
    warm_reset_data = 0x01;
    result = capkey_write_i2c(NULL,ADDR_WARM_RESET,&warm_reset_data,1);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to write reg warm reset 0xFFh, return %d\n", result );
        PRINT_OUT
        return result;
    }
    msleep(10);
    PRINT_OUT
    return result;
}

static int capkey_detect_ATA2538(struct capkey_t *g_ck)
{
    int result = 0;
	
    uint8_t value[ALPHA_SIZE];
     
    PRINT_IN
    result = capkey_read_i2c(NULL, ADD_PA0_ALPHA, &value[0], ALPHA_SIZE);
	if( result && value[0] != INIT_ALPHA_VALUE )
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg 0x00h and value failed 0x%x, return %d\n", value[0],result );
		PRINT_OUT
		result = -EFAULT;
		return result;
	}
	
	#if 0
	
	for ( i = 0 ; i < ALPHA_SIZE ; i++ )
	{
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read 0x0%d = 0x%X\n",i, value[i] );
	}
	#endif
    PRINT_OUT
    return 0;
}

#if 0
static int capkey_release_gpio(struct capkey_t *g_ck )
{
    PRINT_IN
    MY_INFO_PRINTK( 1, "INFO_LEVEL:""capkey_release_gpio: releasing gpio IntPin \n" );
    NvOdmGpioReleasePinHandle(g_ck->hGpio, g_ck->hIntPin);
    
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_release_gpio: releasing gpio RstPin \n");
	NvOdmGpioReleasePinHandle(g_ck->hGpio, g_ck->hRstPin);

    PRINT_OUT 
    return 0;
}
#endif

static int capkey_poweron_device(struct capkey_t *g_ck, NvBool OnOff)
{
    int     rc = 0;

    PRINT_IN
    MY_INFO_PRINTK( 4,"INFO_LEVEL:""power switch %d\n", OnOff );
	g_ck->hPmu = NvOdmServicesPmuOpen();
	
	if (!g_ck->hPmu)
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL:""NvOdmServicesPmuOpen Error \n");
        return -EFAULT;
    }
    
    if (OnOff != g_ck->PowerOn)
    {
        NvOdmServicesPmuVddRailCapabilities vddrailcap;
        NvU32 settletime;
		
        NvOdmServicesPmuGetCapabilities( g_ck->hPmu, g_ck->VddId, &vddrailcap);
		
        if(OnOff)
		{
            NvOdmServicesPmuSetVoltage( g_ck->hPmu, g_ck->VddId, vddrailcap.requestMilliVolts, &settletime);
			MY_INFO_PRINTK( 4,"INFO_LEVEL:""power_switch_VDD(%d)\n", g_ck->VddId);
			MY_INFO_PRINTK( 4,"INFO_LEVEL:""power_switch_VDD_Voltage(%d)\n", vddrailcap.requestMilliVolts);
			
		}
        else
		{
            NvOdmServicesPmuSetVoltage( g_ck->hPmu, g_ck->VddId, NVODM_VOLTAGE_OFF, &settletime);
		}
		
        if (settletime)
            NvOdmOsWaitUS(settletime); 
			
        g_ck->PowerOn = OnOff;
		
		#if 0
        if(OnOff)
            NvOdmOsSleepMS(ATA_POR_DELAY);
		#endif	
    }

    NvOdmServicesPmuClose(g_ck->hPmu);

    return rc;
}

static int capkey_setup_gpio(struct capkey_t *g_ck)
{
    int rc = 0;

    PRINT_IN
    MY_INFO_PRINTK( 4, "INFO_LEVEL:""setup gpio Intpin \n"); 
	NvOdmGpioConfig(g_ck->hGpio,g_ck->hIntPin,NvOdmGpioPinMode_InputInterruptHigh);
	MY_INFO_PRINTK( 4, "INFO_LEVEL:""setup gpio Rstpin \n"); 
	NvOdmGpioConfig(g_ck->hGpio,g_ck->hRstPin,NvOdmGpioPinMode_Output);
    PRINT_OUT
    return rc;
}

static int capkey_config_gpio(struct capkey_t *g_ck)
{
    int rc = 0;
    
    PRINT_IN
    NvOdmGpioSetState(g_ck->hGpio, g_ck->hRstPin, 0);
	MY_INFO_PRINTK( 4,"INFO_LEVEL:""touchpad_config_gpio reset get low : delay 1 ms\n");
	msleep(1);
	NvOdmGpioSetState(g_ck->hGpio, g_ck->hRstPin, 1);
	MY_INFO_PRINTK( 4,"INFO_LEVEL:""config gpio RstPin get high ¡G delay 300 ms\n");
	msleep(300); 
    PRINT_OUT
    return rc;
}


int luna_capkey_callback(int up)
{
	int result = 0;
	uint8_t value;
	
	printk("%s %d\n",__func__,__LINE__);
	mutex_lock(&g_ck->mutex);
	if(up == 0) 
	{
		g_ck->capkey_powercut = 1; 
		if(g_ck->capkey_suspended == 0)
		{	
			if (g_ck->fvs_mode_flag == 0 ) 
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_HOME is released\n");
				input_report_key(g_ck->capkey_input, KEY_HOME, 0);
				input_sync(g_ck->capkey_input);
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_MENU is released\n");
				input_report_key(g_ck->capkey_input, KEY_MENU, 0);
				input_sync(g_ck->capkey_input);
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_BACK is released\n");
				input_report_key(g_ck->capkey_input, KEY_BACK, 0);
				input_sync(g_ck->capkey_input);
			}
			
			capkey_LED_power_switch(0);
			
			NvOdmGpioInterruptMask(g_ck->hGpioIntr, 1);
			mutex_unlock(&g_ck->mutex);
			cancel_work_sync(&g_ck->capkey_work.work);
			mutex_lock(&g_ck->mutex);
			if (result) 
			{
				NvOdmGpioInterruptMask(g_ck->hGpioIntr, 0); 
				MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "enalbe irq %p\n",g_ck->hGpioIntr );
			}
		}
	}
	else 
	{
		g_ck->capkey_powercut = 0; 
		if(g_ck->capkey_suspended == 0)
		{
			#if 0
			capkey_config_gpio(g_ck);
			#endif
			
			result = capkey_config_ATA2538(g_ck);
			if( result )
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config capkey ATA2538\n" );
				mutex_unlock(&g_ck->mutex);
				return result;
			}
			
			capkey_LED_power_switch(1);
			
			NvOdmGpioInterruptMask(g_ck->hGpioIntr, 0);
		}
		else 
		{
			#if 0
			capkey_config_gpio(g_ck);
			#endif
			
			result = capkey_config_ATA2538(g_ck);
			if( result )
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config capkey ATA2538\n" );
				mutex_unlock(&g_ck->mutex);
				return result;
			}
			
			value = 0x01;
			result = capkey_write_i2c(NULL,Enter_SLEEP,&value,1);
			if( result )
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read enter sleep 0X%Xh = 0x%X\n",Enter_SLEEP, result );
				PRINT_OUT
				return result;
			}
		}
	}
	printk("%s %d\n",__func__,__LINE__);
	mutex_unlock(&g_ck->mutex);
	return result;
}
EXPORT_SYMBOL(luna_capkey_callback);
static void capkey_irqWorkHandler( struct work_struct *work )
{	
    uint8_t value;
	NvU32 pinValue;
    struct sched_param s = { .sched_priority = 1 };
    
    PRINT_IN         
    if(!rt_task(current))
    {
 		if(sched_setscheduler_nocheck(current, SCHED_FIFO, &s)!=0)
		{
			MY_INFO_PRINTK( 1, "TEST_INFO_LEVEL¡G" "fail to set rt pri...\n" );
		}
		else
		{
			MY_INFO_PRINTK( 1, "TEST_INFO_LEVEL¡G" "set rt pri...\n" );
		}
    }
	
	mutex_lock(&g_ck->mutex);
    do
	{
		capkey_read_i2c(NULL, PA_TOUCH_BYTE, &value, 1);
		if (g_ck->fvs_mode_flag == 1 ) 
		{
			if(value & 0x08)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F2 is pressed\n");
				input_report_key(g_ck->capkey_input, KEY_F2, 1);
				input_sync(g_ck->capkey_input);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F2 is released\n");
				input_report_key(g_ck->capkey_input, KEY_F2, 0);
				input_sync(g_ck->capkey_input);
			}
			if(value & 0x10)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F5 is pressed\n");
				input_report_key(g_ck->capkey_input, KEY_F5, 1);
				input_sync(g_ck->capkey_input);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F5 is released\n");
				input_report_key(g_ck->capkey_input, KEY_F5, 0);
				input_sync(g_ck->capkey_input);
			}
			if(value & 0x20)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F6 is pressed\n");
				input_report_key(g_ck->capkey_input, KEY_F6, 1);
				input_sync(g_ck->capkey_input);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_F6 is released\n");
				input_report_key(g_ck->capkey_input, KEY_F6, 0);
				input_sync(g_ck->capkey_input);
			}
		}
		else 
		{
			if(value & 0x08)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_HOME is pressed\n");
				input_report_key(g_ck->capkey_input, KEY_HOME, 1);
				input_sync(g_ck->capkey_input);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_HOME is released\n");
				input_report_key(g_ck->capkey_input, KEY_HOME, 0);
				input_sync(g_ck->capkey_input);
			}
			if(value & 0x10)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_MENU is pressed\n");
				input_report_key(g_ck->capkey_input, KEY_MENU, 1);
				input_sync(g_ck->capkey_input);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_MENU is released\n");
				input_report_key(g_ck->capkey_input, KEY_MENU, 0);
				input_sync(g_ck->capkey_input);
			}
			if(value & 0x20)
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" HOME_BACK is pressed\n");
				input_report_key(g_ck->capkey_input, KEY_BACK, 1);
				input_sync(g_ck->capkey_input);
			}
			else
			{
				MY_INFO_PRINTK( 1,"TEST_INFO_LEVEL:"" KEY_BACK is released\n");
				input_report_key(g_ck->capkey_input, KEY_BACK, 0);
				input_sync(g_ck->capkey_input);
			}
		}
		
		NvOdmGpioGetState(g_ck->hGpio, g_ck->hIntPin, &pinValue);
		MY_INFO_PRINTK( 1, "TEST_INFO_LEVEL¡G" "pinValue = %d\n",pinValue );
		
    }while( 1 == pinValue );
	
	
	MY_INFO_PRINTK( 4,"TEST_INFO_LEVEL:" "before enable_irq()\n");
	NvOdmGpioInterruptDone(g_ck->hGpioIntr);
	mutex_unlock(&g_ck->mutex);
	PRINT_OUT
	return;	
}	

static void capkey_irqHandler(void *arg)
{ 
	PRINT_IN
    queue_delayed_work(g_ck->capkey_wqueue, &g_ck->capkey_work, 0);
    PRINT_OUT
}

int  capkey_LED_power_switch(int on)
{     	
	int result = 0;
	uint8_t value;
	
	PRINT_IN;
	if (on) 
	{
		value = 0x0f;
		result = capkey_write_i2c(NULL,ADDR_GPIO_CONFIGURATION,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
			PRINT_OUT
			return result;
		}
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
		printk( "turn on capkey LED light\n" );
	}
	else 
	{
		value = 0x00;
		result = capkey_write_i2c(NULL,ADDR_GPIO_CONFIGURATION,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
			PRINT_OUT
			return result;
		}
		MY_INFO_PRINTK( 4, "INFO_LEVEL:" "read reg_check 0X%Xh = 0x%x\n",ADDR_GPIO_CONFIGURATION, value );
		printk( "turn off capkey LED light\n" );
	} 
	PRINT_OUT;
    return result;    
}
static long ck_misc_ioctl( struct file *fp,
                           unsigned int cmd,
                           unsigned long arg )
{	
	
	int  result = 0;
	uint8_t value[9];
	uint8_t data = 0x7F;
	uint8_t addr;
	int i;
	struct capkey_alpha_t alpha;
	
	uint8_t *pData = NULL;
    uint    length = 0;
	
	PRINT_IN
	MY_INFO_PRINTK( 1,"INFO_LEVEL:" "cmd number=%d\n", _IOC_NR(cmd) );
	switch(cmd)
    {
		case ATA_CAPKEY_IOCTL_GET_SENSOR_VALUE:
			
			result = capkey_read_i2c(NULL, PA3_STRENGTH , &value[0], 3);
			if(result)
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg STRENGTH 0x%x and value failed 0x%x,\n", PA3_STRENGTH,value[0] );
				PRINT_OUT
				result = -EFAULT;
				return result;
			}
			
			
			result = capkey_read_i2c(NULL, PA3_IMPEDANCE, &value[3], 3);
			if(result)
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg IMPEDANCE 0x%x and value failed 0x%x,\n", PA3_IMPEDANCE,value[0] );
				PRINT_OUT
				result = -EFAULT;
				return result;
			}
			
			
			result = capkey_read_i2c(NULL, PA3_REFERENCE_IMPEDANCE, &value[6], 3);
			if(result)
			{
				MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read reg REFERENCE IMPEDANCE 0x%x and value failed 0x%x,\n", PA3_REFERENCE_IMPEDANCE,value[0] );
				PRINT_OUT
				result = -EFAULT;
				return result;
			}
			
			for ( i = 3 ; i < 9 ; i++ )
			{
				if ( i < 6)
				{
					value[i] = data - value[i];
					printk("read IMPEDANCE = %d\n",value[i] );
				}
				else
				{
					value[i] = data - value[i];
					printk("read Cal IMPEDANCE = %d\n",value[i] );
				}
			}
			
			result = copy_to_user( (void *)arg,&value[0],9 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "copy GET_SENSOR_VALUE to user failed!\n" );
                result = -EFAULT;
				return result;
			}
            break;
		 case ATA_CAPKEY_IOCTL_SET_ALPHA:
            addr = ADD_PA0_ALPHA+3;
            pData = (void *)&alpha;
            length = sizeof(alpha);
            if( copy_from_user( (void *)pData,
                                (void *)arg,
                                length) )
            {
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "copy SET_ALPHA from user failed!\n" );
                result = -EFAULT;
				return result;
            }
			else
			{
                value[0] = alpha.PA3_value;
				value[1] = alpha.PA4_value;
				value[2] = alpha.PA5_value;
				value[3] = alpha.reference_delay;
				result = capkey_write_i2c( NULL, addr, &value[0], 3 );
				if (result)
				{
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to write SET_ALPHA!\n" );
					result = -EFAULT;
					return result;
				}
				addr = ADD_PA0_ALPHA+8;
				result = capkey_write_i2c( NULL, addr, &value[3], 1 );
				if (result)
				{
					MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to write SET_ALPHA!\n" );
					result = -EFAULT;
					return result;
				}
            }
            break;
		case ATA_CAPKEY_IOCTL_GET_ALPHA:
			addr = ADD_PA0_ALPHA+3;
            result = capkey_read_i2c( NULL, addr, &value[0], 3 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to read GET_ALPHA!\n" );
                result = -EFAULT;
				return result;
			}
			addr = ADD_PA0_ALPHA+8;
            result = capkey_read_i2c( NULL, addr, &value[3], 1 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "failed to read GET_ALPHA!\n" );
                result = -EFAULT;
				return result;
			}
			result = copy_to_user( (void *)arg,&value[0],4 );
			if (result)
			{
				MY_INFO_PRINTK( 2,"ERROR_LEVEL:" "copy GET_ALPHA to user failed!\n" );
                result = -EFAULT;
				return result;
			}
            break;
	}
	PRINT_OUT
    return result;		
}

static ssize_t ck_misc_write( struct file *fp,
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
	mutex_lock(&g_ck->mutex);
		
		if (g_ck->capkey_suspended == 0)
		{
			echostr[count-1]='\0';
			if ( strcmp(echostr,"on" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				capkey_LED_power_switch(1);
			}
			else if ( strcmp(echostr,"off" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				capkey_LED_power_switch(0);
			}
			else if ( strcmp(echostr,"enter fvs" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				g_ck->fvs_mode_flag = 1; 
			}
			else if ( strcmp(echostr,"exit fvs" ) == 0 )
			{
				MY_INFO_PRINTK( 4, "INFO_LEVEL:" "User Input Echo String : %s\n",echostr);
				g_ck->fvs_mode_flag = 0; 
			}
		}
		else
		{
			MY_INFO_PRINTK(4, "INFO_LEVEL:" "capkey in deep sleep mode and can't power switch LED light\n");
		}
	
	mutex_unlock(&g_ck->mutex);
	PRINT_OUT
    return count;    
}

static struct platform_driver ata_capkey_driver = {
	.driver	 = {
		.name   = CAPKEY_DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	  = capkey_probe,
#if 0
	.remove	 = ts_remove,
    .suspend = ts_suspend,
    .resume  = ts_resume,
	.id_table = i2cPixcirTouch_idtable,
#endif	
};


static int capkey_open(struct input_dev *dev)
{
    int rc = 0;
    
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->open_count == 0 )
    {
        g_ck->open_count++; 
        MY_INFO_PRINTK( 4, "INFO_LEVEL:" "open count : %d\n",g_ck->open_count );      
    }	
    else
    {	
		rc = -EFAULT;
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
    return rc;
}

static void capkey_close(struct input_dev *dev)
{
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->open_count )
    {
        g_ck->open_count--;
        MY_INFO_PRINTK( 4, "INFO_LEVEL:" "input device still opened %d times\n",g_ck->open_count );        
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
}

static int ck_misc_release(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->misc_open_count )
    {
        g_ck->misc_open_count--;      
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
    return result;
}

static int ck_misc_open(struct inode *inode, struct file *fp)
{
    int result = 0;
    
    PRINT_IN
    mutex_lock(&g_ck->mutex);
    if( g_ck->misc_open_count ==0 )
    {
        g_ck->misc_open_count++;
        MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "misc open count : %d\n",g_ck->misc_open_count );          
    }	
    else
    { 
		result = -EFAULT;
		MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "failed to open misc count : %d\n",g_ck->misc_open_count );  
    }
    mutex_unlock(&g_ck->mutex);
    PRINT_OUT
    return result;
}

static struct file_operations ck_misc_fops = {
	.owner 	= THIS_MODULE,
	.open 	= ck_misc_open,
	.release = ck_misc_release,
	.write = ck_misc_write,
	
    .unlocked_ioctl = ck_misc_ioctl,
};

static struct miscdevice ck_misc_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= "ata_misc_capkey",
	.fops 	= &ck_misc_fops,
};


static void capkey_suspend(struct early_suspend *h)
{
	int result = 0;
    struct capkey_t *g_ck = container_of(h,struct capkey_t,capkey_early_suspend);
    uint8_t value;	
	
	PRINT_IN
    MY_INFO_PRINTK( 1, "INFO_LEVEL¡G" "capkey_suspend : E\n" );
	mutex_lock(&g_ck->mutex);
	if( g_ck->capkey_suspended )
	{
		mutex_unlock(&g_ck->mutex);
		PRINT_OUT
		return;
	}
	
	g_ck->capkey_suspended = 1;
	if(g_ck->capkey_powercut != 1)
	{
		
		capkey_LED_power_switch(0);
		
		NvOdmGpioInterruptMask(g_ck->hGpioIntr, 1);
		MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "disable irq %p\n",g_ck->hGpioIntr );		
		result = cancel_work_sync(&g_ck->capkey_work.work);
		if (result) 
		{
			NvOdmGpioInterruptMask(g_ck->hGpioIntr, 0); 
			MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "enalbe irq %p\n",g_ck->hGpioIntr );
		}
		
		
		value = 0x01;
		result = capkey_write_i2c(NULL,Enter_SLEEP,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read enter sleep 0X%Xh = 0x%X\n",Enter_SLEEP, result );
			PRINT_OUT
			return;
		}
    }
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_suspend: X\n" );
	mutex_unlock(&g_ck->mutex);
    PRINT_OUT
	return;
}


static void capkey_resume(struct early_suspend *h)
{
    int result;
	
	uint8_t value;
    struct capkey_t *g_ck = container_of( h,struct capkey_t,capkey_early_suspend);
	
    PRINT_IN
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_resume: E\n" );
	mutex_lock(&g_ck->mutex);
    if( 0 == g_ck->capkey_suspended )
	{
		mutex_unlock(&g_ck->mutex);
		PRINT_OUT
		return;
	}
	
	if(g_ck->capkey_powercut != 1)
	{
		
		value = 0x01;
		result = capkey_write_i2c(NULL,WAKEUP_SLEEP,&value,1);
		if( result )
		{
			MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "unable to read wakeup sleep 0X%Xh = 0x%X\n",WAKEUP_SLEEP, result );
			PRINT_OUT
			return;
		}
		
		
		capkey_LED_power_switch(1);
		
		
		NvOdmGpioInterruptMask(g_ck->hGpioIntr, 0);
		MY_INFO_PRINTK( 4, "INFO_LEVEL¡G" "enalbe irq %p\n",g_ck->hGpioIntr );
	}
	g_ck->capkey_suspended = 0;
	MY_INFO_PRINTK( 1, "INFO_LEVEL:" "capkey_resume: X\n" );
    mutex_unlock(&g_ck->mutex);
	PRINT_OUT
	return;
}

static int capkey_register_input( struct input_dev **input,
                                    struct platform_device *pdev )
{
    int rc = 0;
    struct input_dev *input_dev;
    int i;
    
    PRINT_IN
    i = 0;
    input_dev = input_allocate_device();
    if ( !input_dev ) {
        rc = -ENOMEM;
        return rc;
    }
    input_dev->name = CAPKEY_DRIVER_NAME;
    input_dev->phys = "ATA2538_capkey/input0";
    input_dev->id.bustype = BUS_I2C;
    input_dev->id.vendor = 0x0001;
    input_dev->id.product = 0x0002;
    input_dev->id.version = 0x0100;
    input_dev->open = capkey_open;
    input_dev->close = capkey_close;
    
    
	input_dev->evbit[0] = BIT_MASK(EV_KEY);
    
    set_bit(KEY_MENU, input_dev->keybit);
	set_bit(KEY_HOME, input_dev->keybit);
	set_bit(KEY_BACK, input_dev->keybit);
	
	
	set_bit(KEY_F2, input_dev->keybit);
	set_bit(KEY_F5, input_dev->keybit);
	set_bit(KEY_F6, input_dev->keybit);
    rc = input_register_device( input_dev );

    if ( rc )
    {
        MY_INFO_PRINTK( 2,"ERROR_LEVEL¡G""failed to register input device\\n");
        input_free_device( input_dev );
    }else {
        *input = input_dev;
    }
    PRINT_OUT
    return rc;
}

static int query_capkey_pin_info(struct capkey_t* g_ck)
{
    NvU32 i;
	NvU32 I2cInstance = 0;
    NvU32 IntGpioPort = 0;
    NvU32 IntGpioPin = 0;
	NvU32 RstGpioPort = 0;
    NvU32 RstGpioPin = 0;
	int result = 0;

	const NvOdmPeripheralConnectivity *pConnectivity = NULL;
	
    PRINT_IN
    pConnectivity = NvOdmPeripheralGetGuid(ATA2538_CAPKEY_DEVICE_GUID);
    if (!pConnectivity)
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "NvOdm capkey : pConnectivity is NULL Error \n");
        goto fail;
    }

    if (pConnectivity->Class != NvOdmPeripheralClass_HCI)
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "NvOdm capkey : didn't find any periperal in discovery query for capkey device Error \n");
        goto fail;
    }

	for (i = 0; i < pConnectivity->NumAddress; i++)
    {
        switch (pConnectivity->AddressList[i].Interface)
        {
            case NvOdmIoModule_I2c:
                g_ck->DeviceAddr = (pConnectivity->AddressList[i].Address << 1);
                I2cInstance = pConnectivity->AddressList[i].Instance;
                break;
            case NvOdmIoModule_Gpio:
                if(i == 1)
				{
					IntGpioPort = pConnectivity->AddressList[i].Instance;
					IntGpioPin = pConnectivity->AddressList[i].Address;
				}
				else if(i == 2)
				{
					RstGpioPort = pConnectivity->AddressList[i].Instance;
					RstGpioPin = pConnectivity->AddressList[i].Address;
				}
                break;
            case NvOdmIoModule_Vdd:
                if( i == 3)
					g_ck->VddId = pConnectivity->AddressList[i].Address;
                break;
            default:
                break;
        }
    }
	
    g_ck->hOdmI2c = NvOdmI2cOpen(NvOdmIoModule_I2c, I2cInstance);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:""NvOdm capkey : NvOdmI2cAddr :%d \n",g_ck->DeviceAddr);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:""NvOdm capkey : I2cInstance :%d \n",I2cInstance);
    if (!g_ck->hOdmI2c)
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:""NvOdm capkey : NvOdmI2cOpen Error \n");
        goto fail;
    }

    g_ck->hGpio = NvOdmGpioOpen();
    if (!g_ck->hGpio)
    {        
        MY_INFO_PRINTK( 2, "ERRIR_LEVEL:""NvOdm capkey : NvOdmGpioOpen Error \n");
        goto fail;
    }

    g_ck->hIntPin = NvOdmGpioAcquirePinHandle(g_ck->hGpio, IntGpioPort, IntGpioPin);
	MY_INFO_PRINTK( 1, "INFO_LEVEL:""NvOdm capkey : get GPIO Int_port %d GPIO Int_pin %d \n",IntGpioPort, IntGpioPin);
    if (!g_ck->hIntPin)
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:""NvOdm capkey : Couldn't get GPIO pin \n");
        goto fail;
    }

    g_ck->hRstPin = NvOdmGpioAcquirePinHandle(g_ck->hGpio, RstGpioPort, RstGpioPin);
	MY_INFO_PRINTK( 4, "INFO_LEVEL:""NvOdm capkey : get GPIO Rst_port %d GPIO Rst_pin %d \n",RstGpioPort, RstGpioPin);
    if (!g_ck->hRstPin)
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL:""NvOdm capkey : Couldn't get GPIO pin \n");
        goto fail;
    }
	PRINT_OUT
    return result;
	
fail:
	PRINT_OUT
	result = -EFAULT;
	if (!g_ck) return result;
        
    if (g_ck->hGpio)
    {
        if (g_ck->hIntPin)
        {
            NvOdmGpioReleasePinHandle(g_ck->hGpio, g_ck->hIntPin);
        }
		if (g_ck->hRstPin)
        {
            NvOdmGpioReleasePinHandle(g_ck->hGpio, g_ck->hRstPin);
        }
		
        NvOdmGpioClose(g_ck->hGpio);
    }

    if (g_ck->hOdmI2c)
        NvOdmI2cClose(g_ck->hOdmI2c);

    NvOdmOsFree(g_ck);
	return result;
}

static int __devinit capkey_probe(struct platform_device *pdev)
{
    int    result = 0;
    
    PRINT_IN
    g_ck = kzalloc( sizeof(struct capkey_t), GFP_KERNEL );
    if( !g_ck )
    {
        result = -ENOMEM;
        PRINT_OUT
        return result;
    }
	
    result = query_capkey_pin_info(g_ck);
	if ( result )
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "query_capkey_pin_info: failed to query_capkey_pin_info\n" );
        kfree(g_ck);
        return result;
	}
	platform_set_drvdata(pdev, g_ck);
    mutex_init(&g_ck->mutex);
    
	
    result = capkey_poweron_device(g_ck,1);
    if(result)
	{
    	MY_INFO_PRINTK( 2, "ERROR_LEVEL:" "capkey_probe: failed to power on device\n" );
		kfree(g_ck);
        PRINT_OUT
        return result;
    }
	
	
    result = capkey_setup_gpio(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to setup gpio\n" );
        kfree(g_ck);
        return result;
    }
	
	
    result = capkey_config_gpio(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config gpio\n" );
        kfree(g_ck);
        return result;
    }
	
	
    result = capkey_detect_ATA2538(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to detect\n" );
        kfree(g_ck);
        return result;
    }
	
	
    result = capkey_config_ATA2538(g_ck);
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to config capkey ATA2538\n" );
        kfree(g_ck);
        return result;
    }
	
	
	capkey_LED_power_switch(1);
	
    result = capkey_register_input( &g_ck->capkey_input, NULL );
    if( result )
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to register input\n" );
        kfree(g_ck);
    	return result;
    } 
	
	
	result = misc_register( &ck_misc_device );
    if( result )
    {
       	MY_INFO_PRINTK( 2,"ERROR_LEVEL¡G" "failed register misc driver\n" );
        result = -EFAULT;
		kfree(g_ck);
    	PRINT_OUT
		return result;       
    }
	
	
	INIT_DELAYED_WORK( &g_ck->capkey_work, capkey_irqWorkHandler );
    g_ck->capkey_wqueue = create_singlethread_workqueue("ATA2538_Capkey_Wqueue"); 
    if (!g_ck->capkey_wqueue)
    {
        MY_INFO_PRINTK( 2, "ERROR_LEVEL¡G" "capkey_probe: failed to create singlethread workqueue\n" );
        result = -ESRCH; 
        
        kfree(g_ck); 
        return result;
    }
	
	
	if (NvOdmGpioInterruptRegister(g_ck->hGpio, &g_ck->hGpioIntr,
    g_ck->hIntPin, NvOdmGpioPinMode_InputInterruptHigh, capkey_irqHandler,
	(void*)g_ck, 0) == NV_FALSE)
	{
		MY_INFO_PRINTK( 2, "ERROR_LEVEL:""irq %p requested failed\n", g_ck->hGpioIntr);
		result = -EFAULT;
		kfree(g_ck);
    	PRINT_OUT
    	return result;
	}
    else
	{
        MY_INFO_PRINTK( 4, "INFO_LEVEL:""irq %p requested successfully\n", g_ck->hGpioIntr);
	}
	
	
    g_ck->capkey_early_suspend.level = 150; 
    g_ck->capkey_early_suspend.suspend = capkey_suspend;
    g_ck->capkey_early_suspend.resume = capkey_resume;
    register_early_suspend(&g_ck->capkey_early_suspend);
	
    PRINT_OUT
    return 0;
}


static int __init capkey_init(void)
{
    int rc = 0;

    PRINT_IN
    MY_INFO_PRINTK( 1,"INFO_LEVEL¡G""system_rev=0x%x\n",system_rev);
	return platform_driver_register(&ata_capkey_driver);
    PRINT_OUT
    
	return rc;
}
module_init(capkey_init);

static void __exit capkey_exit(void)
{
    
    PRINT_IN
    platform_driver_unregister(&ata_capkey_driver);
    PRINT_OUT
}
module_exit(capkey_exit);

MODULE_DESCRIPTION("ATA2538 capkey driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Emily Jiang");
MODULE_ALIAS("platform:ata2538_capkey");
