# MPU6050 字符设备测试程序

本实验使用自己编写的 I2C 字符设备驱动。测试程序访问
`/dev/mpu6050_dev`，不直接访问 `/dev/i2c-0`，也不使用 IIO 或 input 接口。

## 驱动锁与失败回滚的修改流程

这次修改 `i2c_dir_dev.c`，保留一次读取 14 字节的接口和现有芯片初始化。
`mpu6050.h` 显式包含 `<linux/mutex.h>`，不依赖其他头文件间接声明互斥锁。

1. 先修改 `read()` 的锁范围。参数检查放在加锁前；成功加锁后，检查
   `dev->client` 和整个 I2C 传输都在锁内完成。原来的代码在发现空指针时
   带锁返回，正常路径又在传输前解锁，不能和设备解绑互斥。现在错误路径
   跳到 `out_unlock`，确保每条已加锁的路径都先解锁。使用
   `mutex_lock_interruptible()`，等待锁时可被信号中断，失败直接返回错误。
2. 把 `copy_to_user()` 放在解锁之后。I2C 读取已经得到当前调用自己的
   `data[14]` 局部快照，后续复制不再依赖 `client`，无需继续阻塞解绑。
   已经完成采样的那次读取可以成功返回；解绑后的新读取返回 `ENODEV`。
3. 将 `mutex_init()` 提前到 `probe()` 的字符设备注册之前。`cdev_add()`
   成功后接口就可能被调用，所以锁和 `client` 必须先准备好。`open()`
   同样在锁内检查 `client`，解绑期间到达的打开请求会返回 `ENODEV`。
4. 分别在 `class_create()`、`device_create()` 失败处用 `PTR_ERR()` 取出
   错误，再跳转清理。不要在共用标签里覆盖 `ret`，否则会丢掉实际错误。
   分配设备号成功后才填写主次设备号；分配失败不执行注销。
5. 清理标签按成功申请的相反顺序排列：设备创建失败时销毁 class，随后
   删除 cdev，再注销设备号；class 创建失败则直接从删除 cdev 开始。
   不销毁未成功创建的对象，不重复调用 `cdev_del()`。
6. `probe()` 用 `i2c_set_clientdata(client, dev)` 保存关联；`remove()`
   用 `i2c_get_clientdata()` 找回同一个状态结构。这只保存指针，不会分配
   或复制结构体。`device_create()` 的父设备改为 `&client->dev`，让字符
   设备在设备模型中归属于对应的 I2C 设备。
7. `remove()` 先获取同一把锁，等待正在进行的传输结束，再将共享的
   `dev->client` 置空，然后解锁并撤销字符设备资源。删除原来的
   `client = NULL`：它只修改函数的局部参数。`cdev_del()` 不会让已有文件
   描述符失效，旧描述符后续读取必须通过状态检查返回 `ENODEV`。
   `probe()` 的失败出口也在锁内清空 `client`，处理接口短暂发布过的情况。

下面的时序是这把锁需要保证的核心关系：

```text
read():   加锁 → 检查 client → I2C 读取 → 解锁 → copy_to_user
remove(): 等待同一把锁 → client 置空 → 解锁 → 撤销设备接口
```

本版仍是一个全局状态的单设备实验，不支持绑定多个 MPU6050。测试解绑后
必须先关闭所有旧文件描述符，再重新绑定；保留旧描述符跨越重新绑定的
生命周期管理没有在本版实现。

### 构建与板上验证

在电脑上编译并传输：

```sh
cd /home/hitle/桌面/H3/driver-labs/17-I2C_MPU6050
make kernel_modules W=1
scp -O i2c_dir_dev.ko root@10.20.30.2:/tmp/
```

这里仅重编译外部模块，不必重编内核或烧写 SD 卡；新模块仍要求板上运行
与本次构建匹配的内核。以下命令在板上执行，先退出所有测试 APP：

```sh
rmmod i2c_dir_dev
insmod /tmp/i2c_dir_dev.ko
/tmp/mpu6050app /dev/mpu6050_dev 10
```

若模块尚未加载，跳过 `rmmod`。先检查正常读取，再检查过小请求：

```sh
dd if=/dev/mpu6050_dev of=/dev/null bs=13 count=1
/tmp/mpu6050app /dev/mpu6050_dev 2
```

第一条预期报 `Invalid argument`，因为驱动要求完整的 14 字节一帧；
第二条应正常读取，确认一次错误请求不会破坏后续访问。

最后用一个保持打开的描述符验证解绑路径。先退出其他 APP，在同一个板子
终端依次执行：

```sh
exec 3</dev/mpu6050_dev
dd bs=14 count=1 <&3 of=/dev/null
printf '%s' '0-0068' > /sys/bus/i2c/drivers/MPU6050/unbind
dd bs=14 count=1 <&3 of=/dev/null
dd bs=14 count=1 <&3 of=/dev/null
exec 3<&-
printf '%s' '0-0068' > /sys/bus/i2c/drivers/MPU6050/bind
/tmp/mpu6050app /dev/mpu6050_dev 2
```

`exec 3<...` 让当前 shell 保留设备描述符，`<&3` 将它交给 `dd` 读取。
解绑前应读到 14 字节，解绑后的两次读取都应立即报 `No such device`，
没有第二次卡住的现象。`exec 3<&-` 先关闭旧描述符，然后才重新绑定。
这里通过 sysfs 解绑来触发 `remove()`；持有文件描述符时正常 `rmmod`
会受到模块引用计数保护，不能用它代替这项解绑验证。

本地模块编译通过不等于这些板上运行路径已经验证，需以上板结果为准。

## 本次改动及原因

1. `MPU6050APP.c`：把原来每次读取一个 `int` 的程序改成每次读取 14 字节。
   当前驱动从 `0x3b` 开始读取完整一组数据，小于 14 字节的请求会返回
   `EINVAL`。程序检查 `read()` 的结果，失败时不会继续打印无效数据。
2. 显式解析高低字节与负数：芯片高字节在前，每个测量值是 16 位补码。
   先用 `(high << 8) | low` 组合；若最高位为 1，再减去 65536 得到负数。
   不把缓冲区直接转换成 `short *`，以免依赖 CPU 字节序和内存对齐。
3. 同时打印原始值、加速度、角速度、芯片内部温度，方便对照寄存器手册。
   支持指定读取组数，或持续运行并用 Ctrl+C 退出；每组之间等待 200 ms，
   避免无间隔循环刷屏。这个等待不改变芯片内部采样率。
4. `Makefile`：增加 `make app`，单独使用 ARM 交叉编译器构建用户态程序；
   原来的 `make` 现在同时构建驱动模块和测试程序。

## 数据格式与换算

驱动与程序约定一次 `read()` 返回下面这 14 字节，高字节在前：

| 字节偏移 | 寄存器 | 数据 |
| --- | --- | --- |
| 0、1 | 0x3b、0x3c | 加速度 X |
| 2、3 | 0x3d、0x3e | 加速度 Y |
| 4、5 | 0x3f、0x40 | 加速度 Z |
| 6、7 | 0x41、0x42 | 芯片内部温度 |
| 8、9 | 0x43、0x44 | 角速度 X |
| 10、11 | 0x45、0x46 | 角速度 Y |
| 12、13 | 0x47、0x48 | 角速度 Z |

换算依据是 InvenSense《MPU-6000/MPU-6050 Register Map and
Descriptions》Revision 4.0 的 4.18～4.20 节，以及当前驱动的量程配置：

```text
ACCEL_CONFIG = 0x00：±2g      加速度(g) = 原始值 / 16384.0
GYRO_CONFIG  = 0x00：±250°/s  角速度(°/s) = 原始值 / 131.0
温度(°C) = 温度原始值 / 340.0 + 36.53
```

如果以后修改驱动的量程，也要调整程序顶部的两个灵敏度常量。
角速度不是角度；本程序没有做姿态融合或零偏校准。温度来自芯片内部，
不能直接当作精确的环境温度。

## 编译与上板

在电脑上的实验目录执行：

```sh
cd /home/hitle/桌面/H3/driver-labs/17-I2C_MPU6050
make app
file mpu6050app
scp -O mpu6050app root@10.20.30.2:/tmp/
```

`make app` 只构建测试程序，不需要重新生成系统镜像。
`file` 用来确认生成的是 ARM 程序。`scp -O` 使用传统 SCP 协议，
兼容当前使用 Dropbear 的开发板。

在开发板终端执行：

```sh
ls -l /dev/mpu6050_dev
chmod +x /tmp/mpu6050app
/tmp/mpu6050app /dev/mpu6050_dev 10
```

上面的 `10` 表示读取 10 组后退出。去掉组数或写 `0` 则持续读取：

```sh
/tmp/mpu6050app
```

默认设备路径为 `/dev/mpu6050_dev`，按 Ctrl+C 退出。
`/tmp` 中的程序重启后需要重新复制。

## 运行流程与结果判断

```text
open("/dev/mpu6050_dev")
    → 驱动 mpu6050_open() 设置 file->private_data
read(fd, data, 14)
    → 驱动 mpu6050_read()
    → mpu6050_read_regs() 经 I2C 读取 0x3b～0x48
    → copy_to_user() 把 14 字节交给程序
    → 程序解析、换算、打印
close(fd)
    → 驱动 mpu6050_release()
```

静止时，加速度向量的大小通常应接近 1g；具体落在哪个轴、正负方向如何，
取决于模块摆放方向。角速度通常接近 0，但可能有零偏。缓慢转动模块，
观察加速度分量和角速度是否随动作变化。

常见错误：

- `No such file or directory`：先核对设备节点和驱动绑定，程序不会创建设备节点。
- `Invalid argument`：确认板上加载的是一次返回 14 字节的驱动版本。
- `Incomplete sample`：实际读取长度不符合约定，程序停止，不拼接两次不同采样。
- `read MPU6050` 后的其他错误：结合 `dmesg` 检查驱动返回值和 I2C 通信。

本程序只测试正常读取路径，不代表驱动的并发、解绑和错误回收路径已验证。
