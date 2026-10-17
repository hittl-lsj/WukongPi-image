# 通过现有设备树节点控制红灯

这是 Linux 6.12.3 的完整字符设备参考驱动，供你逐段对照
`../dts_led.c` 中自己的实现。它显式查找 `/leds/status-led`，通过 GPIO
描述符控制红灯，保留 `.open/.read/.write/.release`。

本课还不引入 `platform_driver`。后续学习平台驱动时，再把显式查找节点
改为 `compatible` 匹配和 `.probe()` 中获取资源。

## 设备树为什么这样写

这份驱动使用现有节点，不需要再加一个 LED 节点。板子实际加载的设备树应有：

```dts
leds {
    compatible = "gpio-leds";

    /* 原来的绿灯节点继续保留。 */

    status-led {
        status = "disabled";
        label = "wukongpi:red:status";
        gpios = <&pio 0 17 GPIO_ACTIVE_HIGH>;
        default-state = "on";
    };
};
```

`status = "disabled"` 让内核已有的 `gpio-leds` 跳过这个红灯子节点，避免
与我们的实验驱动争用 PA17。节点并没有被删除，`of_find_node_by_path()`
仍可以找到它。这个实验模块显式申请其 GPIO，不依赖自动设备匹配。

`gpios` 的 `&pio` 指向 H3 主 GPIO 控制器，`0 17` 是 A 组第 17 根引脚，
即 PA17；`GPIO_ACTIVE_HIGH` 表示高电平有效。驱动不写死 PA17 或寄存器地址，
而是交给 GPIO 框架解析这些信息。

`label` 供标准 LED 驱动命名设备使用，本实验不读取它；它不决定 GPIO 引脚
或输出状态。

`default-state` 可选，本驱动自己读取并处理它：`on` 表示初始化时点亮，
`off` 表示关闭，`keep` 表示读取当前 GPIO 状态并以该状态设置为输出；
没有这个属性时默认关闭。`keep` 表示逻辑上的保留，不承诺引脚模式切换
过程中绝对没有电气毛刺。

原来的 `linux,default-trigger = "heartbeat"` 不会被本驱动处理，因为
本驱动没有注册 LED 类设备，也没有实现 LED 触发器。

## 按调用顺序理解驱动

| 位置 | 工作及原因 |
| --- | --- |
| `dts_led_init()` | 找到节点，读取初始状态，申请 GPIO，再注册字符设备。硬件准备好后才允许应用使用。 |
| `fwnode_gpiod_get_index()` | 解析并申请 GPIO；`con_id=NULL` 查找 `gpios`，索引 `0` 取第一根。返回错误指针时必须停止。 |
| `of_node_put()` | 配对释放查找节点时取得的引用。GPIO 申请成功后有自己的资源生命周期。 |
| `alloc_chrdev_region()` | 动态分配设备号，避免固定主设备号与其他驱动冲突。 |
| `class_create()/device_create()` | 注册类和设备；当前系统的 devtmpfs 自动提供 `/dev/dts_led`。 |
| `cdev_init()/cdev_add()` | 将设备号与 `file_operations` 联系起来。本例最后执行 `cdev_add()`，完成发布。 |
| `.open()` | 把设备对象保存在 `file->private_data`，供后续读写使用；不改变灯。 |
| `.write()` | 检查长度、拷贝并验证命令，再设置 GPIO；成功返回实际处理的字节数。 |
| `.read()` | 读取逻辑状态，输出文本 `0\n` 或 `1\n`；读完返回 EOF，避免 `cat` 一直打印。 |
| `.release()` | 关闭文件，保留灯的最后状态。否则测试程序一退出，刚点亮的灯就会熄灭。 |
| `dts_led_exit()` | 删除字符设备、销毁设备节点和类、释放设备号，最后熄灯并释放 GPIO。 |

`copy_from_user()` 返回的是未拷贝成功的字节数，不是负错误码。因此检查
“是否非零”，而不能用 `< 0` 判断失败。不能在内核里直接解引用用户指针。

`mutex` 保护读状态和写状态，尤其保证翻转的“读旧状态、写新状态”不会被
另一个线程打断。`gpiod_*_cansleep()` 允许底层访问睡眠，适合这些文件操作
的进程上下文；不要直接把它搬进硬中断处理函数。

设置方向、复用和输出电平的底层工作仍然需要做，只是由现有 H3 GPIO/pinctrl
驱动完成。本实验不再自己 `ioremap()` 和读写 PIO 寄存器。

## 在电脑上编译

```sh
cd /home/hitle/桌面/H3/driver-labs/05_dts_led/reference
make
```

`make` 同时生成 `dts_led.ko` 和 `ledapp`。模块交给 Kbuild 编译，使用
当前内核构建目录的配置、生成头文件和符号表；应用使用当前 Buildroot
交叉工具链，保持目标架构和 C 库匹配。本次不需要重建整套镜像。

模块必须配合生成它的内核构建结果使用；仅 `uname -r` 同为 `6.12.3`
不能保证两个不同配置或不同构建的内核都兼容。

## 传到开发板

电脑与板子以太网、SSH 已连通时，在上面的参考目录执行：

```sh
scp -O dts_led.ko ledapp root@10.20.30.2:/tmp/
```

`-O` 使用传统 SCP 协议，适合没有 SFTP 服务端组件的精简 Dropbear 系统。
`/tmp` 是临时目录，重启后文件会消失，适合反复实验。

## 在开发板加载和测试

以下命令在板子的串口或 SSH shell 中执行。

```sh
lsmod
tr -d '\000' < /proc/device-tree/leds/status-led/status
```

第二条读取正在运行的设备树，应该输出 `disabled`。它比查看电脑上 DTS
源码更可靠，因为源码可能还没有更新到板子使用的 DTB。

如果 `lsmod` 中有之前的 `newcharled` 或 `led` 实验模块，先分别使用
`rmmod newcharled` 或 `rmmod led` 卸载，再加载本模块。直接访问寄存器的
旧模块可能不登记 GPIO 所有权，不能靠 GPIO 框架发现这种冲突。

```sh
insmod /tmp/dts_led.ko
ls -l /dev/dts_led
dmesg | tail -n 10
```

`insmod` 加载明确路径的模块；驱动初始化成功后，设备节点自动出现，
日志显示动态分配的主、次设备号，不需要手动 `mknod`。

```sh
chmod 755 /tmp/ledapp
/tmp/ledapp /dev/dts_led 1
cat /dev/dts_led
/tmp/ledapp /dev/dts_led 0
cat /dev/dts_led
/tmp/ledapp /dev/dts_led 2
cat /dev/dts_led
```

`chmod` 保证应用有执行权限。三次命令依次表示点亮、熄灭、翻转；读出的
状态应依次为 `1`、`0`、`1`，并且应对照观察红灯。

应用把命令行字符 `'1'` 转成数值 `1`，再发送一个字节；驱动没有采用
文本写入协议。因此不要用 `echo 1 > /dev/dts_led` 测试，它发送的是
字符 `'1'`（数值 49）及换行，本驱动会返回 `EINVAL`。

测试结束可以执行：

```sh
rmmod dts_led
```

正常卸载会熄灯并释放资源。若应用仍然打开设备，`owner = THIS_MODULE`
会保持模块引用，使正常卸载返回“模块正在使用”。

## 常见失败的含义

- `cannot find /leds/status-led`：板子实际 DTB 没有这个路径。
- `keep ... disabled for this lab`：节点仍启用，先更新设备树以释放红灯。
- `cannot request GPIO: -16`：GPIO 或引脚复用资源被其他使用者占用。
- `cannot request GPIO: -517`：GPIO 控制器尚未就绪；本例是显式加载模块，
  没有平台驱动的自动延迟探测重试，需要就绪后重新加载。
- `write: Invalid argument`：命令长度或数值不符合单字节 `0/1/2` 协议。
- `Invalid module format`：检查板子内核与本地模块构建结果是否匹配。

本地交叉编译能验证 Linux 6.12.3 API 和符号是否可用，实际 GPIO 行为仍须
在板子上完成上述测试。
