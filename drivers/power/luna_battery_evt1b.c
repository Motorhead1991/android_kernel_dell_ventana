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

#define NV_DEBUG 0

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
#include <mach/luna_hwid.h>

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_pmu.h"
#include "nvrm_pwm.h"
#include "../../arch/arm/mach-tegra/odm_kit/adaptations/pmu/tps6586x/nvodm_pmu_tps6586x_supply_info_table.h"
#include "luna_battery_evt1b.h"



static int bat_log_on1  = 0;
static int bat_log_on2  = 1;
static int bat_log_on3  = 0;

#define MSG(format, arg...)   {if(bat_log_on1)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(bat_log_on2)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}
#define MSG3(format, arg...)  {if(bat_log_on3)  printk(KERN_INFO "[BAT]" format "\n", ## arg);}


const char *status_text[] = {"Unknown", "Charging", "Discharging", "Not charging", "Full"};
const char *health_text[] = {"Unknown", "Good", "Overheat", "Dead", "Over voltage", "Unspecified failure", "cold"};
const char *technology_text[] = {"Unknown", "NiMH", "Li-ion", "Li-poly", "LiFe", "NiCd", "LiMn"};
const char *bat_temp_state_text[] = {"Normal", "Hot", "Cold"};
const char *bat_pin_name[] = {"IUSB", "USUS", "CEN ", "DCM ", "LIMD", "LIMB", "OTG ", "UOK ", "FLT ", "CHG ", "DOK ", "GLOW", "BLOW"};
const char *chg_in_name[] = {"NONE   ", "ERROR  ", "DOK_DET", "AC_DET ", "USB_DET"};



static struct luna_bat_eng_data luna_eng_data;
static struct timer_list luna_timer;
static struct work_struct luna_bat_work;
static struct workqueue_struct *luna_bat_wqueue;



static int luna_bat_get_ac_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static int luna_bat_get_usb_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static int luna_bat_get_bat_property(struct power_supply *psy,enum power_supply_property psp,union power_supply_propval *val);
static ssize_t luna_bat_get_other_property(struct device *dev, struct device_attribute *attr,char *buf);
static ssize_t luna_bat_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf);
static ssize_t luna_bat_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count);
static void luna_bat_work_func(struct work_struct *work);
static void luna_bat_work_func_evt2(struct work_struct *work);
static void luna_bat_timer_func(unsigned long temp);
static void luna_bat_early_suspend(struct early_suspend *h);
static void luna_bat_late_resume(struct early_suspend *h);
static int luna_bat_suspend(struct platform_device *pdev, pm_message_t state);
static int luna_bat_resume(struct platform_device *pdev);


extern void luna_datacard_power_up_sequence(int );
extern void luna_tmon_callback(int);
extern void luna_bodysar_callback(int );
extern int luna_capkey_callback(int );



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
  .bat_capacity_history[0] = 50,
  .bat_capacity_history[1] = 50,
  .bat_capacity_history[2] = 50,
  .gagic_err    = 0,
  .bat_low_count = 0,
  .bat_health_err_count = 0,
  
  .inited       = 0,
  .suspend_flag = 0,
  .early_suspend_flag = 0,
  .wake_flag    = 0,
  
  .ac_online    = 0,
  .usb_online   = 0,
  
  .usb_current  = USB_STATUS_USB_0,
  .usb_pmu_det  = 0,
  .ac_pmu_det   = 0,
  .read_again   = ATOMIC_INIT(0),
  .chg_stat     = CHG_STAT_IDLE,
  .chg_in       = CHG_IN_NONE,
  .chg_bat_current  = CHG_BAT_CURRENT_HIGH,
  .chg_ctl      = CHG_CTL_NONE,
};




#define I2C_RETRY_MAX   5
static int gag_read_i2c(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
{
  NvOdmI2cStatus status;
  NvOdmI2cTransactionInfo info[] = {
    [0] = {
      .Address  = addr<<1,  
      .Flags    = NVODM_I2C_IS_WRITE | NVODM_I2C_USE_REPEATED_START,
      .Buf      = (NvU8 *)&reg,
      .NumBytes = 1
    },
    [1] = {
      .Address  = addr<<1,  
      .Flags    = 0,
      .Buf      = (NvU8 *)buf,
      .NumBytes = len
    }
  };
  if(!luna_bat.hI2c)
    return -ENODEV;
  status = NvOdmI2cTransaction(luna_bat.hI2c, info, 2, 100, 1000);
  if(status == NvOdmI2cStatus_Success)
  {
    if(addr == luna_bat.i2c_addr)
    {
      if(luna_bat.gagic_err)
        MSG2("%s, status = 2, gagic_err = 0",__func__);
      luna_bat.gagic_err = 0;
    }
    return 2; 
  }
  else
  {
    if(addr == luna_bat.i2c_addr)
    {
      luna_bat.gagic_err ++;
      if(luna_bat.gagic_err < 20)
        MSG2("%s, status = %d, gagic_err = %d",__func__,status,luna_bat.gagic_err);
    }
    else
      MSG2("%s, status = %d",__func__,status);
    return -EIO;
  }
}

static int gag_write_i2c(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
{
  NvOdmI2cStatus status;
  NvU8 buf_w[64];
  NvS32 i;
  NvOdmI2cTransactionInfo info[] = {
    [0] = {
      .Address  = addr<<1,  
      .Flags    = NVODM_I2C_IS_WRITE,
      .Buf      = (NvU8 *)buf_w,
      .NumBytes = len + 1
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
    if(addr == luna_bat.i2c_addr)
    {
      if(luna_bat.gagic_err)
        MSG2("%s, status = 1, gagic_err = 0",__func__);
      luna_bat.gagic_err = 0;
    }
    return 1; 
  }
  else
  {
    if(addr == luna_bat.i2c_addr)
    {
      luna_bat.gagic_err ++;
      if(luna_bat.gagic_err < 20)
        MSG2("%s, status = %d, gagic_err = %d",__func__,status,luna_bat.gagic_err);
    }
    else
      MSG2("%s, status = %d",__func__,status);
    return -EIO;
  }
}



static int luna_bat_get_ac_property(struct power_supply *psy,
  enum power_supply_property psp,
  union power_supply_propval *val)
{
  int ret = 0;
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = luna_bat.ac_online;
      
      if(luna_bat.usb_online==1 &&
        (luna_bat.usb_current==USB_STATUS_USB_1000 || luna_bat.usb_current==USB_STATUS_USB_2000))
        val->intval = 1;
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
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_ONLINE:
      val->intval = luna_bat.usb_online;
      
      if(luna_bat.usb_online==1 &&
        (luna_bat.usb_current==USB_STATUS_USB_1000 || luna_bat.usb_current==USB_STATUS_USB_2000))
        val->intval = 0;
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
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  switch(psp)
  {
    case POWER_SUPPLY_PROP_STATUS:
      val->intval = luna_bat.bat_status;
      MSG("bat: status = %s", status_text[luna_bat.bat_status]);
      break;
    case POWER_SUPPLY_PROP_HEALTH:
      val->intval = luna_bat.bat_health;
      if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_COLD) 
        luna_bat.bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
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
    
    queue_work(luna_bat_wqueue, &luna_bat_work);
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
      else if((luna_bat.usb_online==1 && luna_bat.usb_current==USB_STATUS_USB_1000) ||
              (luna_bat.usb_online==1 && luna_bat.usb_current==USB_STATUS_USB_2000) )
        val = 2;
      else
        val = 0;
      MSG("bat: batt_type = %d", val);
  }
  return sprintf(buf, "%d\n", val);
}

static void luna_bat_pmu_test(unsigned char* bufLocal, size_t count)
{
  TPS6586xPmuSupply vdd = TPS6586xPmuSupply_Invalid;
  NvOdmServicesPmuVddRailCapabilities caps;
  NvU32 SettlingTime = 0;
  NvU32 MilliVolts, SpecialCtl;

  if(count < 4)
  {
    MSG2("Invalid parameters, count = %d", count);
    return;
  }

  if(bufLocal[1] == 'l' || bufLocal[1] == 'L')  
  {
    MSG2("LDO:%c",bufLocal[2]);
    vdd = bufLocal[2] - '0' + TPS6586xPmuSupply_LDO0;
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
      case '4':
        vdd = Ext_TPSGPIO4PmuSupply_LDO;  
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
      case 'l':
      case 'L':
        vdd = TPS6586xPmuSupply_LED_PWM;
        break;
    }
  }
  else if(bufLocal[1] == 'c' || bufLocal[1] == 'C')  
  {
    for(vdd=TPS6586xPmuSupply_LDO0; vdd<=TPS6586xPmuSupply_LDO9; vdd++)
    {
      NvOdmServicesPmuGetVoltage(luna_bat.hPmu, vdd, &MilliVolts);
      MSG2("LDO%d  = %d", vdd-TPS6586xPmuSupply_LDO0, MilliVolts);
    }
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, Ext_TPS74201PmuSupply_LDO, &MilliVolts);  
    MSG2("GPIO1 = %d", MilliVolts);
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, Ext_TPS72012PmuSupply_LDO, &MilliVolts);  
    MSG2("GPIO2 = %d", MilliVolts);
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, Ext_TPS62290PmuSupply_BUCK, &MilliVolts); 
    MSG2("GPIO3 = %d", MilliVolts);
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, Ext_TPSGPIO4PmuSupply_LDO, &MilliVolts);  
    MSG2("GPIO4 = %d", MilliVolts);
    return;
  }
  else if(bufLocal[1] == 'z' || bufLocal[1] == 'Z')  
  {
    vdd = TPS6586xPmuSupply_LDO0;
    MilliVolts = 0x5A5A5A5A;
    NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, MilliVolts, &SettlingTime);
    MSG2("Clear LDO Bypass Flags!");
    return;
  }
  else
  {
    MSG2("Invalid:%c", bufLocal[1]);
    return;
  }
  NvOdmServicesPmuGetCapabilities(luna_bat.hPmu, vdd, &caps);
  MSG2("vdd=%d, pro=%d, Min=%d, Step=%d, Max=%d, req=%d",
    vdd, caps.RmProtected, caps.MinMilliVolts, caps.StepMilliVolts, caps.MaxMilliVolts, caps.requestMilliVolts);

  if(system_rev >= EVT2 && vdd == TPS6586xPmuSupply_LDO0)
  {
    caps.requestMilliVolts = 2850;  
  }

  if((vdd >= TPS6586xPmuSupply_LDO0 && vdd <= TPS6586xPmuSupply_LDO9) ||      
    (vdd >= Ext_TPS62290PmuSupply_BUCK && vdd <= Ext_TPS74201PmuSupply_LDO) ||
    (vdd >= TPS6586xPmuSupply_RED1 && vdd <= TPS6586xPmuSupply_BLUE1))        
  {
    SpecialCtl = 0xA5000000;
  }
  else
  {
    SpecialCtl = 0;
  }

  switch(bufLocal[3])
  {
    case '0': 
      MSG2("OFF");
      MilliVolts = ODM_VOLTAGE_OFF | SpecialCtl;
      NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, MilliVolts, &SettlingTime);
      break;
    case '1': 
      MSG2("ON");
      if(bufLocal[1] == 'r' || bufLocal[1] == 'R')        
        MilliVolts = caps.MaxMilliVolts | SpecialCtl;
      else
        MilliVolts = caps.requestMilliVolts | SpecialCtl; 
      NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, MilliVolts, &SettlingTime);
      break;
    default:
      MSG2("Invalid On/Off");
      break;
  }
}


extern NvU32 luna_pwm1_bypass;
extern NvU32 luna_pwm1_set_dark;
extern NvU32 luna_pwm1_set_recover;
extern NvRmPwmMode pwm1_Mode_backup;
extern NvU32 pwm1_DutyCycle_backup;
void luna_bat_pwm1_power_up_sequence(int up)
{
  static NvOdmServicesPwmHandle hPwm = NULL;
  NvU32 DutyCycle = 84, RequestedFreq = 10000, ReturnedFreq = 0;
  MSG2("%s+",__func__);
  if(!hPwm)
  {
    hPwm = NvOdmPwmOpen();
    if(!hPwm)
    {
      MSG2("%s, get hPwm get fail!",__func__);
      goto exit;
    }
  }
  if(up == 0) 
  {
    luna_pwm1_bypass = NV_TRUE;     
    if(pwm1_Mode_backup == NvRmPwmMode_Enable)
    {
      luna_pwm1_set_dark = NV_TRUE;
      
      NvOdmPwmConfig(hPwm, NvOdmPwmOutputId_PWM1, NvOdmPwmMode_Enable,
        DutyCycle, &RequestedFreq, &ReturnedFreq);
      luna_pwm1_set_dark = NV_FALSE;
    }
    else
    {
      MSG2("%s, PWM1 was already off, bypass set dark!",__func__);
    }
  }
  else        
  {
    luna_pwm1_set_recover = NV_TRUE;
    
    NvOdmPwmConfig(hPwm, NvOdmPwmOutputId_PWM1, NvOdmPwmMode_Enable,
      DutyCycle, &RequestedFreq, &ReturnedFreq);
    luna_pwm1_set_recover = NV_FALSE;
    luna_pwm1_bypass = NV_FALSE;    
  }
exit:
  MSG2("%s-",__func__);
}
static void luna_bat_datacard_reset(void)
{
  TPS6586xPmuSupply vdd = TPS6586xPmuSupply_Invalid;
  NvOdmServicesPmuVddRailCapabilities caps;
  NvU32 SettlingTime = 0;
  NvU32 MilliVolts, SpecialCtl;

  printk(KERN_CRIT "[BAT]%s+\n",__func__);

  vdd = Ext_TPS72012PmuSupply_LDO;  
  NvOdmServicesPmuGetCapabilities(luna_bat.hPmu, vdd, &caps);
  MSG2("vdd=%d, pro=%d, Min=%d, Step=%d, Max=%d, req=%d",
    vdd, caps.RmProtected, caps.MinMilliVolts, caps.StepMilliVolts, caps.MaxMilliVolts, caps.requestMilliVolts);
  caps.requestMilliVolts = 2850;  
  SpecialCtl = 0xA5000000;

  wake_lock(&luna_bat.wlock_3g);

  luna_tmon_callback(0);
  luna_bat_pwm1_power_up_sequence(0);
  luna_datacard_power_up_sequence(0);
  luna_bodysar_callback(0);
  luna_capkey_callback(0);

  printk(KERN_CRIT "[BAT]%s, 3v3 LDO Off\n",__func__);
  MilliVolts = ODM_VOLTAGE_OFF | SpecialCtl;
  NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, MilliVolts, &SettlingTime);
  msleep(1000);
  MilliVolts = caps.requestMilliVolts | SpecialCtl; 
  NvOdmServicesPmuSetVoltage(luna_bat.hPmu, vdd, MilliVolts, &SettlingTime);
  printk(KERN_CRIT "[BAT]%s, 3v3 LDO On\n",__func__);
  msleep(10);
  luna_datacard_power_up_sequence(1);
  luna_bat_pwm1_power_up_sequence(1);
  luna_tmon_callback(1);
  luna_bodysar_callback(1);
  luna_capkey_callback(1);
  wake_lock_timeout(&luna_bat.wlock_3g, HZ*2);

  printk(KERN_CRIT "[BAT]%s-\n",__func__);
}




static void a2h(char *in, char *out) 
{
  int i;
  char a, h[2];
  for(i=0; i<2; i++)
  {
    a = *in++;
    if(a <= '9')        h[i] = a - '0';
    else if (a <= 'F')  h[i] = a - 'A' + 10;
    else if (a <= 'f')  h[i] = a - 'a' + 10;
    else                h[i] = 0;
  }
  *out = (h[0]<<4) + h[1];
}
static void h2a(char *in, char *out) 
{
  static const char my_ascii[] = "0123456789ABCDEF";
  char c = *in;
  *out++ =  my_ascii[c >> 4];
  *out =    my_ascii[c & 0xF];
}
static void pmu_i2c_test(unsigned char *bufLocal, int count)
{
  
  static NvOdmServicesI2cHandle hOdmI2C = NULL;
  static NvU32 DeviceAddr = (NvU32)NULL;
  
  NvOdmI2cStatus status;
  NvOdmI2cTransactionInfo info[2];
  int i, j;
  char id, reg[2], len, dat[LUNA_BAT_BUF_LENGTH/4];

  printk(KERN_INFO "\n");

  if(hOdmI2C == NULL)
  {
    hOdmI2C = NvOdmI2cOpen(NvOdmIoModule_I2c_Pmu,0x00);
    DeviceAddr = 0x68;
    MSG2("Get pmu i2c handle = 0x%X",(NvU32)hOdmI2C);
  }

  
  
  
  if(bufLocal[1]=='r' && count>=9)
  {
    a2h(&bufLocal[2], &id);     
    a2h(&bufLocal[4], &reg[0]); 
    a2h(&bufLocal[6], &len);    
    if(len >= sizeof(dat))
    {
      MSG2("R %02X:%02X(%02d) Fail: max length=%d", id,reg[0],len,sizeof(dat));
      return;
    }
    info[0].Address   = id<<1;
    info[0].Flags     = NVODM_I2C_IS_WRITE;
    info[0].Buf       = &reg[0];
    info[0].NumBytes  = 1;
    info[1].Address   = id<<1;
    info[1].Flags     = 0;
    info[1].Buf       = &dat[0];
    info[1].NumBytes  = len;
    status = NvOdmI2cTransaction(hOdmI2C, info, 2, 100, 1000);
    if(status == NvOdmI2cStatus_Success)
    {
      j = 0;
      for(i=0; i<len; i++)
      {
        h2a(&dat[i], &bufLocal[j]);
        bufLocal[j+2] = ' ';
        j = j + 3;
      }
      bufLocal[j] = '\0';
      MSG2("R %02X:%02X(%02d) = %s", id,reg[0],len,bufLocal);
    }
    else
    {
      MSG2("R %02X:%02X(%02d) Fail: status=%d", id,reg[0],len,status);
    }
  }
  
  
  
  else if(bufLocal[1]=='w' && count>=9)
  {
    a2h(&bufLocal[2], &id);     
    len = count - 5;
    if(len & 1)
    {
      MSG2("W %02X Fail (invalid data) len=%d", id,len);
      return;
    }
    len = len/2;
    if(len >= sizeof(dat))
    {
      MSG2("W %02X Fail (too many data)", id);
      return;
    }
    j = 4;
    for(i=0; i<len; i++)
    {
      a2h(&bufLocal[j], &dat[i]);
      j = j + 2;
    }
    info[0].Address   = id<<1;
    info[0].Flags     = NVODM_I2C_IS_WRITE;
    info[0].Buf       = &dat[0];
    info[0].NumBytes  = len;
    status = NvOdmI2cTransaction(hOdmI2C, info, 1, 100, 1000);
    if(status == NvOdmI2cStatus_Success)
    {
      MSG2("W %02X = Pass", id);
    }
    else
    {
      MSG2("W %02X = Fail: status=%d", id, status);
    }
  }
  else
  {
    MSG2("PWR_I2C: Pmu (7 bit id)");
    MSG2("rd: r40000B   (addr=40(7bit), reg=00, read count=11");
    MSG2("Rd: R2C010902 (addr=2C(7bit), reg=0109, read count=2");
    MSG2("wr: w40009265CA (addr=40(7bit), reg & data=00,92,65,CA...");
  }
}
static void gag_i2c_test(unsigned char *bufLocal, int count)
{
  int i2c_ret, i, j;
  char id, reg[2], len, dat[LUNA_BAT_BUF_LENGTH/4];

  printk(KERN_INFO "\n");
  
  
  
  if(bufLocal[1]=='r' && count>=9)
  {
    a2h(&bufLocal[2], &id);     
    a2h(&bufLocal[4], &reg[0]); 
    a2h(&bufLocal[6], &len);    
    if(len >= sizeof(dat))
    {
      MSG2("R %02X:%02X(%02d) Fail: max length=%d", id,reg[0],len,sizeof(dat));
      return;
    }
    i2c_ret = gag_read_i2c(id, reg[0], &dat[0], len);
    if(i2c_ret != 2)
    {
      MSG2("R %02X:%02X(%02d) Fail: ret=%d", id,reg[0],len,i2c_ret);
      return;
    }

    j = 0;
    for(i=0; i<len; i++)
    {
      h2a(&dat[i], &bufLocal[j]);
      bufLocal[j+2] = ' ';
      j = j + 3;
    }
    bufLocal[j] = '\0';
    MSG2("R %02X:%02X(%02d) = %s", id,reg[0],len,bufLocal);
  }
  
  
  
  else if(bufLocal[1]=='R' && count>=11)
  {
    
  }
  
  
  
  else if(bufLocal[1]=='w' && count>=9)
  {
    a2h(&bufLocal[2], &id);     
    len = count - 5;
    if(len & 1)
    {
      MSG2("W %02X Fail (invalid data) len=%d", id,len);
      return;
    }
    len = len/2;
    if(len >= sizeof(dat))
    {
      MSG2("W %02X Fail (too many data)", id);
      return;
    }
    j = 4;
    for(i=0; i<len; i++)
    {
      a2h(&bufLocal[j], &dat[i]);
      j = j + 2;
    }
    i2c_ret = gag_write_i2c(id, dat[0], &dat[1], len-1);
    MSG2("W %02X = %s", id, i2c_ret==1 ? "Pass":"Fail");
  }
  else
  {
    MSG2("GEN1_I2C: Audio, Gauge, Vib");
    MSG2("rd: r40000B   (addr=40(7bit), reg=00, read count=11");
    MSG2("Rd: R2C010902 (addr=2C(7bit), reg=0109, read count=2");
    MSG2("wr: w40009265CA (addr=40(7bit), reg & data=00,92,65,CA...");
  }
}

#define SOC_CHECK_A 200
#define SOC_CHECK_B 202
static void luna_bat_gauge_reset(void)
{
  static NvU8 reset[] = {0x54, 0x00};
  MSG2("%s",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0xFE, &reset[0], sizeof(reset));
  msleep(10);
}
static NvBool luna_bat_gauge_verify(void)
{
  static NvU8 w2_0E[] = {0xE8, 0x20};
  static NvU8 w2_0C[] = {0xFF, 0x00};
  NvU8 data2[2], data4[4], result, i;
  NvU8 data14[14];
  static NvU8 unlock[]= {0x4A, 0x57};
  static NvU8 lock[]  = {0x00, 0x00};

  MSG2("%s+",__func__);
  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data14[0], sizeof(data14));
  MSG2("%s, (02)%02X %02X %02X %02X (06)%02X %02X %02X %02X (0A)%02X %02X %02X %02X (0C)%02X %02X",__func__,
    data14[0], data14[1], data14[2], data14[3],
    data14[4], data14[5], data14[6], data14[7],
    data14[8], data14[9], data14[10], data14[11], data14[12], data14[13]);

  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  MSG("%s, read RCOMP (%02X %02X), OCV (%02X %02X)",__func__,data4[0],data4[1],data4[2],data4[3]);
  if((data4[1] & 0x1F) != 0x1F)
  {
    MSG2("%s-, ALERT not match ### FAIL ###",__func__);
    return NV_FALSE;
  }

  
  gag_read_i2c(luna_bat.i2c_addr, 0x00, &data2[0], sizeof(data2));

  
  MSG("%s, unlock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &unlock[0], sizeof(unlock));

  
  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  MSG("%s, backup RCOMP (%02X %02X), OCV (%02X %02X)",__func__,data4[0],data4[1],data4[2],data4[3]);

  
  MSG("%s, write TestOCV %02X %02X",__func__,w2_0E[0],w2_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w2_0E[0], sizeof(w2_0E));

  
  MSG("%s, write RCOMP Max %02X %02X",__func__,w2_0C[0],w2_0C[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &w2_0C[0], sizeof(w2_0C));
  msleep(150);

  
  for(i=0; i<2; i++)
  {
    msleep(150);
    gag_read_i2c(luna_bat.i2c_addr, 0x04, &data2[0], sizeof(data2));
    result = (data2[0] >= SOC_CHECK_A && data2[0] <= SOC_CHECK_B) ? NV_TRUE : NV_FALSE;
    MSG2("%s, TestSOC = %d %d ### %s ###",__func__,data2[0],data2[1],result ? "PASS":"FAIL");
    if(result == NV_TRUE)
      break;
  }

  
  MSG("%s, restore RCOMP, OCV",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  msleep(10);

  
  MSG("%s, lock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &lock[0], sizeof(lock));
  msleep(400);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data4[0], sizeof(data4));
  MSG2("%s, VCELL = %d (%02X %02X), SOC = %d (%02X %02X)",__func__,
    ((data4[0]<<4)+(data4[1]>>4))*5/4, data4[0], data4[1],
    data4[2]>>1, data4[2], data4[3]);

  if(result == NV_TRUE)
  {
    
    luna_bat.bat_capacity_history[2] = data4[2]>>1;
    luna_bat.bat_capacity_history[1] = data4[2]>>1;
    luna_bat.bat_capacity_history[0] = data4[2]>>1;
    MSG2("%s- ### PASS ###",__func__);
    return NV_TRUE;
  }
  else
  {
    MSG2("%s- ### FAIL ###",__func__);
    return NV_FALSE;
  }
}

static NvBool luna_bat_gauge_init(void)
{
  static NvU8 rcomp[] = {0x61, 0x1F}; 
  static NvU8 w2_0E[] = {0xE8, 0x20};
  static NvU8 w2_0C[] = {0xFF, 0x00};
  static NvU8 w3_40[] = {0x9D, 0x30, 0xAD, 0x10, 0xAD, 0x80, 0xAE, 0x20, 0xAE, 0xC0, 0xB2, 0x60, 0xB3, 0xF0, 0xB4, 0x60};
  static NvU8 w3_50[] = {0xB5, 0xA0, 0xB6, 0x90, 0xBD, 0x10, 0xBE, 0x80, 0xC9, 0xF0, 0xCB, 0xA0, 0xCF, 0x40, 0xDE, 0x20};
  static NvU8 w3_60[] = {0x00, 0x40, 0x62, 0x60, 0x26, 0x20, 0x2F, 0x60, 0x19, 0xC0, 0x0B, 0xA0, 0x98, 0x40, 0x43, 0xE0};
  static NvU8 w3_70[] = {0x0D, 0xA0, 0x0F, 0xE0, 0x39, 0x60, 0x0B, 0xA0, 0x24, 0x40, 0x0B, 0x80, 0x00, 0x60, 0x00, 0x60};
  static NvU8 w4_0E[] = {0xE8, 0x20};
  static NvU8 unlock[]= {0x4A, 0x57};
  static NvU8 lock[]  = {0x00, 0x00};
  NvU8 data2[2], data4[4], result, i;
  NvU8 data14[14];

  MSG2("%s+",__func__);
  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data14[0], sizeof(data14));
  MSG("%s, (02)%02X %02X %02X %02X (06)%02X %02X %02X %02X (0A)%02X %02X %02X %02X (0C)%02X %02X",__func__,
    data14[0], data14[1], data14[2], data14[3],
    data14[4], data14[5], data14[6], data14[7],
    data14[8], data14[9], data14[10], data14[11], data14[12], data14[13]);

  
  MSG("%s, write RCOMP %02X %02X",__func__,rcomp[0],rcomp[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &rcomp[0], sizeof(rcomp));
  msleep(10);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x00, &data2[0], sizeof(data2));

  
  MSG("%s, unlock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &unlock[0], sizeof(unlock));

  
  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  MSG("%s, backup RCOMP (%02X %02X), OCV (%02X %02X)",__func__,data4[0],data4[1],data4[2],data4[3]);

  
  MSG("%s, write TestOCV %02X %02X",__func__,w4_0E[0],w4_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w2_0E[0], sizeof(w2_0E));

  
  MSG("%s, write RCOMP Max %02X %02X",__func__,w2_0C[0],w2_0C[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &w2_0C[0], sizeof(w2_0C));

  
  MSG("%s, write Model",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x40, &w3_40[0], sizeof(w3_40));
  gag_write_i2c(luna_bat.i2c_addr, 0x50, &w3_50[0], sizeof(w3_50));
  gag_write_i2c(luna_bat.i2c_addr, 0x60, &w3_60[0], sizeof(w3_60));
  gag_write_i2c(luna_bat.i2c_addr, 0x70, &w3_70[0], sizeof(w3_70));
  msleep(190);

  
  MSG("%s, write TestOCV %02X %02X",__func__,w4_0E[0],w4_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w4_0E[0], sizeof(w4_0E));

  
  for(i=0; i<3; i++)
  {
    msleep(150);
    gag_read_i2c(luna_bat.i2c_addr, 0x04, &data2[0], sizeof(data2));
    result = (data2[0] >= SOC_CHECK_A && data2[0] <= SOC_CHECK_B) ? NV_TRUE : NV_FALSE;
    MSG2("%s, TestSOC = %d %d ## %s ##",__func__,data2[0],data2[1],result ? "PASS":"FAIL");
    if(result == NV_TRUE)
      break;
  }

  
  MSG("%s, restore RCOMP, OCV",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  mdelay(10);

  
  MSG("%s, lock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &lock[0], sizeof(lock));
  msleep(400);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data4[0], sizeof(data4));
  MSG2("%s, VCELL = %d (%02X %02X), SOC = %d (%02X %02X)",__func__,
    ((data4[0]<<4)+(data4[1]>>4))*5/4, data4[0], data4[1],
    data4[2]>>1, data4[2], data4[3]);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data14[0], sizeof(data14));
  MSG("%s, (02)%02X %02X %02X %02X (06)%02X %02X %02X %02X (0A)%02X %02X %02X %02X (0C)%02X %02X",__func__,
    data14[0], data14[1], data14[2], data14[3],
    data14[4], data14[5], data14[6], data14[7],
    data14[8], data14[9], data14[10], data14[11], data14[12], data14[13]);

  if(result == NV_TRUE)
  {
    
    luna_bat.bat_capacity_history[2] = data4[2]>>1;
    luna_bat.bat_capacity_history[1] = data4[2]>>1;
    luna_bat.bat_capacity_history[0] = data4[2]>>1;
    MSG2("%s- ### PASS ###",__func__);
    return NV_TRUE;
  }
  else
  {
    MSG2("%s- ### FAIL ###",__func__);
    return NV_FALSE;
  }
}
NvBool luna_bat_gauge_init_model2(void)
{
  static const NvU8 SOCCheckA = 221, SOCCheckB = 223;
  static NvU8 rcomp[] = {0x65, 0x1F}; 
  static NvU8 w2_0E[] = {0xD9, 0xD0};
  static NvU8 w2_0C[] = {0xFF, 0x00};
  static NvU8 w3_40[] = {0xA7, 0x50, 0xAB, 0xD0, 0xAF, 0x10, 0xB1, 0x60, 0xB3, 0x70, 0xB4, 0x00, 0xB5, 0x20, 0xB5, 0xF0};
  static NvU8 w3_50[] = {0xB7, 0xC0, 0xB8, 0x80, 0xBB, 0x20, 0xBE, 0x00, 0xC2, 0x10, 0xC5, 0xD0, 0xC9, 0xE0, 0xCF, 0xD0};
  static NvU8 w3_60[] = {0x0B, 0x10, 0x0F, 0x10, 0x1C, 0x20, 0x18, 0x60, 0x5F, 0x00, 0x2B, 0x10, 0x3E, 0xF0, 0x19, 0x60};
  static NvU8 w3_70[] = {0x3E, 0xF0, 0x10, 0xF0, 0x11, 0xC0, 0x10, 0xF0, 0x0F, 0x60, 0x0F, 0xE0, 0x09, 0x10, 0x09, 0x10};
  static NvU8 w4_0E[] = {0xD9, 0xD0};
  static NvU8 unlock[] = {0x4A, 0x57};
  static NvU8 lock[]   = {0x00, 0x00};
  NvU8 data2[2], data4[4], check;

  MSG2("%s+",__func__);

  
  MSG("%s, write RCOMP %02X %02X",__func__,rcomp[0],rcomp[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &rcomp[0], sizeof(rcomp));

  
  MSG("%s, unlock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &unlock[0], sizeof(unlock));

  
  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  MSG("%s, backup RCOMP (%02X %02X), OCV (%02X %02X)",__func__,data4[0],data4[1],data4[2],data4[3]);
 
  
  MSG("%s, write TestOCV %02X %02X",__func__,w2_0E[0],w2_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w2_0E[0], sizeof(w2_0E));

  
  MSG("%s, write RCOMP Max %02X %02X",__func__,w2_0C[0],w2_0C[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &w2_0C[0], sizeof(w2_0C));

  
  MSG("%s, write Model",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x40, &w3_40[0], sizeof(w3_40));
  gag_write_i2c(luna_bat.i2c_addr, 0x50, &w3_50[0], sizeof(w3_50));
  gag_write_i2c(luna_bat.i2c_addr, 0x60, &w3_60[0], sizeof(w3_60));
  gag_write_i2c(luna_bat.i2c_addr, 0x70, &w3_70[0], sizeof(w3_70));
  mdelay(190);

  
  MSG("%s, write TestOCV %02X %02X",__func__,w4_0E[0],w4_0E[1]);
  gag_write_i2c(luna_bat.i2c_addr, 0x0E, &w4_0E[0], sizeof(w4_0E));

  
  mdelay(160);
  gag_read_i2c(luna_bat.i2c_addr, 0x04, &data2[0], sizeof(data2));
  MSG2("read TestSOC = %d %d %s",data2[0],data2[1],
    (data2[0] >= SOCCheckA && data2[0] <= SOCCheckB) ? "PASS":"FAIL");
  check = data2[0];

  
  MSG("%s, restore RCOMP, OCV",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x0C, &data4[0], sizeof(data4));
  mdelay(10);

  
  MSG("%s, lock",__func__);
  gag_write_i2c(luna_bat.i2c_addr, 0x3E, &lock[0], sizeof(lock));
  mdelay(400);

  
  gag_read_i2c(luna_bat.i2c_addr, 0x02, &data4[0], sizeof(data4));
  MSG2("%s, VCELL = %d (%02X %02X), SOC = %d (%02X %02X)",__func__,
    ((data4[0]<<4)+(data4[1]>>4))*5/4, data4[0], data4[1],
    data4[2]>>1, data4[2], data4[3]);
 
  gag_read_i2c(luna_bat.i2c_addr, 0x0C, &data2[0], sizeof(data2));
  MSG2("GAG read RCOMP = %02X %02X",data2[0],data2[1]);

  if(check >= SOCCheckA && check <= SOCCheckB)
  {
    
    luna_bat.bat_capacity_history[2] = data4[2]>>1;
    luna_bat.bat_capacity_history[1] = data4[2]>>1;
    luna_bat.bat_capacity_history[0] = data4[2]>>1;
    MSG2("%s- ### PASS ###",__func__);
    return NV_TRUE;
  }
  else
  {
    MSG2("%s- ### FAIL ###",__func__);
    return NV_FALSE;
  }
}

static ssize_t luna_bat_ctrl_show(struct device *dev, struct device_attribute *attr, char *buf)
{
  NvU32 i, PinValue;
  NvU8  gag_08_0D[6];
  
  if(luna_bat.inited && time_after(jiffies, luna_bat.jiff_property_valid_time))
  {
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }

  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    if(luna_bat.pin[i].h_pin)
      NvOdmGpioGetState(luna_bat.hGpio, luna_bat.pin[i].h_pin, &PinValue);
    else
      PinValue = 2;
    luna_eng_data.PinValue[i] = PinValue;
  }

  
  luna_eng_data.cap   = luna_bat.bat_capacity;  
  luna_eng_data.volt  = luna_bat.bat_vol;       

  gag_read_i2c(luna_bat.i2c_addr, 0x08, &gag_08_0D[0], sizeof(gag_08_0D));
  luna_eng_data.ver   = (gag_08_0D[0]<<8) + gag_08_0D[1]; 
  luna_eng_data.rcomp = (gag_08_0D[4]<<8) + gag_08_0D[5]; 

  
  luna_eng_data.temp  = luna_bat.bat_temp;      
  luna_eng_data.chg_vol = luna_bat.chg_vol;     
  luna_eng_data.state.ac_det  = luna_bat.ac_pmu_det;
  luna_eng_data.state.usb_det = luna_bat.usb_pmu_det;
  
  luna_eng_data.state.ac      = luna_bat.ac_online;
  luna_eng_data.state.usb     = luna_bat.usb_online;
  
  if(luna_bat.usb_current == USB_STATUS_USB_0)
    luna_eng_data.state.usb_ma = 0;
  else if(luna_bat.usb_current == USB_STATUS_USB_100)
    luna_eng_data.state.usb_ma = 1;
  else if(luna_bat.usb_current == USB_STATUS_USB_500)
    luna_eng_data.state.usb_ma = 2;
  else if(luna_bat.usb_current == USB_STATUS_USB_1000 ||
          luna_bat.usb_current == USB_STATUS_USB_2000 )
  {
    if(luna_eng_data.PinValue[CHG_LIMD] == luna_bat.pin[CHG_LIMD].pin_en) 
      luna_eng_data.state.usb_ma = 4;
    else  
      luna_eng_data.state.usb_ma = 3;
  }
  else
    luna_eng_data.state.usb_ma = 7;  

  luna_eng_data.end = '\0';

  memcpy(buf,&luna_eng_data,sizeof(luna_eng_data));
  return sizeof(luna_eng_data);
}

static ssize_t luna_bat_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  NvU8 bufLocal[LUNA_BAT_BUF_LENGTH];

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
      MSG2("## Set chg_ctl mode = %c", bufLocal[1]);
      if(bufLocal[1]=='0')
      {
        
        luna_bat.chg_ctl = CHG_CTL_NONE;
        queue_work(luna_bat_wqueue, &luna_bat_work);
      }
      else if(bufLocal[1]=='1')
      {
        
        luna_bat.chg_ctl = CHG_CTL_USB500_DIS;
        queue_work(luna_bat_wqueue, &luna_bat_work);
      }
      else if(bufLocal[1]=='2')
      {
        luna_bat.chg_ctl = CHG_CTL_AC2A_EN;
        queue_work(luna_bat_wqueue, &luna_bat_work);
      }
      else
      {
        MSG2("chg_ctl mode = %d", luna_bat.chg_ctl);
      }
      break;

    
    
    case 'g':
      
      if(bufLocal[1]=='i')
      {
        
        luna_bat_gauge_init();
      }
      else if(bufLocal[1]=='v')
      {
        
        luna_bat_gauge_verify();
      }
      else if(bufLocal[1]=='r')
      {
        
        luna_bat_gauge_reset();
      }
      else if(bufLocal[1]=='2')
      {
        
        luna_bat_gauge_init_model2();
      }
      break;

    
    
    case 'p':
      luna_bat_pmu_test(bufLocal, count);
      break;

    case 'q':
      pmu_i2c_test(bufLocal, count);
      break;

    case 'r': 
      luna_bat_datacard_reset();
      break;

    
    
    case 'i':
      gag_i2c_test(bufLocal, count);
      break;

    case 'u':
      if(bufLocal[1]=='0')
      {
        MSG2("USB 0");
        luna_bat.usb_current = USB_STATUS_USB_0;
        luna_bat.usb_online  = 0;
      }
      else 
      {
        MSG2("USB 2000");
        luna_bat.usb_current = USB_STATUS_USB_2000;
        luna_bat.usb_online  = 1;
      }
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
    luna_bat.usb_current = USB_STATUS_USB_0;    MSG3("Set [USB 0]");
    luna_bat.usb_online  = 0;
  }
  else if(flag & USB_STATUS_USB_100)
  {
    luna_bat.usb_current = USB_STATUS_USB_100;  MSG3("Set [USB 100]");
    luna_bat.usb_online  = 1;
  }
  else if(flag & USB_STATUS_USB_500)
  {
    luna_bat.usb_current = USB_STATUS_USB_500;  MSG3("Set [USB 500]");
    luna_bat.usb_online  = 1;
  }
  else if(flag & USB_STATUS_USB_1000)
  {
    luna_bat.usb_current = USB_STATUS_USB_1000; MSG3("Set [USB 1000]");
    luna_bat.usb_online  = 1;
  }
  else if(flag & USB_STATUS_USB_2000)
  {
    luna_bat.usb_current = USB_STATUS_USB_2000; MSG3("Set [USB 2000]");
    luna_bat.usb_online  = 1;
  }

  if(luna_bat.inited)
  {
    
    atomic_set(&luna_bat.read_again, 3);
    queue_work(luna_bat_wqueue, &luna_bat_work);
    
  }
}
EXPORT_SYMBOL(luna_bat_update_usb_status);
void luna_bat_get_info(struct luna_bat_info_data* binfo)
{
  binfo->bat_status   = luna_bat.bat_status;
  binfo->bat_health   = luna_bat.bat_health;
  binfo->bat_capacity = luna_bat.bat_capacity;
  binfo->bat_vol      = luna_bat.bat_vol;
  binfo->bat_temp     = luna_bat.bat_temp;

  
  binfo->ac_online    = luna_bat.ac_online;
  binfo->usb_online   = luna_bat.usb_online;
  binfo->usb_current  = luna_bat.usb_current;    

  
  binfo->ac_pmu_det   = luna_bat.ac_pmu_det;
  binfo->usb_pmu_det  = luna_bat.usb_pmu_det;
}
EXPORT_SYMBOL(luna_bat_get_info);
int luna_bat_get_online(void)
{
  return ((luna_bat.ac_pmu_det << 8) | luna_bat.usb_pmu_det);
}
EXPORT_SYMBOL(luna_bat_get_online);

static int luna_bat_soc_filter(int input)
{
  static const int middle[] = {1,0,2,0,0,2,0,1};
  int old[3], index = 0;
  luna_bat.bat_capacity_history[2] = luna_bat.bat_capacity_history[1];
  luna_bat.bat_capacity_history[1] = luna_bat.bat_capacity_history[0];
  luna_bat.bat_capacity_history[0] = input;
  old[2] = luna_bat.bat_capacity_history[2];
  old[1] = luna_bat.bat_capacity_history[1];
  old[0] = luna_bat.bat_capacity_history[0];
  if( old[0] > old[1] ) index += 4;
  if( old[1] > old[2] ) index += 2;
  if( old[0] > old[2] ) index ++;
  if(old[middle[index]] > 100)
    return 100;
  else
    return old[middle[index]];
}
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
static NvU32 luna_bat_voltage_to_soc(NvU32 volt)
{
  int soc;
  if(volt > 4170)
    soc = 100;
  else if(volt > 4100)
    soc = 90;
  else if(volt > 4000)
    soc = 80;
  else if(volt > 3900)
    soc = 70;
  else if(volt > 3800)
    soc = 60;
  else if(volt > 3700)
    soc = 40;
  else if(volt > 3600)
    soc = 16;
  else if(volt > 3500)
    soc = 5;
  else
    soc = 1;
  return soc;
}
static NvS32 luna_bat_adc_to_temp(NvU32 adc)
{
  struct {
    NvS32 temp;
    NvS32 volt;
  } static const mapping_table [] = {
    { -400, 2093},  { -350, 2060},  { -300, 2021},  { -250, 1974},  { -200, 1918},
    { -150, 1854},  { -100, 1781},  {  -50, 1698},  {    0, 1608},  {   50, 1512},
    {  100, 1412},  {  150, 1308},  {  200, 1203},  {  250, 1100},  {  300,  998},
    {  350,  901},  {  400,  810},  {  450,  725},  {  500,  646},  {  550,  574},
    {  600,  509},  {  650,  452},  {  700,  400},  {  750,  355},  {  800,  314},
    {  850,  278},  {  900,  247},  {  950,  219},  { 1000,  195},  { 1050,  173},
    { 1100,  155},  { 1150,  138},  { 1200,  123},  { 1250,  110},  { 1250,    0},
  };
  NvS32 i, tmp;
  for(i=0; i<ARRAY_SIZE(mapping_table); i++)
  {
    if(adc >= mapping_table[i].volt)
    {
      if(i == 0)
      {
        return mapping_table[0].temp;
      }
      else
      {
        tmp = (adc - mapping_table[i].volt) * 1024 / (mapping_table[i-1].volt - mapping_table[i].volt);
        return (mapping_table[i-1].temp - mapping_table[i].temp) * tmp / 1024 + mapping_table[i].temp;
      }
    }
  }
  return 1250;  
}
static NvU32 luna_bat_get_chg_voltage(void)
{
  if(system_rev == EVT2)
  {
    NvU32 ADC_9, DCinVolt;
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, TPS6586xPmuSupply_ADC_9, &ADC_9);
    DCinVolt = ADC_9 * 5547 / 256;  
    return DCinVolt;
  }
  else if(system_rev >= EVT2_2)
  {
    NvU32 ADC_1, DCinVolt;
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, TPS6586xPmuSupply_ANLG_1, &ADC_1);
    DCinVolt = ADC_1 * 2600 / 256;  
    return DCinVolt;
  }
  return 9999;  
}




static void luna_bat_work_func(struct work_struct *work)
{
  static char ac_online_old = 0;
  static char usb_online_old = 0;
  
  static int usb_current_old = USB_STATUS_USB_0;
  static int bat_status_old = 0;
  static int bat_health_old = 0;
  static int bat_soc_old = 255;
  static int chg_dok_old = 0;

  int i,status_changed = 0;

  NvU32 MilliVolts, volt, soc;

  
  NvU32 PinValue[CHG_MAX];

  NvOdmServicesPmuBatteryData pmu_data;

  

  if(!luna_bat.inited) 
  {
    MSG2("## Cancel Work, driver not inited! ##");
    return;
  }

  

  
  
  
  {
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, TPS6586xPmuSupply_ANLG_1, &MilliVolts);  
    
    volt = luna_bat_voltage_filter(MilliVolts * 2600 / 1024 * 2);
    soc  = luna_bat_voltage_to_soc(volt);
    luna_bat.bat_capacity = soc;
    luna_bat.bat_vol      = volt;
    if(NvOdmServicesPmuGetBatteryData(luna_bat.hPmu, NvRmPmuBatteryInst_Main, &pmu_data)) 
      luna_bat.bat_temp = luna_bat_adc_to_temp(pmu_data.batteryTemperature);
    else
      luna_bat.bat_temp = 250;
  }

  
  
  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    if(luna_bat.pin[i].h_pin)
      NvOdmGpioGetState(luna_bat.hGpio, luna_bat.pin[i].h_pin, &PinValue[i]);
  }
  
  
  
  
  
  
  MSG3("[%s %s %s %s %s %s] volt = %d, soc = %d, temp = %d",
    PinValue[CHG_CEN]  == luna_bat.pin[CHG_CEN].pin_en  ?"ON ":"OFF" ,
    PinValue[CHG_DCM]  == luna_bat.pin[CHG_DCM].pin_en  ?"DC     " :
    PinValue[CHG_IUSB] == luna_bat.pin[CHG_IUSB].pin_en ?"USB 500":"USB 100" ,
    PinValue[CHG_USUS] == luna_bat.pin[CHG_USUS].pin_en ?"SUS":"   " ,
    PinValue[CHG_CHG]  == luna_bat.pin[CHG_CHG].pin_en  ?"CHG":"   " ,
    PinValue[CHG_DOK]  == luna_bat.pin[CHG_DOK].pin_en  ?"DOK":"   " ,
    PinValue[CHG_FLT]  == luna_bat.pin[CHG_FLT].pin_en  ?"FLT":"   " ,
    luna_bat.bat_vol, luna_bat.bat_capacity, luna_bat.bat_temp);

  
  
  
  
  luna_bat.bat_present = 1;

  
  
  
  if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en)
  {
    if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en &&
      PinValue[CHG_CHG] != luna_bat.pin[CHG_CHG].pin_en &&
      luna_bat.bat_vol > 4100)
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_FULL;
    }
    else if(PinValue[CHG_CHG] == luna_bat.pin[CHG_CHG].pin_en)
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_CHARGING;
      if(soc == 100)
      {
        luna_bat.bat_capacity = 99;
        soc = 99;
      }
    }
    else
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
    }
  }
  else
  {
    luna_bat.bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
  }

  
  
  
  
  
  
    luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;

  
  
  
  {
    if(PinValue[CHG_DOK] != luna_bat.pin[CHG_DOK].pin_en) 
    {
      if(luna_bat.chg_stat != CHG_STAT_IDLE)
        MSG3("## IDLE ##");
      luna_bat.chg_stat = CHG_STAT_IDLE;
    }
    else  
    {
      switch(luna_bat.chg_stat)
      {
        case CHG_STAT_IDLE:
          if(luna_bat.usb_online)
          {
            luna_bat.chg_stat = CHG_STAT_USB;
            MSG3("## USB ##");
          }
          else
          {
            luna_bat.chg_stat = CHG_STAT_WAIT;
            luna_bat.jiff_ac_online_debounce_time = jiffies + HZ*3/2; 
            MSG3("## WAIT ##");
          }
          break;
        case CHG_STAT_WAIT:
          if(luna_bat.usb_online)
          {
            luna_bat.chg_stat = CHG_STAT_USB;
            MSG3("## USB ##");
          }
          else if(time_after(jiffies, luna_bat.jiff_ac_online_debounce_time))
          {
            luna_bat.chg_stat = CHG_STAT_AC;
            MSG3("## AC ##");
          }
          break;
        case CHG_STAT_USB:
          if(luna_bat.usb_online)
          {
            
          }
          else
          {
            luna_bat.chg_stat = CHG_STAT_WAIT;
            luna_bat.jiff_ac_online_debounce_time = jiffies + HZ*3/2; 
            MSG3("## WAIT ##");
          }
          break;
        case CHG_STAT_AC:
          if(luna_bat.usb_online)
          {
            luna_bat.chg_stat = CHG_STAT_USB_AC;
            MSG3("## USB_AC ##");
          }
          else
          {
            
          }
          break;
        case CHG_STAT_USB_AC:
          if(luna_bat.usb_online)
          {
            
          }
          else
          {
            luna_bat.chg_stat = CHG_STAT_WAIT;
            luna_bat.jiff_ac_online_debounce_time = jiffies + HZ*3/2; 
            MSG3("## WAIT ##");
          }
          break;
      }
    }

    
    if(luna_bat.chg_stat == CHG_STAT_AC || luna_bat.chg_stat == CHG_STAT_USB_AC)
      luna_bat.ac_online = 1;
    else
      luna_bat.ac_online = 0;
    chg_dok_old = PinValue[CHG_DOK];
  }

  
  
  
  if(ac_online_old != luna_bat.ac_online)
  {
    MSG3("## ac_online: %d -> %d",ac_online_old,luna_bat.ac_online);
    ac_online_old = luna_bat.ac_online;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(usb_online_old != luna_bat.usb_online)
  {
    MSG3("## usb_online: %d -> %d",usb_online_old,luna_bat.usb_online);
    usb_online_old = luna_bat.usb_online;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(usb_current_old != luna_bat.usb_current)
  {
    MSG2("## usb_current: %d -> %d",
      usb_current_old==USB_STATUS_USB_0? 0:
      usb_current_old==USB_STATUS_USB_100? 100:
      usb_current_old==USB_STATUS_USB_500? 500:
      usb_current_old==USB_STATUS_USB_1000? 1000:
      usb_current_old==USB_STATUS_USB_2000? 2000: 9999  ,
      luna_bat.usb_current==USB_STATUS_USB_0? 0:
      luna_bat.usb_current==USB_STATUS_USB_100? 100:
      luna_bat.usb_current==USB_STATUS_USB_500? 500:
      luna_bat.usb_current==USB_STATUS_USB_1000? 1000:
      luna_bat.usb_current==USB_STATUS_USB_2000? 2000: 9999
      );
    usb_current_old = luna_bat.usb_current;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_status_old != luna_bat.bat_status)
  {
    MSG2("## bat_status: %s -> %s",status_text[bat_status_old],status_text[luna_bat.bat_status]);
    bat_status_old = luna_bat.bat_status;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_health_old != luna_bat.bat_health)
  {
    MSG2("## bat_health: %s -> %s",health_text[bat_health_old],health_text[luna_bat.bat_health]);
    bat_health_old = luna_bat.bat_health;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_soc_old != soc)
  {
    MSG2("## bat_capacity: %d, vol: %d", soc, volt);
    bat_soc_old = soc;
    status_changed ++;
    
  }
  if(soc == 1)  
  {
    if(luna_bat.bat_low_count > 10)
    {
      if(luna_bat.bat_low_count == 11)
      {
        MSG2("## bat_capacity: 0, vol: %d (count = 11)", volt);
        status_changed ++;
      }
      luna_bat.bat_low_count ++;
      luna_bat.bat_capacity = 0;
    }
    else
    {
      MSG2("## bat_capacity: 1, vol: %d (count = %d)", volt, luna_bat.bat_low_count);
      luna_bat.bat_low_count ++;
      atomic_set(&luna_bat.read_again, 3);
    }
  }
  else
  {
    luna_bat.bat_low_count = 0;
  }
  
  if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en &&
    luna_bat.pin[CHG_CHG].intr_mask == NV_TRUE)
  {
    MSG3("## CHG_INTR ON");
    luna_bat.pin[CHG_CHG].intr_mask = NV_FALSE;
    NvOdmGpioInterruptMask(luna_bat.pin[CHG_CHG].h_intr, NV_FALSE);
  }
  else if(PinValue[CHG_CEN] == !luna_bat.pin[CHG_CEN].pin_en &&
    luna_bat.pin[CHG_CHG].intr_mask == NV_FALSE)
  {
    MSG3("## CHG_INTR OFF");
    luna_bat.pin[CHG_CHG].intr_mask = NV_TRUE;
    NvOdmGpioInterruptMask(luna_bat.pin[CHG_CHG].h_intr, NV_TRUE);
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
  

  
  
  {
    
    
    if(PinValue[CHG_USUS]==luna_bat.pin[CHG_USUS].pin_en)
    {
      MSG3("## Disable suspend");
      NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_USUS].h_pin, !luna_bat.pin[CHG_USUS].pin_en); 
    }

    
    
    if(luna_bat.chg_ctl == CHG_CTL_NONE)
    {
      
      
      if(luna_bat.chg_stat == CHG_STAT_IDLE || luna_bat.chg_stat == CHG_STAT_WAIT)
      {
          if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
          {
            MSG3("## IDLE or WAIT + Disable charging");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
      }
      
      
      else if(luna_bat.chg_stat == CHG_STAT_AC)
      {
        if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_GOOD)
        {
          if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en || 
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
          {
            MSG3("## AC + Enable charging");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
        }
        else
        {
          if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
          {
            MSG3("## AC + Disable charging");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
        }
      }
      else if(luna_bat.chg_stat == CHG_STAT_USB || luna_bat.chg_stat == CHG_STAT_USB_AC)
      
      
      {
        switch(luna_bat.usb_current)
        {
          case USB_STATUS_USB_1000:
          case USB_STATUS_USB_2000:
            if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_GOOD)
            {
              if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en || 
                 PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
              {
                MSG3("## USB HC + Enable charging");
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
              }
            }
            else
            {
              if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||
                 PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
              {
                MSG3("## USB HC + Disable charging");
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
              }
            }
            break;
          case USB_STATUS_USB_500:
            
            if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
               PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
               PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
            {
              MSG3("## USB 500 + Disable charging");
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, luna_bat.pin[CHG_IUSB].pin_en);
            }
            
            
            break;
          case USB_STATUS_USB_0:  
          case USB_STATUS_USB_100:
          default:
            
            if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
               PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
               PinValue[CHG_IUSB] != !luna_bat.pin[CHG_IUSB].pin_en)  
            {
              MSG3("## USB 100 + Disable charging");
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, !luna_bat.pin[CHG_IUSB].pin_en);
            }
            
            
            break;
        }
      }
    }
    
    
    else
    {
      
      
      switch(luna_bat.chg_ctl)
      {
        case CHG_CTL_USB500_DIS:
          if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
             PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
             PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
          {
            MSG2("## CTRL: USB_500 + DIS");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, luna_bat.pin[CHG_IUSB].pin_en);
          }
          break;
        case CHG_CTL_USB500_EN:
          if(PinValue[CHG_CEN]  != luna_bat.pin[CHG_CEN].pin_en  || 
             PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
             PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
          {
            MSG2("## CTRL: USB_500 + EN");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, luna_bat.pin[CHG_IUSB].pin_en);
          }
          break;
        case CHG_CTL_AC2A_DIS:
          if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||  
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)     
          {
            MSG2("## CTRL: AC + DIS");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
          break;
        case CHG_CTL_AC2A_EN:
          if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en ||   
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)     
          {
            MSG2("## CTRL: AC + EN");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
          break;
      }
    }
  }



  luna_bat.jiff_property_valid_time = jiffies + luna_bat.jiff_property_valid_interval;

  if(status_changed ||
    atomic_read(&luna_bat.read_again) == 1)
  {
    
    
    power_supply_changed(&luna_bat.psy_bat);
  }


  
  
  if(luna_bat.suspend_flag)
  {
    
    del_timer_sync(&luna_timer);
  }
  else
  {
    if(atomic_read(&luna_bat.read_again) > 0)
    {
      atomic_dec(&luna_bat.read_again);
      mod_timer(&luna_timer, jiffies + 1*HZ);
    }

    else
    {
      mod_timer(&luna_timer, jiffies + luna_bat.jiff_polling_interval);
    }
  }

  
  
}



extern int tp_cable_state(uint32_t send_cable_flag);
static void luna_bat_work_func_evt2(struct work_struct *work)
{
  static char ac_online_old = 0;
  static char usb_online_old = 0;
  static int usb_current_old = USB_STATUS_USB_0;
  static int bat_status_old = 0;
  static int bat_health_old = 0;
  static int bat_soc_old = 255;
  static int bat_aidl_done = 0;
  static int touch_online_old = 2;
  

  int i,status_changed = 0, online_temp;

  NvU32 MilliVolts, volt = 3800, soc = 50;
  NvU32 chg_in_old;

  
  NvU32 PinValue[CHG_MAX];

  NvOdmServicesPmuBatteryData pmu_data;

  NvU8  gag_02_05[4], gag_0c_0d[2];
  NvS32 rcomp = 0xFFFF;
  static NvS32 gag_init_retry = 3;

  

  if(!luna_bat.inited) 
  {
    MSG2("## Cancel Work, driver not inited! ##");
    return;
  }

  
  
  while(gag_init_retry > 0)
  {
    if(gag_init_retry == 3)
    {
      if(luna_bat_gauge_verify() == NV_TRUE)
      {
        MSG2("%s Gauge verify PASS!",__func__);
        gag_init_retry = 0;
        break;
      }
      else
      {
        
        
      }
    }
    if(luna_bat_gauge_init() == NV_TRUE)
      gag_init_retry = 0;
    else
      gag_init_retry --;
  }

  

  
  
  
  {
    
    
    
    
    
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, TPS6586xPmuSupply_UsbDet, &MilliVolts);
    luna_bat.usb_pmu_det = MilliVolts;
    NvOdmServicesPmuGetVoltage(luna_bat.hPmu, TPS6586xPmuSupply_AcDet, &MilliVolts);
    luna_bat.ac_pmu_det = MilliVolts;
    
    online_temp = (luna_bat.ac_pmu_det | luna_bat.usb_pmu_det)? 1:0;
    if(touch_online_old != online_temp)
    {
      touch_online_old = online_temp;
      tp_cable_state(((luna_bat.ac_pmu_det << 8) | luna_bat.usb_pmu_det));
    }

    
    
    if(NvOdmServicesPmuGetBatteryData(luna_bat.hPmu, NvRmPmuBatteryInst_Main, &pmu_data)) 
      luna_bat.bat_temp = luna_bat_adc_to_temp(pmu_data.batteryTemperature);
    else
      luna_bat.bat_temp = 250;
    
    
    luna_bat.chg_vol = luna_bat_get_chg_voltage();
    
    
    gag_read_i2c(luna_bat.i2c_addr, 0x02, &gag_02_05[0], sizeof(gag_02_05));
    gag_read_i2c(luna_bat.i2c_addr, 0x0C, &gag_0c_0d[0], sizeof(gag_0c_0d));
    if(!luna_bat.gagic_err)
    {
      volt  = ((gag_02_05[0]<<4) + (gag_02_05[1]>>4))*5/4;
      if(gag_02_05[2] == 1)     
        soc = 1;
      else
        soc = gag_02_05[2]>>1;  
      luna_bat.bat_vol = volt;
      luna_bat.bat_capacity = luna_bat_soc_filter(soc);
      
      if(luna_bat.bat_temp >= 200)
      {
        rcomp = 97 - (luna_bat.bat_temp - 200) * 21 / 200;  
      }
      else
      {
        rcomp = 97 - (luna_bat.bat_temp - 200) * 79 / 100;  
      }
      if(rcomp > 0xFF)
        rcomp = 0xFF;
      else if(rcomp < 0)
        rcomp = 0x00;
      if(rcomp != gag_0c_0d[0])
      {
        MSG("RCOMP: %02X->%02X (t=%d)",gag_0c_0d[0],rcomp,luna_bat.bat_temp);
        gag_0c_0d[0] = rcomp;
        gag_write_i2c(luna_bat.i2c_addr, 0x0C, &gag_0c_0d[0], sizeof(gag_0c_0d));
      }
      if(TST_BIT(gag_0c_0d[1],5) && (online_temp || soc > 3)) 
      {
        if(luna_bat.pin[CHG_GLOW].h_pin)
          NvOdmGpioGetState(luna_bat.hGpio, luna_bat.pin[CHG_GLOW].h_pin, &PinValue[CHG_GLOW]);
        else
          PinValue[CHG_GLOW] = 2;
        MSG2("RCOMP: %02X->%02X (t=%d), GLOW=%d, CLEAR ALERT %02X",
          gag_0c_0d[0],rcomp,luna_bat.bat_temp,PinValue[CHG_GLOW],gag_0c_0d[1]);
        CLR_BIT(gag_0c_0d[1],5);  
        gag_0c_0d[0] = rcomp;
        gag_write_i2c(luna_bat.i2c_addr, 0x0C, &gag_0c_0d[0], sizeof(gag_0c_0d));
      }
    }
  }

  
  
  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    if(luna_bat.pin[i].h_pin)
      NvOdmGpioGetState(luna_bat.hGpio, luna_bat.pin[i].h_pin, &PinValue[i]);
  }
  MSG3("[%s %s %s %s %s %s %s %s] volt=%04d, soc=%03d, temp=%03d, rc=%02X chg=%04d, acd=%d, usbd=%d (%02X%02X)",
    PinValue[CHG_CEN]  == luna_bat.pin[CHG_CEN].pin_en  ?"ON ":"OFF" ,
    PinValue[CHG_DCM]  == luna_bat.pin[CHG_DCM].pin_en  ?
    (PinValue[CHG_LIMD] == luna_bat.pin[CHG_LIMD].pin_en ?"DC_2A__" : "DC_1A__") :
    PinValue[CHG_IUSB] == luna_bat.pin[CHG_IUSB].pin_en ?"USB_500":"USB_100" ,
    PinValue[CHG_LIMB] == luna_bat.pin[CHG_LIMB].pin_en ?"BAT_2A_":"BAT_500" ,
    PinValue[CHG_OTG]  == luna_bat.pin[CHG_OTG].pin_en  ?"OTG":"___" ,
    PinValue[CHG_CHG]  == luna_bat.pin[CHG_CHG].pin_en  ?"CHG":"___" ,
    PinValue[CHG_DOK]  == luna_bat.pin[CHG_DOK].pin_en  ?"DOK":"___" ,
    PinValue[CHG_FLT]  == luna_bat.pin[CHG_FLT].pin_en  ?"FLT":"___" ,
    PinValue[CHG_GLOW] == luna_bat.pin[CHG_GLOW].pin_en ?"LOW":"___" ,
    luna_bat.bat_vol, soc, luna_bat.bat_temp, rcomp, luna_bat.chg_vol, luna_bat.ac_pmu_det, luna_bat.usb_pmu_det,
    gag_02_05[2],gag_02_05[3]);

  
  
  
  
  luna_bat.bat_present = 1;

  
  
  
  if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en)
  {
    if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en && 
      PinValue[CHG_CHG] != luna_bat.pin[CHG_CHG].pin_en &&  
      luna_bat.bat_vol > 4100)  
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_FULL;
    }
    else if(PinValue[CHG_CHG] == luna_bat.pin[CHG_CHG].pin_en)
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_CHARGING;
    }
    else
    {
      luna_bat.bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
    }
  }
  else
  {
    luna_bat.bat_status = POWER_SUPPLY_STATUS_DISCHARGING;
  }

  
  
  
  if(luna_bat.ac_pmu_det || luna_bat.usb_pmu_det) 
  {
    if(bat_health_old == POWER_SUPPLY_HEALTH_OVERHEAT &&  
      luna_bat.bat_temp > 450)
    {
      luna_bat.bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
    }
    else if(bat_health_old == POWER_SUPPLY_HEALTH_COLD && 
      luna_bat.bat_temp < 50)
    {
      luna_bat.bat_health = POWER_SUPPLY_HEALTH_COLD;
    }
    else
    {
      if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en) 
      {
        if(PinValue[CHG_CHG] == luna_bat.pin[CHG_CHG].pin_en) 
        {
          luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
        }
        else  
        {
          if(PinValue[CHG_FLT] == luna_bat.pin[CHG_FLT].pin_en) 
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_DEAD;
          }
          
          else if(luna_bat.bat_temp > 450)  
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_OVERHEAT;
          }
          else if(luna_bat.bat_temp < 50)   
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_COLD;
          }
          else  
          {
            luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
          }
        }
      }
      else  
      {
        luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
      }
    }
  }
  else  
  {
    luna_bat.bat_health = POWER_SUPPLY_HEALTH_GOOD;
  }

  
  
  
  {
    chg_in_old = luna_bat.chg_in;
    switch(luna_bat.chg_in)
    {
      case CHG_IN_NONE:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          luna_bat.chg_in = CHG_IN_DOK_DET;
        }
        else  
        {
          if(luna_bat.ac_pmu_det || luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_ERROR;
          }
        }
        break;
      case CHG_IN_ERROR:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          luna_bat.chg_in = CHG_IN_DOK_DET;
        }
        else  
        {
          if(!luna_bat.ac_pmu_det && !luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_NONE;
          }
        }
        break;
      case CHG_IN_DOK_DET:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          if(luna_bat.ac_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_AC_DET;
          }
          else if(luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_USB_DET;
          }
        }
        else  
        {
          luna_bat.chg_in = CHG_IN_NONE;
        }
        break;
      case CHG_IN_AC_DET:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          if(!luna_bat.ac_pmu_det && !luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_DOK_DET;
          }
          else if(!luna_bat.ac_pmu_det && luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_USB_DET;
          }
        }
        else  
        {
          luna_bat.chg_in = CHG_IN_NONE;
        }
        break;
      case CHG_IN_USB_DET:
        if(PinValue[CHG_DOK] == luna_bat.pin[CHG_DOK].pin_en) 
        {
          if(luna_bat.ac_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_AC_DET;
          }
          else if(!luna_bat.ac_pmu_det && !luna_bat.usb_pmu_det)
          {
            luna_bat.chg_in = CHG_IN_DOK_DET;
          }
        }
        else  
        {
          luna_bat.chg_in = CHG_IN_NONE;
        }
        break;
    }
    
    if(luna_bat.chg_in == CHG_IN_AC_DET)
      luna_bat.ac_online = 1;
    else
      luna_bat.ac_online = 0;
    
    if(chg_in_old != luna_bat.chg_in)
    {
      MSG3("## %s ##", chg_in_name[luna_bat.chg_in]);
    }
  }

  
  
  
  if(ac_online_old != luna_bat.ac_online)
  {
    MSG2("## ac_online: %d -> %d (%d)",ac_online_old,luna_bat.ac_online,luna_bat.ac_pmu_det);
    ac_online_old = luna_bat.ac_online;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(usb_online_old != luna_bat.usb_online)
  {
    MSG2("## usb_online: %d -> %d (%d)",usb_online_old,luna_bat.usb_online,luna_bat.usb_pmu_det);
    usb_online_old = luna_bat.usb_online;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(usb_current_old != luna_bat.usb_current)
  {
    MSG2("## usb_current: %d -> %d",
      usb_current_old==USB_STATUS_USB_0? 0:
      usb_current_old==USB_STATUS_USB_100? 100:
      usb_current_old==USB_STATUS_USB_500? 500:
      usb_current_old==USB_STATUS_USB_1000? 1000:
      usb_current_old==USB_STATUS_USB_2000? 2000: 9999  ,
      luna_bat.usb_current==USB_STATUS_USB_0? 0:
      luna_bat.usb_current==USB_STATUS_USB_100? 100:
      luna_bat.usb_current==USB_STATUS_USB_500? 500:
      luna_bat.usb_current==USB_STATUS_USB_1000? 1000:
      luna_bat.usb_current==USB_STATUS_USB_2000? 2000: 9999
      );
    usb_current_old = luna_bat.usb_current;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_status_old != luna_bat.bat_status)
  {
    MSG2("## bat_status: %s -> %s",status_text[bat_status_old],status_text[luna_bat.bat_status]);
    bat_status_old = luna_bat.bat_status;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_health_old != luna_bat.bat_health)
  {
    MSG2("## bat_health: %s -> %s",health_text[bat_health_old],health_text[luna_bat.bat_health]);
    bat_health_old = luna_bat.bat_health;
    status_changed ++;
    atomic_set(&luna_bat.read_again, 3);
  }
  if(bat_soc_old !=  soc)
  {
    MSG2("## bat_capacity: %d(%d), vol: %d, temp: %d, (rc=%02X gag=%02X%02X)",
      luna_bat.bat_capacity, soc, luna_bat.bat_vol, luna_bat.bat_temp, rcomp, gag_02_05[2],gag_02_05[3]);
    bat_soc_old = soc;
    status_changed ++;
    
  }

  
  if(luna_bat.bat_status == POWER_SUPPLY_STATUS_FULL)
  {
    luna_bat.bat_capacity = 100;
  }
  else if(luna_bat.bat_capacity >= 100)
  {
    if(luna_bat.bat_status == POWER_SUPPLY_STATUS_CHARGING)
      luna_bat.bat_capacity = 99;
    else
      luna_bat.bat_capacity = 100;
  }
  else if(luna_bat.bat_vol >= 3450 && luna_bat.bat_capacity == 0)
  {
    luna_bat.bat_capacity = 1;
  }

  
  if((soc <= 0 && luna_bat.bat_vol < 3450) ||
    (soc <= 4 && luna_bat.bat_vol < 3200) ||
    (luna_bat.bat_vol < 3100) )
  {
    if(luna_bat.bat_low_count > 10)
    {
      if(luna_bat.bat_low_count == 11)
      {
        MSG2("## bat_capacity: 0, vol: %d, temp: %d (count = 11)", luna_bat.bat_vol, luna_bat.bat_temp);
        status_changed ++;
      }
      luna_bat.bat_low_count ++;
      luna_bat.bat_capacity = 0;
    }
    else
    {
      MSG2("## bat_capacity: %d, vol: %d, temp: %d (count = %d)",
        luna_bat.bat_capacity, luna_bat.bat_vol, luna_bat.bat_temp, luna_bat.bat_low_count);
      luna_bat.bat_low_count ++;
      if(soc == 0)
        luna_bat.bat_capacity = 1;
      atomic_set(&luna_bat.read_again, 3);
    }
  }
  else
  {
    luna_bat.bat_low_count = 0;
  }

  
  if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en &&
    luna_bat.pin[CHG_CHG].intr_mask == NV_TRUE)
  {
    MSG3("## CHG_INTR ON");
    luna_bat.pin[CHG_CHG].intr_mask = NV_FALSE;
    NvOdmGpioInterruptMask(luna_bat.pin[CHG_CHG].h_intr, NV_FALSE);
  }
  else if(PinValue[CHG_CEN] == !luna_bat.pin[CHG_CEN].pin_en &&
    luna_bat.pin[CHG_CHG].intr_mask == NV_FALSE)
  {
    MSG3("## CHG_INTR OFF");
    luna_bat.pin[CHG_CHG].intr_mask = NV_TRUE;
    NvOdmGpioInterruptMask(luna_bat.pin[CHG_CHG].h_intr, NV_TRUE);
  }

  
  if(luna_bat.pin[CHG_GLOW].intr_mask == NV_TRUE)
  {
    MSG3("## GLOW_INTR ON");
    luna_bat.pin[CHG_GLOW].intr_mask = NV_FALSE;
    NvOdmGpioInterruptMask(luna_bat.pin[CHG_GLOW].h_intr, NV_FALSE);
  }

  
  
  
  {
    int wake = 0;
    if(luna_bat.ac_online || luna_bat.usb_online)           
      wake |= 1;
    if(luna_bat.ac_pmu_det || luna_bat.usb_pmu_det)         
      wake |= 1;
    if(PinValue[CHG_GLOW] == luna_bat.pin[CHG_GLOW].pin_en)  
      wake |= 1;
    if(!luna_bat.gagic_err && soc <= 0)  
      wake |= 1;
    if(wake)
    {
      if(!luna_bat.wake_flag)
      {
        luna_bat.wake_flag = 1;
        wake_lock(&luna_bat.wlock);
        MSG2("## wake_lock: 0->1, vol=%d, glow=%d, ac=%d(%d), usb=%d(%d), chg=%d, soc=%d",
          luna_bat.bat_vol, PinValue[CHG_GLOW], luna_bat.ac_online, luna_bat.ac_pmu_det,
          luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol, soc);
      }
    }
    else
    {
      if(luna_bat.wake_flag)
      {
        wake_lock_timeout(&luna_bat.wlock, HZ*2);
        luna_bat.wake_flag = 0;
        MSG2("## wake_lock: 1->0, vol=%d, glow=%d, ac=%d(%d), usb=%d(%d), chg=%d, soc=%d",
          luna_bat.bat_vol, PinValue[CHG_GLOW], luna_bat.ac_online, luna_bat.ac_pmu_det,
          luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol, soc);
      }
    }
  }
  

  
  
  {
    
    
    if((chg_in_old == CHG_IN_AC_DET && luna_bat.chg_in != CHG_IN_AC_DET) ||
      (luna_bat.chg_in == CHG_IN_ERROR))  
    {
      
      NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_OTG].h_pin, !luna_bat.pin[CHG_OTG].pin_en);
      msleep(90);
      NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_OTG].h_pin, luna_bat.pin[CHG_OTG].pin_en);
      MSG3("## Reset AC Detect");
    }

    
    
    if(PinValue[CHG_FLT] == luna_bat.pin[CHG_FLT].pin_en)
    {
      if(PinValue[CHG_CEN] == luna_bat.pin[CHG_CEN].pin_en) 
      {
        MSG2("## Reset FLT (Charging timeout)");
        NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
        msleep(15);
        NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
      }
    }

    
    
    if(luna_bat.chg_ctl == CHG_CTL_NONE)
    {
      
      
      switch(luna_bat.chg_bat_current)
      {
        case CHG_BAT_CURRENT_HIGH:
          if(luna_bat.bat_temp < 150)
          {
            MSG2("## BAT_CURRENT: High -> Low (temp = %d)", luna_bat.bat_temp);
            luna_bat.chg_bat_current = CHG_BAT_CURRENT_LOW;
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMB].h_pin, !luna_bat.pin[CHG_LIMB].pin_en);
          }
          else if(PinValue[CHG_LIMB] != luna_bat.pin[CHG_LIMB].pin_en)
          {
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMB].h_pin, luna_bat.pin[CHG_LIMB].pin_en);
          }
          break;
        case CHG_BAT_CURRENT_LOW:
          if(luna_bat.bat_temp > 150)
          {
            MSG2("## BAT_CURRENT: Low -> High (temp = %d)", luna_bat.bat_temp);
            luna_bat.chg_bat_current = CHG_BAT_CURRENT_HIGH;
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMB].h_pin, luna_bat.pin[CHG_LIMB].pin_en);
          }
          else if(PinValue[CHG_LIMB] != !luna_bat.pin[CHG_LIMB].pin_en)
          {
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMB].h_pin, !luna_bat.pin[CHG_LIMB].pin_en);
          }
          break;
      }

      
      
      if(luna_bat.chg_in == CHG_IN_NONE ||
        luna_bat.chg_in == CHG_IN_DOK_DET)
      {
        if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en  ||  
           PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en  ||  
           PinValue[CHG_IUSB] != !luna_bat.pin[CHG_IUSB].pin_en ||  
           PinValue[CHG_LIMD] != !luna_bat.pin[CHG_LIMD].pin_en )   
        {
          MSG3("## USB_100 + DIS");
          NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
          NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
          NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin,!luna_bat.pin[CHG_IUSB].pin_en);
          NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin,!luna_bat.pin[CHG_LIMD].pin_en);
        }
        bat_aidl_done = 0;
      }
      
      
      else if(luna_bat.chg_in == CHG_IN_AC_DET)
      {
        if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_GOOD)
        {
          if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en || 
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
          {
            MSG3("## AC + EN");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
        }
        else
        {
          if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
          {
            MSG3("## AC + DIS");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
        }
        
        if(!bat_aidl_done ||
          (chg_in_old != CHG_IN_AC_DET && luna_bat.chg_in == CHG_IN_AC_DET))
        {
          luna_bat.chg_vol = luna_bat_get_chg_voltage();
          if(luna_bat.chg_vol < 4400)
          {
            
            {
              MSG2("## AC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin, !luna_bat.pin[CHG_LIMD].pin_en);
            }
          }
          else
          {
            
            {
              MSG2("## AC dcin=%dmV (LIMD set 2A)",luna_bat.chg_vol);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin, luna_bat.pin[CHG_LIMD].pin_en);
            }
            msleep(15);
            luna_bat.chg_vol = luna_bat_get_chg_voltage();
            if(luna_bat.chg_vol < 4400)
            {
              MSG2("## AC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin, !luna_bat.pin[CHG_LIMD].pin_en);
            }
            else
            {
              MSG2("## AC dcin=%dmV (LIMD set 2A) after",luna_bat.chg_vol);
            }
          }
          bat_aidl_done = 1;
        }
      }
      
      
      else if(luna_bat.chg_in == CHG_IN_USB_DET)
      {
        switch(luna_bat.usb_current)
        {
          case USB_STATUS_USB_1000:
          case USB_STATUS_USB_2000:
            if(luna_bat.bat_health == POWER_SUPPLY_HEALTH_GOOD)
            {
              if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en || 
                 PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
              {
                MSG3("## USB_HC + EN");
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
              }
            }
            else
            {
              if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||
                 PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)   
              {
                MSG3("## USB_HC + DIS");
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
                NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
              }
            }
            
            if(!bat_aidl_done ||
              (chg_in_old != CHG_IN_USB_DET && luna_bat.chg_in == CHG_IN_USB_DET))
            {
              luna_bat.chg_vol = luna_bat_get_chg_voltage();
              if(luna_bat.chg_vol < 4400)
              {
                
                {
                  MSG2("## USB_HC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
                  NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin, !luna_bat.pin[CHG_LIMD].pin_en);
                }
              }
              else
              {
                
                {
                  MSG2("## USB_HC dcin=%dmV (LIMD set 2A)",luna_bat.chg_vol);
                  NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin, luna_bat.pin[CHG_LIMD].pin_en);
                }
                msleep(15);
                luna_bat.chg_vol = luna_bat_get_chg_voltage();
                if(luna_bat.chg_vol < 4400)
                {
                  MSG2("## USB_HC dcin=%dmV (LIMD set 1A)",luna_bat.chg_vol);
                  NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin, !luna_bat.pin[CHG_LIMD].pin_en);
                }
              }
              bat_aidl_done = 1;
            }
            break;
          case USB_STATUS_USB_500:
            if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
               PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
               PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
            {
              MSG3("## USB_500 + DIS");
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, luna_bat.pin[CHG_IUSB].pin_en);
            }
            bat_aidl_done = 0;
            break;
          case USB_STATUS_USB_0:
          case USB_STATUS_USB_100:
          default:
            
            if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
               PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
               PinValue[CHG_IUSB] != !luna_bat.pin[CHG_IUSB].pin_en)  
            {
              MSG3("## USB_100 + Disable charging");
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
              NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, !luna_bat.pin[CHG_IUSB].pin_en);
            }
            bat_aidl_done = 0;
            break;
        }
      }
    }
    
    
    else
    {
      
      
      if(PinValue[CHG_LIMD] != luna_bat.pin[CHG_LIMD].pin_en) 
      {
        MSG2("## CTRL: HC_CURRENT: 2A");
        NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMD].h_pin, luna_bat.pin[CHG_LIMD].pin_en);
      }

      
      
      if(PinValue[CHG_LIMB] != luna_bat.pin[CHG_LIMB].pin_en) 
      {
        MSG2("## CTRL: BAT_CURRENT: Low -> High (temp = %d)", luna_bat.bat_temp);
        NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_LIMB].h_pin, luna_bat.pin[CHG_LIMB].pin_en);
        luna_bat.chg_bat_current = CHG_BAT_CURRENT_HIGH;
      }

      
      
      switch(luna_bat.chg_ctl)
      {
        case CHG_CTL_USB500_DIS:
          if(PinValue[CHG_CEN]  != !luna_bat.pin[CHG_CEN].pin_en || 
             PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
             PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
          {
            MSG2("## CTRL: USB_500 + DIS");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, luna_bat.pin[CHG_IUSB].pin_en);
          }
          break;
        case CHG_CTL_USB500_EN:
          if(PinValue[CHG_CEN]  != luna_bat.pin[CHG_CEN].pin_en  || 
             PinValue[CHG_DCM]  != !luna_bat.pin[CHG_DCM].pin_en || 
             PinValue[CHG_IUSB] != luna_bat.pin[CHG_IUSB].pin_en)   
          {
            MSG2("## CTRL: USB_500 + EN");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, !luna_bat.pin[CHG_DCM].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_IUSB].h_pin, luna_bat.pin[CHG_IUSB].pin_en);
          }
          break;
        case CHG_CTL_AC2A_DIS:
          if(PinValue[CHG_CEN] != !luna_bat.pin[CHG_CEN].pin_en ||  
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)     
          {
            MSG2("## CTRL: AC + DIS");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, !luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
          break;
        case CHG_CTL_AC2A_EN:
          if(PinValue[CHG_CEN] != luna_bat.pin[CHG_CEN].pin_en ||   
             PinValue[CHG_DCM] != luna_bat.pin[CHG_DCM].pin_en)     
          {
            MSG2("## CTRL: AC + EN");
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_CEN].h_pin, luna_bat.pin[CHG_CEN].pin_en);
            NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[CHG_DCM].h_pin, luna_bat.pin[CHG_DCM].pin_en);
          }
          break;
      }
    }
  }



  luna_bat.jiff_property_valid_time = jiffies + luna_bat.jiff_property_valid_interval;

  if(status_changed ||
    atomic_read(&luna_bat.read_again) == 1)
  {
    
    
    power_supply_changed(&luna_bat.psy_bat);
  }

  
  
  if(luna_bat.suspend_flag && !luna_bat.ac_pmu_det && luna_bat.usb_pmu_det)
  {
    
    
    del_timer_sync(&luna_timer);
  }
  else
  {
    if(atomic_read(&luna_bat.read_again) > 0)
    {
      atomic_dec(&luna_bat.read_again);
      mod_timer(&luna_timer, jiffies + 1*HZ);
    }
    else
    {
      mod_timer(&luna_timer, jiffies + luna_bat.jiff_polling_interval);
    }
  }

  
  
}

static void luna_bat_timer_func(unsigned long temp)
{
  MSG("%s",__func__);
  if(luna_bat.inited)
  {
    if(!luna_bat.suspend_flag || luna_bat.ac_pmu_det || luna_bat.usb_pmu_det)
      queue_work(luna_bat_wqueue, &luna_bat_work);
  }
}



static void luna_bat_chg_irq_handler(void *args)
{
  if(luna_bat.inited)
  {
    MSG2("%s ## [CHG]", __func__);
    atomic_set(&luna_bat.read_again, 3);
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  else
  {
    MSG2("%s ## [CHG] Cancelled!", __func__);
  }
  NvOdmGpioInterruptDone(luna_bat.pin[CHG_CHG].h_intr);
}
static void luna_bat_dok_irq_handler(void *args)
{
  if(luna_bat.inited)
  {
    MSG2("%s ## [DOK]", __func__);
    atomic_set(&luna_bat.read_again, 3);
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  else
  {
    MSG2("%s ## [DOK] Cancelled!", __func__);
  }
  NvOdmGpioInterruptDone(luna_bat.pin[CHG_DOK].h_intr);
}
static void luna_bat_glow_irq_handler(void *args)
{
  NvU32 PinValue;
  if(luna_bat.inited)
  {
    MSG2("%s ## [GLOW]", __func__);
    if(!luna_bat.wake_flag)
    {
      NvOdmGpioGetState(luna_bat.hGpio, luna_bat.pin[CHG_GLOW].h_pin, &PinValue);
      wake_lock_timeout(&luna_bat.wlock, HZ*2);
      MSG2("## wake_lock 2 sec, vol=%d, glow=%d, ac=%d(%d), usb=%d(%d), chg=%d, soc=%d",
        luna_bat.bat_vol, PinValue, luna_bat.ac_online, luna_bat.ac_pmu_det,
        luna_bat.usb_online, luna_bat.usb_pmu_det, luna_bat.chg_vol, luna_bat.bat_capacity);
    }
    atomic_set(&luna_bat.read_again, 3);
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
  else
  {
    MSG2("%s ## [GLOW] Cancelled!", __func__);
  }
  NvOdmGpioInterruptDone(luna_bat.pin[CHG_GLOW].h_intr);
}

#ifdef CONFIG_HAS_EARLYSUSPEND
  static void luna_bat_early_suspend(struct early_suspend *h)
  {
    
    if(luna_bat.ac_online || (luna_bat.usb_online && luna_bat.usb_current == USB_STATUS_USB_2000))
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
    {
      atomic_inc(&luna_bat.read_again);
      queue_work(luna_bat_wqueue, &luna_bat_work);
    }
    
  }
#endif

static int luna_bat_suspend(struct platform_device *pdev, pm_message_t state)
{
  
  luna_bat.suspend_flag = 1;
  if(luna_bat.inited)
  {
    del_timer_sync(&luna_timer);
    cancel_work_sync(&luna_bat_work);
    flush_workqueue(luna_bat_wqueue);
  }
  return 0;
}
static int luna_bat_resume(struct platform_device *pdev)
{
  
  luna_bat.suspend_flag = 0;
  if(luna_bat.inited)
  {
    atomic_set(&luna_bat.read_again, 3);
    queue_work(luna_bat_wqueue, &luna_bat_work);
  }
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
  static const NvBool pin_en_evt1b[] = {
    1,  
    1,  
    0,  
    1,  
    1,  
    1,  
    0,  
    0,  
    0,  
    1,  
    0,  
    0,  
    0}; 
  static const NvBool pin_en_evt13[] = {
    1,  
    1,  
    1,  
    0,  
    1,  
    1,  
    0,  
    0,  
    0,  
    0,  
    1,  
    0,  
    0}; 
  static const NvBool pin_en_evt2[] = {
    0,  
    1,  
    1,  
    1,  
    1,  
    1,  
    0,  
    0,  
    0,  
    0,  
    0,  
    0,  
    0}; 

  MSG2("%s+", __func__);

  
  
  if(system_rev <= EVT1_2)
  {
    MSG2("%s, pConn for EVT1_B", __func__);
    luna_bat.pConn = NvOdmPeripheralGetGuid(LUNA_BAT_EVT1B_GUID);
  }
  else if(system_rev <= EVT1_3)
  {
    MSG2("%s, pConn for EVT1_3", __func__);
    luna_bat.pConn = NvOdmPeripheralGetGuid(LUNA_BAT_EVT13_GUID);
  }
  else
  {
    MSG2("%s, pConn for EVT2 and after (system_rev = %d)", __func__,system_rev);
    luna_bat.pConn = NvOdmPeripheralGetGuid(LUNA_BAT_EVT2_GUID);
  }
  if(!luna_bat.pConn)
  {
    MSG2("%s, pConn get fail!", __func__);
    fail = -1;
    goto err_exit;
  }

  
  
  luna_bat.hPmu = NvOdmServicesPmuOpen();
  if(!luna_bat.hPmu)
  {
    MSG2("%s, hPmu get fail!", __func__);
    fail = -1;
    goto err_exit;
  }

  
  
  luna_bat.hGpio = NvOdmGpioOpen();
  if(!luna_bat.hGpio)
  {
    MSG2("%s, hGpio get fail!", __func__);
    fail = -1;
    goto err_exit;
  }

  
  
  if(luna_bat.pConn->NumAddress < CHG_MAX)
  {
    MSG2("%s, NumAddress = %d, but require %d!", __func__, luna_bat.pConn->NumAddress, CHG_MAX);
    fail = -1;
    goto err_exit;
  }
  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    
    if((system_rev <= EVT1_3 && (i == CHG_LIMD || i == CHG_LIMB || i == CHG_OTG)) ||
      (system_rev >= EVT2 && i == CHG_USUS) )
    {
      luna_bat.pin[i].port  = 0;
      luna_bat.pin[i].pin   = 0;
      luna_bat.pin[i].h_pin = NULL;
      MSG2("%s pin[%s]=Null, resource not used!", __func__, bat_pin_name[i]);
    }
    else if(luna_bat.pConn->AddressList[i].Interface == NvOdmIoModule_Gpio)
    {
      luna_bat.pin[i].port = luna_bat.pConn->AddressList[i].Instance;
      luna_bat.pin[i].pin  = luna_bat.pConn->AddressList[i].Address;
      luna_bat.pin[i].h_pin =
        NvOdmGpioAcquirePinHandle(luna_bat.hGpio, luna_bat.pin[i].port, luna_bat.pin[i].pin);
      if(luna_bat.pin[i].h_pin)
      {
        MSG2("%s pin[%s]=%c%d", __func__, bat_pin_name[i], luna_bat.pin[i].port + 'A', luna_bat.pin[i].pin);
      }
      else
      {
        MSG2("%s pin[%s]=%c%d, acquire handle fail!", __func__, bat_pin_name[i], luna_bat.pin[i].port + 'A', luna_bat.pin[i].pin);
        fail = -1;
        goto err_exit;
      }
    }
    else
    {
      luna_bat.pin[i].port  = 0;
      luna_bat.pin[i].pin   = 0;
      luna_bat.pin[i].h_pin = NULL;
      MSG2("%s pin[%s]=0, 0, get resource fail!", __func__, bat_pin_name[i]);
      fail = -1;
      goto err_exit;
    }
  }

  
  
  if(luna_bat.pConn->AddressList[i].Interface == NvOdmIoModule_I2c)
  {
    luna_bat.i2c_port = luna_bat.pConn->AddressList[i].Instance;
    luna_bat.i2c_addr = luna_bat.pConn->AddressList[i].Address >> 1;  
    MSG2("%s i2c=%d, 0x%02X",__func__,luna_bat.i2c_port, luna_bat.i2c_addr << 1); 
  }
  else
  {
    MSG2("%s, NvOdmIoModule_I2c get fail!", __func__);
    ret = -1;
    goto err_exit;
  }
  if(system_rev <= EVT2_2)
  {
    MSG2("%s, Gauge use NvOdmIoModule_I2c (EVT2-1, EVT2-2)", __func__);
    luna_bat.hI2c = NvOdmI2cOpen(NvOdmIoModule_I2c, luna_bat.i2c_port);
  }
  else
  {
    MSG2("%s, Gauge use NvOdmIoModule_I2c_Pmu (EVT2-3, ...)", __func__);
    luna_bat.hI2c = NvOdmI2cOpen(NvOdmIoModule_I2c_Pmu, luna_bat.i2c_port);
  }
  if(!luna_bat.hI2c)
  {
    MSG2("%s, hI2c Get Fail!", __func__);
    ret = -1;
    goto err_exit;
  }

  
  
  
  for(i=CHG_IUSB; i<CHG_MAX; i++)
  {
    if (system_rev <= EVT1_2)
      luna_bat.pin[i].pin_en = pin_en_evt1b[i];
    else if(system_rev <= EVT1_3)
      luna_bat.pin[i].pin_en = pin_en_evt13[i];
    else 
      luna_bat.pin[i].pin_en = pin_en_evt2[i];
  }

  
  for(i=CHG_IUSB; i<=CHG_OTG; i++)
  {
    if(luna_bat.pin[i].h_pin)
    {
      NvOdmGpioConfig(  luna_bat.hGpio, luna_bat.pin[i].h_pin, NvOdmGpioPinMode_Output);
      
      #if 1
        if(i==CHG_IUSB || i==CHG_LIMD || i==CHG_LIMB || i==CHG_OTG) 
        {
          NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[i].h_pin, luna_bat.pin[i].pin_en);
          MSG2("%s pin[%s]=o/p %d", __func__, bat_pin_name[i], luna_bat.pin[i].pin_en);
        }
        else  
        {
          NvOdmGpioSetState(luna_bat.hGpio, luna_bat.pin[i].h_pin, !luna_bat.pin[i].pin_en);
          MSG2("%s pin[%s]=o/p %d", __func__, bat_pin_name[i], !luna_bat.pin[i].pin_en);
        }
      #else
        {
          NvU32 PinValue;
          NvOdmGpioGetState(luna_bat.hGpio, luna_bat.pin[i].h_pin, &PinValue);
          MSG2("%s pin[%s]=o/p %d", __func__, bat_pin_name[i], PinValue);
        }
      #endif
    }
    luna_bat.pin[i].h_intr = NULL;
  }

  
  {
    
    NvOdmGpioConfig(  luna_bat.hGpio, luna_bat.pin[CHG_UOK].h_pin, NvOdmGpioPinMode_InputData);
    MSG2("%s pin[%s]=i/p", __func__, bat_pin_name[CHG_UOK]);
    
    NvOdmGpioConfig(  luna_bat.hGpio, luna_bat.pin[CHG_FLT].h_pin, NvOdmGpioPinMode_InputData);
    MSG2("%s pin[%s]=i/p", __func__, bat_pin_name[CHG_FLT]);
    
    ret_bool = NvOdmGpioInterruptRegister(luna_bat.hGpio,
      &luna_bat.pin[CHG_DOK].h_intr,
      luna_bat.pin[CHG_DOK].h_pin,
      NvOdmGpioPinMode_InputInterruptAny,
      luna_bat_dok_irq_handler,
      (void *)luna_bat_dok_irq_handler, 500); 
    if(ret_bool)
    {
      luna_bat.pin[CHG_DOK].intr_mask = NV_FALSE;
      MSG2("%s pin[%s]=Intr, register pass!", __func__, bat_pin_name[CHG_DOK]);
    }
    else
    {
      MSG2("%s pin[%s]=Intr, register fail!", __func__, bat_pin_name[CHG_DOK]);
      fail = -1;
      goto err_exit;
    }
    
    ret_bool = NvOdmGpioInterruptRegister(luna_bat.hGpio,
      &luna_bat.pin[CHG_CHG].h_intr,
      luna_bat.pin[CHG_CHG].h_pin,
      NvOdmGpioPinMode_InputInterruptRisingEdge, 
      luna_bat_chg_irq_handler,
      (void *)luna_bat_chg_irq_handler, 500); 
    if(ret_bool)
    {
      luna_bat.pin[CHG_CHG].intr_mask = NV_TRUE;
      NvOdmGpioInterruptMask(luna_bat.pin[CHG_CHG].h_intr, NV_TRUE);
      MSG2("%s pin[%s]=Intr, register pass! Masked!", __func__, bat_pin_name[CHG_CHG]);
    }
    else
    {
      MSG2("%s pin[%s]=Intr, register fail!", __func__, bat_pin_name[CHG_CHG]);
      fail = -1;
      goto err_exit;
    }
    
    
    NvOdmGpioConfig(  luna_bat.hGpio, luna_bat.pin[CHG_BLOW].h_pin, NvOdmGpioPinMode_InputData);
    MSG2("%s pin[%s]=i/p", __func__, bat_pin_name[CHG_BLOW]);
    
    ret_bool = NvOdmGpioInterruptRegister(luna_bat.hGpio,
      &luna_bat.pin[CHG_GLOW].h_intr,
      luna_bat.pin[CHG_GLOW].h_pin,
      NvOdmGpioPinMode_InputInterruptFallingEdge,
      luna_bat_glow_irq_handler,
      (void *)luna_bat_glow_irq_handler, 500); 
    if(ret_bool)
    {
      luna_bat.pin[CHG_GLOW].intr_mask = NV_TRUE;
      NvOdmGpioInterruptMask(luna_bat.pin[CHG_GLOW].h_intr, NV_TRUE);
      MSG2("%s pin[%s]=Intr, register pass! Masked!", __func__, bat_pin_name[CHG_GLOW]);
    }
    else
    {
      MSG2("%s pin[%s]=Intr, register fail!", __func__, bat_pin_name[CHG_GLOW]);
      fail = -1;
      goto err_exit;
    }

  }

  
  
  
  luna_bat.jiff_ac_online_debounce_time = jiffies + 30*24*60*60*HZ;  

  
  wake_lock_init(&luna_bat.wlock, WAKE_LOCK_SUSPEND, "luna_bat_active");
  wake_lock_init(&luna_bat.wlock_3g, WAKE_LOCK_SUSPEND, "luna_bat_reset_3g");

  
  init_timer(&luna_timer);
  luna_timer.function = luna_bat_timer_func;
  luna_timer.expires = jiffies + 10*HZ;

  
  if (system_rev <= EVT1_3)
  {
    INIT_WORK(&luna_bat_work, luna_bat_work_func);
  }
  else
  {
    INIT_WORK(&luna_bat_work, luna_bat_work_func_evt2);
  }
  luna_bat_wqueue = create_singlethread_workqueue("luna_bat_workqueue");
  if(luna_bat_wqueue) 
  {
    MSG("%s luna_bat_workqueue created PASS!",__func__);
  }
  else  
  {
    MSG2("%s luna_bat_workqueue created FAIL!",__func__);
    fail = -1;
    goto err_exit;
  }
  luna_bat.inited = 1;
  queue_work(luna_bat_wqueue, &luna_bat_work);

  
  
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_ac));
  if(ret)
    MSG2("%s luna_bat.psy_ac, ret = %d", __func__, ret);
  
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_usb));
  if(ret)
    MSG2("%s luna_bat.psy_usb, ret = %d", __func__, ret);
  
  ret = power_supply_register(&(plat_dev->dev), &(luna_bat.psy_bat));
  if(ret)
    MSG2("%s luna_bat.psy_bat, ret = %d", __func__, ret);
  

  
  for(i=0; i<ARRAY_SIZE(luna_bat_ctrl_attrs); i++)
  {
    ret = device_create_file(luna_bat.psy_bat.dev, &luna_bat_ctrl_attrs[i]);
    if(ret) MSG2("%s: create FAIL, ret=%d",luna_bat_ctrl_attrs[i].attr.name,ret);
  }

  MSG2("%s-, ret=0", __func__);
  return 0;

err_exit:

  MSG2("%s-, ret=-1", __func__);
  return -1;
}

static struct platform_driver luna_bat_driver =
{
  .driver = {
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
  int ret;
  printk("BootLog, +%s\n", __func__);
  ret = platform_driver_register(&luna_bat_driver);
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}

module_init(luna_bat_init);
MODULE_DESCRIPTION("Luna Battery Driver");

