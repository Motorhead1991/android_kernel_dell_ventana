
















#include <asm/mach-types.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/hrtimer.h>
#include <linux/err.h>
#include <linux/power_supply.h>
#include <linux/wakelock.h>
#include <linux/earlysuspend.h>
#include <../../../drivers/staging/android/timed_output.h>

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_pmu.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query.h"
#include "nvodm_query_gpio.h"
#include "mach/nvrm_linux.h" 


static int vib_log_on1  = 0;
static int vib_log_on2  = 1;

#define MSG(format, arg...)   {if(vib_log_on1)  printk(KERN_INFO "[VIB]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(vib_log_on2)  printk(KERN_INFO "[VIB]" format "\n", ## arg);}






#define LUNA_VIB_GUID NV_ODM_GUID('l','u','n','a','_','v','i','b')

enum {VIB_HEN=0, VIB_LEN, VIB_CLK, VIB_MAX};
static const char *vib_pin_name[] = {"VIB_HEN", "VIB_LEN", "VIB_CLK"};

struct vib_pin {
  unsigned int  port;
  unsigned int  pin;
  NvOdmGpioPinHandle          h_pin;
  
};
struct luna_vib_data
{
  const NvOdmPeripheralConnectivity *pConn;
  NvOdmServicesI2cHandle      hI2c;
  NvOdmServicesGpioHandle     hGpio;
  unsigned int    i2c_port;
  unsigned int    i2c_addr;
  struct vib_pin  pin[VIB_MAX];
  
  struct early_suspend drv_early_suspend;
  char early_suspend_flag;
  char clock_onOff;
} luna_vib;
struct luna_vib_reg
{
  NvU8 addr;
  NvU8 data;
};




#define I2C_RETRY_MAX   5
#define LUNA_VIB_BUF_LENGTH  256
static int vib_read_i2c(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
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
  if(!luna_vib.hI2c)
    return -ENODEV;
  status = NvOdmI2cTransaction(luna_vib.hI2c, info, 2, 100, 1000);
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

static int vib_write_i2c(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
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
  if(!luna_vib.hI2c)
    return -ENODEV;
  buf_w[0] = reg;
  for(i=0; i<len; i++)
    buf_w[i+1] = buf[i];
  status = NvOdmI2cTransaction(luna_vib.hI2c, info, 1, 100, 1000);
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
static int vib_write_i2c_retry(NvU8 addr, NvU8 reg, NvU8* buf, NvU8 len)
{
  NvS32 i,ret;
  for(i=0; i<I2C_RETRY_MAX; i++)
  {
    ret = vib_write_i2c(addr,reg,buf,len);
    if(ret == 1)
      return ret;
    else
      msleep(10);
  }
  return ret;
}




static int vib_init_chip(void)
{
  struct luna_vib_reg init[] =
  {
    #if 1 
    {0x30, 0x01},{0x00, 0x0F},{0x31, 0xCB},{0x32, 0x00},{0x33, 0x23},
    {0x34, 0x00},{0x35, 0x00},{0x36, 0x86},{0x30, 0x91}
    #else 
    {0x30, 0x01},{0x00, 0x0F},{0x31, 0xCB},{0x32, 0x00},{0x33, 0x23},
    {0x34, 0x00},{0x35, 0x00},{0x36, 0xB9},{0x30, 0x91}
    #endif
  };
  NvU32 i,i2c_ret;
  for(i=0; i<ARRAY_SIZE(init); i++)
  {
    i2c_ret = vib_write_i2c_retry(luna_vib.i2c_addr, init[i].addr, &init[i].data, 1);
    if(i2c_ret != 1)
    {
      MSG2("%s 0x%02X W 0x%02X Fail!",__func__,init[i].addr,init[i].data);
      return -ENODEV;
    }
  }
  return 0;
}


struct hrtimer luna_timed_vibrator_timer;
spinlock_t luna_timed_vibrator_lock;
static void vibrator_clk_onOff(int onOff) 
{
	NvU32 ClockInstances[1];
	NvU32 ClockFrequencies[1];
	NvU32 NumClocks;

  #ifdef CONFIG_TEGRA_ODM_LUNA_MLK
    if(onOff && !luna_vib.clock_onOff)
    {
  	  NvOdmExternalClockConfig(LUNA_VIB_GUID, NV_FALSE, ClockInstances, ClockFrequencies, &NumClocks);
  	  luna_vib.clock_onOff = 1;
  	  MSG("%s, On,  ClockInstances=%d ClockFrequencies=%d", __func__, ClockInstances[0], ClockFrequencies[0]);
    }
    else if(!onOff && luna_vib.clock_onOff)
    {
    	NvOdmExternalClockConfig(LUNA_VIB_GUID, NV_TRUE, ClockInstances, ClockFrequencies, &NumClocks);
  	  luna_vib.clock_onOff = 0;
  	  MSG("%s, Off, ClockInstances=%d ClockFrequencies=%d", __func__, ClockInstances[0], ClockFrequencies[0]);
    }
  #endif
}

static void vibrator_onOff(char onOff)
{
  if(onOff)
  {
    vibrator_clk_onOff(1);
    
    NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_CLK].h_pin, 0);  
    
    
    NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_HEN].h_pin, 1);  
  }
  else
  {
    NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_HEN].h_pin, 0);  
    NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_CLK].h_pin, 1);  
    
    vibrator_clk_onOff(0);
  }
  MSG("VIB %s", onOff?"ON":"OFF");
}
static enum hrtimer_restart luna_timed_vibrator_timer_func(struct hrtimer *timer)
{
  MSG("%s", __func__);
  vibrator_onOff(0);
  return HRTIMER_NORESTART;
}
static void luna_timed_vibrator_init(void)
{
  
  hrtimer_init(&luna_timed_vibrator_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
  luna_timed_vibrator_timer.function = luna_timed_vibrator_timer_func;
  spin_lock_init(&luna_timed_vibrator_lock);
}
int luna_timed_vibrator_get_time(struct timed_output_dev *sdev)
{
  ktime_t remain;
  int value = 0;
  MSG("%s", __func__);
  if(hrtimer_active(&luna_timed_vibrator_timer))
  {
    remain = hrtimer_get_remaining(&luna_timed_vibrator_timer);
    value = remain.tv.sec * 1000 + remain.tv.nsec / 1000000;
  }
  MSG("timeout = %d",value);
  return value;
}
void luna_timed_vibrator_enable(struct timed_output_dev *sdev, int timeout)
{
  unsigned long flags;
  MSG("%s", __func__);

  spin_lock_irqsave(&luna_timed_vibrator_lock, flags);
  hrtimer_cancel(&luna_timed_vibrator_timer);
  if(!timeout)  
    vibrator_onOff(0);
  else  
    vibrator_onOff(1);
  if(timeout > 0) 
  {
    hrtimer_start(&luna_timed_vibrator_timer,
      ktime_set(timeout / 1000, (timeout % 1000) * 1000000),
      HRTIMER_MODE_REL);
    MSG("%s Set timeout", __func__);
  }
  spin_unlock_irqrestore(&luna_timed_vibrator_lock, flags);
}

static struct timed_output_dev luna_timed_vibrator = {
  .name     = "vibrator",
  .enable   = luna_timed_vibrator_enable,
  .get_time = luna_timed_vibrator_get_time,
};



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
static void vib_i2c_test(unsigned char *bufLocal, int count)
{
  int i2c_ret, i, j;
  char id, reg[2], len, dat[LUNA_VIB_BUF_LENGTH/4];

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
    i2c_ret = vib_read_i2c(id, reg[0], &dat[0], len);
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
    NvOdmI2cStatus status;
    NvOdmI2cTransactionInfo info[2];
    a2h(&bufLocal[2], &id);     
    a2h(&bufLocal[4], &reg[0]); 
    a2h(&bufLocal[6], &reg[1]); 
    a2h(&bufLocal[8], &len);    
    if(len >= sizeof(dat))
    {
      MSG2("R %02X:%02X%02X(%02d) Fail (max length=%d)", id,reg[0],reg[1],len,sizeof(dat));
      return;
    }
    info[0].Address   = id<<1;
    info[0].Flags     = NVODM_I2C_IS_WRITE,
    info[0].Buf       = (NvU8 *)&reg,
    info[0].NumBytes  = 2,
    info[1].Address   = (id<<1) + 1;
    info[1].Flags     = 0,
    info[1].Buf       = (NvU8 *)&dat[0],
    info[1].NumBytes  = len;
    status = NvOdmI2cTransaction(luna_vib.hI2c, info, 2, 100, 1000);
    if(status != NvOdmI2cStatus_Success)
    {
      MSG2("R %02X:%02X%02X(%02d) Fail (ret=%d)", id,reg[0],reg[1],len,status);
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
    MSG2("R %02X:%02X%02X(%02d) = %s", id,reg[0],reg[1],len,bufLocal);
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
    i2c_ret = vib_write_i2c(id, dat[0], &dat[1], len-1);
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
static ssize_t luna_vib_ctrl_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  NvU8 bufLocal[LUNA_VIB_BUF_LENGTH];

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
        vib_log_on1 = 0;
        vib_log_on2 = 0;
      }
      else if(bufLocal[1]=='1')
      {
        MSG2("Dynamic Log 1 On");
        vib_log_on1 = 1;
      }
      else if(bufLocal[1]=='2')
      {
        MSG2("Dynamic Log 2 On");
        vib_log_on2 = 1;
      }
      break;

    case 'b':
      vib_init_chip();
      break;

    case 'g':
      switch(bufLocal[1])
      {
        case 'h':
          if(bufLocal[2]=='0')
          {
            MSG2("HEN = 0");
            NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_HEN].h_pin, 0);
          }
          else if(bufLocal[2]=='1')
          {
            MSG2("HEN = 1 (On)");
            NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_HEN].h_pin, 1);
          }
          break;
        case 'l':
          if(bufLocal[2]=='0')
          {
            MSG2("LEN = 0");
            NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_LEN].h_pin, 0);
          }
          else if(bufLocal[2]=='1')
          {
            MSG2("LEN = 1 (On)");
            NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_LEN].h_pin, 1);
          }
          break;
        case 'c':
          if(bufLocal[2]=='0')
          {
            MSG2("CLK = 0 (On)");
            NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_CLK].h_pin, 0);
          }
          else if(bufLocal[2]=='1')
          {
            MSG2("CLK = 1");
            NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_CLK].h_pin, 1);
          }
          break;
        default:
          {
            NvU32 i, pin_value[VIB_MAX];
            for(i=VIB_HEN; i<VIB_MAX; i++)
              NvOdmGpioGetState(luna_vib.hGpio, luna_vib.pin[i].h_pin, &pin_value[i]);
            MSG2("HEN LEN CLK = %d %d %d", pin_value[VIB_HEN],pin_value[VIB_LEN],pin_value[VIB_CLK]);
          }
          break;
      }
      break;


    
    
    case 'i':
      vib_i2c_test(bufLocal, count);
      break;

    default:
      break;
  }
  return count;
}

static struct device_attribute luna_vib_ctrl_attrs[] = {
  __ATTR(ctrl, 0666, NULL, luna_vib_ctrl_store),
};


static void luna_vib_early_suspend(struct early_suspend *h)
{
  
  luna_vib.early_suspend_flag = 1;
  
}
static void luna_vib_late_resume(struct early_suspend *h)
{
  
  luna_vib.early_suspend_flag = 0;
  
}

void luna_vib_shutdown(struct platform_device *pdev)
{
  NvU8 data = 0x00;
  vibrator_clk_onOff(0);
  vib_write_i2c_retry(luna_vib.i2c_addr, 0x30, &data, sizeof(data));
  NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_HEN].h_pin, 0);  
  NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_LEN].h_pin, 0);  
  NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_CLK].h_pin, 1);  
}


static int luna_vib_probe(struct platform_device *pdev)
{
  int i, ret=0;

  
  
  luna_vib.pConn = NvOdmPeripheralGetGuid(LUNA_VIB_GUID);
  if(!luna_vib.pConn)
  {
    MSG2("%s, pConn get Fail!", __func__);
    ret = -1;
    goto err_exit;
  }
  if(luna_vib.pConn->NumAddress != 5)
  {
    MSG2("%s, NumAddress not 5!", __func__);
    ret = -1;
    goto err_exit;
  }
  if(luna_vib.pConn->AddressList[0].Interface == NvOdmIoModule_I2c)
  {
    luna_vib.i2c_port = luna_vib.pConn->AddressList[0].Instance;
    luna_vib.i2c_addr = luna_vib.pConn->AddressList[0].Address >> 1;  
    MSG2("%s i2c=%d, 0x%02X",__func__,luna_vib.i2c_port,luna_vib.i2c_addr << 1);  
  }
  else
  {
    MSG2("%s, NvOdmIoModule_I2c get fail!", __func__);
    ret = -1;
    goto err_exit;
  }
  for(i=VIB_HEN; i<VIB_MAX; i++)
  {
    luna_vib.pin[i].port = luna_vib.pConn->AddressList[i+1].Instance;
    luna_vib.pin[i].pin  = luna_vib.pConn->AddressList[i+1].Address;
    MSG2("%s pin[%s]=%c%d", __func__, vib_pin_name[i], luna_vib.pin[i].port + 'A', luna_vib.pin[i].pin);
  }

  
  
  luna_vib.hI2c = NvOdmI2cOpen(NvOdmIoModule_I2c, luna_vib.i2c_port);
  if(!luna_vib.hI2c)
  {
    MSG2("%s, hI2c Get Fail!", __func__);
    ret = -1;
    goto err_exit;
  }

  
  
  luna_vib.hGpio = NvOdmGpioOpen();
  if(!luna_vib.hGpio)
  {
    MSG2("%s, hGpio Get Fail!", __func__);
    ret = -1;
    goto err_exit;
  }

  
  
  for(i=VIB_HEN; i<VIB_MAX; i++)
  {
    luna_vib.pin[i].h_pin =
      NvOdmGpioAcquirePinHandle(luna_vib.hGpio, luna_vib.pin[i].port, luna_vib.pin[i].pin);
    if(luna_vib.pin[i].h_pin)
    {
      NvOdmGpioConfig(  luna_vib.hGpio, luna_vib.pin[i].h_pin, NvOdmGpioPinMode_Output);
      if(i==VIB_CLK ||  
        i==VIB_HEN)     
        NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[i].h_pin, 0);
      else              
        NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[i].h_pin, 1);
    }
    else
    {
      MSG2("%s, pin[%s] acquire handle get fail!", __func__, vib_pin_name[i]);
      ret = -1;
      goto err_exit;
    }
  }
  msleep(1);  

  
  luna_vib.clock_onOff = 1;	
  vibrator_clk_onOff(0);

  
  
  {
    int i2c_ret;
    NvU8 data = 0x80;
    i2c_ret = vib_write_i2c_retry(luna_vib.i2c_addr, 0x32, &data, sizeof(data));

    
    
    {
      
      mdelay(1);
      if(!vib_init_chip())  
      {
        MSG2("%s, Vibrator init Pass!",__func__);
      }
      else
      {
        MSG2("%s, Vibrator init Fail!",__func__);
        
        
      }
    }
    
    
    
    
    
    
    NvOdmGpioSetState(luna_vib.hGpio, luna_vib.pin[VIB_CLK].h_pin, 1);  
  }

  
  luna_vib.drv_early_suspend.level    = EARLY_SUSPEND_LEVEL_DISABLE_FB;
  luna_vib.drv_early_suspend.suspend  = luna_vib_early_suspend;
  luna_vib.drv_early_suspend.resume   = luna_vib_late_resume;
  register_early_suspend(&luna_vib.drv_early_suspend);

  
  
  luna_timed_vibrator_init();
  ret = timed_output_dev_register(&luna_timed_vibrator);

  
  ret = device_create_file(&pdev->dev, &luna_vib_ctrl_attrs[0]);
  if(ret)
  {
    MSG2("%s: create FAIL, ret=%d",luna_vib_ctrl_attrs[0].attr.name,ret);
  }
  else
  {
    MSG2("%s: create PASS, ret=%d",luna_vib_ctrl_attrs[0].attr.name,ret);
  }

err_exit:
  return ret;

}

static struct platform_driver luna_vibrator_driver = {
  
  
  .shutdown = luna_vib_shutdown,
  .probe    = luna_vib_probe,
  .driver   = {
    .name   = "luna_vibrator",
    .owner    = THIS_MODULE,
  },
};
static int __init luna_vib_init(void)
{
  int ret;
  printk("BootLog, +%s\n", __func__);
  ret = platform_driver_register(&luna_vibrator_driver);
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}
static void __exit luna_vib_exit(void)
{
  platform_driver_unregister(&luna_vibrator_driver);
}

module_init(luna_vib_init);
module_exit(luna_vib_exit);

