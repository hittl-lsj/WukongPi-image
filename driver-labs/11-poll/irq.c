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
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/jiffies.h>
#include <linux/pinctrl/consumer.h>
#include <linux/wait.h>
#include <linux/poll.h>


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
	atomic_t key_value;// 记录按键状态
	struct timer_list timer;	// 定时器结构体

	atomic_t key_release;// 是否完成按键释放

	//等待队列
	wait_queue_head_t key_wait_qu;

	unsigned int irq_num;// 记录中断号
};

static struct dts_key_dev dts_key;

static int key_open(struct inode *inode, struct file *file)
{
	file->private_data = &dts_key;
	return 0;
}
static unsigned int key_poll(struct file *file, struct poll_table_struct *wait)
{
	struct dts_key_dev *dev = file->private_data;
	int state = atomic_read(&dev->key_state);
	if(state == KEY_ON){
		return POLLIN | POLLPRI;
	}
	return 0;
}
static ssize_t key_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct dts_key_dev *dev = file->private_data;
	int state;
	int ret;

	if (count < sizeof(int))
		return -EINVAL;
	if(file->f_flags & O_NONBLOCK){
		if(atomic_read(&dev->key_state) == KEY_OFF){
			pr_info("now is \n");
			return -EAGAIN;
		}
		else {
			ret = wait_event_interruptible(dev->key_wait_qu,
				       atomic_read(&dev->key_state) == 1);
		}
	}
	/* ★ 阻塞等待：只等按键按下 */
	
	if (ret)
		return ret;

	state = atomic_read(&dev->key_state);
	atomic_set(&dev->key_state, KEY_OFF);

	if (copy_to_user(buf, &state, sizeof(int)))
		return -EFAULT;

	return sizeof(int);   /* ★ 必须返回读到的字节数，不能返回 0！ */
}



static ssize_t key_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	/* 保留 write 回调；按键是输入设备，不接受状态写入。 */
	return count ? -EOPNOTSUPP : 0;
}

static irqreturn_t key_isr(int irq, void *dev_id)
{
    struct dts_key_dev *dev = dev_id;
    mod_timer(&dev->timer, jiffies + msecs_to_jiffies(20));

    return IRQ_HANDLED;
}


static void timer_callback(struct timer_list *t)
{
	struct dts_key_dev *dev = from_timer(dev, t, timer);
	int state = gpiod_get_value(dev->gpio);   /* ACTIVE_LOW: 1=按下，0=松开 */

	if (state == 1) {
		/* 按下 */
		if (atomic_read(&dev->key_state) != 1) {
			atomic_set(&dev->key_state, 1);
			pr_info("key is pressed\n");
			wake_up_interruptible(&dev->key_wait_qu);  /* ★ 只有按下才唤醒 */
		}
	} else {
		/* 松开 */
		if (atomic_read(&dev->key_state) != 0) {
			atomic_set(&dev->key_state, 0);
			pr_info("key is released\n");
			/* ★ 松开时什么都不用做，不唤醒，避免空转 */
		}
	}
}
static const struct file_operations key_fops = {
	.owner = THIS_MODULE,
	.open = key_open,
	.read = key_read,
	.write = key_write,
	.poll = key_poll,
};

static int __init dts_key_init(void)
{
	struct device_node *np;
	int ret;

	atomic_set(&dts_key.users, 1);
	atomic_set(&dts_key.key_state, KEY_OFF);
	init_waitqueue_head(&dts_key.key_wait_qu);
	
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
	// 在 dts_key_init 里面：
struct pinctrl *p;
struct pinctrl_state *state;


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
// 1. 告诉内核去解析你的 pinctrl
p = pinctrl_get(dts_key.device); 
if (!IS_ERR(p)) {
    // 2. 找到名叫 "default" 的状态（就是 DTS 里的 pinctrl-names）
    state = pinctrl_lookup_state(p, "default");
    if (!IS_ERR(state)) {
        // 3. 应用它（这时候内部上拉才会真正生效！）
        pinctrl_select_state(p, state); 
    }
}

	/* 最后发布 cdev；此后 open/read/write 才能进入本驱动。 */
	ret = cdev_add(&dts_key.cdev, dts_key.devt, 1);
	if (ret)
		goto err_destroy_device;

	pr_info("dts_key: loaded, /dev/%s, major=%u minor=%u\n",
		KEY_NAME, MAJOR(dts_key.devt), MINOR(dts_key.devt));

	dts_key.irq_num = gpiod_to_irq(dts_key.gpio);
	if (dts_key.irq_num < 0) {
		pr_err("dts_key: cannot get IRQ: %d\n", dts_key.irq_num);
		ret = dts_key.irq_num;
		goto err_destroy_device;
	}
	timer_setup(&dts_key.timer, timer_callback, 0);
	ret = request_irq(dts_key.irq_num, key_isr, IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			      KEY_NAME, &dts_key);
	if (ret) {
		pr_err("dts_key: cannot request IRQ: %d\n", ret);
		ret = dts_key.irq_num;
		goto err_destroy_irq;
	}

	/* 初始化定时器，指定到期后执行的函数 */




	
	return 0;
err_destroy_irq:
	free_irq(dts_key.irq_num, &dts_key);
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
	timer_shutdown_sync(&dts_key.timer); 
	free_irq(dts_key.irq_num, &dts_key);

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
