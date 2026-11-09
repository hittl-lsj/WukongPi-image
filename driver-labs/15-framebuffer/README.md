# RAM 虚拟 Framebuffer：先理解显存和用户接口

本课面向当前悟空派 H3 的 Linux 6.12.3。用普通 RAM 模拟固定的
320×240、RGB565 显存，通过 `/dev/fbX` 查询参数、读写和 mmap。
没有物理显示输出，也不驱动 ATK-7016；彩条可以导出为 PPM 文件，传到电脑
查看。书中第 12.3.4 节的 `fb_info/fb_ops` 分层在这里可以直接观察。

## 本次修改范围

| 层次 | 本课内容 | 原因 |
| --- | --- | --- |
| 驱动 | 新增 `ramfb.c`，生成 `ramfb.ko` | 注册 fbdev，分配 RAM，提供读写和映射回调。 |
| 应用 | 新增 `fbapp.c`，生成 `fbapp` | 查询参数、画 RGB 彩条、校验读写，并可导出图片。 |
| 设备树 | 不需要修改 | 没有外设、GPIO、IRQ 或硬件寄存器需要描述。 |
| Linux | 需手动启用 `CONFIG_FB=y`、`CONFIG_FB_DEVICE=y` | 提供 fbdev 核心及 `/dev/fbX` 接口。 |
| Linux | 本课要求 `CONFIG_FRAMEBUFFER_CONSOLE=n` | 只实现供应用使用的接口，尚未实现 fbcon 所需绘图回调。 |
| Buildroot | 不要求新软件包 | 应用只使用标准 C 库和 Linux 用户接口。 |

这里的配置操作须由你执行，仓库中的 DTS、Buildroot/内核配置没有被本课
自动修改。RAM 设备不需要 `simple-framebuffer` 设备树节点，也不用启用
`CONFIG_FB_VIRTUAL`；后者是内核自带的另一个虚拟驱动，会替代我们要学的工作。

## 1. 先理解内存布局

RGB565 每个像素 16 位，即 2 字节：

```text
15             11 10                 5 4           0
       R(5)               G(6)              B(5)

一行：320 × 2 = 640 字节
一帧：640 × 240 = 153600 字节
```

红色是 `0xf800`，绿色是 `0x07e0`，蓝色是 `0x001f`。在 H3 小端模式下，
红色像素存成字节 `00 f8`。像素值和内存字节顺序是两个概念。
应用必须先查询 `line_length` 和 RGB 位域，不能把其他 framebuffer 也当作
本课的 RGB565。定位像素的一般方法是：

```text
字节偏移 = (y + yoffset) × line_length + (x + xoffset) × 每像素字节数
```

这个公式用于字节对齐的 packed-pixel 格式，不适用于所有单色/平面格式。
本课不允许平移，`xoffset=yoffset=0`，虚拟分辨率与可见分辨率相同。

## 2. 对照代码看分层

```text
fbapp: open / ioctl / read / write / mmap
                       ↓
fbdev 核心：创建标准字符设备接口、处理通用 ioctl 和引用
                       ↓
ramfb_ops：读写 RAM、校验固定模式、映射 vmalloc 页
                       ↓
153600 字节像素 RAM（没有显示控制器扫描它）
```

按下面的顺序阅读 `ramfb.c`：

1. `framebuffer_alloc(0, NULL)` 只分配设备描述 `fb_info`，不分配像素内存。
   参数 0 表示没有额外私有数据；NULL 表示这里没有硬件父设备。
2. `vmalloc_user()` 分配并清零可映射给用户的 RAM。它在内核虚拟地址上连续，
   物理页可以不连续，因此使用 `remap_vmalloc_range()` 映射。
3. `fix` 填写名称、显存长度、行跨度等；`var` 填写分辨率及 RGB565 位域。
   `screen_buffer` 是内核虚拟地址；`smem_start` 保持 0，不能塞入虚拟地址
   冒充物理地址。
4. `fbops` 指向操作表。核心已经实现字符设备层，无需自己写 cdev/class。
   本课使用 `simple_read_from_buffer()`、`simple_write_to_buffer()`，
   处理用户空间拷贝、文件偏移、EOF 和短读写；它们适用于普通 RAM。
5. `ramfb_check_var()` 拒绝超出本课范围的模式，保证用户不能改变分辨率后
   按更大的尺寸访问原来的小显存。RGB 位域会规范化回固定格式。
6. `register_framebuffer()` 最后调用，确保应用打开时信息已完整。
   `FBIOGET_FSCREENINFO` / `FBIOGET_VSCREENINFO` 由核心处理，驱动不重复写。

`write()` 每次需要一次用户/内核数据拷贝；`mmap(MAP_SHARED)` 则让两边
访问同一组页。修改映射后，再用 `read()` 读回，应看到相同数据。
只重写 `fb_write` 捕捉不到所有 mmap 写入；本课没有物理刷新问题，后续
SPI 屏需要另学延迟刷新等机制。

### 页对齐和释放为什么要这样写

用户映射以页为单位。H3 的 4 KiB 页下，153600 字节会向上对齐到
155648 字节。`vmalloc_user()` 把最后一页的填充也清零；`read/write` 只暴露
153600 字节的有效像素，`mmap` 可以访问最后一页的零填充，但不能越过
实际分配的页范围。映射回调先检查页偏移再移位，避免整数溢出。

`fb_ops.owner=THIS_MODULE` 让核心在文件打开期间持有模块引用。mmap 的
VMA 也持有文件引用，因此关闭 fd 后，尚未解除的映射仍保护模块。
退出先 `unregister_framebuffer()`；核心放下最后一个 `fb_info` 引用后，
调用 `fb_destroy`，再 `vfree()` 和 `framebuffer_release()`。
不能在退出函数里再释放一次，也不要强制卸载正在使用的模块。

这版没有 fbcon、调色板、内核绘图回调、模式切换、双缓冲、DMA、VSync，
也没有 SPI 传输。固定范围让这一课只聚焦 fbdev 接口和显存映射。

## 3. 由你启用内核选项

在电脑的 Buildroot 源码目录执行：

```sh
cd /home/hitle/桌面/H3/buildroot-2026.02.3
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  linux-menuconfig
```

进入 `Device Drivers → Graphics support`，启用
`Support for frame buffer device drivers`，并确认
`Provide legacy /dev/fb* device` 已启用。按 `/` 搜索下列符号核对位置和值：

```text
CONFIG_FB=y
CONFIG_FB_DEVICE=y
CONFIG_MODULES=y
# CONFIG_FRAMEBUFFER_CONSOLE is not set
```

无需开启任何真实显卡/屏幕驱动来完成这个 RAM 实验，也不依赖
`CONFIG_DRM_FBDEV_EMULATION`。若已有 Framebuffer Console，请在本课配置
中关闭；本驱动也会检查并拒绝在它开启时注册，防止缺失的绘图回调被调用。
这不会关闭串口的 `ttyS0` 控制台。

保存退出后检查，确认使用的是正确输出目录：

```sh
grep -E '^CONFIG_(FB|FB_DEVICE|MODULES)=|^# CONFIG_FRAMEBUFFER_CONSOLE is not set' \
  ../output-wukongpi/build/linux-6.12.3/.config
```

为了以后清理后可复现，按照
[项目 README 的内核配置保存方法](../../README.md#把内核配置保存到仓库)
保存这些选择。之后重新编译并打包：

```sh
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  linux-rebuild

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2
```

第一步生成新内核，第二步把它装进 `rootfs.ext4` 并生成
`../output-wukongpi/images/sdcard.img`。然后按项目的刷写流程使用新镜像，
或按已有内核更新流程部署它，并重启到新内核。仅在电脑编译完成，不会
给板上仍运行的旧内核增加 fbdev 符号。

## 4. 编译和上板

在内核构建完成后执行：

```sh
cd /home/hitle/桌面/H3/driver-labs/15-framebuffer
make
scp -O ramfb.ko fbapp root@10.20.30.2:/tmp/
```

Makefile 先检查上述内核配置，再用相同内核构建目录生成模块。只想先编译
应用可以运行 `make fbapp`。若缺少 fbdev 核心，Makefile 会提前给出提示，
不能绕过 MODPOST 检查把有未定义符号的模块当作成功。

在板上执行：

```sh
insmod /tmp/ramfb.ko
dmesg | tail -n 10
cat /proc/fb
```

找到名称为 `ramfb-lab` 的行，例如 `0 ramfb-lab` 才表示 `/dev/fb0`；
编号按注册顺序分配，不保证为 0。以下假设实际编号为 0：

```sh
test -e /dev/fb0 || mdev -s
cat /sys/class/graphics/fb0/name
/tmp/fbapp /dev/fb0 /tmp/ramfb.ppm
```

`register_framebuffer()` 先在内核中注册设备；Buildroot 的 mdev 再根据 sysfs
创建 `/dev/fb0`。正常热插拔配置会自动创建，`mdev -s` 只是设备节点缺失时
重新扫描，不会注册驱动本身。

应用先确认设备名和像素布局，再通过 mmap 画红绿蓝三根竖条，逐行 read
校验，然后通过 write 将第一个像素改白，并从映射读回验证。预期输出：

```text
ramfb-lab: 320x240, 16 bpp, stride=640, bytes=153600
mmap -> read: all rows match
write -> mmap: first pixel is white (0xffff)
Saved /tmp/ramfb.ppm (RGB bars with one white pixel)
```

导出文件不是屏幕截图，而是应用将 RAM 中的 RGB565 转成 RGB888 的 PPM。
在电脑上复制并查看，可以直观看到像素内容：

```sh
scp -O root@10.20.30.2:/tmp/ramfb.ppm /tmp/ramfb.ppm
xdg-open /tmp/ramfb.ppm
```

仅打开 `/dev/fbX` 不会把串口变成图形终端；这个驱动也不会使 ATK-7016
亮屏。它验证的是内存、ioctl、读写与映射。退出应用后可在板上卸载：

```sh
rmmod ramfb
cat /proc/fb
```

对应记录应消失；重新加载后显存重新清零。若提示模块被使用，应先关闭
打开设备的程序/映射，不要强制卸载。

## 5. 本课需要自己理解的三个问题

1. 为什么 `framebuffer_alloc()` 后还需要分配 `screen_buffer`？
2. 为什么 vmalloc RAM 不能用 `virt_to_phys()` 当作一块连续显存映射？
3. 为什么有了 `/dev/fbX`，ATK-7016 仍不会显示画面？

可对照本地内核源码：`include/linux/fb.h`、
`drivers/video/fbdev/core/fb_chrdev.c`、`fb_info.c`、`fbmem.c`，以及
`Documentation/fb/api.rst`。这里依据 Linux 6.12.3 实现；书中 Linux 4.0
的字符设备核心代码位置和默认读写行为已经变化。
