// SPDX-License-Identifier: GPL-2.0
/*
 * WuKong Pi H3：通过设备树获取 GPIO 的字符设备实验。
 *
 * 设备树节点：/leds/status-led，必须保持 status = "disabled"。
 * 设备文件：/dev/dts_led。
 * write 协议：一个二进制字节，0=关闭，1=开启，2=翻转。
 * read 协议：文本 "0\n" 或 "1\n"，同一次打开读完后返回 EOF。
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
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>

#define LED_NAME "dts_led"
#define LED_NODE_PATH "/leds/status-led"

#define LED_OFF 0
#define LED_ON 1
#define LED_TOGGLE 2

struct dts_led_dev {
	dev_t devt;
	struct cdev cdev;
	struct class *class;
	struct device *device;
	struct gpio_desc *gpio;
	struct mutex lock;
	atomic_t users;// 记录当前打开的文件描述符数量//
};

static struct dts_led_dev dts_led;

static int led_open(struct inode *inode, struct file *file)
{
	/* 从当前打开的 cdev 找到整个设备对象，供 read/write 使用。 */
	file->private_data = container_of(inode->i_cdev,
					struct dts_led_dev, cdev);
    if(atomic_read(&dts_led.users)<=0)
		return -EBUSY;
	else 
		atomic_dec(&dts_led.users);
	/* GPIO 设备不支持像普通文件一样移动读写位置。 */
	return nonseekable_open(inode, file);
}

static int led_release(struct inode *inode, struct file *file)
{
	struct dts_led_dev *led = file->private_data;
	atomic_inc(&led->users);// 增加打开文件描述符数量
	/* 关闭文件不改变 LED；灯保持最后一次写入的状态。 */
	return 0;
}

static ssize_t led_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct dts_led_dev *led = file->private_data;
	char state_buf[3];
	int state;
	int len;

	if (!count)
		return 0;

	mutex_lock(&led->lock);
	state = gpiod_get_value_cansleep(led->gpio);
	mutex_unlock(&led->lock);
	if (state < 0)
		return state;

	len = scnprintf(state_buf, sizeof(state_buf), "%d\n", !!state);

	/* 处理用户空间拷贝、短读和文件位置，避免 cat 无限输出。 */
	return simple_read_from_buffer(buf, count, ppos, state_buf, len);
}
/*
* write 协议：一个二进制字节，0=关闭，1=开启，2=翻转。
*/
static ssize_t led_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct dts_led_dev *led = file->private_data;
	u8 command;
	int state;
	int ret = 0;

	if (!count)
		return 0;

	/* 本实验一次写入一条命令，每条命令严格占一个字节。 */
	if (count != sizeof(command))
		return -EINVAL;

	/* 返回值是未成功拷贝的字节数，非零即失败，不是检查 < 0。 */
	if (copy_from_user(&command, buf, sizeof(command)))
		return -EFAULT;
	if (command > LED_TOGGLE)
		return -EINVAL;

	/* 翻转包含“读取+写入”，必须一起加锁，避免并发丢失操作。 */
	mutex_lock(&led->lock);
	if (command == LED_TOGGLE) {
		state = gpiod_get_value_cansleep(led->gpio);
		if (state < 0) {
			ret = state;
			goto out_unlock;
		}
		state = !state;
	} else {
		state = command == LED_ON;
	}

	/* 使用逻辑值；有效电平由设备树 GPIO_ACTIVE_HIGH/LOW 决定。 */
	gpiod_set_value_cansleep(led->gpio, state);

out_unlock:
	mutex_unlock(&led->lock);
	if (ret)
		return ret;

	/* 允许同一个文件描述符写入后重新读取状态。 */
	*ppos = 0;
	return count;
}

static const struct file_operations led_fops = {
	.owner = THIS_MODULE,
	.open = led_open,
	.read = led_read,
	.write = led_write,
	.release = led_release,
};

static int __init dts_led_init(void)
{
	atomic_set(&dts_led.users, 1);
	struct device_node *np;
	const char *default_state = "off";
	enum gpiod_flags flags;
	int state;
	int ret;

	mutex_init(&dts_led.lock);

	/* disabled 节点仍然存在，可以被显式查找和读取。 */
	np = of_find_node_by_path(LED_NODE_PATH);
	if (!np) {
		pr_err("dts_led: cannot find %s\n", LED_NODE_PATH);
		return -ENODEV;
	}

	/* 此实验复用 gpio-leds 的子节点，必须避免原驱动占用它。 */
	if (of_device_is_available(np)) {//检查节点是否可用
		pr_err("dts_led: keep %s disabled for this lab\n",
		       LED_NODE_PATH);
		ret = -EBUSY;
		goto err_put_node;
	}

	/* 属性只是数据，读取后必须由驱动解释并执行。缺省为关闭。 */
	if (of_find_property(np, "default-state", NULL)) {
		ret = of_property_read_string(np, "default-state", &default_state);
		if (ret)
			goto err_put_node;
	}

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

	/*
	 * 转为通用固件节点句柄后获取 GPIO。
	 * con_id=NULL 对应本节点的 "gpios"，index=0 表示第一根 GPIO。
	 * 此接口既解析设备树，又申请 GPIO，并按 flags 设置方向/初值。
	 */
	dts_led.gpio = fwnode_gpiod_get_index(of_fwnode_handle(np), NULL,0, flags, LED_NAME);//获取gpio//index=0 表示第一根 GPIO。//flags=GPIO_OUT_HIGH 表示输出高电平
	of_node_put(np);
	if (IS_ERR(dts_led.gpio)) {
		ret = PTR_ERR(dts_led.gpio);
		pr_err("dts_led: cannot request GPIO: %d\n", ret);
		return ret;
	}

	if (flags == GPIOD_ASIS) {
		/* keep：先读取当前逻辑电平，再以这个电平设置为输出。 */
		state = gpiod_get_value_cansleep(dts_led.gpio);
		if (state < 0) {
			ret = state;
			goto err_put_gpio;
		}
		ret = gpiod_direction_output(dts_led.gpio, state);//
		if (ret)
			goto err_put_gpio;
	}

	/* 动态分配设备号，避免写死主设备号造成冲突。 */
	ret = alloc_chrdev_region(&dts_led.devt, 0, 1, LED_NAME);
	if (ret)
		goto err_put_gpio;

	/* Linux 6.12 的 class_create() 只接收类名。 */
	dts_led.class = class_create(LED_NAME);
	if (IS_ERR(dts_led.class)) {
		ret = PTR_ERR(dts_led.class);
		goto err_unregister;
	}

	cdev_init(&dts_led.cdev, &led_fops);
	dts_led.cdev.owner = THIS_MODULE;

	/* 当前系统挂载了 devtmpfs，注册设备后自动创建 /dev/dts_led。 */
	dts_led.device = device_create(dts_led.class, NULL, dts_led.devt,
				       &dts_led, LED_NAME);
	if (IS_ERR(dts_led.device)) {
		ret = PTR_ERR(dts_led.device);
		goto err_destroy_class;
	}

	/* 最后发布 cdev；此后 open/read/write 才能进入本驱动。 */
	ret = cdev_add(&dts_led.cdev, dts_led.devt, 1);
	if (ret)
		goto err_destroy_device;

	pr_info("dts_led: loaded, /dev/%s, major=%u minor=%u\n",
		LED_NAME, MAJOR(dts_led.devt), MINOR(dts_led.devt));
	return 0;

err_destroy_device:
	device_destroy(dts_led.class, dts_led.devt);
err_destroy_class:
	class_destroy(dts_led.class);
err_unregister:
	unregister_chrdev_region(dts_led.devt, 1);
err_put_gpio:
	gpiod_put(dts_led.gpio);
	return ret;
err_put_node:
	of_node_put(np);
	return ret;
}

static void __exit dts_led_exit(void)
{
	/* owner=THIS_MODULE 使正常卸载必须等应用关闭设备文件。 */
	cdev_del(&dts_led.cdev);
	device_destroy(dts_led.class, dts_led.devt);
	class_destroy(dts_led.class);
	unregister_chrdev_region(dts_led.devt, 1);
	gpiod_set_value_cansleep(dts_led.gpio, LED_OFF);
	gpiod_put(dts_led.gpio);
	pr_info("dts_led: unloaded\n");
}

module_init(dts_led_init);
module_exit(dts_led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("Device-tree GPIO character LED driver for Linux 6.12");
