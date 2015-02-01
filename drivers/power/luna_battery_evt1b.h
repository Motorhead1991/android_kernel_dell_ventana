













#ifndef __LUNA_BATTERY_EVT1B_H__
#define __LUNA_BATTERY_EVT1B_H__

#ifndef NV_DEBUG
#define NV_DEBUG 0
#endif

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




#define LUNA_BAT_EVT1B_GUID NV_ODM_GUID('l','u','n','a','_','b','1','b')  
#define LUNA_BAT_EVT13_GUID NV_ODM_GUID('l','u','n','a','_','b','1','3')  
#define LUNA_BAT_EVT2_GUID  NV_ODM_GUID('l','u','n','a','_','b','2',' ')  




#define MYBIT(b)        (1<<b)
#define TST_BIT(x,b)    ((x & (1<<b)) ? 1 : 0)
#define CLR_BIT(x,b)    (x &= (~(1<<b)))
#define SET_BIT(x,b)    (x |= (1<<b))








#define USB_STATUS_USB_0                    MYBIT(3)
#define USB_STATUS_USB_100                  MYBIT(4)
#define USB_STATUS_USB_500                  MYBIT(5)
#define USB_STATUS_USB_1000                 MYBIT(6)  
#define USB_STATUS_USB_2000                 MYBIT(7)  





#define LUNA_BAT_BUF_LENGTH  256




enum {CHG_IUSB=0, CHG_USUS, CHG_CEN, CHG_DCM, CHG_LIMD, CHG_LIMB, CHG_OTG, CHG_UOK, CHG_FLT, CHG_CHG, CHG_DOK, CHG_GLOW, CHG_BLOW, CHG_MAX};
struct chg_pin {
  NvU32   port;
  NvU32   pin;
  NvOdmGpioPinHandle          h_pin;
  NvOdmServicesGpioIntrHandle h_intr;
  NvBool  pin_en;     
  NvBool  intr_mask;
};


enum {CHG_STAT_IDLE=0, CHG_STAT_WAIT, CHG_STAT_AC, CHG_STAT_USB_AC, CHG_STAT_USB, CHG_STAT_MAX};


enum {CHG_IN_NONE=0, CHG_IN_ERROR, CHG_IN_DOK_DET, CHG_IN_AC_DET, CHG_IN_USB_DET, CHG_IN_MAX};

enum {CHG_BAT_CURRENT_HIGH=0, CHG_BAT_CURRENT_LOW};

enum {CHG_CTL_NONE=0, CHG_CTL_USB500_DIS, CHG_CTL_USB500_EN, CHG_CTL_AC2A_DIS, CHG_CTL_AC2A_EN};

struct luna_bat_data
{
  const NvOdmPeripheralConnectivity *pConn;
  NvOdmServicesPmuHandle      hPmu;
  NvOdmServicesGpioHandle     hGpio;
  NvOdmServicesI2cHandle      hI2c;

  NvU32    i2c_port;
  NvU32    i2c_addr;

  
  struct chg_pin  pin[CHG_MAX];

  
  struct power_supply psy_ac;
  struct power_supply psy_usb;
  struct power_supply psy_bat;

  
  #ifdef CONFIG_HAS_EARLYSUSPEND
    struct early_suspend drv_early_suspend;
  #endif

  
  struct wake_lock wlock;
  struct wake_lock wlock_3g;

  
  
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

  int bat_capacity_history[3];

  int chg_vol;

  
  int gagic_err;

  
  int bat_low_count;  
  unsigned long jiff_bat_low_count_wait_time; 

  
  
  int bat_health_err_count;

  
  char  inited;             
  char  suspend_flag;       
  char  early_suspend_flag; 
  char  wake_flag;          

  
  char  ac_online;
  char  usb_online;
  
  int   usb_current;    

  
  char  ac_pmu_det;
  char  usb_pmu_det;

  
  NvU32 chg_stat;         

  
  NvU32 chg_in;           
  NvU32 chg_bat_current;  
  NvU32 chg_ctl;          

  atomic_t read_again;
};

struct luna_bat_info_data
{
  int bat_status;
  int bat_health;
  int bat_capacity;
  int bat_vol;
  int bat_temp;

  
  char  ac_online;
  char  usb_online;
  int   usb_current;    

  
  char  ac_pmu_det;
  char  usb_pmu_det;
};

struct luna_bat_eng_data
{
  
  char PinValue[CHG_MAX];

  
  int cap;      
  int volt;     
  int rcomp;    
  int ver;      

  
  int temp;     
  int chg_vol;  

  struct {
    
    unsigned int ac_det: 1;       
    unsigned int usb_det: 1;      
    
    unsigned int ac: 1;           
    unsigned int usb: 1;          
    unsigned int usb_ma: 3;       
  } state;

  char end;
};


#if defined(CONFIG_LUNA_BATTERY_EVT1B) || defined(CONFIG_LUNA_BATTERY)
void luna_bat_update_usb_status(int flag);
void luna_bat_get_info(struct luna_bat_info_data* binfo);
int luna_bat_get_online(void);
#else
static inline void luna_bat_update_usb_status(int flag) {}
static inline void luna_bat_get_info(struct luna_bat_info_data* binfo)  {}
static inline int luna_bat_get_online(void)  {  return 0; }
#endif

#endif



