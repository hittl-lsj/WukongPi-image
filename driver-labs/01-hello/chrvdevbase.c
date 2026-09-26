#include <linux/module.h>

static int __init chrvdevbase_init(void)
{
    printk("chrvdevbase_init\n");

    // 1. 注册字符设备驱动
    int ret = register_chrdev(0, 1, "chrvdevbase");
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
