// SPDX-License-Identifier: GPL-2.0
/*
 * WuKong Pi H3：GPIO 中断 + 定时器消抖 + Linux Input 子系统。
 *
 * 继续显式获取 /key 节点的 gpios，不在本课引入平台驱动匹配。
 * GPIO_ACTIVE_LOW 使按下的逻辑值为 1、松开为 0，无须再取反。
 * 本课不自动应用 pinctrl-0，须保证松开时 GPIO 有可靠上拉。
 * 用户接口由 evdev 提供：/dev/input/eventX，数据为 struct input_event。
 */

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

#define KEY_NAME "dts_key"
#define KEY_NODE_PATH "/key"
#define KEY_CODE KEY_ENTER
//#define KEY_CODE KEY_ENTER
#define KEY_DEBOUNCE_MS 20

#define KEY_ON 1
#define KEY_OFF 0



struct dts_key_dev {
	struct gpio_desc *gpio;
	struct input_dev *input;
	struct timer_list timer;
	int irq_num; /* gpiod_to_irq() 失败时返回负错误码，不能用 unsigned。 */

	atomic_t key_state;
};

static struct dts_key_dev dts_key;

static void timer_callback(struct timer_list *timer)
{
	struct dts_key_dev *dev = from_timer(dev, timer, timer);
	int state;

	/* 定时器回调不能睡眠；初始化时已排除需要睡眠的 GPIO。 */
	state = gpiod_get_value(dev->gpio);
	if (state < 0) {
		pr_err_ratelimited("dts_key: cannot read GPIO: %d\n", state);
		return;
	}
	if (state == 1) {
		/* 按下 */
		if (atomic_read(&dev->key_state) != 1) {
			atomic_set(&dev->key_state, 1);
			pr_info("key is pressed\n");
			// 上报按键按下事件
			// KEY_ON 表示按键按下，KEY_OFF 表示按键松开
			// KEY_CODE 表示按键代码，KEY_0 表示数字 0，KEY_ENTER 表示回车键
			input_report_key(dev->input, KEY_CODE, KEY_ON);

		}
	} else {
		/* 松开 */
		if (atomic_read(&dev->key_state) != 0) {
			atomic_set(&dev->key_state, 0);
			pr_info("key is released\n");
			input_report_key(dev->input, KEY_CODE, KEY_OFF);
		}
	}

	/* Input core 维护按键状态，重复的相同状态不会重复通知应用。 */
	input_report_key(dev->input, KEY_CODE, state);
	/* EV_SYN/SYN_REPORT 表示这一组事件上报完毕。 */
	input_sync(dev->input);
}

static irqreturn_t key_isr(int irq, void *dev_id)
{
	struct dts_key_dev *dev = dev_id;

	/* 每次边沿都把采样推迟 20 ms，等待机械触点抖动结束。 */
	mod_timer(&dev->timer, jiffies + msecs_to_jiffies(KEY_DEBOUNCE_MS));
	return IRQ_HANDLED;
}

static int __init dts_key_init(void)
{
	struct device_node *np;
	int ret;

	atomic_set(&dts_key.key_state, KEY_OFF);

	np = of_find_node_by_path(KEY_NODE_PATH);
	if (!np) {
		pr_err("dts_key: cannot find %s\n", KEY_NODE_PATH);
		return -ENODEV;
	}

	/* NULL 对应 gpios 属性，0 为第一根 GPIO，GPIOD_IN 设置输入。 */
	dts_key.gpio = fwnode_gpiod_get_index(of_fwnode_handle(np), NULL,
					      0, GPIOD_IN, KEY_NAME);
	of_node_put(np);
	if (IS_ERR(dts_key.gpio)) {
		ret = PTR_ERR(dts_key.gpio);
		pr_err("dts_key: cannot request GPIO: %d\n", ret);
		return ret;
	}

	/* H3 片上 GPIO 不需要睡眠；I2C/SPI GPIO 应改用工作队列消抖。 */
	if (gpiod_cansleep(dts_key.gpio)) {
		pr_err("dts_key: timer debounce requires a non-sleeping GPIO\n");
		ret = -EOPNOTSUPP;
		goto err_put_gpio;
	}

	dts_key.irq_num = gpiod_to_irq(dts_key.gpio);
	if (dts_key.irq_num < 0) {
		ret = dts_key.irq_num;
		pr_err("dts_key: cannot get IRQ: %d\n", ret);
		goto err_put_gpio;
	}

	dts_key.input = input_allocate_device();
	if (!dts_key.input) {
		ret = -ENOMEM;
		goto err_put_gpio;
	}

	dts_key.input->name = KEY_NAME;

	__set_bit(EV_KEY,  dts_key.input->evbit);/* 设置产生按键事件*/
	__set_bit(EV_REP, dts_key.input->evbit);/* 重复事件*/
	__set_bit(KEY_CODE, dts_key.input->keybit);

	// dts_key.input->phys = "wukongpi/key0";
	// dts_key.input->id.bustype = BUS_HOST;
	// /* 声明支持 EV_KEY 类型和 KEY_0 键码；不启用长按自动重复。 */
	// input_set_capability(dts_key.input, EV_KEY, KEY_CODE);

	/* 必须在 request_irq() 前初始化，注册中断后 ISR 可立即运行。 */
	timer_setup(&dts_key.timer, timer_callback, 0);

	ret = input_register_device(dts_key.input);
	if (ret) {
		pr_err("dts_key: cannot register input device: %d\n", ret);
		goto err_free_input;
	}

	/* 按下和松开都需要上报，因此同时监听物理上升沿和下降沿。 */
	ret = request_irq(dts_key.irq_num, key_isr,
			  IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
			  KEY_NAME, &dts_key);
	if (ret) {
		pr_err("dts_key: cannot request IRQ: %d\n", ret);
		goto err_unregister_input;
	}

	/* 主动采样一次，覆盖加载模块时按键已按下、没有新边沿的情况。 */
	mod_timer(&dts_key.timer,
		  jiffies + msecs_to_jiffies(KEY_DEBOUNCE_MS));
	pr_info("dts_key: loaded, input name=%s, code=%u, irq=%d\n",
		KEY_NAME, KEY_CODE, dts_key.irq_num);
	return 0;

err_unregister_input:
	/* 注册成功后只能 unregister；它也会释放 input_dev 的引用。 */
	input_unregister_device(dts_key.input);
	goto err_put_gpio;
err_free_input:
	/* 注册失败时仍由本驱动释放未注册的 input_dev。 */
	input_free_device(dts_key.input);
err_put_gpio:
	gpiod_put(dts_key.gpio);
	return ret;
}

static void __exit dts_key_exit(void)
{
	/* 先停止事件来源，防止回调访问已注销的 Input 设备或 GPIO。 */
	free_irq(dts_key.irq_num, &dts_key);
	timer_shutdown_sync(&dts_key.timer);
	input_unregister_device(dts_key.input);
	gpiod_put(dts_key.gpio);
	pr_info("dts_key: unloaded\n");
}

module_init(dts_key_init);
module_exit(dts_key_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("Device-tree GPIO key using the Linux input subsystem");
