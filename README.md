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

生成的镜像位于：

```text
output-wukongpi/images/sdcard.img
```

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
