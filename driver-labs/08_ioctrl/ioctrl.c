#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/timer.h>


// #define RTC_AIE_ON	_IO('p', 0x01)	/* Alarm int. enable on		*/
// #define RTC_AIE_OFF	_IO('p', 0x02)	/* ... off			*/
// #define RTC_UIE_ON	_IO('p', 0x03)	/* Update int. enable on	*/
// #define RTC_UIE_OFF	_IO('p', 0x04)	/* ... off			*/
// #define RTC_PIE_ON	_IO('p', 0x05)	/* Periodic int. enable on	*/
// #define RTC_PIE_OFF	_IO('p', 0x06)	/* ... off			*/
// #define RTC_WIE_ON	_IO('p', 0x0f)  /* Watchdog int. enable on	*/
// #define RTC_WIE_OFF	_IO('p', 0x10)  /* ... off			*/

// #define RTC_ALM_SET	_IOW('p', 0x07, struct rtc_time) /* Set alarm time  */
// #define RTC_ALM_READ	_IOR('p', 0x08, struct rtc_time) /* Read alarm time */
// #define RTC_RD_TIME	_IOR('p', 0x09, struct rtc_time) /* Read RTC time   */
// #define RTC_SET_TIME	_IOW('p', 0x0a, struct rtc_time) /* Set RTC time    */
// #define RTC_IRQP_READ	_IOR('p', 0x0b, unsigned long)	 /* Read IRQ rate   */
// #define RTC_IRQP_SET	_IOW('p', 0x0c, unsigned long)	 /* Set IRQ rate    */
// #define RTC_EPOCH_READ	_IOR('p', 0x0d, unsigned long)	 /* Read epoch      */
// #define RTC_EPOCH_SET	_IOW('p', 0x0e, unsigned long)	 /* Set epoch       */

#define cmdname  'j'
#define TIME_ON _IO(cmdname, 0x01)             //定时器开启
#define TIME_OFF _IO(cmdname, 0x02)            //定时器关闭
#define TIME_TOGGLE _IOW(cmdname, 0x03, unsigned long)   //定时器设置周期


#define TIMER_CNT 1
#define TIMER_NAME "timer"

#define LED_NAME "dts_led"
#define LED_NODE_PATH "/leds/status-led"

#define LED_OFF 0
#define LED_ON 1
#define LED_TOGGLE 2

//timer设备结构体
struct timer_dev
{
    dev_t devt; //设备号
    int major; //主设备号
    int minor; //次设备号
    struct cdev timer_cdev;//字符设备结构体
    struct class *timer_class;//类结构体指针
    struct device *timer_device;//设备结构体指针
    struct device_node *timer_node;//设备节点结构体指针
    struct timer_list timer;//定时器结构体
    //定时器时间，单位为jiffies
    unsigned long timer_period ; //默认500ms
    struct gpio_desc *gpio;//GPIO描述结构体指针

};


static struct timer_dev timerdev;
 
static void timer_callback(struct timer_list *timer)
{
    static bool led_state = false;

    //切换LED状态
    led_state = !led_state; 
    gpiod_set_value(timerdev.gpio, led_state);
    printk("timer callback function called\n");
    //重新设置定时器时间
    mod_timer(timer, jiffies + msecs_to_jiffies(timerdev.timer_period)); //设置定时器时间为500ms
}


static int timer_open(struct inode *inode, struct file *file)
{

    return 0;
}

static int timer_release(struct inode *inode, struct file *file)
{
 
    return 0;
}

static ssize_t timer_read(struct file *file, char __user *buf,
                        size_t count, loff_t *ppos)
{


    return 0;
}

static ssize_t timer_write(struct file *file,
                         const char __user *buf,
                         size_t count, loff_t *ppos)
{

    return 0;
}
static long timer_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    

	switch (cmd) {
    case TIME_ON:
        //开启定时器
        mod_timer(&timerdev.timer, jiffies + msecs_to_jiffies(timerdev.timer_period));
        printk("timer on\n");
        break;
    case TIME_OFF:
        //关闭定时器
        del_timer(&timerdev.timer);
        printk("timer off\n");
        break;
    case TIME_TOGGLE:
        //设置定时器周期
        if (copy_from_user(&timerdev.timer_period, (void __user *)arg, sizeof(timerdev.timer_period))) {
            return -EFAULT;
        }
        printk("set period to %lu\n", timerdev.timer_period);
        break;
    default:
        return -ENOTTY;
	}
    return 0;
}

// long (*unlocked_ioctl) (struct file *, unsigned int, unsigned long);
static const struct file_operations timer_fops = {
    .owner   = THIS_MODULE,
    .open    = timer_open,
    .read    = timer_read,
    .write   = timer_write,
    .release = timer_release,
    .unlocked_ioctl	= timer_ioctl,
};


static int __init timer_init(void)
{
    int ret;

    int  flags;
    const char *default_state = "off";

    //获取设备节点
    timerdev.timer_node = of_find_node_by_path(LED_NODE_PATH);
    if (!timerdev.timer_node) {
        ret = -ENODEV;
        goto fail_node;
    }



    if (of_find_property(timerdev.timer_node, "default-state", NULL)) {
		ret = of_property_read_string(timerdev.timer_node, "default-state", &default_state);
		if (ret)
			goto err_put_node;
	}

    //根据默认状态设置GPIO标志位
    if (!strcmp(default_state, "on")) {
		flags = GPIOD_OUT_HIGH;
	} else if (!strcmp(default_state, "off")) {
		flags = GPIOD_OUT_LOW;
	} else if (!strcmp(default_state, "keep")) {
		flags = GPIOD_ASIS;
	} else {
		pr_err("dts_led: invalid default-state: %s\n", default_state);
		ret = -EINVAL;
		goto err_put_node;
	}

    //获取GPIO描述
    timerdev.gpio = fwnode_gpiod_get_index(of_fwnode_handle(timerdev.timer_node), NULL,0, flags, LED_NAME);//获取gpio//index=0 表示第一根 GPIO。//flags=GPIO_OUT_HIGH 表示输出高电平
    if (IS_ERR(timerdev.gpio)) {
        printk("f_gpio_get_index failed\n");
        ret = PTR_ERR(timerdev.gpio);
        goto err_put_node;
    }



    /* 注册字符设备驱动 */
    if(timerdev.major) {
        timerdev.devt = MKDEV(timerdev.major, 0);
        ret = register_chrdev_region(timerdev.devt, TIMER_CNT, TIMER_NAME);
    } else {
        ret = alloc_chrdev_region(&timerdev.devt, 0, TIMER_CNT, TIMER_NAME);
        timerdev.major = MAJOR(timerdev.devt);
        timerdev.minor = MINOR(timerdev.devt);
    }
    if (ret < 0) {
        printk("register_chrdev_region faitimer\n");
        goto fail_devid;
    }
    timerdev.timer_cdev.owner = THIS_MODULE;
    cdev_init(&timerdev.timer_cdev, &timer_fops);

    ret = cdev_add(&timerdev.timer_cdev, timerdev.devt, TIMER_CNT);

     
    if (ret < 0) {
        printk("cdev_add faitimer\n");
        goto fail_cdevadd;
    }

    timerdev.timer_class = class_create("timer");
    if (IS_ERR(timerdev.timer_class)) {
        printk("class_create faitimer\n");
        goto fail_class;
    }


    timerdev.timer_device = device_create(timerdev.timer_class, NULL, timerdev.devt, NULL, "timer");
    if (IS_ERR(timerdev.timer_device)) {
        printk("device_create faitimer\n");
        goto fail_device;
    }
    printk("timer major: %d, minor: %d\n", timerdev.major, MINOR(timerdev.devt));
    



    //初始化定时器
    /* 初始化定时器，指定到期后执行的函数 */
    timer_setup(&timerdev.timer, timer_callback, 0);
    timerdev.timer_period = 500; //默认500ms
    /* 启动定时器：首次在约 5 秒后到期 */
    mod_timer(&timerdev.timer, jiffies + msecs_to_jiffies(timerdev.timer_period));
    
    return 0;
fail_device:
    class_destroy(timerdev.timer_class);
fail_class:
    cdev_del(&timerdev.timer_cdev);
fail_cdevadd:
    unregister_chrdev_region(timerdev.devt, TIMER_CNT);
fail_devid:
fail_gpio:
    gpiod_put(timerdev.gpio);//释放GPIO描述
err_put_node:
    of_node_put(timerdev.timer_node);//释放设备节点引用
fail_node:

    return ret;
}

static void __exit timer_exit(void)
{
    timer_shutdown_sync(&timerdev.timer);//删除定时器
    cdev_del(&timerdev.timer_cdev);
    unregister_chrdev_region(timerdev.devt, TIMER_CNT);

    device_destroy(timerdev.timer_class, timerdev.devt);
    class_destroy(timerdev.timer_class);
    gpiod_put(timerdev.gpio);//释放GPIO描述
     of_node_put(timerdev.timer_node);   // ← 补上这一行
    pr_info("wukong_timer: unloaded\n");
}

module_init(timer_init);
module_exit(timer_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("WuKong Pi H3 PA17 timer driver");