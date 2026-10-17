// SPDX-License-Identifier: GPL-2.0
/*
 * WuKong Pi H3：通过设备树获取 GPIO 的字符设备实验。
 *
 * 设备树节点：/key，gpios 使用 GPIO_ACTIVE_LOW，按下的逻辑值为 1。
 * 设备文件：/dev/dts_key。
 * read 协议：每次返回一个二进制 int，0=松开，1=按下。
 * 每次 read 都采样当前状态，不等待按键释放，也不返回文件末尾。
 * write 保留在字符设备接口中，但按键不支持写入。
 *
 * 本课显式查找节点；以后可改为 platform_driver + probe 自动匹配。
 */

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
#include <linux/uaccess.h>
#include <linux/atomic.h>

#define KEY_NAME "dts_key"
#define KEY_NODE_PATH "/key"

#define KEY_OFF 0// 按键松开
#define KEY_ON 1// 按键按下

struct dts_key_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	struct gpio_desc *gpio;
	struct mutex lock;
	atomic_t key_state;// 记录按键状态
	atomic_t users; /* 1=可以打开，0=已经打开；本实验只允许一个使用者。 */
};

static struct dts_key_dev dts_key;

static int key_open(struct inode *inode, struct file *file)
{
	struct dts_key_dev *key;

	/* 从当前打开的 cdev 找到整个设备对象，供 read/write 使用。 */
	key = container_of(inode->i_cdev, struct dts_key_dev, cdev);
	/* 检查和占用必须合为一次原子操作，避免两个进程同时打开成功。 */
	if (atomic_cmpxchg(&key->users, 1, 0) != 1)
		return -EBUSY;
	file->private_data = key;
	/* GPIO 设备不支持像普通文件一样移动读写位置。 */
	return nonseekable_open(inode, file);
}

static int key_release(struct inode *inode, struct file *file)
{
	struct dts_key_dev *key = file->private_data;

	atomic_set(&key->users, 1);
	/* 按键是输入，关闭文件只释放使用权，不设置 GPIO 输出。 */
	return 0;
}

static ssize_t key_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct dts_key_dev *key = file->private_data;
	int state;
	int value;

	if (!count)
		return 0;
	/* 本实验约定一次读取一个 int，用户缓冲区必须足够大。 */
	if (count < sizeof(value))
		return -EINVAL;

	mutex_lock(&key->lock);
	state = gpiod_get_value_cansleep(key->gpio);
	if (state >= 0) {
		/* GPIO_ACTIVE_LOW 已处理极性：按下=1，松开=0。 */
		value = state ? KEY_ON : KEY_OFF;
		atomic_set(&key->key_state, value);
	}
	mutex_unlock(&key->lock);
	if (state < 0)
		return state;

	/* copy_to_user 返回未复制的字节数，成功为 0；失败应返回负错误码。 */
	if (copy_to_user(buf, &value, sizeof(value)))
		return -EFAULT;

	/* read 返回已读取的字节数。后续 read 再采样，不按 ppos 返回 EOF。 */
	return sizeof(value);
}

static ssize_t key_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	/* 保留 write 回调；按键是输入设备，不接受状态写入。 */
	return count ? -EOPNOTSUPP : 0;
}

static const struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.write = key_write,
	.release = key_release,
};

static int __init dts_key_init(void)
{
	struct device_node *np;
	int ret;

	atomic_set(&dts_key.users, 1);
	atomic_set(&dts_key.key_state, KEY_OFF);
	mutex_init(&dts_key.lock);

	/* 本课显式读取自定义 /key 节点，不要求节点 disabled。 */
	np = of_find_node_by_path(KEY_NODE_PATH);
	if (!np) {
		pr_err("dts_key: cannot find %s\n", KEY_NODE_PATH);
		return -ENODEV;
	}

	/*
	 * 转为通用固件节点句柄后获取 GPIO。
	 * con_id=NULL 对应本节点的 "gpios"，index=0 表示第一根 GPIO。
	 * 按键始终是输入，不使用 LED 的 default-state 或输出初值。
	 * 此处不自动选择 pinctrl-0，接线须有上拉电阻保证松开时为高电平。
	 */
	dts_key.gpio = fwnode_gpiod_get_index(of_fwnode_handle(np), NULL,
					      0, GPIOD_IN, KEY_NAME);
	of_node_put(np);
	if (IS_ERR(dts_key.gpio)) {
		ret = PTR_ERR(dts_key.gpio);
		pr_err("dts_key: cannot request GPIO: %d\n", ret);
		return ret;
	}

	/* 动态分配设备号，避免写死主设备号造成冲突。 */
	ret = alloc_chrdev_region(&dts_key.devt, 0, 1, KEY_NAME);
	if (ret)
		goto err_put_gpio;

	/* Linux 6.12 的 class_create() 只接收类名。 */
	dts_key.class = class_create(KEY_NAME);
	if (IS_ERR(dts_key.class)) {
		ret = PTR_ERR(dts_key.class);
		goto err_unregister;
	}

	cdev_init(&dts_key.cdev, &key_fops);
	dts_key.cdev.owner = THIS_MODULE;

	/* 当前系统挂载了 devtmpfs，注册设备后自动创建 /dev/dts_key。 */
	dts_key.device = device_create(dts_key.class, NULL, dts_key.devt,
				       &dts_key, KEY_NAME);
	if (IS_ERR(dts_key.device)) {
		ret = PTR_ERR(dts_key.device);
		goto err_destroy_class;
	}

	/* 最后发布 cdev；此后 open/read/write 才能进入本驱动。 */
	ret = cdev_add(&dts_key.cdev, dts_key.devt, 1);
	if (ret)
		goto err_destroy_device;

	pr_info("dts_key: loaded, /dev/%s, major=%u minor=%u\n",
		KEY_NAME, MAJOR(dts_key.devt), MINOR(dts_key.devt));
	return 0;

err_destroy_device:
	device_destroy(dts_key.class, dts_key.devt);
err_destroy_class:
	class_destroy(dts_key.class);
err_unregister:
	unregister_chrdev_region(dts_key.devt, 1);
err_put_gpio:
	gpiod_put(dts_key.gpio);
	/* np 已在获取 GPIO 后释放，错误清理时不能再次 of_node_put。 */
	return ret;
}

static void __exit dts_key_exit(void)
{
	/* owner=THIS_MODULE 使正常卸载必须等应用关闭设备文件。 */
	cdev_del(&dts_key.cdev);
	device_destroy(dts_key.class, dts_key.devt);
	class_destroy(dts_key.class);
	unregister_chrdev_region(dts_key.devt, 1);
	gpiod_put(dts_key.gpio);
	pr_info("dts_key: unloaded\n");
}

module_init(dts_key_init);
module_exit(dts_key_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("Device-tree GPIO character KEY driver for Linux 6.12");
