
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/timer.h>
#include "mpu6050.h"
#include <linux/i2c.h>
#include <linux/fs.h>

struct mpu6050_dev mpu6050_dev;

/*
 * @brief 读取MPU6050寄存器
 * 
 * @param client I2C客户端
 * @param reg 寄存器地址
 * @param data 寄存器数据指针
 * @param len 寄存器数据长度
 * @return int 0 成功，其他错误码
 */
static int mpu6050_read_regs(struct i2c_client *client,
                            u8 reg, u8 *data, u16 len)
{
    struct i2c_msg msgs[2] = {
        {
            .addr  = client->addr,
            .flags = 0,
            .len   = 1,
            .buf   = &reg,
        },
        {
            .addr  = client->addr,
            .flags = I2C_M_RD,//读取标志位
            .len   = len,
            .buf   = data,
        },
    };
    int ret;

    ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret < 0)
        return ret;

    if (ret != ARRAY_SIZE(msgs))
        return -EIO;

    return 0;
}
/*
 * @brief 写入MPU6050寄存器
 * 
 * @param client I2C客户端
 * @param reg 寄存器地址
 * @param value 寄存器值
 * @return int 0 成功，其他错误码
 */
static int mpu6050_write_reg(struct i2c_client *client,
                             u8 reg, u8 value)
{
    u8 data[2] = { reg, value };
    struct i2c_msg msg = {
        .addr  = client->addr,
        .flags = 0,
        .len   = sizeof(data),
        .buf   = data,
    };
    int ret;

    ret = i2c_transfer(client->adapter, &msg, 1);
    if (ret < 0)
        return ret;

    if (ret != 1)
        return -EIO;

    return 0;
}
/*
 * @brief 初始化MPU6050硬件
 * 
 * @param client I2C客户端
 * @return int 0 成功，其他错误码
 */
static int mpu6050_hw_init(struct i2c_client *client)
{
    int ret;

    /* 唤醒，选择 X 轴陀螺仪 PLL 时钟 */
    ret = mpu6050_write_reg(client, PMPU6050_PWR_MGMT_1, 0x01);
    if (ret)
        return ret;

    /* 启用全部加速度计和陀螺仪轴 */
    ret = mpu6050_write_reg(client, PMPU6050_PWR_MGMT_2, 0x00);
    if (ret)
        return ret;

    /* 陀螺仪量程：±250°/s */
    ret = mpu6050_write_reg(client, PMPU6050_GYRO_CONFIG, 0x00);
    if (ret)
        return ret;

    /* 加速度计量程：±2g */
    ret = mpu6050_write_reg(client, PMPU6050_ACCEL_CONFIG, 0x00);
    if (ret)
        return ret;

    /* 本实验留出 100ms 启动等待 */
    msleep(100);

    return 0;
}
static int mpu6050_open(struct inode *inode, struct file *file)
{
    struct mpu6050_dev *dev = container_of(inode->i_cdev,
                                         struct mpu6050_dev, mpu6050_cdev);
    int ret;

    ret = mutex_lock_interruptible(&dev->lock);
    if (ret)
        return ret;

    /* 删除 cdev 之前也可能有新的 open 到达，必须检查绑定状态。 */
    if (!dev->client) {
        mutex_unlock(&dev->lock);
        return -ENODEV;
    }

    file->private_data = dev;
    mutex_unlock(&dev->lock);

    printk("mpu6050_fops_open\n");
    return 0;
}
/*
 * @brief 释放MPU6050设备
 * 
 * @param inode inode结构体指针
 * @param file file结构体指针
 * @return int 0 成功，其他错误码
 */
static int mpu6050_release(struct inode *inode, struct file *file)
{
    printk("mpu6050_fops_release\n");
    return 0;
}
/*
 * @brief 读取MPU6050寄存器
 * 
 * @param file file结构体指针
 * @param buf 用户空间缓冲区指针
 * @param count 读取字节数
 * @param off 文件偏移指针
 * @return ssize_t 读取的字节数
 */
static ssize_t mpu6050_read(struct file *file,
                     char __user *buf,
                     size_t count,
                     loff_t *off)
{
    struct mpu6050_dev *dev = file->private_data;
    u8 data[MPU6050_SAMPLE_BYTES];
    int ret;

    if (count == 0)
        return 0;

    if (count < sizeof(data))
        return -EINVAL;

    if (!dev)
        return -ENODEV;

    ret = mutex_lock_interruptible(&dev->lock);
    if (ret)
        return ret;

    /* 检查 client 和使用 client 必须处于同一个临界区。 */
    if (!dev->client) {
        ret = -ENODEV;
        goto out_unlock;
    }

    ret = mpu6050_read_regs(dev->client,
                            PMPU6050_ACCEL_XOUT_H,
                            data, sizeof(data));

out_unlock:
    /* 所有已经成功加锁的路径，都先解锁再返回。 */
    mutex_unlock(&dev->lock);
    if (ret)
        return ret;

    /* data 是本次 read 的局部快照，复制到用户空间时无需继续持锁。 */
    if (copy_to_user(buf, data, sizeof(data)))
        return -EFAULT;

    return sizeof(data);
}
static ssize_t mpu6050_write(struct file *file, const char __user *buf, size_t count, loff_t *off)
{
    printk("mpu6050_fops_write\n");
    return 0;
}


static const struct file_operations mpu6050_fops = {
    .owner   = THIS_MODULE,
    .open    = mpu6050_open,
    .read    = mpu6050_read,
    .write   = mpu6050_write,
    .release = mpu6050_release,
};

static int mpu6050_probe(struct i2c_client *client)
{
    struct mpu6050_dev *dev = &mpu6050_dev;
    int ret;
    /* 单设备实验：先初始化回调依赖的状态，再用 cdev_add 发布接口。 */
    mutex_init(&dev->lock);
    dev->client = client;
    
    if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
        dev_err(&client->dev, "raw I2C is not supported\n");
        return -EOPNOTSUPP;
    }

    /* 单设备实验：先初始化回调依赖的状态，再用 cdev_add 发布接口。 */
    mutex_init(&dev->lock);
    dev->client = client;

    ret = mpu6050_hw_init(client);
    if (ret) {
        dev_err(&client->dev, "sensor init failed: %d\n", ret);
        goto err_clear_client;
    }

    dev->major = 0; /* 0 表示由内核动态分配设备号。 */
    if (dev->major) {
        dev->devt = MKDEV(dev->major, 0);
        ret = register_chrdev_region(dev->devt, 1, MPU6050_DEV_NAME);
    } else {
        ret = alloc_chrdev_region(&dev->devt, 0, 1, MPU6050_DEV_NAME);
    }
    if (ret) {
        dev_err(&client->dev, "device number allocation failed: %d\n", ret);
        goto err_clear_client;
    }
    dev->major = MAJOR(dev->devt);
    dev->minor = MINOR(dev->devt);

    cdev_init(&dev->mpu6050_cdev, &mpu6050_fops);
    dev->mpu6050_cdev.owner = THIS_MODULE;

    ret = cdev_add(&dev->mpu6050_cdev, dev->devt, 1);
    if (ret) {
        dev_err(&client->dev, "cdev_add failed: %d\n", ret);
        goto err_unregister;
    }

    dev->mpu6050_class = class_create(MPU6050_DEV_NAME);
    if (IS_ERR(dev->mpu6050_class)) {
        ret = PTR_ERR(dev->mpu6050_class);
        dev_err(&client->dev, "class_create failed: %d\n", ret);
        goto err_del_cdev;
    }

    dev->mpu6050_device = device_create(dev->mpu6050_class, &client->dev,
                                      dev->devt, NULL, MPU6050_DEV_NAME);
    if (IS_ERR(dev->mpu6050_device)) {
        ret = PTR_ERR(dev->mpu6050_device);
        dev_err(&client->dev, "device_create failed: %d\n", ret);
        goto err_destroy_class;
    }

    i2c_set_clientdata(client, dev);
    dev_info(&client->dev, "loaded, major: %d, minor: %d\n",
             dev->major, dev->minor);
    return 0;

    /* 只撤销已经成功的步骤；保留失败现场设置的 ret。 */
err_destroy_class:
    class_destroy(dev->mpu6050_class);
err_del_cdev:
    cdev_del(&dev->mpu6050_cdev);
err_unregister:
    unregister_chrdev_region(dev->devt, 1);
err_clear_client:
    /* cdev 曾经发布成功时，也要等待可能已经开始的读取结束。 */
    mutex_lock(&dev->lock);
    dev->client = NULL;
    mutex_unlock(&dev->lock);
    return ret;
}

static void mpu6050_remove(struct i2c_client *client)
{
    struct mpu6050_dev *dev = i2c_get_clientdata(client);

    /* 等当前传输结束，并让已有文件描述符后续读取返回 -ENODEV。 */
    mutex_lock(&dev->lock);
    dev->client = NULL;
    mutex_unlock(&dev->lock);

    cdev_del(&dev->mpu6050_cdev);
    device_destroy(dev->mpu6050_class, dev->devt);
    class_destroy(dev->mpu6050_class);
    unregister_chrdev_region(dev->devt, 1);
}


// 定义设备匹配表
static const struct of_device_id mpu6050_of_match[] = {
	{ .compatible = "wukong,mpu6050" },
	{ /* sentinel */ }
};
// 注册设备匹配表
MODULE_DEVICE_TABLE(of, mpu6050_of_match);
// 定义I2C驱动
static struct i2c_driver 	MPU6050_driver =
{
	.driver = {
		.name = "MPU6050",
		.owner = THIS_MODULE,
		.of_match_table = mpu6050_of_match,
	},
	.probe = mpu6050_probe,
	.remove = mpu6050_remove,
	
	/* data */
};


module_i2c_driver(MPU6050_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("Device-tree MPU6050 driver using the Linux I2C subsystem");
