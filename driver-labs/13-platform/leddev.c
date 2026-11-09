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
#include <linux/gpio/machine.h>

 
/* H3 PA17 */
#define PIO_BASE       0x01c20800   //pio寄存器基地址
#define PIO_SIZE       0x24         //pio寄存器大小
#define PA17_CFG_REG   0x08         //pio17配置寄存器偏移量
#define PA17_DATA_REG  0x10         //pio17数据寄存器偏移量
#define PA17_CFG_SHIFT 4            //pio17配置寄存器偏移量位移
#define PA17_DATA_BIT  17           //pio17数据寄存器位移量

#define LED_DEV_NAME "wukong_led"
#define LED_ON  1
#define LED_OFF 0

static struct resource led_resources[] = {
    [0] = {
        .start = PIO_BASE,
        .end   = PIO_BASE + PIO_SIZE - 1,
        .flags = IORESOURCE_MEM,
    },
};
void led_release(struct device *dev)
{

    printk("led_release\n");
}


static struct platform_device leddev_device={
    .name = LED_DEV_NAME,
    .id = PLATFORM_DEVID_NONE,
    .dev = {
        .release = &led_release,
    },
    .resource = led_resources,
    .num_resources = ARRAY_SIZE(led_resources),
};


static struct gpiod_lookup_table led_gpio_table = {
    .dev_id = LED_DEV_NAME,
    .table = {
        GPIO_LOOKUP("1c20800.pinctrl", 17, "led", GPIO_ACTIVE_HIGH),
        { }
    },
};
static int __init leddev_init(void)
{
    int ret;

    gpiod_add_lookup_table(&led_gpio_table);
    //注册platform设备
    ret = platform_device_register(&leddev_device);
    if (ret) {
        pr_err("LED device registration failed: %d\n", ret);
        gpiod_remove_lookup_table(&led_gpio_table);
        platform_device_put(&leddev_device);
        return ret;
    }
    return 0;
}

static void __exit leddev_exit(void)
{
    //注销platform设备
    platform_device_unregister(&leddev_device);
    gpiod_remove_lookup_table(&led_gpio_table);
}

module_init(leddev_init);
module_exit(leddev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("WuKong Pi H3 PA17 LED driver");