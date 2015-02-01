














#ifndef __LUNA_BATTERY_H__
#define __LUNA_BATTERY_H__

#include <linux/kernel.h>
#include <linux/power_supply.h>
#include <linux/mutex.h>
#include <linux/clocksource.h>
#include <linux/wakelock.h>
#include <linux/earlysuspend.h>
#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_pmu.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query.h"
#include "nvodm_query_gpio.h"




#define LUNA_BAT_GUID NV_ODM_GUID('l','u','n','a','_','b','a','t')




#define MYBIT(b)        (1<<b)
#define TST_BIT(x,b)    ((x & (1<<b)) ? 1 : 0)
#define CLR_BIT(x,b)    (x &= (~(1<<b)))
#define SET_BIT(x,b)    (x |= (1<<b))





#define USB_STATUS_USB_NONE                 MYBIT(0)
#define USB_STATUS_USB_IN                   MYBIT(1)
#define USB_STATUS_USB_WALL_IN              MYBIT(2)
#define USB_STATUS_USB_0                    MYBIT(3)
#define USB_STATUS_USB_100                  MYBIT(4)
#define USB_STATUS_USB_500                  MYBIT(5)
#define USB_STATUS_USB_1500                 MYBIT(6)  


#define AC_STATUS_AC_NONE                   MYBIT(16)
#define AC_STATUS_AC_IN                     MYBIT(17)

void luna_bat_update_usb_status(int flag);





#define CHARGE_ADDR   (0x80 >> 1)











struct luna_bat_data
{
  const NvOdmPeripheralConnectivity *pConn;
  NvOdmServicesI2cHandle      hI2c;
	NvOdmServicesGpioHandle     hGpio;
	NvOdmGpioPinHandle          hGpio_chg_en_pin;   
	NvOdmGpioPinHandle          hGpio_chg_int_pin;  
	NvOdmServicesGpioIntrHandle hGpio_chg_int_intr;
	NvOdmServicesPmuHandle      hPmu;

  unsigned int  chg_i2c_port; 
  unsigned int  chg_i2c_addr;
  unsigned int  chg_en_port;  
  unsigned int  chg_en_pin;
  unsigned int  chg_int_port; 
  unsigned int  chg_int_pin;

  
  struct power_supply psy_ac;
  struct power_supply psy_usb;
  struct power_supply psy_bat;

  
  #ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend drv_early_suspend;
  #endif

  
  struct wake_lock wlock;

  
  
  unsigned long jiff_property_valid_time;
  

  
  unsigned long jiff_property_valid_interval;
  

  
  unsigned long jiff_polling_interval;

  
  unsigned long jiff_charging_timeout;

  
  unsigned long jiff_ac_online_debounce_time;

  
  int bat_status;
  int bat_health;
  int bat_present;
  int bat_capacity;
  int bat_vol;
  int bat_temp;
  int bat_technology;

  
  int bat_low_count;  
  unsigned long jiff_bat_low_count_wait_time; 

  
  
  int bat_health_err_count;

  
  struct {
     unsigned int enable:1;       
     unsigned int overtemp:1;     
     unsigned int bat_temp_high:1;
     unsigned int bat_temp_low:1; 
     unsigned int trick_chg:1;    
     unsigned int pre_chg:1;      
     unsigned int fast_chg:1;     
     unsigned int taper_chg:1;    
     unsigned int wdt_timeout:1;  
     unsigned int input_ovlo:1;   
     unsigned int input_uvlo:1;   
  } chg_stat; 

  
  char  inited;             
  char  suspend_flag;       
  char  early_suspend_flag; 
  char  wake_flag;          

  
  char  ac_online;
  char  ac_online_tmp;  
  char  usb_online;
  char  flight_mode;
  char  charger_changed;
  char  low_bat_power_off;  
  int   usb_current;    
  int   read_again;     
                        

  
  char  chgic_err;
};



#endif



