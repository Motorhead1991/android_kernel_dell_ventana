
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <mach/gpio.h>

#include "nvodm_services.h"
#include "nvodm_query_discovery.h"
#include "nvodm_query.h"
#include "nvodm_query_gpio.h"



#include <mach/ecompass.h>







#define DBG_MONITOR 0



#define ENABLE_ECOMPASS_INT 0


#define ECOMPASS_LOG_ENABLE 1
#define ECOMPASS_LOG_DBG 0
#define ECOMPASS_LOG_INFO 0
#define ECOMPASS_LOG_WARNING 1
#define ECOMPASS_LOG_ERR 1

#if ECOMPASS_LOG_ENABLE
	#if ECOMPASS_LOG_DBG
		#define ECOMPASS_LOGD printk
	#else
		#define ECOMPASS_LOGD(a...) {}
	#endif

	#if ECOMPASS_LOG_INFO
		#define ECOMPASS_LOGI printk
	#else
		#define ECOMPASS_LOGI(a...) {}
	#endif

	#if ECOMPASS_LOG_WARNING
		#define ECOMPASS_LOGW printk
#else
		#define ECOMPASS_LOGW(a...) {}
	#endif

	#if ECOMPASS_LOG_ERR
		#define ECOMPASS_LOGE printk
	#else
		#define ECOMPASS_LOGE(a...) {}
	#endif
#else
	#define ECOMPASS_LOGD(a...) {}
	#define ECOMPASS_LOGI(a...) {}
	#define ECOMPASS_LOGW(a...) {}
	#define ECOMPASS_LOGE(a...) {}
#endif

#define ECOMPASS_GUID (NV_ODM_GUID('a','k','m','8','9','7','3','s'))
#define ECOMPASS_EVT1B_GUID (NV_ODM_GUID('a','k','8','9','7','3','1','b'))
#define ECOMPASS_NAME 		"ecompass_ak8973s"
#define ECOMPASS_I2C_NAME "ecompass_ak8973s_i2c"
#define ECOMPASS_I2C_SCLK 100
#define ECOMPASS_I2C_TIMEOUT 3000
#define ECOMPASS_PHYSLEN 	128
#define AK8973S_VENDORID	1

#define ST_REG_ADDR		0xC0
#define TMPS_REG_ADDR		0xC1
#define H1X_REG_ADDR		0xC2
#define H1Y_REG_ADDR		0xC3
#define H1Z_REG_ADDR		0xC4
#define MS1_REG_ADDR		0xE0
#define HXDA_REG_ADDR		0xE1
#define HYDA_REG_ADDR		0xE2
#define HZDA_REG_ADDR		0xE3
#define HXGA_REG_ADDR		0xE4
#define HYGA_REG_ADDR		0xE5
#define HZGA_REG_ADDR		0xE6


#define ETS_REG_ADDR		0x62
#define EVIR_REG_ADDR		0x63
#define EIHE_REG_ADDR		0x64

#define EHXGA_REG_ADDR		0x66
#define EHYGA_REG_ADDR		0x67
#define EHZGA_REG_ADDR		0x68


#define SENSOR_DATA_MASK	0xFF

#define ST_INT_MASK			0x01
#define ST_WEN_MASK		0x02

#define MS1_MODE_MASK		0x03
#define MS1_WEN_MASK		0xF8

#define HXGA_MASK			0x0F
#define HYGA_MASK			0x0F
#define HZGA_MASK			0x0F


#define ETS_MASK			0x3F

#define EVIR_IREF_MASK		0x0F
#define EVIR_VREF_MASK		0xF0

#define EIHE_OSC_MASK		0x0F
#define EIHE_HE_MASK		0xF0


#define ST_WEN_SHIFT		1
#define MS1_WEN_SHIFT		3


#define EVIR_VREF_SHIFT		4
#define EIHE_HE_SHIFT		4


#define ST_INT_RST				0
#define ST_INT_INTR				1
#define ST_WEN_READ			(0 << ST_WEN_SHIFT)
#define ST_WEN_WRITE			(1 << ST_WEN_SHIFT)

#define MS1_MODE_MEASURE		0
#define MS1_MODE_EEPROM		2
#define MS1_MODE_PWR_DOWN	3

#define MS1_WEN_WRITE			(0x15 << MS1_WEN_SHIFT)
#define MS1_WEN_READ			(0x00 << MS1_WEN_SHIFT)

struct ecompass_driver_data_t
{
	struct miscdevice *misc_ecompass_dev;
	struct work_struct        work_data;
	NvU32 I2CAddr;
	NvOdmServicesGpioHandle hGpio;
	NvOdmGpioPinHandle hRstGpioPin;
	NvOdmGpioPinHandle hIntrGpioPin;
	NvOdmServicesGpioIntrHandle hIntrGpioIntr;
	NvOdmServicesI2cHandle hI2C;
	atomic_t	open_count;
	int user_pid;
	#if DBG_MONITOR
	struct timer_list dbg_monitor_timer;
	struct work_struct dbg_monitor_work_data;
	#endif
};

struct ecompass_magnetic_data_t
{
	int              magnetic_x;
	int              magnetic_y;
	int              magnetic_z;
	int              temp;
};

static struct ecompass_driver_data_t ecompass_driver_data = {
	.open_count = ATOMIC_INIT(0),
	.user_pid = 0,
};

static int i2c_ecompass_probe(struct platform_device *pdev);
static int i2c_ecompass_remove(struct platform_device *pdev);
static int i2c_ecompass_suspend(struct platform_device *pdev, pm_message_t state);
static int i2c_ecompass_resume(struct platform_device *pdev);

static int misc_ecompass_open(struct inode *inode_p, struct file *fp);
static int misc_ecompass_release(struct inode *inode_p, struct file *fp);
static long misc_ecompass_ioctl(struct file *fp, unsigned int cmd, unsigned long arg);
static ssize_t misc_ecompass_read(struct file *fp, char __user *buf, size_t count, loff_t *f_pos);
static ssize_t misc_ecompass_write(struct file *fp, const char __user *buf, size_t count, loff_t *f_pos);

static struct platform_driver i2c_ecompass_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = ECOMPASS_I2C_NAME,			
	},
	.probe	 = i2c_ecompass_probe,
	.remove	 = i2c_ecompass_remove,
	.suspend = i2c_ecompass_suspend,
	.resume  = i2c_ecompass_resume,
};

static struct file_operations misc_ecompass_fops = {
	.owner 	= THIS_MODULE,
	.open 	= misc_ecompass_open,
	.release = misc_ecompass_release,
	.unlocked_ioctl = misc_ecompass_ioctl,
	.read = misc_ecompass_read,
	.write = misc_ecompass_write,
};

static struct miscdevice misc_ecompass_device = {
	.minor 	= MISC_DYNAMIC_MINOR,
	.name 	= ECOMPASS_NAME,
	.fops 	= &misc_ecompass_fops,
};

static int i2c_ecompass_read(NvOdmServicesI2cHandle hI2C, uint8_t regaddr, uint8_t *buf, uint32_t len)
{
	uint8_t ldat = regaddr;
	NvOdmI2cStatus I2cTransStatus = -NvOdmI2cStatus_Timeout;
	NvOdmI2cTransactionInfo TransactionInfo[] = {
		[0] = {
			.Address	= ecompass_driver_data.I2CAddr,
			.Flags	= NVODM_I2C_IS_WRITE | NVODM_I2C_USE_REPEATED_START,
			.Buf	= (NvU8 *) &ldat,
			.NumBytes	= 1
		},
		[1] = {
			.Address	= ecompass_driver_data.I2CAddr,
			.Flags	= 0,
			.Buf	= (NvU8 *) buf,
			.NumBytes	= len
		}
	};

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+, regaddr:0x%x, len:%d\n", __func__, regaddr, len);

	I2cTransStatus = NvOdmI2cTransaction(hI2C, &TransactionInfo[0], 2, ECOMPASS_I2C_SCLK, ECOMPASS_I2C_TIMEOUT);
	if (I2cTransStatus != NvOdmI2cStatus_Success)
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::NvOdmI2cTransaction failed-I2cTransStatus:%d\n", __func__, -I2cTransStatus);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-ret:%d\n", __func__, -I2cTransStatus);
	return -I2cTransStatus;
}

static int i2c_ecompass_write(NvOdmServicesI2cHandle hI2C, uint8_t regaddr, uint8_t *buf, uint32_t len)
{
	uint8_t write_buf[32];
	NvOdmI2cStatus I2cTransStatus = -NvOdmI2cStatus_Timeout;
	NvOdmI2cTransactionInfo TransactionInfo[] = {
		[0] = {
			.Address	= ecompass_driver_data.I2CAddr,
			.Flags	= NVODM_I2C_IS_WRITE,
			.Buf	= (NvU8 *) write_buf,
			.NumBytes	= len + 1,
			},
	};

	if (len > 31)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: len > 31!!\n", __func__);
		return -ENOMEM;
	}

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+regaddr:0x%x, len:%d\n", __func__, regaddr, len);

	write_buf[0] = regaddr;
	NvOdmOsMemcpy((void *)(write_buf + 1), buf, len);

	I2cTransStatus = NvOdmI2cTransaction(hI2C, &TransactionInfo[0], 1, ECOMPASS_I2C_SCLK, ECOMPASS_I2C_TIMEOUT);
	if (I2cTransStatus != NvOdmI2cStatus_Success)
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::NvOdmI2cTransaction failed-I2cTransStatus:%d\n", __func__, I2cTransStatus);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-ret\n", __func__, -I2cTransStatus);
	return -I2cTransStatus;
}


static int ecompass_reset_pin(struct ecompass_driver_data_t *driver_data, int value)
{
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	if (driver_data->hGpio && driver_data->hRstGpioPin)
		NvOdmGpioSetState(driver_data->hGpio, driver_data->hRstGpioPin, value);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return 0;
}



#if ENABLE_ECOMPASS_INT
static void ecompass_irq_handler(void* arg)
{
	struct ecompass_driver_data_t *driver_data = (struct ecompass_driver_data_t *)arg;

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	schedule_work(&driver_data->work_data);

	if (driver_data->hIntrGpioIntr)
		NvOdmGpioInterruptDone(driver_data->hIntrGpioIntr);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
}
#endif


#if DBG_MONITOR
static void debug_monitor(unsigned long data)
{
	struct ecompass_driver_data_t *driver_data = (struct ecompass_driver_data_t *) data;
	schedule_work(&driver_data->dbg_monitor_work_data);
	return;
}

static void debug_monitor_work_func(struct work_struct *work)
{
	int i;
	uint8_t rx_buf[7];
	uint8_t tx_buf;
	struct ecompass_driver_data_t *driver_data = container_of(work, struct ecompass_driver_data_t, dbg_monitor_work_data);
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	
	NvOdmOsMemset(rx_buf, 0, sizeof(rx_buf));
	i2c_ecompass_read(driver_data->hI2C, ST_REG_ADDR, rx_buf, 5);

	for (i = 0; i < 5; i++)
	{
		ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] [0x%2x] %2x\n", (i + 0xC0), rx_buf[i]);
	}

	
	NvOdmOsMemset(rx_buf, 0, sizeof(rx_buf));
	i2c_ecompass_read(driver_data->hI2C, MS1_REG_ADDR, rx_buf, sizeof(rx_buf));

	for (i = 0; i < sizeof(rx_buf); i++)
	{
		ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] [0x%2x] %2x\n", (i + 0xE0), rx_buf[i]);
	}

	
	tx_buf = MS1_MODE_EEPROM;
	i2c_ecompass_write(driver_data->hI2C, MS1_REG_ADDR, &tx_buf, sizeof(tx_buf));

	
	NvOdmOsMemset(rx_buf, 0, sizeof(rx_buf));
	i2c_ecompass_read(driver_data->hI2C, ETS_REG_ADDR, rx_buf, sizeof(rx_buf));

	for (i = 0; i < sizeof(rx_buf); i++)
	{
		ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] [0x%2x] %2x\n", (i + 0x62), rx_buf[i]);
	}

	
	tx_buf = MS1_MODE_PWR_DOWN;
	i2c_ecompass_write(driver_data->hI2C, MS1_REG_ADDR, &tx_buf, sizeof(tx_buf));

	mod_timer(&driver_data->dbg_monitor_timer, jiffies + HZ);
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return;
}
#endif

static int misc_ecompass_open(struct inode *inode_p, struct file *fp)
{
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);
	ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] inode_p->i_rdev: 0x%x\n", inode_p->i_rdev);
	atomic_inc(&ecompass_driver_data.open_count);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return 0;
}

static int misc_ecompass_release(struct inode *inode_p, struct file *fp)
{
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);
	atomic_dec(&ecompass_driver_data.open_count);
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return 0;
}

static long misc_ecompass_ioctl(struct file *fp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	void __user *argp = (void __user *)arg;
	struct ecompass_ioctl_t ioctl_data;
	unsigned char *kbuf = NULL;
	unsigned char __user *ubuf = NULL;
	int pid;

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+cmd_nr:%d\n", __func__, _IOC_NR(cmd));

	if (_IOC_TYPE(cmd) != ECOMPASS_IOC_MAGIC)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::Not ECOMPASS_IOC_MAGIC\n", __func__);
		return -ENOTTY;
	}

	ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] arg:%d\n", arg);

	if (cmd == ECOMPASS_IOC_REG_INT)
	{
		ret = !access_ok(VERIFY_READ, (void __user*)arg, _IOC_SIZE(cmd));
		if (ret)
		{
			ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::access_ok check err\n", __func__);
			return -EFAULT;
		}
	}
	else
	{
		if (cmd != ECOMPASS_IOC_RESET)
		{
			if (copy_from_user(&ioctl_data, (void *)argp, sizeof(struct ecompass_ioctl_t)))
			{
				ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::copy_from_user fail\n", __func__);
				return -EFAULT;
			}

			if (ioctl_data.len <= 0)
			{
				return -EFAULT;
			}

			ubuf = ioctl_data.buf;
		}

		if (_IOC_DIR(cmd) & _IOC_READ)
		{
			ret = !access_ok(VERIFY_WRITE, (void __user*)ubuf, ioctl_data.len);
		}
		if (_IOC_DIR(cmd) & _IOC_WRITE)
		{
			ret = !access_ok(VERIFY_READ, (void __user*)ubuf, ioctl_data.len);
		}
		if (ret)
		{
			ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::access_ok check err\n", __func__);
			return -EFAULT;
		}

		if (cmd == ECOMPASS_IOC_READ || cmd == ECOMPASS_IOC_WRITE)
		{
			kbuf = kmalloc(ioctl_data.len, GFP_KERNEL);
			if (!kbuf)
			{
				ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::kmalloc fail!!\n", __func__);
				return -ENOMEM;
			}
		}
	}

	switch(cmd)
	{
		case ECOMPASS_IOC_RESET:
			ecompass_reset_pin(&ecompass_driver_data, 1);
			ecompass_reset_pin(&ecompass_driver_data, 0);
			ecompass_reset_pin(&ecompass_driver_data, 1);
			break;
		case ECOMPASS_IOC_READ:
			ret = i2c_ecompass_read(ecompass_driver_data.hI2C, ioctl_data.regaddr, kbuf, ioctl_data.len);
			if (ret)
			{
				ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::i2c_ecompass_read fail-ret:%d\n", __func__, ret);
				ret = -EIO;
				goto misc_ecompass_ioctl_err_exit;
			}

			if (copy_to_user(ubuf, kbuf, ioctl_data.len))
			{
				ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::ECOMPASS_IOC_READ:copy_to_user fail-\n", __func__);
				ret = -EFAULT;
				goto misc_ecompass_ioctl_err_exit;
			}
			break;
		case ECOMPASS_IOC_WRITE:
			if (copy_from_user(kbuf, ubuf, ioctl_data.len))
			{
				ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::ECOMPASS_IOC_WRITE:copy_from_user fail-\n", __func__);
				ret = -EFAULT;
				goto misc_ecompass_ioctl_err_exit;
			}

			ret = i2c_ecompass_write(ecompass_driver_data.hI2C, ioctl_data.regaddr, kbuf, ioctl_data.len);
			if (ret)
			{
				ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::i2c_ecompass_write fail-ret:%d\n", __func__, ret);
				ret = -EIO;
				goto misc_ecompass_ioctl_err_exit;
			}
			break;
		case ECOMPASS_IOC_REG_INT:
			if (copy_from_user(&pid, (void *)argp, _IOC_SIZE(cmd)))
			{
				ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::ECOMPASS_IOC_REG_INT:copy_from_user fail-\n", __func__);
				ret = -EFAULT;
				goto misc_ecompass_ioctl_err_exit;
			}
			ecompass_driver_data.user_pid = pid;
			ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] %s::user_pid=%d\n", __func__, ecompass_driver_data.user_pid);
			break;
		default:
			ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::default-\n", __func__);
			ret = -ENOTTY;
			break;
	}

misc_ecompass_ioctl_err_exit:

	if (kbuf)
		kfree(kbuf);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return ret;
}

static ssize_t misc_ecompass_read(struct file *fp, char __user *buf, size_t count, loff_t *f_pos)
{
	int ret = 0;

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return ret;
}

static ssize_t misc_ecompass_write(struct file *fp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int ret = 0;

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+fp:0x%x, buf:0x%x, count:%d, f_pos:%d\n", __func__, &fp, &buf, count, *f_pos);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return ret;
}



#if ENABLE_ECOMPASS_INT
static void ecompass_intr_work_func(struct work_struct *work)
{
	
	
	
	

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	
	
	
	

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
}
#endif


static int i2c_ecompass_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	const NvOdmPeripheralConnectivity *pConn;
	NvU32 found = 0;
	NvU32 I2CPort = 0;
	NvU32 RstGpioPort = 0;
	NvU32 RstGpioPin = 0;
	NvU32 IntrGpioPort = 0;
	NvU32 IntrGpioPin = 0;
	NvU8 buf[1];

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	
	
	
	

	
	
	
		pConn = NvOdmPeripheralGetGuid(ECOMPASS_EVT1B_GUID);

	if (!pConn)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: pConn == NULL\n", __func__);
		return -1;
	}

	for (i = 0; i < pConn->NumAddress; i++)
	{
		switch (pConn->AddressList[i].Interface)
		{
			case NvOdmIoModule_I2c:
				ecompass_driver_data.I2CAddr = (pConn->AddressList[i].Address << 1);
				I2CPort = pConn->AddressList[i].Instance;
				found |= 1;
				break;

			case NvOdmIoModule_Gpio:
				RstGpioPort = pConn->AddressList[i].Instance;
				RstGpioPin = pConn->AddressList[i].Address;
				IntrGpioPort = pConn->AddressList[i+1].Instance;
				IntrGpioPin = pConn->AddressList[i+1].Address;
				found |= 2;
				break;

			default:
				break;
		}
		if (found == 3)
			break;
	}

	if (found != 3)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: found = %d err\n", __func__, found);
		return -1;
	}

	ecompass_driver_data.hGpio = NvOdmGpioOpen();
	if (!ecompass_driver_data.hGpio)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: NvOdmGpioOpen fail err\n", __func__);
		return -1;
	}

	ecompass_driver_data.hIntrGpioPin = NvOdmGpioAcquirePinHandle(ecompass_driver_data.hGpio, IntrGpioPort, IntrGpioPin);
	if (!ecompass_driver_data.hIntrGpioPin)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: NvOdmGpioAcquirePinHandle err\n", __func__);
		ret = -1;
		goto i2c_ecompass_probe_NvOdmGpioAcquirePinHandle_IntrGpioPin_err;
	}

	NvOdmGpioConfig(ecompass_driver_data.hGpio, ecompass_driver_data.hIntrGpioPin, NvOdmGpioPinMode_InputData);

	ecompass_driver_data.hRstGpioPin = NvOdmGpioAcquirePinHandle(ecompass_driver_data.hGpio, RstGpioPort, RstGpioPin);
	if (!ecompass_driver_data.hRstGpioPin)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: NvOdmGpioAcquirePinHandle err\n", __func__);
		ret = -1;
		goto i2c_ecompass_probe_NvOdmGpioAcquirePinHandle_RstGpioPin_err;
	}

	NvOdmGpioConfig(ecompass_driver_data.hGpio, ecompass_driver_data.hRstGpioPin, NvOdmGpioPinMode_Output);

	ecompass_driver_data.hI2C = NvOdmI2cOpen(NvOdmIoModule_I2c, I2CPort);
	if (!ecompass_driver_data.hI2C)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: NvOdmI2cOpen err\n", __func__);
		ret = -1;
		goto i2c_ecompass_probe_NvOdmI2cOpen_err;
	}

	
	
	#if ENABLE_ECOMPASS_INT
	INIT_WORK(&ecompass_driver_data.work_data, ecompass_intr_work_func);

	if (!ecompass_driver_data.hIntrGpioPin)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::hIntrGpioPin is NULL\n", __func__);
		ret = -1;
		goto i2c_ecompass_probe_err_irq_number;
	}
	ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] IntrPin:%d\n", IntrGpioPin);
	#endif
	

	#if	DBG_MONITOR
	INIT_WORK(&ecompass_driver_data.dbg_monitor_work_data, debug_monitor_work_func);
	init_timer(&ecompass_driver_data.dbg_monitor_timer);
	ecompass_driver_data.dbg_monitor_timer.data = (unsigned long) &ecompass_driver_data;
	ecompass_driver_data.dbg_monitor_timer.function = debug_monitor;
	ecompass_driver_data.dbg_monitor_timer.expires = jiffies + HZ;
	add_timer(&ecompass_driver_data.dbg_monitor_timer);
	#endif

	
	
	#if ENABLE_ECOMPASS_INT
	if (!NvOdmGpioInterruptRegister(ecompass_driver_data.hGpio, &ecompass_driver_data.hIntrGpioIntr, ecompass_driver_data.hIntrGpioPin, NvOdmGpioPinMode_InputInterruptRisingEdge, ecompass_irq_handler, (void*) &ecompass_driver_data, 0))
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::NvOdmGpioInterruptRegister fail!!\n", __func__);
		goto i2c_ecompass_probe_err_NvOdmGpioInterruptRegister;
	}
	#endif
	

	ecompass_reset_pin(&ecompass_driver_data, 0);
	ecompass_reset_pin(&ecompass_driver_data, 1);

	ret = i2c_ecompass_read(ecompass_driver_data.hI2C, 0xE0, buf, 1);
	if (ret)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::i2c_ecompass_read fail-ret:%d\n", __func__, ret);
		ret = -EIO;
		goto i2c_ecompass_probe_err_i2c_ecompass_read;
	}

	if ((buf[0] & 0x03) != 0x03)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s::[E0] = %d != 3 error\n", __func__, buf[0]);
		ret = -EIO;
		goto i2c_ecompass_probe_err_mode;
	}

	
	ret = misc_register(&misc_ecompass_device);
	if (ret)
	{
		ECOMPASS_LOGE(KERN_ERR "[ECOMPASS] %s:: misc_register error-ret:%d\n", __func__, ret);
		goto i2c_ecompass_probe_err_misc_register;
	}

	ECOMPASS_LOGI(KERN_INFO "[ECOMPASS] misc_ecompass_device->devt:0x%x\n", misc_ecompass_device.this_device->devt);

	platform_set_drvdata(pdev, &ecompass_driver_data);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-ret:%d\n", __func__, ret);
	return 0;

i2c_ecompass_probe_err_misc_register:
i2c_ecompass_probe_err_mode:
i2c_ecompass_probe_err_i2c_ecompass_read:


#if ENABLE_ECOMPASS_INT
	NvOdmGpioInterruptUnregister(ecompass_driver_data.hGpio, ecompass_driver_data.hIntrGpioPin, ecompass_driver_data.hIntrGpioIntr);
i2c_ecompass_probe_err_NvOdmGpioInterruptRegister:
#endif



#if ENABLE_ECOMPASS_INT
i2c_ecompass_probe_err_irq_number:
#endif

	NvOdmI2cClose(ecompass_driver_data.hI2C);
i2c_ecompass_probe_NvOdmI2cOpen_err:
	NvOdmGpioConfig(ecompass_driver_data.hGpio, ecompass_driver_data.hRstGpioPin, NvOdmGpioPinMode_Tristate);
	NvOdmGpioReleasePinHandle(ecompass_driver_data.hGpio, ecompass_driver_data.hRstGpioPin);
i2c_ecompass_probe_NvOdmGpioAcquirePinHandle_RstGpioPin_err:
	NvOdmGpioConfig(ecompass_driver_data.hGpio, ecompass_driver_data.hIntrGpioPin, NvOdmGpioPinMode_Tristate);
	NvOdmGpioReleasePinHandle(ecompass_driver_data.hGpio, ecompass_driver_data.hIntrGpioPin);
i2c_ecompass_probe_NvOdmGpioAcquirePinHandle_IntrGpioPin_err:
	NvOdmGpioClose(ecompass_driver_data.hGpio);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-ret:%d\n", __func__, ret);
	return ret;
}

static int i2c_ecompass_remove(struct platform_device *pdev)
{
	struct ecompass_driver_data_t *driver_data = platform_get_drvdata(pdev);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	misc_deregister(&misc_ecompass_device);

	if (driver_data->hI2C)
		NvOdmI2cClose(driver_data->hI2C);
	driver_data->hI2C = NULL;

	#if ENABLE_ECOMPASS_INT
	if (driver_data->hGpio && driver_data->hIntrGpioPin && driver_data->hIntrGpioIntr)
	{
		NvOdmGpioInterruptUnregister(driver_data->hGpio, driver_data->hIntrGpioPin, driver_data->hIntrGpioIntr);
		driver_data->hIntrGpioIntr = NULL;
	}
	#endif

	if (driver_data->hGpio && driver_data->hIntrGpioPin)
		NvOdmGpioConfig(driver_data->hGpio, driver_data->hIntrGpioPin, NvOdmGpioPinMode_Tristate);

	if (driver_data->hGpio && driver_data->hRstGpioPin)
		NvOdmGpioConfig(driver_data->hGpio, driver_data->hRstGpioPin, NvOdmGpioPinMode_Tristate);

	if (driver_data->hIntrGpioPin)
		NvOdmGpioReleasePinHandle(driver_data->hGpio, driver_data->hIntrGpioPin);
	driver_data->hIntrGpioPin = NULL;

	if (driver_data->hRstGpioPin)
		NvOdmGpioReleasePinHandle(driver_data->hGpio, driver_data->hRstGpioPin);
	driver_data->hRstGpioPin = NULL;

	if (driver_data->hGpio)
		NvOdmGpioClose(driver_data->hGpio);
	driver_data->hGpio = NULL;

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return 0;
}

static int i2c_ecompass_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct ecompass_driver_data_t *driver_data = platform_get_drvdata(pdev);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	#if ENABLE_ECOMPASS_INT
	if (driver_data->hIntrGpioIntr)
		NvOdmGpioInterruptMask(driver_data->hIntrGpioIntr, NV_TRUE);
	#endif

	NvOdmGpioConfig(driver_data->hGpio, driver_data->hIntrGpioPin, NvOdmGpioPinMode_Tristate);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return 0;
}

static int i2c_ecompass_resume(struct platform_device *pdev)
{
	struct ecompass_driver_data_t *driver_data = platform_get_drvdata(pdev);

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);

	#if ENABLE_ECOMPASS_INT
	if (driver_data->hIntrGpioIntr)
	{
		NvOdmGpioConfig(driver_data->hGpio, driver_data->hIntrGpioPin, NvOdmGpioPinMode_InputInterruptRisingEdge);
		NvOdmGpioInterruptMask(driver_data->hIntrGpioIntr, NV_FALSE);
	}
	else
	#endif
	{
		NvOdmGpioConfig(driver_data->hGpio, driver_data->hIntrGpioPin, NvOdmGpioPinMode_InputData);
	}

	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
	return 0;
}

static int __init ecompass_init(void)
{
	int ret = 0;

	printk("BootLog, +%s+\n", __func__);

	NvOdmOsMemset(&ecompass_driver_data, 0, sizeof(struct ecompass_driver_data_t));
	ret = platform_driver_register(&i2c_ecompass_driver);

	printk("BootLog, -%s-, ret=0\n", __func__);
	return ret;
}

static void __exit ecompass_exit(void)
{
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s+\n", __func__);
	platform_driver_unregister(&i2c_ecompass_driver);
	ECOMPASS_LOGD(KERN_DEBUG "[ECOMPASS] %s-\n", __func__);
}

module_init(ecompass_init);
module_exit(ecompass_exit);

MODULE_DESCRIPTION("AKM AK8973S E-compass Driver");
MODULE_LICENSE("GPL");

