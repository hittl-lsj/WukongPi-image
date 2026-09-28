#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

#define LED_MAJOR 200
#define LED_NAME "led"


#define PIO_BASE 0x01c20800

static char red_buf[128] = "Hello, this is a character device driver test.\n";
static char write_buf[128] = {0};
static char kernel_buf[128] = {"kernel_buf"};

static int led_open(struct inode *inode, struct file *file)
{
    int fd = open("/dev/chrdevbase", O_RDWR);
    if(fd < 0){ 
        printk("open led failed\n");
        return -1;
    }
    printk("open led success\n");
    return 0;
}
static int led_release(struct inode *inode, struct file *file)
{   
    printk("led_release\n");
    return 0;
}
static ssize_t led_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    //printk("led_read\n");
    int ret = copy_to_user(buf, kernel_buf, count);
    if(ret < 0){
        printk("copy_to_user failed\n");
        return ret;
    }
    printk("read: %s\n", red_buf);
    return ret;
}
static ssize_t led_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    //printk("led_write\n");
    int ret = copy_from_user(write_buf, buf, count);
    if(ret < 0){
        printk("copy_from_user failed\n");
        return ret;
    }
    printk("write: %s\n", write_buf);
    return ret;
}


struct const struct file_operations led_fops={
    .owner = THIS_MODULE,
    .open = led_open,
    .release = led_release,
    .read = led_read,
    .write = led_write, 
    .close = led_release, 
};

static int __init led_init(void)
{
    printk("led_init\n");
    // 初始化led设备



       // 1. 注册字符设备驱动
    int ret = register_chrdev(LED_MAJOR, LED_NAME, &led_fops);
    if (ret < 0) {
        printk("Failed to register led device\n");
        return -EIO;
    }

    return 0;
}
static void __exit led_exit(void)
{
    printk("led_exit\n");

    // 2. 注销字符设备驱动
    unregister_chrdev(LED_MAJOR, LED_NAME); 
}
/*
模块初始化函数
*/
module_init(led_init);
/*
模块退出函数
*/
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("First led-device module experiment");
