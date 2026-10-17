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
#include <linux/of.h>  
#include <linux/gpio/consumer.h>
#include <linux/of_gpio.h>

#define LED_NAME "dts_led"
#define LED_NODE_PATH "/leds/status-led"

struct newcharled_dev
{
    dev_t devt; //设备号
    struct cdev led_cdev;//字符设备结构体
    struct class *led_class;//类结构体指针
    struct device *led_device;//设备结构体指针
    int major; //主设备号
    int minor; //次设备号
	struct gpio_desc *gpio;//gpio描述符结构体指针
	struct mutex lock; //互斥锁
};


static struct newcharled_dev newcharled;

static bool led_state;//led状态
static int flags=0;//gpio标志位


static void dts_led_led_set(bool on)
{

    gpiod_set_value(newcharled.gpio, on);

}

static int led_open(struct inode *inode, struct file *file)
{
    mutex_lock(&newcharled.lock);//加锁
    dts_led_led_set(true);//打开led
	
	mutex_unlock(&newcharled.lock);//解锁
    return 0;
}

static int led_release(struct inode *inode, struct file *file)
{
	pr_info("dts_led: release\n");
	dts_led_led_set(false);//关闭led
    return 0;
}

static ssize_t led_read(struct file *file, char __user *buf,
                        size_t count, loff_t *ppos)
{
    led_state = gpiod_get_value(newcharled.gpio);
    return 0;
}

static ssize_t led_write(struct file *file,
                         const char __user *buf,
                         size_t count, loff_t *ppos)
{
    int cmd = 0;
	

    return 0;
}

static const struct file_operations led_fops = {
    .owner   = THIS_MODULE,
    .open    = led_open,
    .read    = led_read,
    .write   = led_write,
    .release = led_release,
};

static int __init dts_led_init(void)
{
	int ret;

	struct device_node *np;
	np = of_find_node_by_path(LED_NODE_PATH);
	if (!np) {
		pr_err("Failed to find of node\n");
		ret=-ENODEV;
		goto err_find_node;
	}
	
	if (!newcharled.gpio) {
		pr_err("Failed to get gpio\n");
		ret=-ENODEV;
		goto err_get_gpio;
	}
	if(of_find_property(np, "default-state", NULL)) {
		    /*get node property*/
		const char *regulator_type;
		ret = of_property_read_string(np, "default-state", &regulator_type);
		if (ret) {
			pr_err("Failed to read default-state property\n");
			goto err_get_property;
		}
		printk("default-state: %s\n", regulator_type);


		if (!strcmp(regulator_type, "on")) {
			flags = GPIOD_OUT_HIGH;
		} else if (!strcmp(regulator_type, "off")) {
			flags = GPIOD_OUT_LOW;
		} else if (!strcmp(regulator_type, "keep")) {
			flags = GPIOD_ASIS;
		} else {
			pr_err("dts_led: invalid default-state: %s\n", regulator_type);
			ret = -EINVAL;
			goto err_put_node;
		}
	}
	newcharled.gpio = fwnode_gpiod_get_index(of_fwnode_handle(np), NULL,
					      0, flags, LED_NAME);
	of_node_put(np);
	

	if (newcharled.major == 0) {
		newcharled.devt = MKDEV(newcharled.major, newcharled.minor);
		ret = register_chrdev(newcharled.devt, LED_NAME, &led_fops);//注册字符设备驱动
	}else {
		ret = alloc_chrdev_region(&newcharled.devt, 0, 1, LED_NAME);//分配设备号
		newcharled.major = MAJOR(newcharled.devt);
		newcharled.minor = MINOR(newcharled.devt);
	}
	if (ret < 0) {
		pr_err("Failed to register char device\n");
		goto err_register_chrdev;
	}	

	newcharled.led_cdev.owner = THIS_MODULE;//设置所有者
	cdev_init(&newcharled.led_cdev, &led_fops);//初始化字符设备结构体
	ret = cdev_add(&newcharled.led_cdev, newcharled.devt, 1);//添加字符设备到内核

	if(ret < 0) {
		pr_err("Failed to add cdev\n");
		goto err_register_chrdev;
	}
	newcharled.led_class = class_create(LED_NAME);//创建设备类
	newcharled.led_device = device_create(newcharled.led_class, NULL, newcharled.devt, NULL, LED_NAME);//创建设备节点
	mutex_lock(&newcharled.lock);//加锁
    dts_led_led_set(false);//关闭led
    mutex_unlock(&newcharled.lock);//解锁
	
	err_register_chrdev:
	err_put_node:
    err_get_property:
    err_get_gpio:
    err_find_node:	
	return ret;
}

static void __exit dts_led_exit(void)
{
    unregister_chrdev_region(newcharled.devt, 1);
    cdev_del(&newcharled.led_cdev);
    device_destroy(newcharled.led_class, newcharled.devt);
    class_destroy(newcharled.led_class);
}

module_init(dts_led_init);
module_exit(dts_led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("WuKong Pi H3 dts led of use");

/*
/dts-v1/;

#include "sun8i-h3.dtsi"
#include "sunxi-commo n-regulators.dtsi"
#include <dt-bindings/interrupt-controller/irq.h>
#include <dt-bindings/gpio/gpio.h>

/ {
	model = "WuKong Pi H3";
	compatible = "wukong,wukongpi-h3", "allwinner,sun8i-h3";

	aliases {
		serial0 = &uart0;
		ethernet0 = &emac;
		ethernet1 = &xr819;
	};

	chosen {
		stdout-path = "serial0:115200n8";
	};

	leds {
		compatible = "gpio-leds";

		power-led {
			label = "wukongpi:green:power";
			gpios = <&r_pio 0 10 GPIO_ACTIVE_HIGH>;
			default-state = "on";
		};

		status-led {
    		status = "disabled";
    		abel = "wukongpi:red:status";
			gpios = <&pio 0 17 GPIO_ACTIVE_HIGH>;
			linux,default-trigger = "heartbeat";
		};
	};

	reg_vdd_cpux: vdd-cpux-regulator {
		compatible = "regulator-gpio";
		regulator-name = "vdd-cpux";
		regulator-type = "voltage";
		regulator-boot-on;
		regulator-always-on;
		regulator-min-microvolt = <1100000>;
		regulator-max-microvolt = <1300000>;
		regulator-ramp-delay = <50>;

		gpios = <&r_pio 0 6 GPIO_ACTIVE_HIGH>;
		enable-active-high;
		gpios-states = <1>;
		states = <1100000 0>, <1300000 1>;
	};
		reg_vcc_wifi: reg-vcc-wifi {
		compatible = "regulator-fixed";
		regulator-name = "vcc-wifi";
		regulator-min-microvolt = <3300000>;
		regulator-max-microvolt = <3300000>;
		gpio = <&pio 0 20 GPIO_ACTIVE_HIGH>;
		enable-active-high;
	};

	wifi_pwrseq: wifi-pwrseq {
		compatible = "mmc-pwrseq-simple";
		reset-gpios = <&r_pio 0 7 GPIO_ACTIVE_LOW>;
		post-power-on-delay-ms = <200>;
	};
};

&cpu0 {
	cpu-supply = <&reg_vdd_cpux>;
};

&mmc0 {
	vmmc-supply = <&reg_vcc3v3>;
	bus-width = <4>;
	cd-gpios = <&pio 5 6 GPIO_ACTIVE_LOW>;
	status = "okay";
};

&uart0 {
	pinctrl-names = "default";
	pinctrl-0 = <&uart0_pa_pins>;
	status = "okay";
};

&emac {
	phy-handle = <&int_mii_phy>;
	phy-mode = "mii";
	allwinner,leds-active-low;
	status = "okay";
};

&ehci0 {
	status = "okay";
};

&ohci0 {
	status = "okay";
};

&ehci1 {
	status = "okay";
};

&ohci1 {
	status = "okay";
};

&usb_otg {
	dr_mode = "peripheral";
	status = "okay";
};

&usbphy {
	usb0_id_det-gpios = <&pio 6 12 GPIO_ACTIVE_HIGH>;
	status = "okay";
};
&mmc1 {
	vmmc-supply = <&reg_vcc_wifi>;
	mmc-pwrseq = <&wifi_pwrseq>;
	bus-width = <4>;
	non-removable;
	status = "okay";

	xr819: wifi@1 {
		reg = <1>;
		compatible = "xradio,xr819";
		interrupt-parent = <&pio>;
		interrupts = <6 10 IRQ_TYPE_EDGE_RISING>;
	};
};

*/