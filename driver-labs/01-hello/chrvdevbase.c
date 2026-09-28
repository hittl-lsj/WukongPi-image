#include <linux/module.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/uaccess.h>

#define DEVICE_MAJOR 200
#define DEVICE_NAME "chrvdevbase"

static char red_buf[128] = "Hello, this is a character device driver test.\n";
static char write_buf[128] = {0};
static char kernel_buf[128] = {"kernel_buf"};

static int chrdevbase_open(struct inode *inode, struct file *file)
{
    printk("chrdevbase_open\n");
    return 0;
}
static int chrdevbase_release(struct inode *inode, struct file *file)
{   
    printk("chrdevbase_release\n");
    return 0;
}
static ssize_t chrdevbase_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    //printk("chrdevbase_read\n");
    int ret = copy_to_user(buf, kernel_buf, count);
    if(ret < 0){
        printk("copy_to_user failed\n");
        return ret;
    }
    printk("read: %s\n", red_buf);
    return ret;
}
static ssize_t chrdevbase_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
    //printk("chrdevbase_write\n");
    int ret = copy_from_user(write_buf, buf, count);
    if(ret < 0){
        printk("copy_from_user failed\n");
        return ret;
    }
    printk("write: %s\n", write_buf);
    return ret;
}


struct file_operations chrdevbase_fops={
    .owner = THIS_MODULE,
    .open = chrdevbase_open,
    .release = chrdevbase_release,
    .read = chrdevbase_read,
    .write = chrdevbase_write,  
};

static int __init chrvdevbase_init(void)
{
    printk("chrvdevbase_init\n");

    // 1. 注册字符设备驱动
    int ret = register_chrdev(DEVICE_MAJOR, DEVICE_NAME, &chrdevbase_fops);
    if (ret < 0) {
        printk("Failed to register character device\n");
        return ret;
    }

    return 0;
}
static void __exit chrvdevbase_exit(void)
{
    printk("chrvdevbase_exit\n");

    // 2. 注销字符设备驱动
    unregister_chrdev(DEVICE_MAJOR, DEVICE_NAME); 
}
/*
模块初始化函数
*/
module_init(chrvdevbase_init);
/*
模块退出函数
*/
module_exit(chrvdevbase_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("First character-device module experiment");
