# GPIO 按键接入 Linux Input 子系统

本实验保留 `/key` 的 GPIO、双边沿中断和 20 ms 定时器消抖，将原来的
`/dev/dts_key` 自定义接口换成标准 Input/evdev 接口。`irq.c` 编译为
`irq.ko`，`keyapp.c` 编译为读取标准事件的 `keyapp`。

## 本次改了什么、系统需要哪些配置

| 层次 | 本次改动或当前要求 | 为什么 |
| --- | --- | --- |
| 驱动 `irq.c` | 使用 `input_allocate_device()`、能力声明、注册和事件上报；去掉自定义字符设备接口；修正初始化及释放顺序。 | 让按键接入标准 Input/evdev 接口，并保证异步回调访问的对象仍有效。 |
| 应用 `keyapp.c` | 从读取自定义 int 改为阻塞读取 `struct input_event`。 | `/dev/input/eventX` 的数据格式由 Input 子系统规定。 |
| 设备树 | 本次未修改，沿用现有 `/key` 节点和 PA6 低电平有效的 GPIO 描述。 | 驱动仍通过 `/key` 取得 GPIO；换接口不等于换硬件连接。 |
| Linux 内核 | 本次未修改配置；本地已有 `CONFIG_INPUT=y`、`CONFIG_INPUT_EVDEV=y`、`CONFIG_MODULES=y`。 | 分别提供 Input 核心、事件设备节点和可加载模块支持。 |
| Buildroot | 本次未修改配置，也不要求新增软件包；`evtest` 为可选工具。 | 本实验的 `keyapp` 可直接观察事件，evdev 本身是内核功能。 |

若要重新检查内核，在 Buildroot 源码目录执行：

```sh
cd /home/hitle/桌面/H3/buildroot-2026.02.3
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  linux-menuconfig
```

菜单中的主要位置如下，`[*]` 表示编入内核：

```text
[*] Enable loadable module support               CONFIG_MODULES（主菜单）
Device Drivers
  Input device support
    [*] Event interface                         CONFIG_INPUT_EVDEV
```

可按 `/` 搜索 `INPUT`、`INPUT_EVDEV`、`MODULES`，确认符号与位置。
`CONFIG_INPUT` 默认启用，其 `Generic input layer` 开关只有启用 `EXPERT`
时才显示，不必为了找这个开关额外开启 `EXPERT`。GPIO 和 H3
引脚控制也须可用，本地已有 `CONFIG_GPIOLIB=y` 和
`CONFIG_PINCTRL_SUN8I_H3=y`。不需要把 `/key` 的 `compatible` 改成
`gpio-keys`：那会改用内核自带按键驱动，本实验是自己实现 Input 驱动。

如需安装 `evtest`，使用另一个菜单：

```sh
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  menuconfig
```

按 `/` 搜索 `BR2_PACKAGE_EVTEST` 后进入对应位置启用。这个菜单配置的是
Buildroot 软件包，不是内核的 `CONFIG_INPUT_EVDEV`。此处仅给出操作方法，
不表示已替你运行菜单或安装 `evtest`。

设备树修改后、两个菜单保存后，如何保留配置、增量编译并生成
`sdcard.img`，见[项目 README 的完整流程](../../README.md#修改配置后重新生成镜像)。
特别注意：`linux-rebuild` 后还须顶层 `make` 才会重新打包整卡镜像；
Buildroot 的 `savedefconfig` 不保存内核菜单中的改动。

## 工作原理

```text
按下/松开 PA6 -> GPIO IRQ -> mod_timer() 延后 20 ms
    -> 定时器读取 GPIO -> input_report_key() -> input_sync()
    -> Input core -> evdev -> /dev/input/eventX -> keyapp
```

中断里只重置定时器，不忙等。抖动产生多个边沿时，`mod_timer()` 不断
推迟到期时间，等最后一次边沿的 20 ms 后采样。这是延迟采样消抖，短于
消抖窗口的快速按下/松开可能不会被记录。

| 接口 | 作用及原因 |
| --- | --- |
| `input_allocate_device()` | 分配 Input 设备描述对象。 |
| `input_set_capability(input, EV_KEY, KEY_0)` | 声明事件类型和键码，让 Input handler 知道设备支持什么。 |
| `input_register_device()` | 注册给 Input core，已有的 evdev handler 提供标准事件节点。 |
| `input_report_key(input, KEY_0, state)` | 上报按下=1、松开=0，Input core 过滤重复的相同状态。 |
| `input_sync(input)` | 产生 `EV_SYN/SYN_REPORT`，标记一组事件结束，不是 CPU 内存屏障。 |

不再自行分配字符设备号、创建 class、实现 `file_operations`、等待队列和
`copy_to_user()`；这些用户接口工作由 Input/evdev 完成。用户程序改读
`struct input_event`，包含时间戳、`type`、`code`、`value`，不能再只读 int。
无事件时阻塞在 `read()`，不需要 `usleep()` 轮询。

`KEY_0` 是键盘数字 0 的标准键码（11），不是 PA6 编号或设备次设备号。
想换键可修改 `KEY_CODE`；普通按钮也可用 `BTN_0`。本例未启用 `EV_REP`，
所以长按不会自动重复。`GPIO_ACTIVE_LOW` 已处理极性，读取的逻辑值是
按下=1、松开=0，不要再次取反。Input 状态去重不能替代机械消抖。

## 硬件和资源生命周期

设备树源码是
[`sun8i-h3-wukongpi.dts`](../../wukongpi-br2/board/wukongpi-h3/dts/allwinner/sun8i-h3-wukongpi.dts)。
现有节点的相关内容如下，`key` 位于根节点 `/`，引脚配置位于 `&pio`：

```dts
/ {
    key {
        compatible = "wukongpi,key";
        pinctrl-names = "default";
        pinctrl-0 = <&key_pa6_pins>;
        gpios = <&pio 0 6 GPIO_ACTIVE_LOW>;
        interrupt-parent = <&pio>;
        interrupts = <0 6 IRQ_TYPE_EDGE_BOTH>;
        status = "okay";
    };
};

&pio {
    key_pa6_pins: key_pa6_pins {
        pins = "PA6";
        function = "gpio_in";
        bias-pull-up;
    };
};
```

`&pio 0 6` 表示 PIO 控制器的 A 组第 6 根引脚，即 PA6；
`GPIO_ACTIVE_LOW` 表示低电平为有效状态，`IRQ_TYPE_EDGE_BOTH` 描述双边沿。
这些宏来自源码顶部已有的 GPIO 和中断绑定头文件。

板上实际 DTB 须有 `/key` 节点及 `gpios = <&pio 0 6 GPIO_ACTIVE_LOW>`。
本课保留 `of_find_node_by_path()`，没有引入 platform_driver。
`fwnode_gpiod_get_index()` 申请 GPIO 并设为输入，`gpiod_to_irq()` 映射 IRQ。
IRQ 用 `int` 保存，因为失败结果是负数。本驱动通过 GPIO 映射 IRQ，不
读取节点的 `interrupts` 属性；双边沿标志保证按下和松开都能触发采样。

按下时 PA6 接 GND，松开时须由可靠上拉保持高电平。本驱动不自动应用
`pinctrl-0`，仅在 DTS 写 `bias-pull-up` 不保证被应用；需要外接上拉或
已正确配置的引脚，不能悬空。其他占用 PA6 的实验模块须先卸载。

定时器回调不能睡眠，故初始化检查 `gpiod_cansleep()`；H3 片上 GPIO
适用，I2C/SPI 扩展 GPIO 需要工作队列等可睡眠的处理方式。

先初始化定时器、注册 Input，再申请 IRQ，避免中断访问未就绪对象。
加载后主动安排一次采样，覆盖加载时已按下却没有新边沿的情况。
卸载按 `free_irq()` -> `timer_shutdown_sync()` ->
`input_unregister_device()` -> `gpiod_put()`，先停止异步回调再释放资源。
注册失败用 `input_free_device()`；注册成功用 `input_unregister_device()`，
不能额外再 free 一次。

## 编译与验证

电脑上执行：

```sh
cd /home/hitle/桌面/H3/driver-labs/14-input
make
scp -O irq.ko keyapp root@10.20.30.2:/tmp/
```

Makefile 使用现有 Buildroot 工具链和 Linux 构建目录，无须重建整张镜像。
本地内核已启用 `CONFIG_INPUT=y`、`CONFIG_INPUT_EVDEV=y`。模块须与板上
内核构建兼容，不能只凭同为 `6.12.3` 判断一致。

### 如果要把实验文件放进镜像

上面的 `make` 只在实验目录生成文件，Buildroot 不会自动收集仓库里的
`irq.ko` 和 `keyapp`。如果想随镜像携带它们，可在内核编译完成后执行：

```sh
cd /home/hitle/桌面/H3/buildroot-2026.02.3
make -C ../driver-labs/14-input

mkdir -p ../wukongpi-br2/board/wukongpi-h3/rootfs-overlay/root/input-lab
cp ../driver-labs/14-input/irq.ko ../driver-labs/14-input/keyapp \
  ../wukongpi-br2/board/wukongpi-h3/rootfs-overlay/root/input-lab/

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2
```

先编译实验，是为了让模块匹配刚构建的内核；再复制到 overlay，文件会以
相同的相对路径进入目标系统 `/root/input-lab/`；最后的顶层 `make` 才会
重新打包 `rootfs.ext4` 和 `sdcard.img`。这只是手工打包方法，每次修改驱动
或更换内核都要重新编译和复制，不会自动加载模块。

刷写并启动新镜像后，可用 `insmod /root/input-lab/irq.ko` 加载，再用
`/root/input-lab/keyapp /dev/input/eventX` 验证。若采用前面的 SCP 方法，
则继续使用下面的 `/tmp/` 路径。

### 板上验证

板上先用 `lsmod` 检查并卸载占用 PA6 的旧实验模块，再执行：

```sh
insmod /tmp/irq.ko
dmesg | tail -n 15
cat /proc/bus/input/devices
```

找到 `Name="dts_key"` 的一段，读取同段 `Handlers=` 中的 `eventX`。
编号动态分配，不保证是 `event0`。假设是 `event2`：

```sh
/tmp/keyapp /dev/input/event2
```

按下一次再松开，预期输出：

```text
EV_KEY code=11 value=1 (pressed)
SYN_REPORT
EV_KEY code=11 value=0 (released)
SYN_REPORT
```

也可使用已安装的 `evtest /dev/input/event2`。`cat` 输出是二进制，不适合
观察；应用启动前发生的事件不会作为历史记录重放。本课应用遇到
`SYN_DROPPED` 会报告队列溢出并退出，不实现完整的状态恢复。

退出应用后执行 `rmmod irq`，检查卸载正常。交叉编译只验证 API 和符号，
GPIO 电平、消抖、事件输出及卸载仍须实机验证。

参考：[Linux Input 驱动文档](https://docs.kernel.org/input/input-programming.html)。
