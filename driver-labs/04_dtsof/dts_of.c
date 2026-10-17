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



static int __init dts_of_init(void)
{
    int ret = 0;
    /*find of node by path*/
    struct device_node *of_node;
    const struct property *prop;
    
    of_node = of_find_node_by_path("/vdd-cpux-regulator");
    if (!of_node) {
        pr_err("Failed to find of node\n");
        ret=-ENODEV;
        goto err_find_node;
    

    /*get node property*/

    prop = of_get_property(of_node, "compatible", NULL);
    if (!prop) {
        pr_err("Failed to get property\n");
        ret=-ENODEV;
        goto err_get_property;
    }
    //printk("compatible: %s\n", (char *)prop->value);
    

    /*get node property*/
    const char *regulator_type;
    ret = of_property_read_string(of_node, "regulator-type", &regulator_type);
    if (ret) {
        pr_err("Failed to read regulator-type property\n");
        goto err_get_property;
    }
    printk("regulator-type: %s\n", regulator_type);


    return 0;


    err_find_node:
        return ret;
    err_get_property:
        return ret;
    
}

static void __exit dts_of_exit(void)
{
  
}

module_init(dts_of_init);
module_exit(dts_of_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("WuKong Pi driver lab");
MODULE_DESCRIPTION("WuKong Pi H3 dts of use");
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