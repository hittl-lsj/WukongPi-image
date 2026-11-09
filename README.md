# WuKong Pi H3 Buildroot 镜像

本仓库包含用于为 WuKong Pi H3 开发板构建精简 Linux 镜像的 Buildroot
外部树和辅助脚本。

该开发板采用全志 H3（ARM Cortex-A7）处理器，配备 512 MiB 内存，并从 TF
卡启动。镜像包含 U-Boot、Linux 6.12.3、定制的 WuKong Pi H3 设备树、
BusyBox、Dropbear SSH、ext4 根文件系统以及终端登录提示信息。

## Repository layout

```text
wukongpi-br2/                 Buildroot external tree
  board/wukongpi-h3/          DTS、U-Boot 配置片段、genimage 和覆盖文件
  configs/wukongpi_h3_defconfig
flash-wukongpi.sh             Safe removable-disk detection and flashing
```

Buildroot 本身及其下载内容不会提交到仓库，因为它们属于体积较大的生成
文件或第三方目录。

## 构建

在项目目录中下载并解压匹配的 Buildroot 版本：

```bash
wget https://buildroot.org/downloads/buildroot-2026.02.3.tar.xz
tar -xf buildroot-2026.02.3.tar.xz
```

在项目目录中配置并构建：

```bash
cd buildroot-2026.02.3
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  wukongpi_h3_defconfig
```

在公开发布或生产构建前，请打开 `menuconfig` 设置私有的 root 密码，或配置
SSH 公钥。密码不会存储在本仓库中。

```bash
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  menuconfig

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  -j"$(nproc)"
```

## 修改配置后重新生成镜像

以下命令均在 Buildroot 源码目录执行：

```bash
cd /home/hitle/桌面/H3/buildroot-2026.02.3
```

`O=../output-wukongpi` 指定构建输出目录，`BR2_EXTERNAL=../wukongpi-br2`
指定本项目的板级外部树。每次使用同一组参数，才能修改和构建同一套系统。
已有配置时不要为了重新编译而运行 `wukongpi_h3_defconfig`，它会重新加载
仓库中的配置，覆盖当前尚未保存回仓库的 Buildroot 选项。

### 先分清修改的是哪一层

| 修改内容 | 编辑入口或文件 | 影响什么 |
| --- | --- | --- |
| 设备树 | `wukongpi-br2/board/wukongpi-h3/dts/allwinner/sun8i-h3-wukongpi.dts` | 描述 GPIO、引脚、外设连接，编译为 DTB。 |
| Linux 内核选项 | `make … linux-menuconfig` | 选择内核驱动和功能，保存到 `output-wukongpi/build/linux-6.12.3/.config`。 |
| Buildroot 选项 | `make … menuconfig` | 选择架构、工具链、软件包、镜像格式等，保存到 `output-wukongpi/.config`。 |
| 根文件系统中的自定义文件 | `wukongpi-br2/board/wukongpi-h3/rootfs-overlay/` | 在打包前复制到目标系统，如启动脚本、网络配置、实验程序。 |

两个 `.config` 用途不同；修改菜单后选择保存，只更新各自输出目录中的
工作配置。要在清理输出目录或换电脑后重现配置，还需要保存到外部树，
方法见下文。不要把 `output-wukongpi/target/` 当作长期维护源码的位置。

### 修改设备树后

```bash
nano ../wukongpi-br2/board/wukongpi-h3/dts/allwinner/sun8i-h3-wukongpi.dts   // 编辑设备树

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  linux-rebuild

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2
```

第一条 `make` 让 Buildroot 再次进入内核编译流程，将外部树里的 DTS 复制
到内核源码目录并编译 DTB。直接编辑输出目录里的 DTS，下次构建可能被
外部树版本覆盖。`linux-rebuild` 会让内核的 make 按文件变化增量编译，
不是删除整个内核后重编。

第二条顶层 `make` 重新生成根文件系统并运行 post-image 脚本。当前配置
把内核和 DTB 安装到根文件系统的 `/boot`，再由 genimage 将
`u-boot-sunxi-with-spl.bin` 和 `rootfs.ext4` 组装成 `sdcard.img`。
只运行 `linux-rebuild`，不能认为整卡镜像已经更新。

### 修改 Linux 内核选项后

```bash
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  linux-menuconfig
```

在菜单中修改并保存退出。比如 Input 实验需要 `CONFIG_INPUT=y` 和
`CONFIG_INPUT_EVDEV=y`，前者提供 Input 核心，后者提供
`/dev/input/eventX` 用户接口；模块加载还需要 `CONFIG_MODULES=y`。
具体实验要求见 [14-input 的说明](driver-labs/14-input/README.md)。

保存菜单后，运行与设备树修改相同的两步：

```bash
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  linux-rebuild

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2
```

这会使用当前内核 `.config` 编译并重新打包。内核配置变化后，自己单独
编译的 `.ko` 也应针对新内核重新编译；它们不属于 Buildroot 软件包时，
Buildroot 不会自动替你重建。

#### 把内核配置保存到仓库

当前仓库的板级 defconfig 使用内核自带的 `sunxi` 配置。仅运行
`linux-menuconfig` 不会把修改保存回这个仓库。需要长期保留全部内核选择时，
可以在上面的 `linux-menuconfig` 保存后，按以下顺序改用自定义配置文件：

1. 运行下文的 Buildroot `menuconfig`，进入 `Kernel`。
2. 在 `Kernel configuration` 中选择 `Using a custom (def)config file`。
3. 在 `Configuration file path` 填写下面的路径，保存退出。

```text
$(BR2_EXTERNAL_WUKONGPI_PATH)/board/wukongpi-h3/linux_defconfig
```

这是菜单字段的值，`$(...)` 由 Buildroot/Make 展开，不是让 shell 执行的
命令。随后先执行：

```bash
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  linux-update-defconfig
```

它将现有内核配置精简后保存到刚指定的 `linux_defconfig`，文件尚不存在时
也会创建。切换配置来源后应先执行它，再构建，避免构建时找不到该文件。
该目标在仍使用内核自带 `sunxi` defconfig 时不可用；本流程也假定没有另外
设置内核配置片段。以后改完 `linux-menuconfig`，再运行它即可更新此文件。

最后还要执行下文的 Buildroot `savedefconfig`，把“改用这个内核配置文件”
的选择一并保存。内核配置文件和板级 defconfig 都应纳入版本管理。
这里只说明保存方法，不表示本仓库已经切换到自定义内核配置。

### 修改 Buildroot 选项后

```bash
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  menuconfig
```

在这里选择用户空间软件包、文件系统格式等。例如添加 `evtest` 时，可按
`/` 搜索 `BR2_PACKAGE_EVTEST`，根据搜索结果进入对应菜单启用。
保存退出后，将当前 Buildroot 配置保存到仓库中的板级 defconfig：

```bash
make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2 \
  savedefconfig \
  BR2_DEFCONFIG=/home/hitle/桌面/H3/wukongpi-br2/configs/wukongpi_h3_defconfig

make O=../output-wukongpi \
  BR2_EXTERNAL=../wukongpi-br2
```

`savedefconfig` 保存的是 Buildroot 选项，不会替代
`linux-update-defconfig` 保存内核选项；保存配置本身也不生成镜像。
添加独立的新软件包通常直接运行顶层 `make` 即可。修改已构建软件包的
子选项时，要先针对该包执行 `<包名>-reconfigure`，再运行顶层 `make`；
若影响其他包的可选依赖，也需重建相应包。

改变架构、浮点 ABI、工具链或 C 库，以及要求彻底移除已安装软件包时，
应保存配置后在干净的输出目录完整重建，不能套用普通增量构建步骤。
修改 overlay 中的文件通常只需顶层 `make`；删除 overlay 文件则不会自动
删除 `target/` 中原来已复制的文件，需要另外处理遗留文件或干净重建。

### 检查镜像与刷写

构建成功后，在 Buildroot 源码目录检查：

```bash
ls -lh \
  ../output-wukongpi/images/zImage \
  ../output-wukongpi/images/sun8i-h3-wukongpi.dtb \
  ../output-wukongpi/images/rootfs.ext4 \
  ../output-wukongpi/images/sdcard.img
```

供整卡刷写的是 `output-wukongpi/images/sdcard.img`。确认本次构建成功且镜像
时间已更新后，按下一节刷写。重刷会覆盖卡上的系统及个人修改；编译生成
镜像不会自动更新板子上正在运行的系统。

## 刷写 TF 卡

插入 TF 卡后，先执行只读检测：

```bash
./flash-wukongpi.sh --dry-run
```

然后执行刷写：

```bash
./flash-wukongpi.sh
```

该脚本只提供可写的 USB/可移动磁盘供选择，会排除包含当前运行系统和镜像
文件的磁盘，显示检测到的型号和挂载点，并要求在写入前输入完整设备路径。

## 首次启动

使用 3.3 V USB-TTL 适配器，串口参数为 115200 8N1。串口控制台为 `ttyS0`。
镜像从 `/dev/mmcblk0p1` 启动，并提供 Dropbear SSH 服务器。

开发调试时，如果通过网线直连电脑，请配置匹配的临时地址：

```text
WuKong Pi: 10.20.30.2/24
Computer: 10.20.30.1/24
```

然后连接：

```bash
ssh root@10.20.30.2
```

## 许可证

开发板相关文件和辅助脚本采用 MIT 许可证发布。Buildroot 及其他第三方组件
仍遵循各自的许可证。
