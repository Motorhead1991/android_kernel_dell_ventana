/*
 * arch/arm/mach-tegra/nvos_user.c
 *
 * User-land access to NvOs APIs
 *
 * Copyright (c) 2008-2009, NVIDIA Corporation.
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

#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/rwsem.h>
#include <mach/irqs.h>
#include "nvos.h"
#include "nvos_ioctl.h"
#include "nvassert.h"

#define wifi_mac_size 12
char wifi_mac[wifi_mac_size] = {0};
EXPORT_SYMBOL(wifi_mac);
static int __init wifi_mac_setup(char *options)
{
    if (!options || !*options)
        return 0;

    memcpy(wifi_mac, options, wifi_mac_size);

    return 0;
}
__setup("wifi_mac=", wifi_mac_setup);

static int wifi_mac_open(struct inode *inode, struct file *file);
static int wifi_mac_close(struct inode *inode, struct file *file);
static ssize_t wifi_mac_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos);

static const struct file_operations wifi_mac_fops =
{
    .owner = THIS_MODULE,
    .open = wifi_mac_open,
    .release = wifi_mac_close,
    .read = wifi_mac_read
};

static struct miscdevice wifi_mac_device =
{
    .name = "wifi_mac",
    .fops = &wifi_mac_fops,
    .minor = MISC_DYNAMIC_MINOR,
};

#define bt_mac_size 12
char bt_mac[bt_mac_size] = {0};
static int __init bt_mac_setup(char *options)
{
    if (!options || !*options)
        return 0;

    memcpy(bt_mac, options, bt_mac_size);

    return 0;
}
__setup("bt_mac=", bt_mac_setup);

static int bt_mac_open(struct inode *inode, struct file *file);
static int bt_mac_close(struct inode *inode, struct file *file);
static ssize_t bt_mac_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos);

static const struct file_operations bt_mac_fops =
{
    .owner = THIS_MODULE,
    .open = bt_mac_open,
    .release = bt_mac_close,
    .read = bt_mac_read
};

static struct miscdevice bt_mac_device =
{
    .name = "bt_mac",
    .fops = &bt_mac_fops,
    .minor = MISC_DYNAMIC_MINOR,
};

#define drm_key_size 512
char drm_key[drm_key_size] = {0};
static int __init drm_key_setup(char *options)
{
    if (!options || !*options)
        return 0;

    memcpy(drm_key, options, drm_key_size);

    return 0;
}
__setup("drm_key=", drm_key_setup);

static int drm_key_open(struct inode *inode, struct file *file);
static int drm_key_close(struct inode *inode, struct file *file);
static ssize_t drm_key_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos);

static const struct file_operations drm_key_fops =
{
    .owner = THIS_MODULE,
    .open = drm_key_open,
    .release = drm_key_close,
    .read = drm_key_read
};

static struct miscdevice drm_key_device =
{
    .name = "drm_key",
    .fops = &drm_key_fops,
    .minor = MISC_DYNAMIC_MINOR,
};

static int __init protected_data_init( void )
{
    int ret;

    printk("BootLog, +%s+\n", __func__);

    ret = misc_register(&wifi_mac_device);
    if (ret != 0) {
        pr_err("%s error 0x%x registering %s\n", __func__, ret, wifi_mac_device.name);
        return ret;
    }

    ret = misc_register(&bt_mac_device);
    if (ret != 0) {
        pr_err("%s error 0x%x registering %s\n", __func__, ret, bt_mac_device.name);
        return ret;
    }



    printk("BootLog, -%s-\n", __func__);

    return 0;
}

static void __exit protected_data_deinit( void )
{
    misc_deregister (&wifi_mac_device);
    misc_deregister (&bt_mac_device);
    misc_deregister (&drm_key_device);
}

static int wifi_mac_open(struct inode *inode, struct file *filp)
{
    printk("wifi_mac_open, -%s-\n", __func__);
    return 0;
}

static int wifi_mac_close(struct inode *inode, struct file *filp)
{
    printk("wifi_mac_close, -%s-\n", __func__);
    return 0;
}

static ssize_t wifi_mac_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
    int nRead = (count <= wifi_mac_size) ? count : wifi_mac_size;
    NvOsCopyOut(buf, wifi_mac, nRead);
    return 0;
}

static int bt_mac_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int bt_mac_close(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t bt_mac_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
    int nRead = (count <= bt_mac_size) ? count : bt_mac_size;
    NvOsCopyOut(buf, bt_mac, nRead);
    return 0;
}

static int drm_key_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static int drm_key_close(struct inode *inode, struct file *filp)
{
    return 0;
}

static ssize_t drm_key_read(struct file *file, char __user *buf, size_t count, loff_t *f_pos)
{
    int nRead = (count <= drm_key_size) ? count : drm_key_size;
    NvOsCopyOut(buf, drm_key, nRead);
    return 0;
}

module_init(protected_data_init);
module_exit(protected_data_deinit);
