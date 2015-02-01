#define NV_DEBUG 0

#ifndef _CAMERA_DAEMON_H_
#define _CAMERA_DAEMON_H_
#include <asm/ioctl.h>

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_pmu.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query.h"
#include "nvodm_query_gpio.h"




NvBool luna_read_fa_data(loff_t offset, char *buf, NvU32 size);
NvBool luna_write_fa_data(loff_t offset, char *buf, NvU32 size);





enum {CAM_9665_PWDN=0, CAM_9665_RESET, CAM_5642_PWDN, CAM_5642_RESET, CAM_GPIO_MAX};
struct camera_gpio_pin {
  NvOdmGpioPinHandle  h_pin;
  NvU32   port;
  NvU32   pin;
  NvBool  pin_en;     
};
struct camera_daemon_drv_data
{
  int opened;     
  int i2c_err;    

  const NvOdmPeripheralConnectivity *pConn_9665;
  const NvOdmPeripheralConnectivity *pConn_5642;
  NvOdmServicesGpioHandle hGpio;

  NvOdmServicesI2cHandle hI2c_9665;
  NvOdmServicesI2cHandle hI2c_5642;
  unsigned int  i2c_port_9665;
  unsigned int  i2c_addr_9665;
  unsigned int  i2c_port_5642;
  unsigned int  i2c_addr_5642;

  struct camera_gpio_pin  pin[CAM_GPIO_MAX];
};




#define CAMERA_DAEMON_IOC_MAGIC   'C' 

#define CAMERA_IOC_9665_ON      _IO(CAMERA_DAEMON_IOC_MAGIC, 1)
#define CAMERA_IOC_9665_OFF     _IO(CAMERA_DAEMON_IOC_MAGIC, 2)
#define CAMERA_IOC_9665_STANDBY _IO(CAMERA_DAEMON_IOC_MAGIC, 3)

#define CAMERA_IOC_5642_ON      _IO(CAMERA_DAEMON_IOC_MAGIC, 11)
#define CAMERA_IOC_5642_OFF     _IO(CAMERA_DAEMON_IOC_MAGIC, 12)
#define CAMERA_IOC_5642_STANDBY _IO(CAMERA_DAEMON_IOC_MAGIC, 13)

#endif

