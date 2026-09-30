#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#define LED_MAJOR 201
#define LED_NAME  "wukong_led"

/* H3 PA17 */
#define PIO_BASE       0x01c20800   //pio寄存器基地址
#define PIO_SIZE       0x24         //pio寄存器大小
#define PA17_CFG_REG   0x08         //pio17配置寄存器偏移量
#define PA17_DATA_REG  0x10         //pio17数据寄存器偏移量
#define PA17_CFG_SHIFT 4            //pio17配置寄存器偏移量位移
#define PA17_DATA_BIT  17           //pio17数据寄存器位移量

static void __iomem *pio_base;//pio寄存器基地址
static DEFINE_MUTEX(led_lock);//互斥锁
static bool led_state;//led状态

#define LED_ON  1
#define LED_OFF 0

static void h3_led_set(bool on)
{
    u32 value;

    /* PA17 的复用值 1 表示 gpio_out */
    value = readl(pio_base + PA17_CFG_REG);
    value &= ~(0xfU << PA17_CFG_SHIFT);
    value |=  (0x1U << PA17_CFG_SHIFT);
    writel(value, pio_base + PA17_CFG_REG);

    /* 设置 PA17 输出电平 */
    value = readl(pio_base + PA17_DATA_REG);

    if (on)
        value |= BIT(PA17_DATA_BIT);
    else
        value &= ~BIT(PA17_DATA_BIT);

    writel(value, pio_base + PA17_DATA_REG);
    led_state = on;
}

static int led_open(struct inode *inode, struct file *file)
{
    pr_info("wukong_led: open\n");
    h3_led_set(LED_ON);//打开led
    return 0;
}

static int led_release(struct inode *inode, struct file *file)
{
    pr_info("wukong_led: release\n");

    return 0;
}

static ssize_t led_read(struct file *file, char __user *buf,
                        size_t count, loff_t *ppos)
{
    char state_buf[4];
    bool state;
    int len;

    mutex_lock(&led_lock);
    state = led_state;
    mutex_unlock(&led_lock);

    len = scnprintf(state_buf, sizeof(state_buf),
                    "%d\n", state ? 1 : 0);

    return simple_read_from_buffer(buf, count, ppos,
                                   state_buf, len);
}

static ssize_t led_write(struct file *file,
                         const char __user *buf,
                         size_t count, loff_t *ppos)
{
    int retvalue;
    char data[1];
    retvalue = copy_from_user(data, buf, 1);
    if (retvalue < 0) {
        printk("copy_from_user failed\n");
        return -EFAULT;
    }
    if (data[0] == 1)
        h3_led_set(LED_ON);//打开led
    else if (data[0] == 0)
        h3_led_set(LED_OFF);//关闭led
    else if (data[0] == 2)
        h3_led_set(!led_state);//切换led状态
    else {
        printk("Invalid command\n");   
        return -EINVAL;
    }

    return 0;
}

static const struct file_operations led_fops = {
    .owner   = THIS_MODULE,
    .open    = led_open,
    .read    = led_read,
    .write   = led_write,
    .release = led_release,
};

static int __init led_init(void)
{
    int ret;

    pio_base = ioremap(PIO_BASE, PIO_SIZE);//映射pio寄存器
    if (!pio_base) {
        printk("Failed to map pio base\n");
        return -ENOMEM;
    }

    ret = register_chrdev(LED_MAJOR, LED_NAME, &led_fops);//注册字符设备驱动
    if (ret < 0) {
        iounmap(pio_base);//取消映射pio寄存器
        return -EIO;
    }

    mutex_lock(&led_lock);//加锁
    h3_led_set(false);//关闭led
    mutex_unlock(&led_lock);//解锁

    pr_info("wukong_led: loaded\n");
    return 0;
}

static void __exit led_exit(void)
{
    mutex_lock(&led_lock);
    h3_led_set(false);
    mutex_unlock(&led_lock);

    unregister_chrdev(LED_MAJOR, LED_NAME);
    iounmap(pio_base);

    pr_info("wukong_led: unloaded\n");
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("WuKong Pi H3 PA17 LED driver");