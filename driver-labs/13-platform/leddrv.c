#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/device.h>   
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h> 


#define LED_DEV_NAME "wukong_led"
#define LED_COUNT 1



#define LED_ON  1
#define LED_OFF 0


//led设备结构体
struct newcharled_dev
{
    dev_t devt; //设备号
    struct cdev led_cdev;//字符设备结构体
    struct class *led_class;//类结构体指针
    struct device *led_device;//设备结构体指针
    int major; //主设备号
    int minor; //次设备号

    struct gpio_desc *gpio;

    bool led_state;

};
static DEFINE_MUTEX(led_lock);//互斥锁
static void __iomem *pio_base;//pio寄存器基地址
static struct newcharled_dev newcharled;
 

static const struct file_operations led_fops = {

};

static int h3_led_set(struct newcharled_dev *led, bool on)
{
    if (!led->gpio)
        return -ENODEV;

    /*
     * on 是逻辑值，不一定等于物理高电平。
     * GPIO_ACTIVE_HIGH/LOW 会由 GPIO 框架处理。
     */
    gpiod_set_value_cansleep(led->gpio, on);
    led->led_state = on;

    return 0;
}

static int leddev_probe(struct platform_device *pdev)
{
    struct resource *res;
    int ret;

    //初始化设备
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (res == NULL) {
        printk("leddev_probe: failed to get resource\n");
        ret = -ENODEV;
        goto get_resource_failed;
    }

    pio_base = ioremap(res->start, resource_size(res));//映射pio寄存器
    if (!pio_base) {
        printk("Failed to map pio base\n");
        return -ENOMEM;
    }
    if(newcharled.major) {
        newcharled.devt = MKDEV(newcharled.major, 0);
        ret = register_chrdev_region(newcharled.devt, 1, "newcharled");
    } else {
        ret = alloc_chrdev_region(&newcharled.devt, 0, 1, "newcharled");//分配设备号
        newcharled.major = MAJOR(newcharled.devt);
        newcharled.minor = MINOR(newcharled.devt);
    }
    if (ret < 0) {
        printk("register_chrdev_region failed\n");
        return -EIO;
    }
    //ret = register_chrdev(newcharled.major, "newcharled", &led_fops);//注册字符设备驱动
    newcharled.led_cdev.owner = THIS_MODULE;
    cdev_init(&newcharled.led_cdev, &led_fops);

    ret = cdev_add(&newcharled.led_cdev, newcharled.devt, 1);

     
    if (ret < 0) {
        printk("cdev_add failed\n");
        return ret;
    }
    if (ret < 0) {
        iounmap(pio_base);//取消映射pio寄存器
        return -EIO;
    }

    //创建类
    newcharled.led_class = class_create("newcharled");
    if (IS_ERR(newcharled.led_class)) {
        printk("class_create failed\n");
        return PTR_ERR(newcharled.led_class);
    }

    //创建设备
    newcharled.led_device = device_create(newcharled.led_class, NULL, newcharled.devt, NULL, "newcharled");
    if (IS_ERR(newcharled.led_device)) {
        printk("device_create failed\n");
        return PTR_ERR(newcharled.led_device);
    }
    printk("newcharled major: %d, minor: %d\n", newcharled.major, MINOR(newcharled.devt));
    //获取led gpio
    newcharled.gpio = devm_gpiod_get(&pdev->dev,
                                 "led",
                                 GPIOD_OUT_LOW);
    if (IS_ERR(newcharled.gpio))
        return dev_err_probe(&pdev->dev,PTR_ERR(newcharled.gpio),"failed to get LED GPIO\n");

newcharled.led_state = false;

    mutex_lock(&led_lock);//加锁
    h3_led_set(&newcharled,false);//关闭led
    mutex_unlock(&led_lock);//解锁

    pr_info("wukong_led: loaded\n");
    return 0;



get_resource_failed:
    return ret;

}

static void leddev_remove(struct platform_device *pdev)
{
    printk("leddev_remove\n");
}



static struct platform_driver leddev_driver = {
    .driver = {
        .name = LED_DEV_NAME,
    },
    .probe = leddev_probe,
    .remove = leddev_remove,

};

static int __init leddrv_init(void)
{
    int ret;
    ret = platform_driver_register(&leddev_driver);
    return ret;
}

static void __exit leddrv_exit(void)
{
    platform_driver_unregister(&leddev_driver);
}

module_init(leddrv_init);
module_exit(leddrv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("WuKong Pi H3 PA17 LED driver");