#define NV_DEBUG 0

#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <mach/gpio.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_pmu.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query.h"
#include "nvodm_query_gpio.h"
#include "../odm_kit/query/luna/include/nvodm_query_discovery_imager.h"
#include "camera_daemon.h"




#include <linux/fs.h>
#include <linux/file.h>
#include <linux/uaccess.h>

#define MSG_FA(format, arg...)   {if(1)  printk(KERN_INFO "[FA]" format "\n", ## arg);}

#define FA_PARTITION_PATH "/dev/fa"

NvBool luna_read_fa_data(loff_t offset, char *buf, NvU32 size);
NvBool luna_write_fa_data(loff_t offset, char *buf, NvU32 size);

NvBool read_partition_data(char *path, loff_t offset, char *buf, NvU32 size)
{
  NvBool ret = NV_TRUE;
  mm_segment_t oldfs;
  struct file *fp;
  ssize_t size_actual;

  oldfs = get_fs();
  set_fs(KERNEL_DS);
  fp = filp_open(path, O_RDONLY, 0);
  if(fp != NULL)
  {
    fp->f_pos = fp->f_op->llseek(fp, offset, 0);
    size_actual = fp->f_op->read(fp, buf, size, &fp->f_pos);
    if(size_actual !=  size)
    {
      MSG_FA("%s Error!! read size = %d, actual read size = %d", __func__,size, size_actual);
      ret = NV_FALSE;
    }
  }
  else
  {
    MSG_FA("%s Error!! open fail: %s", __func__,path);
    ret = NV_FALSE;
  }
  filp_close(fp, NULL);
  set_fs(oldfs);
  return ret;
}
NvBool write_partition_data(char *path, loff_t offset, char *buf, NvU32 size)
{
  NvBool ret = NV_TRUE;
  mm_segment_t oldfs;
  struct file *fp;
  ssize_t size_actual;

  oldfs = get_fs();
  set_fs(KERNEL_DS);
  fp = filp_open(path, O_RDWR, 0);
  if(fp != NULL)
  {
    MSG_FA("%s fp = 0x%X", __func__,(NvU32)fp);
    fp->f_pos = fp->f_op->llseek(fp, offset, 0);
    MSG_FA("%s f_pos = %d", __func__,(NvU32)fp->f_pos);
    size_actual = fp->f_op->write(fp, buf, size, &fp->f_pos);
    MSG_FA("%s size = %d", __func__,size_actual);
    if(size_actual !=  size)
    {
      MSG_FA("%s Error!! write size = %d, actual write size = %d, ", __func__,size, size_actual);
      ret = NV_FALSE;
    }
  }
  else
  {
    MSG_FA("%s Error!! open fail: %s", __func__,path);
    ret = NV_FALSE;
  }
  filp_close(fp, NULL);
  set_fs(oldfs);
  return ret;
}
NvBool luna_read_fa_data(loff_t offset, char *buf, NvU32 size)
{
  return read_partition_data(FA_PARTITION_PATH, offset, buf, size);
}
NvBool luna_write_fa_data(loff_t offset, char *buf, NvU32 size)
{
  return write_partition_data(FA_PARTITION_PATH, offset, buf, size);
}
EXPORT_SYMBOL(luna_read_fa_data);
EXPORT_SYMBOL(luna_write_fa_data);




static int camera_daemon_log_on1  = 0;
static int camera_daemon_log_on2  = 1;

#define MSG(format, arg...)   {if(camera_daemon_log_on1)  printk(KERN_INFO "[CAM]" format "\n", ## arg);}
#define MSG2(format, arg...)  {if(camera_daemon_log_on2)  printk(KERN_INFO "[CAM]" format "\n", ## arg);}


const char *gpio_pin_name[] = {"CAM_9665_PWDN ", "CAM_9665_RESET", "CAM_5642_PWDN ", "CAM_5642_RESET"};

struct camera_daemon_drv_data  cam_drv;








static ssize_t camera_daemon_show(struct device *dev, struct device_attribute *attr,char *buf)
{
  return 0;
}
static ssize_t camera_daemon_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
  return 0;
}
static struct device_attribute camera_daemon_ctrl_attrs[] = {
  __ATTR(ctrl, 0666, camera_daemon_show, camera_daemon_store),
};





static int camera_daemon_open(struct inode *inode_p, struct file *fp)
{
  cam_drv.opened ++;
  MSG("%s    <== [%d] (%04d)",__func__,cam_drv.opened,current->pid);
  return 0;
}
static int camera_daemon_release(struct inode *inode_p, struct file *fp)
{
  cam_drv.opened --;
  MSG("%s <== [%d] (%04d)\n",__func__,cam_drv.opened,current->pid);
  return 0;
}
static int camera_daemon_ioctl(struct inode *inode_p, struct file *fp, unsigned int cmd, unsigned long arg)
{
  int ret = 0;

  

  
  
  if(_IOC_TYPE(cmd) != CAMERA_DAEMON_IOC_MAGIC)
  {
    MSG2("%s: Not CAMERA_DAEMON_IOC_MAGIC", __func__);
    return -ENOTTY;
  }
  if(_IOC_DIR(cmd) & _IOC_READ)
  {
    ret = !access_ok(VERIFY_WRITE, (void __user*)arg, _IOC_SIZE(cmd));
    if(ret)
    {
      MSG2("%s: access_ok check write err", __func__);
      return -EFAULT;
    }
  }
  if(_IOC_DIR(cmd) & _IOC_WRITE)
  {
    ret = !access_ok(VERIFY_READ, (void __user*)arg, _IOC_SIZE(cmd));
    if(ret)
    {
      MSG2("%s: access_ok check read err", __func__);
      return -EFAULT;
    }
  }

  
  
  switch (cmd)
  {
    
    
    case CAMERA_IOC_9665_ON:
      MSG2("%s: CAMERA_IOC_9665_ON  (%04d)", __func__,current->pid);
      mdelay(5);  
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_9665_PWDN ].h_pin, !cam_drv.pin[CAM_9665_PWDN ].pin_en);
      mdelay(5);  
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_9665_RESET].h_pin, !cam_drv.pin[CAM_9665_RESET].pin_en);
      
      
      
      
      
      
      
      break;

    case CAMERA_IOC_9665_OFF:
      MSG2("%s: CAMERA_IOC_9665_OFF (%04d)", __func__,current->pid);
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_9665_RESET].h_pin, cam_drv.pin[CAM_9665_RESET].pin_en);
      mdelay(5);
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_9665_PWDN ].h_pin, cam_drv.pin[CAM_9665_PWDN ].pin_en);
      break;

    case CAMERA_IOC_9665_STANDBY:
      MSG2("%s: CAMERA_IOC_9665_STANDBY (%04d)", __func__,current->pid);
      
      
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_9665_PWDN ].h_pin, cam_drv.pin[CAM_9665_PWDN ].pin_en);
      break;

    
    
    case CAMERA_IOC_5642_ON:
      MSG2("%s: CAMERA_IOC_5642_ON  (%04d)", __func__,current->pid);
      mdelay(5);  
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_5642_PWDN ].h_pin, !cam_drv.pin[CAM_5642_PWDN ].pin_en);
      mdelay(5);  
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_5642_RESET].h_pin, !cam_drv.pin[CAM_5642_RESET].pin_en);
      mdelay(20); 
      break;

    case CAMERA_IOC_5642_OFF:
      MSG2("%s: CAMERA_IOC_5642_OFF (%04d)", __func__,current->pid);
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_5642_RESET].h_pin, cam_drv.pin[CAM_5642_RESET].pin_en);
      mdelay(5);
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_5642_PWDN ].h_pin, cam_drv.pin[CAM_5642_PWDN ].pin_en);
      break;

    case CAMERA_IOC_5642_STANDBY:
      MSG2("%s: CAMERA_IOC_9665_STANDBY (%04d)", __func__,current->pid);
      NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[CAM_5642_PWDN ].h_pin, cam_drv.pin[CAM_5642_PWDN ].pin_en);
      break;

    default:
      MSG2("%s: unknown ioctl = 0x%X", __func__,cmd);
      break;
  }

  
  return ret;
}

static struct file_operations camera_daemon_fops = {
  .owner    = THIS_MODULE,
  .open     = camera_daemon_open,
  .release  = camera_daemon_release,
  .ioctl    = camera_daemon_ioctl,
};
static struct miscdevice camera_daemon_device = {
  .minor  = MISC_DYNAMIC_MINOR,
  .name   = "camera_daemon",
  .fops   = &camera_daemon_fops,
};

static int camera_daemon_probe(struct platform_device *plat_dev)
{
  int i, pin_idx = 0, ret, fail = 0;
  NvOdmIoModule Interface;
  NvU32 Instance, Address, Purpose;

  MSG2("%s+", __func__);

  
  
  cam_drv.hGpio = NvOdmGpioOpen();
  if(!cam_drv.hGpio)
  {
    MSG2("%s, hGpio Get Fail!", __func__);
    ret = -1;
    goto err_exit;
  }

  
  
  cam_drv.pConn_9665 = NvOdmPeripheralGetGuid(SENSOR_YUV_OV9665_GUID);
  if(!cam_drv.pConn_9665)
  {
    MSG2("%s, pConn_9665 get fail!", __func__);
    fail = -1;
    goto err_exit;
  }
  for(i=0; i<cam_drv.pConn_9665->NumAddress; i++)
  {
    Interface = cam_drv.pConn_9665->AddressList[i].Interface;
    Instance  = cam_drv.pConn_9665->AddressList[i].Instance;
    Address   = cam_drv.pConn_9665->AddressList[i].Address;
    Purpose   = cam_drv.pConn_9665->AddressList[i].Purpose;
    
    
    if(Interface == NvOdmIoModule_I2c)
    {
      cam_drv.i2c_port_9665 = Instance;
      cam_drv.i2c_addr_9665 = Address >> 1; 
      cam_drv.hI2c_9665 = NvOdmI2cOpen(NvOdmIoModule_I2c, cam_drv.i2c_port_9665);
      if(!cam_drv.hI2c_9665)
      {
        MSG2("%s, hI2c_9665 Get Fail!", __func__);
        ret = -1;
        goto err_exit;
      }
      else
      {
        MSG2("%s, hI2c_9665=%d, 0x%02X",__func__,cam_drv.i2c_port_9665,cam_drv.i2c_addr_9665 << 1);  
      }
    }
    
    
    else if(cam_drv.pConn_9665->AddressList[i].Interface == NvOdmIoModule_Gpio)
    {
      switch(NVODM_IMAGER_FIELD(Address))
      {
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_POWERDOWN):
          pin_idx = CAM_9665_PWDN;
          cam_drv.pin[pin_idx].pin_en = 1;
          break;
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_POWERDOWN_AL):
          pin_idx = CAM_9665_PWDN;
          cam_drv.pin[pin_idx].pin_en = 0;
          break;
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_RESET):
          pin_idx = CAM_9665_RESET;
          cam_drv.pin[pin_idx].pin_en = 1;
          break;
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_RESET_AL):
          pin_idx = CAM_9665_RESET;
          cam_drv.pin[pin_idx].pin_en = 0;
          break;
        default:
          MSG2("%s, Error! 9665 get pin error, Addr = %X %X",__func__,Address, NVODM_IMAGER_FIELD(Address));
          break;
      }
      if(NVODM_IMAGER_IS_SET(Address))
      {
        cam_drv.pin[pin_idx].port = Instance;
        cam_drv.pin[pin_idx].pin  = NVODM_IMAGER_CLEAR(Address);
        cam_drv.pin[pin_idx].h_pin =
          NvOdmGpioAcquirePinHandle(cam_drv.hGpio, cam_drv.pin[pin_idx].port, cam_drv.pin[pin_idx].pin);
        if(cam_drv.pin[pin_idx].h_pin)
        {
          MSG2("%s, pin[%s]=%c%d", __func__,
            gpio_pin_name[pin_idx], cam_drv.pin[pin_idx].port + 'A', cam_drv.pin[pin_idx].pin);
        }
        else
        {
          MSG2("%s, pin[%s]=%c%d, acquire handle fail!", __func__,
            gpio_pin_name[pin_idx], cam_drv.pin[pin_idx].port + 'A', cam_drv.pin[pin_idx].pin);
          fail = -1;
          goto err_exit;
        }
        NvOdmGpioConfig(cam_drv.hGpio, cam_drv.pin[pin_idx].h_pin, NvOdmGpioPinMode_Output);
        NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[pin_idx].h_pin, cam_drv.pin[pin_idx].pin_en);
        MSG2("%s, pin[%s]=%c%d o/p %d", __func__,
          gpio_pin_name[pin_idx], cam_drv.pin[pin_idx].port + 'A', cam_drv.pin[pin_idx].pin, cam_drv.pin[pin_idx].pin_en);
      }
    }
  }

  
  
  cam_drv.pConn_5642 = NvOdmPeripheralGetGuid(SENSOR_YUV_OV5642_GUID);
  if(!cam_drv.pConn_5642)
  {
    MSG2("%s, pConn_5642 get fail!", __func__);
    fail = -1;
    goto err_exit;
  }
  for(i=0; i<cam_drv.pConn_5642->NumAddress; i++)
  {
    Interface = cam_drv.pConn_5642->AddressList[i].Interface;
    Instance  = cam_drv.pConn_5642->AddressList[i].Instance;
    Address   = cam_drv.pConn_5642->AddressList[i].Address;
    Purpose   = cam_drv.pConn_5642->AddressList[i].Purpose;
    
    
    if(Interface == NvOdmIoModule_I2c)
    {
      cam_drv.i2c_port_5642 = Instance;
      cam_drv.i2c_addr_5642 = Address >> 1; 
      cam_drv.hI2c_5642 = NvOdmI2cOpen(NvOdmIoModule_I2c, cam_drv.i2c_port_5642);
      if(!cam_drv.hI2c_5642)
      {
        MSG2("%s, hI2c_5642 Get Fail!", __func__);
        ret = -1;
        goto err_exit;
      }
      else
      {
        MSG2("%s, i2c_5642=%d, 0x%02X",__func__,cam_drv.i2c_port_5642,cam_drv.i2c_addr_5642 << 1);  
      }
    }
    
    

    else if(Interface == NvOdmIoModule_Gpio)
    {
      switch(NVODM_IMAGER_FIELD(Address))
      {
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_POWERDOWN):
          pin_idx = CAM_5642_PWDN;
          cam_drv.pin[pin_idx].pin_en = 1;
          break;
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_POWERDOWN_AL):
          pin_idx = CAM_5642_PWDN;
          cam_drv.pin[pin_idx].pin_en = 0;
          break;
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_RESET):
          pin_idx = CAM_5642_RESET;
          cam_drv.pin[pin_idx].pin_en = 1;
          break;
        case NVODM_IMAGER_FIELD(NVODM_IMAGER_RESET_AL):
          pin_idx = CAM_5642_RESET;
          cam_drv.pin[pin_idx].pin_en = 0;
          break;
        default:
          MSG2("%s, Error! 5642 get pin error, Addr = %X %X",__func__,Address, NVODM_IMAGER_FIELD(Address));
          break;
      }
      if(NVODM_IMAGER_IS_SET(Address))
      {
        cam_drv.pin[pin_idx].port = Instance;
        cam_drv.pin[pin_idx].pin  = NVODM_IMAGER_CLEAR(Address);
        cam_drv.pin[pin_idx].h_pin =
          NvOdmGpioAcquirePinHandle(cam_drv.hGpio, cam_drv.pin[pin_idx].port, cam_drv.pin[pin_idx].pin);
        if(cam_drv.pin[pin_idx].h_pin)
        {
          MSG2("%s, pin[%s]=%c%d", __func__,
            gpio_pin_name[pin_idx], cam_drv.pin[pin_idx].port + 'A', cam_drv.pin[pin_idx].pin);
        }
        else
        {
          MSG2("%s, pin[%s]=%c%d, acquire handle fail!", __func__,
            gpio_pin_name[pin_idx], cam_drv.pin[pin_idx].port + 'A', cam_drv.pin[pin_idx].pin);
          fail = -1;
          goto err_exit;
        }
        NvOdmGpioConfig(cam_drv.hGpio, cam_drv.pin[pin_idx].h_pin, NvOdmGpioPinMode_Output);
        NvOdmGpioSetState(cam_drv.hGpio, cam_drv.pin[pin_idx].h_pin, cam_drv.pin[pin_idx].pin_en);
        MSG2("%s, pin[%s]=%c%d o/p %d", __func__,
          gpio_pin_name[pin_idx], cam_drv.pin[pin_idx].port + 'A', cam_drv.pin[pin_idx].pin, cam_drv.pin[pin_idx].pin_en);
      }
    }
  }

  
  
  ret = misc_register(&camera_daemon_device);
  if(ret)
  {
    MSG2("%s, camera daemon misc_register Fail, ret=%d", __func__, ret);
    fail = -1;;
    goto err_exit;
  }

  
  
  for(i=0; i<ARRAY_SIZE(camera_daemon_ctrl_attrs); i++)
  {
    ret = device_create_file(&plat_dev->dev, &camera_daemon_ctrl_attrs[i]);
    if(ret) MSG2("%s, create FAIL, ret=%d",camera_daemon_ctrl_attrs[i].attr.name,ret);
  }

  MSG2("%s-, ret=0", __func__);
  return 0;

err_exit:

  MSG2("%s-, ret=-1", __func__);
  return -1;
}

static struct platform_driver camera_daemon_driver =
{
  .driver = {
    .name   = "camera_daemon",
    .owner  = THIS_MODULE,
  },
  .probe    = camera_daemon_probe,
};

static int __init camera_daemon_init(void)
{
  int ret;
  printk("BootLog, +%s\n", __func__);
  ret = platform_driver_register(&camera_daemon_driver);
  printk("BootLog, -%s, ret=%d\n", __func__,ret);
  return ret;
}

module_init(camera_daemon_init);
MODULE_DESCRIPTION("Camera Daemon Driver");


