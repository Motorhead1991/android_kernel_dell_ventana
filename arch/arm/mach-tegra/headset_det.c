#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/switch.h>
#include <linux/input.h>
#include <linux/wakelock.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/wait.h>
#include <mach/gpio.h>

#define DRIVER_VERSION "v1.1"
#define RECOVER_CHK_HEADSET_TYPE_TIMES    10

#define __LUNA_HEADSETIO 0xAA
#define LUNA_HEADSET_IOCTL_SET_CODEC_START  _IOW(__LUNA_HEADSETIO, 1, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_BIAS_ENABLE  _IOW(__LUNA_HEADSETIO, 2, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_BIAS_DISABLE _IOW(__LUNA_HEADSETIO, 3, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_FVS          _IOW(__LUNA_HEADSETIO, 4, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_LINEOUT_ON   _IOW(__LUNA_HEADSETIO, 5, struct luna_hdst0_info_t )
#define LUNA_HEADSET_IOCTL_SET_LINEOUT_OFF  _IOW(__LUNA_HEADSETIO, 6, struct luna_hdst0_info_t )

#define PM_LOG_EVENT(a,b)
#define PM_LOG_AUTIO_MIC 0
#define PM_LOG_ON 1
#define PM_LOG_OFF 0

#define LUNA_CODEC_HEADSET_DETECTION
#define DEBUG
#ifdef DEBUG
#define HEADSET_FUNCTION(fmt, arg...)  printk(KERN_INFO "hdst function %s: " fmt "\n" , __FUNCTION__ , ## arg)
#define HEADSET_INFO(fmt, arg...) printk(KERN_INFO "headset detection %s: " fmt "\n" , __FUNCTION__ , ## arg)
#define HEADSET_DBG(fmt, arg...)  printk(KERN_DEBUG "headset detection %s: " fmt "\n" , __FUNCTION__ , ## arg)
#define HEADSET_ERR(fmt, arg...)  printk(KERN_ERR  "headset detection %s: " fmt "\n" , __FUNCTION__ , ## arg)


int g_debounceDelay1         = 300;
int g_debounceDelay2         = 600;
int g_debounceDelayCodec     = 1000;
int g_debounceDelayHookkey   = 90;
int g_debounceDelayDock      = 65;

module_param(g_debounceDelay1, int, 0644);
module_param(g_debounceDelay2, int, 0644);
module_param(g_debounceDelayCodec, int, 0644);
module_param(g_debounceDelayHookkey, int, 0644);
module_param(g_debounceDelayDock, int, 0644);
#else

#define HEADSET_FUNCTION(fmt, arg...)  printk(KERN_INFO "hdst function %s: " fmt "\n" , __FUNCTION__ , ## arg)
#define HEADSET_INFO(fmt, arg...)  printk(KERN_INFO "headset detection %s: " fmt "\n" , __FUNCTION__ , ## arg)
#define HEADSET_DBG(fmt, arg...)  printk(KERN_DEBUG "headset detection %s: " fmt "\n" , __FUNCTION__ , ## arg)
#define HEADSET_ERR(fmt, arg...)  printk(KERN_ERR  "headset detection %s: " fmt "\n" , __FUNCTION__ , ## arg)
#endif


static struct workqueue_struct *g_detection_work_queue;


void cable_ins_det_work_func(struct work_struct *work);

void dock_det_work_func(struct work_struct *work);


void check_hook_key_state_work_func(struct work_struct *work);



DECLARE_DELAYED_WORK(cable_ins_det_work, cable_ins_det_work_func);

DECLARE_DELAYED_WORK(check_hook_key_state_work, check_hook_key_state_work_func);

DECLARE_DELAYED_WORK(dock_det_work, dock_det_work_func);

typedef enum  {
    HSD_IRQ_DISABLE,
    HSD_IRQ_ENABLE
}irq_status;

struct headset_info{
    unsigned int    jack_gpio;
    unsigned int    hook_gpio;
    unsigned int    dock_gpio;
    unsigned int    jack_irq;
    unsigned int    hook_irq;
    unsigned int    dock_irq;
    unsigned int    jack_status;
    unsigned int    dock_status;
    unsigned int    debounceDelay1;
    unsigned int    debounceDelay2;
    unsigned int    debounceDelayCodec;
    unsigned int    debounceDelayHookKey;
    unsigned int    debounceDelayDock;
    irq_status      hook_irq_status;        
    irq_status      jack_irq_status;        
    irq_status      dock_irq_status;
    struct switch_dev   sdev;
    struct input_dev    *input;
    atomic_t        is_button_press;
    unsigned int    recover_chk_times;
    
    struct wake_lock headset_det_wakelock;
    

    struct switch_dev dock_sdev;
    struct switch_dev dock_mode_sdev;
    struct switch_dev fvs_mode_sdev;
    struct switch_dev hdst0;
    wait_queue_head_t notify_wq;
    bool            codec_bias_enable;
    int             fvs_mode;
} ;


typedef enum  {
    HSD_NONE = 0,
    HSD_HEADSET = 1, 
    HSD_EAR_PHONE = 2 
}headset_type;

struct luna_hdst0_info_t {
    uint32_t   val;
};

static void fvs_debugfs_init(struct headset_info *hi);

static struct headset_info    headset_data;

headset_type g_headset_type;

MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR("Terry.Cheng");
MODULE_DESCRIPTION("Headset detection");
MODULE_LICENSE("GPL v2");

static int hdst0_misc_open(struct inode *inode, struct file *fp)
{
    int result = 0;
    HEADSET_FUNCTION("");
    return result;
}

static int hdst0_misc_release(struct inode *inode, struct file *fp)
{
    int result = 0;
    HEADSET_FUNCTION("");
    return result;
}

static long hdst0_misc_ioctl( struct file *fp,
                           unsigned int cmd,
                           unsigned long arg )
{   
    int  result = 0;
    HEADSET_DBG("+hdst0_misc_ioctl, cmd: %x, %x", cmd, LUNA_HEADSET_IOCTL_SET_FVS);
    switch(cmd)
    {
        case LUNA_HEADSET_IOCTL_SET_CODEC_START:
        {
            HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_CODEC_START");
            switch_set_state(&headset_data.dock_mode_sdev, 8);
            break;
        }
        case LUNA_HEADSET_IOCTL_SET_BIAS_ENABLE:
        {
            HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_BIAS_ENABLE");
            headset_data.codec_bias_enable = true;
            wake_up(&headset_data.notify_wq);
            break;
        }
        case LUNA_HEADSET_IOCTL_SET_BIAS_DISABLE:
        {
            HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_BIAS_DISABLE");
            headset_data.codec_bias_enable = false;
            wake_up(&headset_data.notify_wq);
            break;
        }
        case LUNA_HEADSET_IOCTL_SET_FVS:
        {
            HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_FVS");
            {
            struct luna_hdst0_info_t fvs_mode;
            if( copy_from_user( (void *)&fvs_mode, (void *)arg, sizeof(struct luna_hdst0_info_t)) )
            {
                printk("debug_write_fvs_mode: copy data failed\n");
                return -1;
            }
            printk("debug_write_fvs_mode: switch_set_state => %d\n", fvs_mode.val);

            headset_data.fvs_mode = fvs_mode.val;

            switch_set_state(&headset_data.fvs_mode_sdev, fvs_mode.val);
            }
            break;
        }
        case LUNA_HEADSET_IOCTL_SET_LINEOUT_ON:
        {
            HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_LINEOUT %d", 1);
            switch_set_state(&headset_data.dock_mode_sdev, 1);
            break;
        }
        case LUNA_HEADSET_IOCTL_SET_LINEOUT_OFF:
        {
            HEADSET_DBG("LUNA_HEADSET_IOCTL_SET_LINEOUT %d", 0);
            switch_set_state(&headset_data.dock_mode_sdev, 0);
            break;
        }
    } 
    return result;
}

static void button_pressed(void)
{
    HEADSET_INFO("");
    input_report_key(headset_data.input, KEY_MEDIA, 1);
    input_sync(headset_data.input);
}

static void button_released(void)
{
    HEADSET_INFO("");
    input_report_key(headset_data.input, KEY_MEDIA, 0);
    input_sync(headset_data.input);
}

static int set_codec_bias_enable(bool enable)
{
    int ret = 0;

    if (headset_data.debounceDelayCodec != g_debounceDelayCodec)
    {
        headset_data.debounceDelayCodec = g_debounceDelayCodec;
    }
    switch_set_state(&headset_data.hdst0, (enable == true) ? 1 : 0);
    if (enable == true)
    {
        printk("+%s %d\n", __func__, enable);
        ret = wait_event_timeout(headset_data.notify_wq, (headset_data.codec_bias_enable == enable), msecs_to_jiffies(headset_data.debounceDelayCodec));
        printk("%s ret: %d\n", __func__, ret);
    }
    if (ret == 0)
    {
    }
    return 0;
}

static void remove_headset(void)
{
    int rc = 0;
    unsigned long irq_flags;

    HEADSET_FUNCTION("");
    
    set_codec_bias_enable(false);
    if (HSD_HEADSET == g_headset_type)
    {
#if 0
        
        gpio_set_value(AUDIO_HEADSET_MIC_SHTDWN_N, 1);
#endif
        HEADSET_INFO("Turn off mic en");

        
        PM_LOG_EVENT(PM_LOG_OFF, PM_LOG_AUTIO_MIC);
        

        if ( 1 == atomic_read(&headset_data.is_button_press))
        {
            
            button_released();
            atomic_set(&headset_data.is_button_press, 0);
        }
        if ( HSD_IRQ_ENABLE == headset_data.hook_irq_status)
        {
            local_irq_save(irq_flags);
            
            
            disable_irq_nosync(headset_data.hook_irq);
            headset_data.hook_irq_status = HSD_IRQ_DISABLE;
            set_irq_wake(headset_data.hook_irq, 0);
            local_irq_restore(irq_flags);
        }
    }
    g_headset_type = HSD_NONE;
    
    rc = set_irq_type(headset_data.jack_irq, IRQF_TRIGGER_LOW);
    if (rc)
        HEADSET_ERR("change IRQ detection type as low fail!!");
    
    headset_data.recover_chk_times = RECOVER_CHK_HEADSET_TYPE_TIMES;

    HEADSET_INFO("Headset remove, jack_irq active LOW");
}

static void insert_headset(void)
{
    int hook_key_status = 0;
    int rc = 0;
    unsigned long irq_flags;
    int need_update_path = 0;
    
#ifdef LUNA_CODEC_HEADSET_DETECTION
    set_codec_bias_enable(true);
#endif
    HEADSET_INFO("Turn on mic en");
    
    
    PM_LOG_EVENT(PM_LOG_ON, PM_LOG_AUTIO_MIC);
    
    msleep(headset_data.debounceDelay2);

    
    if( 1 == gpio_get_value(headset_data.jack_gpio))
    {
        HEADSET_INFO("Headset removed while detection\n");
        return;
    }
    

    hook_key_status = gpio_get_value(headset_data.hook_gpio);
    if ( 1 == hook_key_status )
    {
        
        g_headset_type = HSD_EAR_PHONE;
        
#if 0
        gpio_set_value(AUDIO_HEADSET_MIC_SHTDWN_N, 1);
#endif
        HEADSET_INFO("Turn off mic en");
        
        
        PM_LOG_EVENT(PM_LOG_OFF, PM_LOG_AUTIO_MIC);

        if (headset_data.recover_chk_times > 0)
        {
            headset_data.recover_chk_times--;
            
            HEADSET_INFO("Start recover timer, recover_chk_times = %d", headset_data.recover_chk_times);
            queue_delayed_work(g_detection_work_queue,&cable_ins_det_work, (HZ/10));
        }
    }
    else
    {

        
        msleep(headset_data.debounceDelay1);
        
        if( 1 == gpio_get_value(headset_data.jack_gpio))
        {
            HEADSET_INFO("Headset removed while detection\n");
            return;
        }
        
        
        
        if ( g_headset_type == HSD_EAR_PHONE)
        {
            need_update_path = 1;
        }
        
        g_headset_type = HSD_HEADSET;
        if(1 == need_update_path)
        {
            
            
            
            
            
            HEADSET_INFO("need_update_path = 1, switch audio path to CAD_HW_DEVICE_ID_HEADSET_MIC ");
        }
        
        rc = set_irq_type(headset_data.hook_irq, IRQF_TRIGGER_HIGH);
        if (rc)
            HEADSET_ERR("Set hook key interrupt detection type as rising Fail!!!");
        
        if(HSD_IRQ_DISABLE == headset_data.hook_irq_status)
        {
            local_irq_save(irq_flags);
            
            enable_irq(headset_data.hook_irq);
            headset_data.hook_irq_status = HSD_IRQ_ENABLE;
            set_irq_wake(headset_data.hook_irq, 1);
            local_irq_restore(irq_flags);
        }
    }
    
    HEADSET_INFO("g_headset_type= %d", g_headset_type);
    
    rc = set_irq_type(headset_data.jack_irq, IRQF_TRIGGER_HIGH);
    if (rc)
        HEADSET_ERR(" change IRQ detection type as high active fail!!");

}

void dock_det_work_func(struct work_struct *work)
{
    int rc = 0;

    
    headset_data.dock_status  = gpio_get_value(headset_data.dock_gpio);

    if( 0 == headset_data.dock_status)
    {
        HEADSET_INFO("dock detection status 0");
        headset_data.dock_irq_status = HSD_IRQ_ENABLE;

        switch_set_state(&headset_data.dock_sdev, 1);

        rc = set_irq_type(headset_data.dock_irq, IRQF_TRIGGER_HIGH);
        if (rc)
        {
            HEADSET_ERR("Fail to set_irq_type to IRQF_TRIGGER_HIGH!!!");
        }
        enable_irq(headset_data.dock_irq);
    }
    else
    {
        HEADSET_INFO("dock detection status 1");
        headset_data.dock_irq_status = HSD_IRQ_ENABLE;

        switch_set_state(&headset_data.dock_sdev, 0);

        rc = set_irq_type(headset_data.dock_irq, IRQF_TRIGGER_LOW);
        if (rc)
        {
            HEADSET_ERR("Fail to set_irq_type to IRQF_TRIGGER_LOW!!!");
        }
        enable_irq(headset_data.dock_irq);
    }
}

void cable_ins_det_work_func(struct work_struct *work)
{
    unsigned long irq_flags;

    
    
    HEADSET_FUNCTION("");
    
    headset_data.jack_status  = gpio_get_value(headset_data.jack_gpio);

    if( 0 == headset_data.jack_status)
    {
        if (g_headset_type != 0)
        {
            HEADSET_INFO("g_headset_type == %x, no need to do insert_headset", g_headset_type);
#if 0
            queue_delayed_work(g_detection_work_queue,&cable_ins_det_work, (HZ/10));
            return;
#endif
        }
        
        
        HEADSET_INFO("lock headset wakelock when doing headset insert detection");
        
        wake_lock_timeout(&headset_data.headset_det_wakelock, (2*HZ) );
        insert_headset();
    }
    else
    {
        
        
        HEADSET_INFO("lock headset wakelock when doing headset remove detection");
        wake_lock_timeout(&headset_data.headset_det_wakelock, HZ );
        remove_headset();
    }
    

    if ( HSD_IRQ_DISABLE == headset_data.jack_irq_status)
    {
        local_irq_save(irq_flags);
        
        enable_irq(headset_data.jack_irq);
        headset_data.jack_irq_status = HSD_IRQ_ENABLE;
        local_irq_restore(irq_flags);
    }

    HEADSET_INFO("switch_set_state ==> headset %d", g_headset_type);
    
    switch_set_state(&headset_data.sdev, g_headset_type);
}


void check_hook_key_state_work_func(struct work_struct *work)
{
    int hook_key_status = 0;
    int rc = 0;
    unsigned long irq_flags;

    HEADSET_DBG("");
#if 0 
    if ( 1 == atomic_read(&headset_data.is_button_press))
    {
        HEADSET_INFO("skip hook while jack handling!!");
        return;
    }
#endif
    if (headset_data.fvs_mode != 0)
    {
        HEADSET_ERR("hook key not work in FVS mode\n");
        return;
    }
    if ( HSD_HEADSET != g_headset_type )
    {
        HEADSET_INFO("Headset remove!! or may ear phone noise !!");
        
        return;
    }
    hook_key_status = gpio_get_value(headset_data.hook_gpio);
    if ( 1 == hook_key_status )
    {
        
        button_pressed();
        atomic_set(&headset_data.is_button_press, 1);
        
        
        rc = set_irq_type(headset_data.hook_irq, IRQF_TRIGGER_LOW);
        if (rc)
            HEADSET_ERR( "change hook key detection type as low fail!!");

    }
    else
    {
        if ( 1 == atomic_read(&headset_data.is_button_press))
        {
            
            button_released();
            atomic_set(&headset_data.is_button_press, 0);
            
            
            rc = set_irq_type(headset_data.hook_irq, IRQF_TRIGGER_HIGH);
            HEADSET_DBG("Hook Key release change hook key detection type as high");
            if (rc)
                HEADSET_ERR("change hook key detection type as high fail!!");
        }
    }
    
    if ( HSD_IRQ_DISABLE == headset_data.hook_irq_status)
    {
        local_irq_save(irq_flags);
        
        enable_irq(headset_data.hook_irq);
        headset_data.hook_irq_status = HSD_IRQ_ENABLE;
        set_irq_wake(headset_data.hook_irq, 1);
        local_irq_restore(irq_flags);

    }
}

static irqreturn_t hook_irqhandler(int irq, void *dev_id)
{
    unsigned long irq_flags;

    HEADSET_FUNCTION("");
    local_irq_save(irq_flags);
    
    
    disable_irq_nosync(headset_data.hook_irq);
    headset_data.hook_irq_status = HSD_IRQ_DISABLE;
    set_irq_wake(headset_data.hook_irq, 0);
    local_irq_restore(irq_flags);

    if (headset_data.debounceDelayHookKey != g_debounceDelayHookkey)
    {
        headset_data.debounceDelayHookKey = g_debounceDelayHookkey;
    }

    
    
    queue_delayed_work(g_detection_work_queue,&check_hook_key_state_work, msecs_to_jiffies(headset_data.debounceDelayHookKey));

    return IRQ_HANDLED;
}


static irqreturn_t dock_irqhandler(int irq, void *dev_id)
{
    unsigned long irq_flags;

    HEADSET_FUNCTION("");
    local_irq_save(irq_flags);
    disable_irq_nosync(headset_data.dock_irq);
    headset_data.jack_irq_status = HSD_IRQ_DISABLE;
    local_irq_restore(irq_flags);

    queue_delayed_work(g_detection_work_queue,&dock_det_work, msecs_to_jiffies(headset_data.debounceDelayDock));
    return IRQ_HANDLED;
}

static irqreturn_t jack_irqhandler(int irq, void *dev_id)
{
    unsigned long irq_flags;

    HEADSET_FUNCTION("");
    local_irq_save(irq_flags);
    
    
    disable_irq_nosync(headset_data.jack_irq);
    headset_data.jack_irq_status = HSD_IRQ_DISABLE;
    local_irq_restore(irq_flags);

    queue_delayed_work(g_detection_work_queue,&cable_ins_det_work, 0);
    return IRQ_HANDLED;
}

static ssize_t headset_print_state(struct switch_dev *sdev, char *buf)
{

    switch (switch_get_state(&headset_data.fvs_mode_sdev)) {
    case 0:
        return sprintf(buf, "No Device\n");
    case 1:
        return sprintf(buf, "Headset\n");
    }

    return -EINVAL;
}

static ssize_t switch_fvs_mode_print_state(struct switch_dev *sdev, char *buf)
{

    switch (switch_get_state(&headset_data.fvs_mode_sdev)) {
    case 0:
        return sprintf(buf, "FVS_0\n");
    case 1:
        return sprintf(buf, "FVS_1\n");
    case 2:
        return sprintf(buf, "FVS_2\n");
    }

    return -EINVAL;
}

static ssize_t switch_hds_print_state(struct switch_dev *sdev, char *buf)
{

    switch (switch_get_state(&headset_data.sdev)) {
    case HSD_NONE:
        return sprintf(buf, "No Device\n");
    case HSD_HEADSET:
        return sprintf(buf, "Headset\n");
    case HSD_EAR_PHONE:
        return sprintf(buf, "Ear Phone");
    }

    return -EINVAL;
}

static struct file_operations hdst0_misc_fops = {
    .owner  = THIS_MODULE,
    .open   = hdst0_misc_open,
    .release = hdst0_misc_release,
    .unlocked_ioctl = hdst0_misc_ioctl,
};

static struct miscdevice hdst0_misc_device = {
    .minor  = MISC_DYNAMIC_MINOR,
    .name   = "hdst0",
    .fops   = &hdst0_misc_fops,
};


static int __init_or_module headset_probe(struct platform_device *pdev)
{

    int ret;
    struct resource *res;
    unsigned int    irq;

    
    memset(&headset_data, 0, sizeof( headset_data));

    ret = misc_register( &hdst0_misc_device );
    if( ret )
    {
        printk(KERN_ERR "%s failed to register misc devices\n", __func__);
    }

    init_waitqueue_head(&headset_data.notify_wq);

    
    headset_data.dock_sdev.name = "dock";

    ret = switch_dev_register(&headset_data.dock_sdev);
    if (ret < 0)
    {
        goto err_switch_dev_register;
    }

    
    headset_data.dock_mode_sdev.name = "dock_mode";

    ret = switch_dev_register(&headset_data.dock_mode_sdev);
    if (ret < 0)
    {
        goto err_switch_dev_register;
    }

    
    headset_data.sdev.name = pdev->name;
    headset_data.sdev.print_name = switch_hds_print_state;

    ret = switch_dev_register(&headset_data.sdev);
    if (ret < 0)
    {
        goto err_switch_dev_register;
    }

    
    headset_data.hdst0.name = "hdst0";
    headset_data.hdst0.print_name = headset_print_state;

    ret = switch_dev_register(&headset_data.hdst0);
    if (ret < 0)
    {
        goto err_switch_dev_register;
    }

    
    headset_data.fvs_mode_sdev.name = "FVS";
    headset_data.fvs_mode_sdev.print_name = switch_fvs_mode_print_state;

    ret = switch_dev_register(&headset_data.fvs_mode_sdev);
    if (ret < 0)
    {
        goto err_switch_dev_register;
    }

    headset_data.debounceDelay1         = g_debounceDelay1;         
    headset_data.debounceDelay2         = g_debounceDelay2;         
    headset_data.debounceDelayCodec     = g_debounceDelayCodec;
    headset_data.debounceDelayHookKey   = g_debounceDelayHookkey;   
    headset_data.debounceDelayDock      = g_debounceDelayDock;
    headset_data.jack_status = 0;
    headset_data.recover_chk_times = RECOVER_CHK_HEADSET_TYPE_TIMES;
    headset_data.codec_bias_enable = false;
    headset_data.fvs_mode = 0;

    g_headset_type = HSD_NONE;
    atomic_set(&headset_data.is_button_press, 0);

    
    printk(KERN_INFO "Init headset wake lock\n");
    wake_lock_init(&headset_data.headset_det_wakelock, WAKE_LOCK_SUSPEND, "headset_det");    
    

    g_detection_work_queue = create_workqueue("headset_detection");
    if (g_detection_work_queue == NULL) {
        ret = -ENOMEM;
        goto err_create_work_queue;
    }

    res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
                "dock_detect");
    if (!res) {
        HEADSET_ERR("couldn't find dock detect\n");
    }
    headset_data.dock_gpio = res->start;

    ret = gpio_request(headset_data.dock_gpio, "dock_detection");
    if (ret < 0)
    {
        HEADSET_ERR("couldn't request dock detect\n");
    }

    ret = gpio_direction_input(headset_data.dock_gpio);
    if (ret < 0)
    {
        HEADSET_ERR("couldn't gpio_direction_input dock detect\n");
    }

    
    irq = gpio_to_irq(headset_data.dock_gpio);
    if (irq <0)
    {
        ret = irq;
        HEADSET_ERR("couldn't get IRQ number for dock detect\n");
    }
    headset_data.dock_irq = irq;

    
    headset_data.dock_irq_status = HSD_IRQ_ENABLE;
    
    ret = request_irq(headset_data.dock_irq, dock_irqhandler, IRQF_TRIGGER_LOW, "dock_detection", &headset_data);
    if (ret < 0)
    {
        HEADSET_ERR("Fail to request dock irq");
    }

#if 0
    ret = set_irq_wake(headset_data.jack_irq, 1);
    if (ret < 0)
    {
        HEADSET_ERR("Fail to set_irq_wake for dock irq");
    }
#endif

    res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
                "hook_int");
    if (!res) {
        HEADSET_ERR("couldn't find hook int\n");
        ret = -ENODEV;
        goto err_get_hook_gpio_resource;
    }
    headset_data.hook_gpio = res->start;

    ret = gpio_request(headset_data.hook_gpio, "headset_hook");
    if (ret < 0)
        goto err_request_hook_gpio;

    ret = gpio_direction_input(headset_data.hook_gpio);
    if (ret < 0)
        goto err_set_hook_gpio;

    
    irq = gpio_to_irq(headset_data.hook_gpio);
    if (irq <0)
    {
        ret = irq;
        goto err_get_hook_detect_irq_num_failed;
    }
    headset_data.hook_irq = irq;

    
    
    set_irq_flags(headset_data.hook_irq, IRQF_VALID | IRQF_NOAUTOEN);
    ret = request_irq(headset_data.hook_irq, hook_irqhandler, IRQF_TRIGGER_NONE, "headset_hook", &headset_data);
    if( ret < 0)
    {
        HEADSET_ERR("Fail to request hook irq");
        goto err_request_austin_headset_hook_irq;
    }

    res = platform_get_resource_byname(pdev, IORESOURCE_IRQ,
                "jack_int");
    if (!res) {
        HEADSET_ERR("couldn't find jack int\n");
        ret = -ENODEV;
        goto err_get_jack_int_resource;
    }
    headset_data.jack_gpio = res->start;

    ret = gpio_request(headset_data.jack_gpio, "headset_jack");
    if (ret < 0)
        goto err_request_jack_gpio;

    ret = gpio_direction_input(headset_data.jack_gpio);
    if (ret < 0)
        goto err_set_jack_gpio;

    
    
    irq = gpio_to_irq(headset_data.jack_gpio);
    if (irq <0)
    {
        ret = irq;
        goto err_get_jack_detect_irq_num_failed;
    }
    headset_data.jack_irq = irq;


    
    headset_data.jack_irq_status = HSD_IRQ_ENABLE;
    ret = request_irq(headset_data.jack_irq, jack_irqhandler, IRQF_TRIGGER_LOW, "headset_jack", &headset_data);
    if (ret < 0)
    {
        HEADSET_ERR("Fail to request jack irq");
        goto err_request_austin_headset_jack_irq;
    }

    ret = set_irq_wake(headset_data.jack_irq, 1);
    if (ret < 0)
        goto err_request_input_dev;


    headset_data.input = input_allocate_device();
    if (!headset_data.input) {
        ret = -ENOMEM;
        goto err_request_input_dev;
    }
    headset_data.input->name = "Luna headset";
    headset_data.input->evbit[0] = BIT_MASK(EV_KEY);
    set_bit(KEY_MEDIA, headset_data.input->keybit);
    ret = input_register_device(headset_data.input);
    if (ret < 0)
    {
        HEADSET_ERR("Fail to register input device");
        goto err_register_input_dev;
    }

    fvs_debugfs_init(&headset_data);
    
    
    PM_LOG_EVENT(PM_LOG_OFF, PM_LOG_AUTIO_MIC);

    return 0;
err_register_input_dev:
    input_free_device(headset_data.input);
err_request_input_dev:
    free_irq(headset_data.jack_irq, 0);
err_request_austin_headset_jack_irq:
err_get_jack_detect_irq_num_failed:
err_set_jack_gpio:
    gpio_free(headset_data.jack_gpio);
err_request_jack_gpio:
err_get_jack_int_resource:
    free_irq(headset_data.hook_irq, 0);
err_request_austin_headset_hook_irq:
err_get_hook_detect_irq_num_failed:
err_set_hook_gpio:
     gpio_free(headset_data.hook_gpio);
err_request_hook_gpio:
err_get_hook_gpio_resource:
    destroy_workqueue(g_detection_work_queue);
err_create_work_queue:
    switch_dev_unregister(&headset_data.sdev);
    switch_dev_unregister(&headset_data.dock_sdev);
    switch_dev_unregister(&headset_data.dock_mode_sdev);
    switch_dev_unregister(&headset_data.fvs_mode_sdev);
    switch_dev_unregister(&headset_data.hdst0);
err_switch_dev_register:
    HEADSET_ERR(" Failed to register driver");

    return ret;

}

static int headset_remove(struct platform_device *pdev)
{
    input_unregister_device(headset_data.input);
    free_irq(headset_data.hook_irq, 0);
    free_irq(headset_data.jack_irq, 0);
    destroy_workqueue(g_detection_work_queue);
    switch_dev_unregister(&headset_data.sdev);
    return 0;
}

static struct platform_driver headset_driver =
{
    .probe = headset_probe,
    .remove = headset_remove,
    .driver = {
        .name = "headset",
        .owner = THIS_MODULE,
    },
};


static int __init headset_init(void)
{
    int ret;

    printk(KERN_INFO "BootLog, +%s\n", __func__);
    ret = platform_driver_register(&headset_driver);

    printk(KERN_INFO "BootLog, -%s, ret=%d\n", __func__, ret);
    return ret;
}


static void __exit headset_exit(void)
{

    HEADSET_DBG("Poweroff, +");
    platform_driver_unregister(&headset_driver);

}

#if defined(CONFIG_DEBUG_FS)

#define FVS_MODE_STR "QISDA:AUDIO_FVS_MODE:"

static ssize_t debug_write_fvs_mode(struct file *file, const char __user *buf,
                 size_t count, loff_t *ppos)
{
    char kbuf[128];
    char testchar;

    memset(kbuf, 0, 128);
    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    if (strncmp(kbuf, FVS_MODE_STR, strlen(FVS_MODE_STR) - 1))
    {
        printk("%s invalid string for fvs mode : %s\n", __func__, kbuf);
        return -EINVAL;
    }

    testchar = kbuf[strlen(FVS_MODE_STR)] - '0';

    {
    printk("debug_write_fvs_mode: switch_set_state => %d\n", testchar);
    switch_set_state(&headset_data.fvs_mode_sdev, testchar);
    }

    return count;
}

static int debug_open(struct inode *inode, struct file *file)
{
    file->private_data = inode->i_private;
    return 0;
}

const struct file_operations debug_fvs_mode_ops = {
    .open = debug_open,
    .write = debug_write_fvs_mode,
};

static void fvs_debugfs_init(struct headset_info *hi)
{
    struct dentry *dent;
    dent = debugfs_create_dir("audiotest", 0);
    if (IS_ERR(dent))
        return;

    debugfs_create_file("fvs_mode", 0222, dent, hi, &debug_fvs_mode_ops);
}
#else
static void fvs_debugfs_init(struct headset_info *hi) {}
#endif
module_init(headset_init);
module_exit(headset_exit);

