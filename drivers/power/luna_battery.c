/*
 * drivers/power/tegra_odm_battery.c
 *
 * Battery driver for batteries implemented using NVIDIA Tegra ODM kit PMU
 * adaptation interface
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


#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/debugfs.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>
#include <linux/jiffies.h>
#include <linux/earlysuspend.h>

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_pmu.h"
#include "mach/nvrm_linux.h" 
#include "../../arch/arm/mach-tegra/odm_kit/adaptations/pmu/tps6586x/nvodm_pmu_tps6586x_supply_info_table.h"
#include "luna_battery.h"

static int bat_log_on1  = 0;
static int bat_log_on2  = 1;
static int bat_log_on3  = 0;

#define MSG(format, arg...)   {if(bat_log_on1)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(bat_log_on2)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}
#define MSG3(format, arg...)  {if(bat_log_on3)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}


const char *status_text[] = {"Unknown", "Charging", "Discharging", "Not charging", "Full"};
const char *health_text[] = {"Unknown", "Good", "Overheat", "Dead", "Over voltage", "Unspecified failure"};
const char *technology_text[] = {"Unknown", "NiMH", "Li-ion", "Li-poly", "LiFe", "NiCd", "LiMn"};
const char *bat_temp_state_text[] = {"Normal", "Hot", "Cold"};

#define I2C_RETRY_MAX   5





static struct delayed_work luna_bat_work;
static struct workqueue_struct *luna_bat_wqueue;

static int luna_bat_get_ac_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static int luna_bat_get_usb_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static int luna_bat_get_bat_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static ssize_t luna_bat_get_other_property(struct device *dev, struct device_attribute *attr,char *buf);
static ssize_t luna_bat_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t luna_bat_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static void luna_bat_work_func(struct work_struct *work);
static void luna_bat_early_suspend(struct early_suspend *h);
static void luna_bat_late_resume(struct early_suspend *h);
static int luna_bat_suspend(struct platform_device *pdev, pm_message_t state);
static int luna_bat_resume(struct platform_device *pdev);





static struct device_attribute luna_bat_ctrl_attrs[] = {
  __ATTR(batt_vol,  0444, luna_bat_get_other_property, NULL),
  __ATTR(batt_temp, 0444, luna_bat_get_other_property, NULL),
  __ATTR(chg_type,  0444, luna_bat_get_other_property, NULL),
  __ATTR(ctrl,      0666, luna_bat_ctrl_show, luna_bat_ctrl_store),
};
static enum power_supply_property luna_bat_ac_props[] = {
  POWER_SUPPLY_PROP_ONLINE,
};
static enum power_supply_property luna_bat_usb_props[] = {
  POWER_SUPPLY_PROP_ONLINE,
};
static enum power_supply_property luna_bat_bat_props[] = {
  POWER_SUPPLY_PROP_STATUS,
  POWER_SUPPLY_PROP_HEALTH,
  POWER_SUPPLY_PROP_PRESENT,
  POWER_SUPPLY_PROP_CAPACITY,
  POWER_SUPPLY_PROP_TECHNOLOGY,
};

static struct luna_bat_data luna_bat = {
  .psy_ac = {
    .name   = "ac",
    .type   = POWER_SUPPLY_TYPE_MAINS,
    .properties = luna_bat_ac_props,
    .num_properties = ARRAY_SIZE(luna_bat_ac_props),
    .get_property = luna_bat_get_ac_property,
  },
  .psy_usb = {
    .name   = "usb",
    .type   = POWER_SUPPLY_TYPE_USB,
    .properties = luna_bat_usb_props,
    .num_properties = ARRAY_SIZE(luna_bat_usb_props),
    .get_property = luna_bat_get_usb_property,
  },
  .psy_bat = {
    .name   = "battery",
    .type   = POWER_SUPPLY_TYPE_BATTERY,
    .properties = luna_bat_bat_props,
    .num_properties = ARRAY_SIZE(luna_bat_bat_props),
    .get_property = luna_bat_get_bat_property,
  },
  
  #ifdef CONFIG_HAS_EARLYSUSPEND
  	.drv_early_suspend.level = 150,
  	.drv_early_suspend.suspend = luna_bat_early_suspend,
  	.drv_early_suspend.resume = luna_bat_late_resume,
  #endif
  
  .jiff_property_valid_interval = 1*HZ/2,
  .jiff_polling_interval = 10*HZ,
  
  .bat_status   = POWER_SUPPLY_STATUS_UNKNOWN,
  .bat_health   = POWER_SUPPLY_HEALTH_UNKNOWN,
  .bat_present  = 1,
  .bat_capacity = 50,
  .bat_vol      = 3800,
  .bat_temp     = 270,
  .bat_technology = POWER_SUPPLY_TECHNOLOGY_LION,
  .bat_low_count = 0,
  .bat_health_err_count = 0,
  
  .inited       = 0,
  .suspend_flag = 0,
  .early_suspend_flag = 0,
  .wake_flag    = 0,
  
  .ac_online    = 0,
  .usb_online   = 0,
  .flight_mode  = 0,
  .charger_changed = 1,
  .low_bat_power_off = 0,
  .usb_current  = USB_STATUS_USB_0,
  .read_again   = 0,
  
  .chgic_err    = 0,
};




static int bat_read_i2c(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
{
  NvOdmI2cStatus status;
	NvOdmI2cTransactionInfo info[] = {
		[0] = {
			.Address  = luna_bat.chg_i2c_addr,
			.Flags	  = NVODM_I2C_IS_WRITE,
			.Buf	    = (NvU8 *)&reg,
			.NumBytes	= 1
		},
		[1] = {
			.Address	= luna_bat.chg_i2c_addr,
			.Flags	  = 0,
			.Buf	    = (NvU8 *)buf,
			.NumBytes = len
		}
	};
  if(!luna_bat.hI2c)
    return -ENODEV;
	status = NvOdmI2cTransaction(luna_bat.hI2c, info, 2, 100, 1000);
	if(status == NvOdmI2cStatus_Success)
	{
    return 2; 
	}
	else
	{
		MSG2("%s, status = %d",__func__,status);
    return -EIO;
	}
}
static int bat_read_i2c_retry(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
{
  NvS32 i,ret;
  for(i=0; i<I2C_RETRY_MAX; i++)
  {
    ret = bat_read_i2c(addr,reg,buf,len);
    if(ret == 2)
      return ret;
    else
      msleep(10);
  }
  return ret;
}
static int bat_write_i2c(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
{
  NvOdmI2cStatus status;
  NvU8 buf_w[64];
  NvS32 i;
	NvOdmI2cTransactionInfo info[] = {
		[0] = {
			.Address	= luna_bat.chg_i2c_addr,
			.Flags	  = NVODM_I2C_IS_WRITE,
			.Buf	    = (NvU8 *)buf_w,
			.NumBytes	= len + 1
		}
	};
  if(len >= sizeof(buf_w))  
    return -ENOMEM;
  if(!luna_bat.hI2c)
    return -ENODEV;
  buf_w[0] = reg;
  for(i=0; i<len; i++)
    buf_w[i+1] = buf[i];
	status = NvOdmI2cTransaction(luna_bat.hI2c, info, 1, 100, 1000);
	if(status == NvOdmI2cStatus_Success)
	{
    return 1; 
	}
	else
	{
		MSG2("%s, status = %d",__func__,status);
    return -EIO;
	}
}
static int bat_write_i2c_retry(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
{
  NvS32 i,ret;
  for(i=0; i<I2C_RETRY_MAX; i++)
  {
    ret = bat_write_i2c(addr,reg,buf,len);
    if(ret == 1)
      return ret;
    else
      msleep(10);
  }
  return ret;
}


static int luna_bat_get_ac_property(struct power_supply *psy,
  enum power_supply_property psp,
  union power_supply_propval *val)
{
  int ret = 0;
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    cancel_delayed_work_sync(&luna_bat_work);
    queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = luna_bat.ac_online;
      
      
      MSG("ac:  online = %d", luna_bat.ac_online);
      break;
    default:
      ret = -EINVAL;
      break;
  }
  return ret;
}

static int luna_bat_get_usb_property(struct power_supply *psy,
  enum power_supply_property psp,
  union power_supply_propval *val)
{
  int ret = 0;
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    cancel_delayed_work_sync(&luna_bat_work);
    queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = luna_bat.usb_online;
      
      
      MSG("usb: online = %d", luna_bat.usb_online);
      break;
    default:
      ret = -EINVAL;
      break;
  }
  return ret;
}

static int luna_bat_get_bat_property(struct power_supply *psy,
  enum power_supply_property psp,
  union power_supply_propval *val)
{
  static int ap_get_cap_0 = 0;
  int ret = 0;
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    cancel_delayed_work_sync(&luna_bat_work);
    queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_STATUS:
      val->intval = luna_bat.bat_status;
      MSG("bat: status = %s", status_text[luna_bat.bat_status]);
      break;
    case POWER_SUPPLY_PROP_HEALTH:
      val->intval = luna_bat.bat_health;
      MSG("bat: health = %s", health_text[luna_bat.bat_health]);
      break;
    case POWER_SUPPLY_PROP_PRESENT:
      val->intval = luna_bat.bat_present;
      MSG("bat: present = %d", luna_bat.bat_present);
      break;
    case POWER_SUPPLY_PROP_CAPACITY:
      val->intval = luna_bat.bat_capacity;
      MSG("bat: capacity = %d", luna_bat.bat_capacity);
      if(val->intval != 0)
      {
        ap_get_cap_0 = 0;
      }
      else if(!ap_get_cap_0)
      {
        ap_get_cap_0 = 1;
        MSG2("## AP get bat_capacity = 0, it will power off!");
      }
      break;
    case POWER_SUPPLY_PROP_TECHNOLOGY:
      val->intval = luna_bat.bat_technology;
      break;
    default:
      ret = -EINVAL;
      break;
  }
  return ret;
}


static ssize_t luna_bat_get_other_property(struct device *dev, struct device_attribute *attr,char *buf)
{
  int val=0;
  const ptrdiff_t off = attr - luna_bat_ctrl_attrs;  
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    cancel_delayed_work_sync(&luna_bat_work);
    queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
  }
  switch(off)
  {
    case 0: 
      val = luna_bat.bat_vol;
      MSG("bat: batt_vol = %d", luna_bat.bat_vol);
      break;
    case 1: 
      val = luna_bat.bat_temp;
      MSG("bat: batt_temp = %d", luna_bat.bat_temp);
      break;
    case 2: 
      if(luna_bat.ac_online)
        val = 1;
      else if(luna_bat.usb_online==1 && luna_bat.usb_current==USB_STATUS_USB_1500)
        val = 2;
      else
        val = 0;
      MSG("bat: batt_type = %d", val);
      break;
  }
  return sprintf(buf, "%d\n", val);
}

static void luna_bat_pmu_test(unsigned char* bufLocal, size_t count)
{
  TPS6586xPmuSupply vdd = TPS6586xPmuSupply_Invalid;
  NvOdmServicesPmuVddRailCapabilities caps;
  NvU32 SettlingTime = 0;

  if(count < 4)
  {
    MSG2("Invalid parameters, count = %d", count);
    return;
  }

  if(bufLocal[1] == 'l' || bufLocal[1] == 'L')  
  {
    MSG2("LDO:%c",bufLocal[2]);
    switch(bufLocal[2])
    {
      case '0':
        vdd = TPS6586xPmuSupply_LDO0;
        break;
      case '6':
        vdd = TPS6586xPmuSupply_LDO6;
        break;
      case '7':
        vdd = TPS6586xPmuSupply_LDO7;
        break;
      case '8':
        vdd = TPS6586xPmuSupply_LDO8;
        break;
    }
  }
  else if(bufLocal[1] == 'g' || bufLocal[1] == 'G')  
  {
    MSG2("GPIO:%c",bufLocal[2]);
    switch(bufLocal[2])
    {
      case '1':
        vdd = Ext_TPS74201PmuSupply_LDO;  
        break;
      case '2':
        vdd = Ext_TPS72012PmuSupply_LDO;  
        break;
      case '3':
        vdd = Ext_TPS62290PmuSupply_BUCK; 
        break;
    }
  }
  else if(bufLocal[1] == 'r' || bufLocal[1] == 'R')  
  {
    MSG2("RGB:%c",bufLocal[2]);
    switch(bufLocal[2])
    {
      case '1':
        vdd = TPS6586xPmuSupply_RED1;   
        break;
      case '2':
        vdd = TPS6586xPmuSupply_GREEN1; 
        break;
      case '3':
        vdd = TPS6586xPmuSupply_BLUE1;
        break;
      case '4':
        vdd = TPS6586xPmuSupply_RED2;
        break;
      case '5':
        vdd = TPS6586xPmuSupply_GREEN2;
        break;
      case '6':
        vdd = TPS6586xPmuSupply_BLUE2;
        break;
    }
  }
  else
  {
    MSG2("Invalid:%c", bufLocal[1]);
    return;
  }
  NvOdmServicesPmuGetCapabilities(luna_bat.hPmu, vdd, &caps);
  MSG2("vdd=%d, pro=%d, Min=%d, Step=%d, Max=%d, req=%d",
    vdd, caps.RmProtected, caps.MinMilliVolts, caps.StepMilliVolts, caps.MaxMilliVolts, caps.requestMilliVolts);

  switch(bufLocal[3])
  {
    case '0': 
      MSG2("OFF");
      NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, ODM_VOLTAGE_OFF, &SettlingTime);
      break;
    case '1': 
      MSG2("ON");
      if(bufLocal[1] == 'r' || bufLocal[1] == 'R')  
        NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, caps.MaxMilliVolts>>1, &SettlingTime);
      else
      NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, caps.requestMilliVolts, &SettlingTime);
      break;
    default:
      MSG2("Invalid On/Off");
      break;
  }
}

static ssize_t luna_bat_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return 0;
}
static ssize_t luna_bat_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  static unsigned char bufLocal[256];
  

  printk(KERN_INFO "\n");
  if(count >= sizeof(bufLocal))
  {
    MSG2("%s input invalid, count = %d", __func__, count);
    return count;
  }
  memcpy(bufLocal,buf,count);

  switch(bufLocal[0])
  {
    
    
    case 'z':
      if(bufLocal[1]=='0')
      {
        MSG2("Dynamic Log All Off");
        bat_log_on1 = 0;
        bat_log_on2 = 1;
        bat_log_on3 = 0;
      }
      else if(bufLocal[1]=='1')
      {
        MSG2("Dynamic Log 1 On");
        bat_log_on1 = 1;
      }
      else if(bufLocal[1]=='3')
      {
        MSG2("Dynamic Log 3 On");
        bat_log_on3 = 1;
      }
      break;

    
    
    case 'f':
      if(count<2) break;
      MSG2("## Set flightmode = %c", bufLocal[1]);
      if(bufLocal[1]=='0')
      {
        luna_bat.flight_mode = 0;
        cancel_delayed_work_sync(&luna_bat_work);
        queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
      }
      else if(bufLocal[1]=='1')
      {
        luna_bat.flight_mode = 1;
        cancel_delayed_work_sync(&luna_bat_work);
        queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
      }
      else
      {
        MSG2("flight mode = %d", luna_bat.flight_mode);
      }
      break;

    case 'p':
      
      luna_bat_pmu_test(bufLocal,count);
      break;
    case 'b':
      {
        NvOdmServicesPmuBatteryData pmu_data;
        NvBool ret_bool;
        ret_bool = NvOdmServicesPmuGetBatteryData(luna_bat.hPmu, NvRmPmuBatteryInst_Main, &pmu_data);
        MSG2("BatteryData, ret=%d, vol=%d, temp=%d",
          ret_bool, pmu_data.batteryVoltage, pmu_data.batteryTemperature);
      }
      break;
  }

  return count;
}


void luna_bat_update_usb_status(int flag)
{
  
  if(flag & USB_STATUS_USB_0)
  {
    luna_bat.usb_current = USB_STATUS_USB_0;    MSG("Set [USB 0]");
    luna_bat.usb_online  = 0;
  }
  else if(flag & USB_STATUS_USB_100)
  {
    luna_bat.usb_current = USB_STATUS_USB_100;  MSG("Set [USB 100]");
    luna_bat.usb_online  = 1;
  }
  else if(flag & USB_STATUS_USB_500)
  {
    luna_bat.usb_current = USB_STATUS_USB_500;  MSG("Set [USB 500]");
    luna_bat.usb_online  = 1;
  }
  else if(flag & USB_STATUS_USB_1500)
  {
    luna_bat.usb_current = USB_STATUS_USB_1500; MSG("Set [USB 1500]");
    luna_bat.usb_online  = 1;
  }

  luna_bat.charger_changed = 1;

  if(luna_bat.inited)
  {
    cancel_delayed_work_sync(&luna_bat_work);
    queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
    
  }
}

EXPORT_SYMBOL(luna_bat_update_usb_status);

static int luna_bat_voltage_filter(int input)
{
  static int middle[] = {1,0,2,0,0,2,0,1};
  static int old[3] = {3800,3800,3800};
  int index = 0;
  old[2] = old[1];
  old[1] = old[0];
  old[0] = input;
  if( old[0] > old[1] ) index += 4;
  if( old[1] > old[2] ) index += 2;
  if( old[0] > old[2] ) index ++;
  return old[middle[index]];
}
static int luna_bat_voltage_to_soc(int volt)
{
  int soc;
  if(volt > 4100)
    soc = 100;
  else if(volt > 4000)
    soc = 90;
  else if(volt > 3900)
    soc = 75;
  else if(volt > 3800)
    soc = 60;
  else if(volt > 3700)
    soc = 45;
  else if(volt > 3600)
    soc = 30;
  else if(volt > 3500)
    soc = 15;
  else if(volt > 3400)
    soc = 5;
  else
    soc = 1;
  return soc;
}

static void luna_bat_work_func(struct work_struct *work)
{
  
  static char ac_online_old = 0;
  static char usb_online_old = 0;
  
  static char chg_current_term_old = 0;
  static int usb_current_old = USB_STATUS_USB_0;
  static int bat_status_old = 0;
  static int bat_health_old = 0;
  static int bat_soc_old = 255;

  int i,tmp,status_changed = 0;

  int volt, soc;

  
  char chg_reg_05;
  char chg_reg_30;
  char chg_cmd_31;
  char chg_reg_30_39[10];
  int ret_chg_info, ret_chg;
  NvU32 PinValue;

  
  NvOdmServicesPmuBatteryData  pPmuData = {0};

  

  if(!luna_bat.inited) 
  {
    MSG2("## Cancel Work, driver not inited! ##");
    return;
  }

  

  
  
  
  {
    #if 0
    if(!NvRmPmuGetAcLineStatus(s_hRmGlobal, &ACStatus)) 
    {
      MSG2("%s GetAcLineStatus Fail!",__func__);
      luna_bat.ac_online = 1;
    }
    else
    {
      MSG3("%s GetAcLineStatus = %d",__func__,ACStatus);
      if(ACStatus == NvRmPmuAcLine_Online)
        luna_bat.ac_online = 1;
      else
        luna_bat.ac_online = 0;
    }
    #endif
    if(!NvOdmServicesPmuGetBatteryData(luna_bat.hPmu, NvRmPmuBatteryInst_Main, &pPmuData)) 
    {
      MSG2("%s PmuGetBatteryData Fail!",__func__);
      volt = 3600;
      soc  = 25;
    }
    else
    {
      volt = luna_bat_voltage_filter(pPmuData.batteryVoltage);
      soc  = luna_bat_voltage_to_soc(volt);
      luna_bat.bat_capacity = soc;
      luna_bat.bat_vol      = volt;
      MSG3("%s PmuGetBatteryData, volt = %d, soc = %d",
        __func__,volt,soc);
    }
  }

  
  
  
  memset(&chg_reg_30_39,0,sizeof(chg_reg_30_39));
  
  {
    
    ret_chg_info = bat_read_i2c_retry(CHARGE_ADDR,0x30,&chg_reg_30_39[0],sizeof(chg_reg_30_39));
    if(ret_chg_info != 2)   
    {
      if(luna_bat.chgic_err < 10)
        MSG2("## chgic read fail, count = %d", luna_bat.chgic_err);
      luna_bat.chgic_err ++;
    }
    else
      luna_bat.chgic_err = 0;

    
    for(i=0; i<3; i++)
    {
      NvOdmGpioGetState(luna_bat.hGpio, luna_bat.hGpio_chg_int_pin, &PinValue);
      if(PinValue)  
        break;
      MSG3("## (31~35= %02X %02X %02X %02X %02X, 36~39= %02X %02X %02X %02X)",
        chg_reg_30_39[1], chg_reg_30_39[2], chg_reg_30_39[3], chg_reg_30_39[4], chg_reg_30_39[5],
        chg_reg_30_39[6], chg_reg_30_39[7], chg_reg_30_39[8], chg_reg_30_39[9]);
      chg_reg_30 = 0xFF;
      ret_chg = bat_write_i2c(CHARGE_ADDR,0x30,&chg_reg_30,sizeof(chg_reg_30));
    }

    
    ret_chg = bat_read_i2c_retry(CHARGE_ADDR,0x05,&chg_reg_05,sizeof(chg_reg_05));
    #if 1
      if(ret_chg==2 && chg_reg_05!=0x07)
      {
        
        chg_cmd_31 = 0x98;
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        mdelay(1);
        
        chg_reg_05 = 0x07;
        bat_write_i2c_retry(CHARGE_ADDR,0x05,&chg_reg_05,sizeof(chg_reg_05));
        MSG2("## Charger Mode = [I2C Control] (reg05=07)");
      }
    #endif

    
    memset(&luna_bat.chg_stat,0,sizeof(luna_bat.chg_stat));
    if(ret_chg_info == 2)
    {
      if(TST_BIT(chg_reg_30_39[6],0))
        luna_bat.chg_stat.enable = 1;       
      if(TST_BIT(chg_reg_30_39[7],0))
        luna_bat.chg_stat.trick_chg = 1;
      tmp = chg_reg_30_39[6] & 0x06;        
      if(tmp == 0x02)       luna_bat.chg_stat.pre_chg = 1;
      else if(tmp == 0x04)  luna_bat.chg_stat.fast_chg = 1;
      else if(tmp == 0x06)  luna_bat.chg_stat.taper_chg = 1;
    }
    
  }

  
  
  
  
  luna_bat.bat_present = 1;

  
  
  
  if(luna_bat.chgic_err)                
  {
    luna_bat.bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
  }
  else if(chg_reg_30_39[6] & 0x06)      
  {
    luna_bat.bat_status = POWER_SUPPLY_STATUS_CHARGING;
  }
  else if(TST_BIT(chg_reg_30_39[6],6))  
  {
    luna_bat.bat_status = POWER_SUPPLY_STATUS_FULL;
  }
  else
  {
    luna_bat.bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
  }

  
  
  
  if(luna_bat.chgic_err)
    luna_bat.bat_status = POWER_SUPPLY_HEALTH_UNKNOWN;
  else
    luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;

  
  
  
  if(luna_bat.charger_changed)
  {
    luna_bat.charger_changed = 0;
    status_changed ++;
    luna_bat.read_again = 3;
  }
  if(ac_online_old != luna_bat.ac_online)
  {
    MSG2("## ac_online: %d -> %d",ac_online_old,luna_bat.ac_online);
    ac_online_old = luna_bat.ac_online;
    status_changed ++;
    luna_bat.read_again = 3;
  }
  if(usb_online_old != luna_bat.usb_online)
  {
    MSG2("## usb_online: %d -> %d",usb_online_old,luna_bat.usb_online);
    usb_online_old = luna_bat.usb_online;
    status_changed ++;
    luna_bat.read_again = 3;
  }
  if(usb_current_old != luna_bat.usb_current)
  {
    MSG2("## usb_current: %d -> %d",
      usb_current_old==USB_STATUS_USB_0? 0:
      usb_current_old==USB_STATUS_USB_100? 100:
      usb_current_old==USB_STATUS_USB_500? 500:
      usb_current_old==USB_STATUS_USB_1500? 1000: 9999  ,
      luna_bat.usb_current==USB_STATUS_USB_0? 0:
      luna_bat.usb_current==USB_STATUS_USB_100? 100:
      luna_bat.usb_current==USB_STATUS_USB_500? 500:
      luna_bat.usb_current==USB_STATUS_USB_1500? 1000: 9999
      );
    usb_current_old = luna_bat.usb_current;
    status_changed ++;
    luna_bat.read_again = 3;
  }
  if(bat_status_old != luna_bat.bat_status)
  {
    MSG2("## bat_status: %s -> %s",status_text[bat_status_old],status_text[luna_bat.bat_status]);
    bat_status_old = luna_bat.bat_status;
    status_changed ++;
    luna_bat.read_again = 3;
  }
  if(bat_health_old != luna_bat.bat_health)
  {
    MSG2("## bat_health: %s -> %s",health_text[bat_health_old],health_text[luna_bat.bat_health]);
    bat_health_old = luna_bat.bat_health;
    status_changed ++;
    luna_bat.read_again = 3;
  }
  if(bat_soc_old != soc)
  {
    MSG2("## bat_capacity: %d, vol: %d", soc, volt);
    bat_soc_old = soc;
    status_changed ++;
    
  }

  
  
  
  {
    int wake = 0;
    if(luna_bat.ac_online || luna_bat.usb_online)                     
      wake |= 1;
    if(wake)
    {
      if(!luna_bat.wake_flag)
      {
        luna_bat.wake_flag = 1;
        wake_lock(&luna_bat.wlock);
        MSG2("## wake_lock: 0 -> 1, vol: %d, ac: %d, usb: %d",
          volt, luna_bat.ac_online, luna_bat.usb_online);
      }
    }
    else
    {
      if(luna_bat.wake_flag)
      {
        wake_lock_timeout(&luna_bat.wlock, HZ*2);
        luna_bat.wake_flag = 0;
        MSG2("## wake_lock: 1 -> 0, vol: %d, ac: %d, usb: %d",
          volt, luna_bat.ac_online, luna_bat.usb_online);
      }
    }
  }
  

  
  if(TST_BIT(chg_reg_30_39[6],6) != chg_current_term_old)
  {
    MSG2("## CHG CURRENT TERMINATE %d -> %d", chg_current_term_old, TST_BIT(chg_reg_30_39[6],6));
    chg_current_term_old = TST_BIT(chg_reg_30_39[6],6);
  }

  
  
  {
    if(luna_bat.chgic_err)
      goto exit_status_changed;

    
    
    
    if(luna_bat.flight_mode)
    {
      if(luna_bat.chg_stat.enable)
      {
        if(luna_bat.ac_online ||
          (luna_bat.usb_online && luna_bat.usb_current==USB_STATUS_USB_1500) )
        {
          chg_cmd_31 = 0x8C;
        }
        
        else if(luna_bat.usb_online)
        {
          chg_cmd_31 = 0x88;
        }
        #if 0
        else
        {
          chg_cmd_31 = 0x80;
        }
        #endif
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        chg_reg_05 = 0x07;
        bat_write_i2c_retry(CHARGE_ADDR,0x05,&chg_reg_05,sizeof(chg_reg_05));
        mdelay(10);
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        MSG2("## Charger Mode = [Disable] (cmd31=%02X, reg05=07)",chg_cmd_31);
      }
      goto exit_status_changed;
    }

    
    
    if(luna_bat.ac_online ||
      (luna_bat.usb_online && luna_bat.usb_current==USB_STATUS_USB_1500) )
    {
      if(TST_BIT(chg_reg_30_39[3],5))
      {
        
        goto exit_status_changed;
      }
      else
      {
        chg_cmd_31 = 0x9C;
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        chg_reg_05 = 0x07;
        bat_write_i2c_retry(CHARGE_ADDR,0x05,&chg_reg_05,sizeof(chg_reg_05));
        mdelay(10);
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        MSG2("## Charger Mode = [USB HC] (cmd31=9C, reg05=07)");
      }
    }
    
    else if(luna_bat.usb_online)
    {
      if(TST_BIT(chg_reg_30_39[3],6) && !TST_BIT(chg_reg_30_39[3],5))
      {
        
        goto exit_status_changed;
      }
      else
      {
        chg_cmd_31 = 0x98;
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        chg_reg_05 = 0x07;
        bat_write_i2c_retry(CHARGE_ADDR,0x05,&chg_reg_05,sizeof(chg_reg_05));
        mdelay(10);
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        MSG2("## Charger Mode = [USB5] (cmd31=98, reg05=07)");
      }
    }
    #if 0
    else
    {
      if(!TST_BIT(chg_reg_30_39[3],6) && !TST_BIT(chg_reg_30_39[3],5))
      {
        
        goto exit_status_changed;
      }
      else
      {
        chg_cmd_31 = 0x98;
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        chg_reg_05 = 0x07;
        bat_write_i2c_retry(CHARGE_ADDR,0x05,&chg_reg_05,sizeof(chg_reg_05));
        mdelay(10);
        bat_write_i2c_retry(CHARGE_ADDR,0x31,&chg_cmd_31,sizeof(chg_cmd_31));
        MSG2("## Charger Mode = [USB1] (cmd31 = 90, reg05 = 07)");
      }
    }
    #endif
  }

exit_status_changed:

  

  luna_bat.jiff_property_valid_time = jiffies + luna_bat.jiff_property_valid_interval;

  if(status_changed || luna_bat.read_again==1)
  {
    
    
    power_supply_changed(&luna_bat.psy_bat);
  }

  if(luna_bat.suspend_flag)
  {
    
  }
  else
  {
    if(luna_bat.read_again > 0)
    {
      luna_bat.read_again --;
      queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 1*HZ);
    }
    else
    {
      queue_delayed_work(luna_bat_wqueue, &luna_bat_work, luna_bat.jiff_polling_interval);
    }
  }

  
  
}

static void luna_bat_chg_irq_handler(void *args)
{
  if(luna_bat.inited)
  {
    
    cancel_delayed_work(&luna_bat_work);
    queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
  }
  else
  {
    MSG2("%s ## Cancelled!", __func__);
  }
  luna_bat.read_again = 3;
  NvOdmGpioInterruptDone(luna_bat.hGpio_chg_int_intr);
  
}

#ifdef CONFIG_HAS_EARLYSUSPEND
  static void luna_bat_early_suspend(struct early_suspend *h)
  {
    
    if(luna_bat.ac_online || (luna_bat.usb_online && luna_bat.usb_current == USB_STATUS_USB_1500))
      luna_bat.jiff_charging_timeout = jiffies + 4*60*60*HZ; 
    else
      luna_bat.jiff_charging_timeout = jiffies + 30*24*60*60*HZ;  
    luna_bat.early_suspend_flag = 1;
    
  }
  static void luna_bat_late_resume(struct early_suspend *h)
  {
    
    
    luna_bat.jiff_charging_timeout = jiffies + 30*24*60*60*HZ;  
    luna_bat.early_suspend_flag = 0;
    if(luna_bat.inited)
      queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
    
  }
#endif

static int luna_bat_suspend(struct platform_device *pdev, pm_message_t state)
{
  
  luna_bat.suspend_flag = 1;
  if(luna_bat.inited)
  {
    cancel_delayed_work_sync(&luna_bat_work);
    flush_workqueue(luna_bat_wqueue);
  }
  return 0;
}
static int luna_bat_resume(struct platform_device *pdev)
{
  
  luna_bat.suspend_flag = 0;
  if(luna_bat.inited)
    queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);
  return 0;
}
static void luna_bat_shutdown(struct platform_device *pdev)
{
  MSG2("%s",__func__);
}

static int luna_bat_probe(struct platform_device *plat_dev)
{
	int i, ret, fail = 0;
	NvBool ret_bool;

  MSG2("%s+", __func__);

  



  
  
	luna_bat.pConn = NvOdmPeripheralGetGuid(LUNA_BAT_GUID);
	if(!luna_bat.pConn)
	{
		MSG2("%s, pConn get Fail!", __func__);
		fail = -1;
		goto err_exit;
	}
  for(i=0; i<(luna_bat.pConn->NumAddress - 1); i++)
	{
		switch (luna_bat.pConn->AddressList[i].Interface)
		{
			case NvOdmIoModule_I2c:
				luna_bat.chg_i2c_port = luna_bat.pConn->AddressList[i].Instance;
				luna_bat.chg_i2c_addr = luna_bat.pConn->AddressList[i].Address;
				break;
			case NvOdmIoModule_Gpio:
				luna_bat.chg_en_port  = luna_bat.pConn->AddressList[i].Instance;
				luna_bat.chg_en_pin   = luna_bat.pConn->AddressList[i].Address;
				luna_bat.chg_int_port = luna_bat.pConn->AddressList[i+1].Instance;
				luna_bat.chg_int_pin  = luna_bat.pConn->AddressList[i+1].Address;
				break;
			default:
				break;
		}
	}

  
  
	luna_bat.hI2c = NvOdmI2cOpen(NvOdmIoModule_I2c, luna_bat.chg_i2c_port);
	if(!luna_bat.hI2c)
	{
		MSG2("%s, hI2c Get Fail!", __func__);
		fail = -1;
		goto err_exit;
  }

  
  
	luna_bat.hGpio = NvOdmGpioOpen();
	if(!luna_bat.hGpio)
	{
    MSG2("%s, hGpio Get Fail!", __func__);
		fail = -1;
		goto err_exit;
	}

  
  
  luna_bat.hPmu = NvOdmServicesPmuOpen();
	if(!luna_bat.hPmu)
	{
		MSG2("%s, hPmu Get Fail!", __func__);
		fail = -1;
		goto err_exit;
  }

  
  
	luna_bat.hGpio_chg_en_pin =
	  NvOdmGpioAcquirePinHandle(luna_bat.hGpio, luna_bat.chg_en_port, luna_bat.chg_en_pin);
	if(!luna_bat.hGpio_chg_en_pin)
	{
	  MSG2("%s, hGpio_chg_en_pin Get Fail!", __func__);
		fail = -1;
		goto err_exit;
	}
  
  NvOdmGpioConfig(luna_bat.hGpio, luna_bat.hGpio_chg_en_pin, NvOdmGpioPinMode_Output);
  
  NvOdmGpioSetState(luna_bat.hGpio, luna_bat.hGpio_chg_en_pin, 0);
  
  {
    char chg_reg_00_0B[12];
    char chg_reg_30_3C[13];
    bat_read_i2c_retry(CHARGE_ADDR,0x00,&chg_reg_00_0B[0],sizeof(chg_reg_00_0B));
    bat_read_i2c_retry(CHARGE_ADDR,0x30,&chg_reg_30_3C[0],sizeof(chg_reg_30_3C));
    MSG2("## (00~04= %02X %02X %02X %02X %02X)",
      chg_reg_00_0B[0], chg_reg_00_0B[1], chg_reg_00_0B[2], chg_reg_00_0B[3], chg_reg_00_0B[4]);
    MSG2("## (05~09= %02X %02X %02X %02X %02X,  0A~0B= %02X %02X)",
      chg_reg_00_0B[5], chg_reg_00_0B[6], chg_reg_00_0B[7], chg_reg_00_0B[8], chg_reg_00_0B[9],
      chg_reg_00_0B[10], chg_reg_00_0B[11]);
    MSG2("## (30~34= %02X %02X %02X %02X %02X)",
      chg_reg_30_3C[0], chg_reg_30_3C[1], chg_reg_30_3C[2], chg_reg_30_3C[3], chg_reg_30_3C[4]);
    MSG2("## (35~39= %02X %02X %02X %02X %02X,  0A~0C= %02X %02X %02X)",
      chg_reg_30_3C[5], chg_reg_30_3C[6], chg_reg_30_3C[7], chg_reg_30_3C[8], chg_reg_30_3C[9],
      chg_reg_30_3C[10], chg_reg_30_3C[11], chg_reg_30_3C[12]);
  }

  
  
	luna_bat.hGpio_chg_int_pin =
	  NvOdmGpioAcquirePinHandle(luna_bat.hGpio, luna_bat.chg_int_port, luna_bat.chg_int_pin);
	if(!luna_bat.hGpio_chg_int_pin)
	{
	  MSG2("%s, hGpio_chg_int_pin Get Fail!", __func__);
		fail = -1;
		goto err_exit;
	}
  

  
  
  ret_bool = NvOdmGpioInterruptRegister(luna_bat.hGpio,
    &luna_bat.hGpio_chg_int_intr,
    luna_bat.hGpio_chg_int_pin,
    NvOdmGpioPinMode_InputInterruptFallingEdge, 
    luna_bat_chg_irq_handler,
    (void*)luna_bat_chg_irq_handler, 0);
  if(!ret_bool)
  {
    MSG2("%s, hGpio_chg_int_intr Get Fail!", __func__);
		fail = -1;
		goto err_exit;
  }

  
  
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_ac));
  
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_usb));
  
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_bat));
  

  
  for(i=0; i<ARRAY_SIZE(luna_bat_ctrl_attrs); i++)
  {
    ret = device_create_file(luna_bat.psy_bat.dev, &luna_bat_ctrl_attrs[i]);
    if(ret) MSG2("%s: create FAIL, ret=%d",luna_bat_ctrl_attrs[i].attr.name,ret);
  }

  
  wake_lock_init(&luna_bat.wlock, WAKE_LOCK_SUSPEND, "luna_bat_active");

  
  INIT_DELAYED_WORK(&luna_bat_work, luna_bat_work_func);
  luna_bat_wqueue = create_singlethread_workqueue("luna_bat_workqueue");
  if(luna_bat_wqueue) 
  {
    MSG2("%s luna_bat_workqueue created PASS!",__func__);
  }
  else  
  {
    MSG2("%s luna_bat_workqueue created FAIL!",__func__);
		fail = -1;
		goto err_exit;
  }
  luna_bat.inited = 1;
  queue_delayed_work(luna_bat_wqueue, &luna_bat_work, 0);

  MSG2("%s-, ret=0", __func__);
	return 0;

err_exit:

  MSG2("%s-, ret=-1", __func__);
  return -1;
}



static struct platform_driver luna_bat_driver =
{
  .driver	= {
    .name   = "luna_battery",
    .owner  = THIS_MODULE,
	},
	.suspend  = luna_bat_suspend,
	.resume   = luna_bat_resume,
	.shutdown = luna_bat_shutdown,
	.probe    = luna_bat_probe,
  
};

static int __init luna_bat_init(void)
{
	platform_driver_register(&luna_bat_driver);
	return 0;
}
static void __exit luna_bat_exit(void)
{
	platform_driver_unregister(&luna_bat_driver);
}

module_init(luna_bat_init);
module_exit(luna_bat_exit);
MODULE_DESCRIPTION("Luna Battery Driver");

