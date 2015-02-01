

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <asm/io.h>

#include "mach/nvrm_linux.h"
#include "nvodm_services.h"

#if 0
static int i2ctest_suspend(struct i2c_client *kbd, pm_message_t mesg);
static int i2ctest_resume(struct i2c_client *kbd);
static int __devexit i2ctest_remove(struct i2c_client *kbd);
static int __devinit i2ctest_probe(struct i2c_client *client, \
				    const struct i2c_device_id *id);

static struct i2c_client *my_test_i2c_client = NULL;
#endif
static struct mutex		mutex = __MUTEX_INITIALIZER(mutex);


#define __REGDRVIO	0xAA
#define REGDRV_I2C_TX		_IO(__REGDRVIO, 1) 
#define REGDRV_APP_ACCESS	_IO(__REGDRVIO, 2)

#define MAX_I2C_BUF_SIZE 1024
#define MAX_APP_BUF_SIZE MAX_I2C_BUF_SIZE

struct drv_info {
	struct miscdevice	misc;
};

struct i2c_msg_from_user
{
	struct i2c_msg msgs[2];	
	char buf[MAX_I2C_BUF_SIZE+2]; 
	int len;
};

enum ACTION
{
	READ_ACTION,
	WRITE_ACTION,
	TEST_ACTION
};

struct app_msg_from_user
{
	int action;
	unsigned long reg_phy_addr;
	char buf[MAX_APP_BUF_SIZE];
	int len;
};

ssize_t hello_aio_write(struct kiocb *iocb, const struct iovec *iov,
			 unsigned long nr_segs, loff_t ppos)
{
	ssize_t ret = 0;
	printk(KERN_ALERT "+hello_aio_write+\n");
	printk(KERN_ALERT "-hello_aio_write-\n");
	return ret;
}

static ssize_t hello_read(struct file *file, char __user *buf,
			   size_t count, loff_t *pos)
{
	ssize_t ret=0;
	printk(KERN_ALERT "+hello_read+\n");
	printk(KERN_ALERT "-hello_read-\n");	
	return ret;
}

static int copy_i2c_params_from_user_space(struct i2c_msg_from_user* msgs_from_user,struct i2c_msg_from_user* drv_mirror_msg)
{
	int ret = 0;
	int offset = 0;
	int i;
	
	ret = copy_from_user(drv_mirror_msg, msgs_from_user, sizeof(struct i2c_msg_from_user));
	if(ret != 0)
	{
		return ret;
	}

	if(drv_mirror_msg->len != 1 && drv_mirror_msg->len !=2)
	{
		return -1; 
	}
	
	for(i=0; i<drv_mirror_msg->len; i++)
	{
		if(drv_mirror_msg->msgs[i].len > 0 && drv_mirror_msg->msgs[i].len < MAX_I2C_BUF_SIZE + 2 - offset)
		{
			ret = copy_from_user(drv_mirror_msg->buf+offset, drv_mirror_msg->msgs[i].buf, drv_mirror_msg->msgs[i].len);
			if(ret != 0)
			{
				return ret;
			}
			drv_mirror_msg->msgs[i].buf = drv_mirror_msg->buf+offset;
		}
		else
		{
			return -1;
		}
		offset += drv_mirror_msg->msgs[i].len;
	}
	return 0;
}

static int copy_result_to_user_space(struct i2c_msg_from_user* msgs_from_user,struct i2c_msg_from_user* drv_mirror_msg)
{
	return copy_to_user(msgs_from_user->buf, drv_mirror_msg->buf, MAX_I2C_BUF_SIZE+2);
}

static int copy_app_params_from_user_space(struct app_msg_from_user* app_msgs_from_user, struct app_msg_from_user* drv_mirror_app_msg)
{
	int ret = 0;
	ret = copy_from_user(drv_mirror_app_msg, app_msgs_from_user, sizeof(struct app_msg_from_user));
	if(ret != 0)
	{
		return ret;
	}
	if(drv_mirror_app_msg->reg_phy_addr % 4 != 0)
	{
		return -1;
	}
	if( drv_mirror_app_msg->len <= 0 || drv_mirror_app_msg->len > MAX_APP_BUF_SIZE || drv_mirror_app_msg->len % 4 != 0)
	{
		return -1;
	}
	return ret;
}

static int access_memory(void* vaddr,struct app_msg_from_user* drv_mirror_app_msg, struct app_msg_from_user* app_msgs_from_user)
{
	int ret = 0;
	unsigned int* source_ptr;
	unsigned int* dest_ptr;
	int i;
	
	if(drv_mirror_app_msg->action == READ_ACTION)
	{
		source_ptr = (unsigned int*)vaddr;
		dest_ptr = (unsigned int*)(drv_mirror_app_msg->buf);

		mutex_lock(&mutex);
		for(i=0;i<drv_mirror_app_msg->len/sizeof(unsigned int);i++)
		{
			dest_ptr[i] = source_ptr[i]; 

		}
		mutex_unlock(&mutex);
		return copy_to_user(app_msgs_from_user->buf, drv_mirror_app_msg->buf, drv_mirror_app_msg->len);
	}
	else if(drv_mirror_app_msg->action == WRITE_ACTION)
	{
		source_ptr = (unsigned int*)(drv_mirror_app_msg->buf);
		dest_ptr = (unsigned int*)vaddr;
		mutex_lock(&mutex);
		for(i=0;i<drv_mirror_app_msg->len/sizeof(unsigned int);i++)
		{
			dest_ptr[i] = source_ptr[i]; 
		}
		mutex_unlock(&mutex);
	}
	else
	{
		ret = -1;
	}
	return ret;
}

#define I2C_SPEED_KHZ 100
#define MAX_I2C_HANDLE 10

static int g_max_num_of_i2c_controller = 0;
static NvOdmServicesI2cHandle g_hOdmI2C[MAX_I2C_HANDLE];

static int do_i2c_transfer(NvOdmI2cTransactionInfo *ptr, int num)
{
    int j;
	NvOdmI2cStatus status = NvOdmI2cStatus_Success;
	
	for(j=0;j<g_max_num_of_i2c_controller;j++)
	{
		status = NvOdmI2cTransaction(g_hOdmI2C[j], ptr, num, 
										I2C_SPEED_KHZ, NV_WAIT_INFINITE);
		if (status != NvOdmI2cStatus_Success)
		{
			switch (status)
			{
				case NvOdmI2cStatus_Timeout:
					printk("%s Failed: Timeout\n",__func__); 
					break;
				 case NvOdmI2cStatus_SlaveNotFound:
				 default:
					printk("%s Failed: SlaveNotFound\n",__func__);
					break;             
			}
			continue;
		}  
		return num;
	}
	return -1;
}

static int i2c_transfer_ss(struct i2c_adapter *adap, struct i2c_msg *msgs,int num)
{
    int i;
	int result;
    NvOdmI2cTransactionInfo TransactionInfo[2];

    for(i=0;i<num;i++)
	{
		if(msgs[i].flags == 0)
		{
			TransactionInfo[i].Address = msgs[i].addr << 1;
			TransactionInfo[i].Buf = msgs[i].buf;
			TransactionInfo[i].Flags = NVODM_I2C_IS_WRITE;
			TransactionInfo[i].NumBytes = msgs[i].len;
			result = do_i2c_transfer(&TransactionInfo[i], 1);
		}
		else if(msgs[i].flags == I2C_M_RD)
		{
			TransactionInfo[i].Address = ((msgs[i].addr << 1) | 0x1);
			TransactionInfo[i].Buf = msgs[i].buf;
			TransactionInfo[i].Flags = 0;
			TransactionInfo[i].NumBytes = msgs[i].len;
			result = do_i2c_transfer(&TransactionInfo[i], 1);
		}
		else
		{
			printk("invalid i2c_msg\n");
		}
	}

	return result;
}

static int i2c_transfer_rs(struct i2c_adapter *adap, struct i2c_msg *msgs,int num)
{
    int i;    
    NvOdmI2cTransactionInfo TransactionInfo[2];

    for(i=0;i<num;i++)
	{
		if(msgs[i].flags == 0)
		{
			TransactionInfo[i].Address = msgs[i].addr << 1;
			TransactionInfo[i].Buf = msgs[i].buf;
			TransactionInfo[i].Flags = NVODM_I2C_IS_WRITE;
			TransactionInfo[i].NumBytes = msgs[i].len;    
		}
		else if(msgs[i].flags == I2C_M_RD)
		{
			TransactionInfo[i].Address = ((msgs[i].addr << 1) | 0x1);
			TransactionInfo[i].Buf = msgs[i].buf;
			TransactionInfo[i].Flags = 0;
			TransactionInfo[i].NumBytes = msgs[i].len;
		}
		else
		{
			printk("invalid i2c_msg\n");
		}
	}

	return do_i2c_transfer(&TransactionInfo[0], i);
}

static int is_ss_ic(__u16 addr)
{
	if(addr == 0x38  ||
	addr == 0x4A  ||
	addr == 0x30 )
	{
		return 1;
	}
	return 0;
}

static int my_i2c_transfer(struct i2c_adapter *adap, struct i2c_msg *msgs,int num)
{
	if(is_ss_ic(msgs[0].addr))
	{
		return i2c_transfer_ss(adap, msgs, num);
	}
	else
	{
		return i2c_transfer_rs(adap, msgs, num);
	}
}

static long hello_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct i2c_msg_from_user* msgs_from_user;
	struct i2c_msg_from_user drv_mirror_msg;
	struct app_msg_from_user* app_msgs_from_user;
	struct app_msg_from_user drv_mirror_app_msg;
	void* vaddr;

				
	printk(KERN_ALERT "+hello_ioctl+\n");
	switch(cmd)
	{
		case REGDRV_I2C_TX:
			msgs_from_user = (struct i2c_msg_from_user*)arg;
			ret = copy_i2c_params_from_user_space(msgs_from_user,&drv_mirror_msg);
			if(ret == 0 )
			{
				ret = my_i2c_transfer(NULL, drv_mirror_msg.msgs, drv_mirror_msg.len);
				if(ret >= 0)
				{
					ret = copy_result_to_user_space(msgs_from_user,&drv_mirror_msg);
				}
			}
		break;
		case REGDRV_APP_ACCESS:
			app_msgs_from_user = (struct app_msg_from_user*)arg;
			ret = copy_app_params_from_user_space(app_msgs_from_user,&drv_mirror_app_msg);

			if(ret == 0)
			{
				vaddr = ioremap_nocache(drv_mirror_app_msg.reg_phy_addr,drv_mirror_app_msg.len);

				if(vaddr)
				{
					ret = access_memory(vaddr,&drv_mirror_app_msg,app_msgs_from_user);


					iounmap(vaddr);
				}
				else
				{
					ret = -1;
				}
			}				
		break;
	}
	printk(KERN_ALERT "-hello_ioctl-\n");	
	return ret;
}

static int hello_open(struct inode *inode, struct file *file)
{
	int ret;
	printk(KERN_ALERT "+hello_open+\n");
	
	ret = nonseekable_open(inode, file);
	if (ret)
	{
		printk(KERN_ALERT "-hello_open-\n");
		return ret;
	}
	printk(KERN_ALERT "-hello_open-\n");
	return 0;
}

static int hello_release(struct inode *ignored, struct file *file)
{
	printk(KERN_ALERT "+hello_release+\n");
	printk(KERN_ALERT "-hello_release-\n");	
	return 0;
}

#define DRV_NAME		"Reg_Drv"

#define DEFINE_DEVICE_INFO(VAR, NAME) \
static struct drv_info VAR = { \
	.misc = { \
		.minor = MISC_DYNAMIC_MINOR, \
		.name = NAME, \
		.fops = &hello_fops, \
		.parent = NULL, \
	}, \
};

static struct file_operations hello_fops = {
	.owner = THIS_MODULE,
	.read = hello_read,
	.aio_write = hello_aio_write,
	.unlocked_ioctl = hello_ioctl,
	.compat_ioctl = hello_ioctl,
	.open = hello_open,
	.release = hello_release,
};

#if 0
#define i2ctest_name "msm-i2ckbd"

static const struct i2c_device_id i2ctest_idtable[] = {
       { i2ctest_name, 0 },
       { }
};

MODULE_DEVICE_TABLE(i2c, i2ctest_idtable);

static struct i2c_driver i2ctest_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name  = i2ctest_name,
	},
	.probe	  = i2ctest_probe,
	.remove	  = __devexit_p(i2ctest_remove),
	.suspend  = i2ctest_suspend,
	.resume   = i2ctest_resume,
	.id_table = i2ctest_idtable,
};
#endif

DEFINE_DEVICE_INFO(hello_drv, DRV_NAME)

#if 0
static int i2ctest_suspend(struct i2c_client *kbd, pm_message_t mesg)
{
	
	return 0;
}

static int i2ctest_resume(struct i2c_client *kbd)
{
	
	return 0;
}

static int __devexit i2ctest_remove(struct i2c_client *kbd)
{
	return 0;
}

static int __devinit i2ctest_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	client->driver = &i2ctest_driver;
	my_test_i2c_client = client;
	return 0;
}
#endif

static void get_i2c_handle(void)
{
    int i;
	NvU32 NumI2cConfigs;
    const NvU32 *pI2cConfigs;
	
	NvOdmQueryPinMux(NvOdmIoModule_I2c,&pI2cConfigs,&NumI2cConfigs);
	for(i=0;i<NumI2cConfigs;i++)
	{
		if(pI2cConfigs[i] == NvOdmI2cPinMap_Config1)
		{
			g_hOdmI2C[g_max_num_of_i2c_controller] = NvOdmI2cOpen(NvOdmIoModule_I2c, i);
			g_max_num_of_i2c_controller++;
		}
	}
	
	NvOdmQueryPinMux(NvOdmIoModule_I2c_Pmu,&pI2cConfigs,&NumI2cConfigs);
	for(i=0;i<NumI2cConfigs;i++)
	{
		if(pI2cConfigs[i] == NvOdmI2cPmuPinMap_Config1)
		{
			g_hOdmI2C[g_max_num_of_i2c_controller] = NvOdmI2cOpen(NvOdmIoModule_I2c_Pmu, i);
			g_max_num_of_i2c_controller++;
		}
	}	
}


static int hello_init(void)
{
	int ret;
	printk(KERN_ALERT "+hello_init+\n");

	ret = misc_register(&hello_drv.misc);
	if (unlikely(ret)) {
		printk(KERN_ERR "hello: failed to register misc "
		       "device '%s'!\n", hello_drv.misc.name);
		printk(KERN_ALERT "-hello_init-\n");
		return ret;
	}
	
	
	get_i2c_handle();
    printk(KERN_ALERT "-hello_init-\n");
    return ret;
}


static void hello_exit(void)
{
        printk(KERN_ALERT "+hello_exit+\n");
        misc_deregister(&hello_drv.misc);
        
        
        printk(KERN_ALERT "-hello_exit-\n");
}

module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Shakespeare");
