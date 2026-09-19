#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
DEFAULT_IMAGE="${SCRIPT_DIR}/output-wukongpi/images/sdcard.img"

IMAGE="${DEFAULT_IMAGE}"
REQUESTED_DEVICE=""
DRY_RUN=0

usage() {
	cat <<EOF
用法: $(basename "$0") [选项] [镜像文件]

自动检测 USB/可移动磁盘，卸载其分区，并写入 WuKong Pi 镜像。

选项:
  -d, --device DEVICE  指定整个磁盘，例如 /dev/sda（不能是 /dev/sda1）
  -n, --dry-run        只检测并显示目标，不卸载、不写入
  -h, --help           显示此帮助

默认镜像:
  ${DEFAULT_IMAGE}
EOF
}

die() {
	printf '错误: %s\n' "$*" >&2
	exit 1
}

need_command() {
	command -v "$1" >/dev/null 2>&1 || die "缺少命令: $1"
}

while (($# > 0)); do
	case "$1" in
		-d|--device)
			(($# >= 2)) || die "$1 需要一个设备路径"
			REQUESTED_DEVICE="$2"
			shift 2
			;;
		-n|--dry-run)
			DRY_RUN=1
			shift
			;;
		-h|--help)
			usage
			exit 0
			;;
		--)
			shift
			break
			;;
		-*)
			die "未知选项: $1"
			;;
		*)
			IMAGE="$1"
			shift
			(($# == 0)) || die "只能指定一个镜像文件"
			;;
	esac
done

if (($# > 0)); then
	(($# == 1)) || die "只能指定一个镜像文件"
	IMAGE="$1"
fi

for cmd in lsblk findmnt readlink stat dd sync awk sort cut grep; do
	need_command "$cmd"
done

[[ -f "$IMAGE" ]] || die "镜像不存在: $IMAGE"
[[ -r "$IMAGE" ]] || die "镜像不可读: $IMAGE"
IMAGE="$(readlink -f -- "$IMAGE")"
IMAGE_SIZE="$(stat -c '%s' -- "$IMAGE")"
((IMAGE_SIZE > 0)) || die "镜像文件为空"

declare -A PROTECTED_DISKS=()

protect_source_disk() {
	local source="$1"
	local name type

	[[ "$source" == /dev/* ]] || return 0
	while read -r name type; do
		if [[ "$type" == "disk" ]]; then
			PROTECTED_DISKS["$name"]=1
		fi
	done < <(lsblk -s -nrpo NAME,TYPE -- "$source" 2>/dev/null || true)
}

# Never offer the disks that contain the running system or the image file.
protect_source_disk "$(findmnt -nro SOURCE / 2>/dev/null || true)"
protect_source_disk "$(findmnt -nro SOURCE --target "$IMAGE" 2>/dev/null || true)"

is_removable_candidate() {
	local device="$1"
	local type tran rm ro

	type="$(lsblk -dnro TYPE -- "$device")"
	tran="$(lsblk -dnro TRAN -- "$device")"
	rm="$(lsblk -dnro RM -- "$device")"
	ro="$(lsblk -dnro RO -- "$device")"
	[[ "$type" == "disk" ]] || return 1
	[[ "$ro" == "0" ]] || return 1
	[[ "$tran" == "usb" || "$rm" == "1" ]] || return 1
	[[ -z "${PROTECTED_DISKS[$device]:-}" ]] || return 1
}

declare -a CANDIDATES=()
while read -r device; do
	[[ -b "$device" ]] || continue
	if is_removable_candidate "$device"; then
		CANDIDATES+=("$device")
	fi
done < <(lsblk -dnrpo NAME,TYPE | awk '$2 == "disk" { print $1 }')

TARGET=""
if [[ -n "$REQUESTED_DEVICE" ]]; then
	[[ -e "$REQUESTED_DEVICE" ]] || die "设备不存在: $REQUESTED_DEVICE"
	TARGET="$(readlink -f -- "$REQUESTED_DEVICE")"
	[[ -b "$TARGET" ]] || die "不是块设备: $TARGET"
	is_removable_candidate "$TARGET" || \
		die "拒绝写入 $TARGET：它不是可写 USB/可移动磁盘，或承载了当前系统/镜像"
else
	case "${#CANDIDATES[@]}" in
		0)
			die "未检测到可写的 USB/可移动磁盘，请插入 TF 卡后重试"
			;;
		1)
			TARGET="${CANDIDATES[0]}"
			;;
		*)
			printf '检测到多个候选磁盘:\n'
			for i in "${!CANDIDATES[@]}"; do
				printf '  [%d] ' "$((i + 1))"
				lsblk -dno NAME,SIZE,MODEL,SERIAL,TRAN -- "${CANDIDATES[$i]}"
			done
			read -r -p "请输入序号: " choice
			[[ "$choice" =~ ^[0-9]+$ ]] || die "无效序号"
			((choice >= 1 && choice <= ${#CANDIDATES[@]})) || die "序号超出范围"
			TARGET="${CANDIDATES[$((choice - 1))]}"
			;;
	esac
fi

TARGET_SIZE="$(lsblk -bdnro SIZE -- "$TARGET")"
((TARGET_SIZE >= IMAGE_SIZE)) || die "目标容量小于镜像"

printf '\n即将使用:\n'
printf '  镜像: %s (%s 字节)\n' "$IMAGE" "$IMAGE_SIZE"
printf '  目标: '
lsblk -dno NAME,SIZE,MODEL,SERIAL,TRAN -- "$TARGET"
printf '\n目标磁盘当前的分区和挂载点:\n'
lsblk -o NAME,SIZE,FSTYPE,MOUNTPOINTS -- "$TARGET"

if ((DRY_RUN)); then
	printf '\nDry-run 完成：未卸载分区，也未写入任何数据。\n'
	exit 0
fi

printf '\n警告：%s 上的所有现有数据都会被覆盖。\n' "$TARGET"
read -r -p "请输入完整设备名 $TARGET 以确认: " confirmation
[[ "$confirmation" == "$TARGET" ]] || die "确认不匹配，已取消"

if ((EUID == 0)); then
	SUDO=()
else
	need_command sudo
	SUDO=(sudo)
fi

# Authenticate before unmounting, then verify the selected device still exists.
if ((EUID != 0)); then
	sudo -v
fi
[[ -b "$TARGET" ]] || die "目标设备已断开: $TARGET"

mapfile -t TARGET_NODES < <(lsblk -nrpo NAME -- "$TARGET")

for node in "${TARGET_NODES[@]}"; do
	if command -v swapon >/dev/null 2>&1 && \
		swapon --noheadings --raw --show=NAME 2>/dev/null | grep -Fxq -- "$node"; then
		printf '正在停用 swap: %s\n' "$node"
		"${SUDO[@]}" swapoff -- "$node"
	fi
done

for node in "${TARGET_NODES[@]}"; do
	mapfile -t MOUNT_POINTS < <(
		findmnt -rn -S "$node" -o TARGET 2>/dev/null |
			awk '{ print length($0) "\t" $0 }' |
			sort -rn |
			cut -f2-
	)
	for mount_point in "${MOUNT_POINTS[@]}"; do
		[[ -n "$mount_point" ]] || continue
		printf '正在卸载: %s\n' "$mount_point"
		"${SUDO[@]}" umount -- "$mount_point"
	done
done

for node in "${TARGET_NODES[@]}"; do
	if findmnt -rn -S "$node" >/dev/null 2>&1; then
		die "仍有文件系统挂载在 $node，拒绝写入"
	fi
done

printf '\n正在写入 %s ...\n' "$TARGET"
"${SUDO[@]}" dd \
	if="$IMAGE" \
	of="$TARGET" \
	bs=4M \
	status=progress \
	conv=fsync

sync
printf '\n写入完成。\n'

if command -v eject >/dev/null 2>&1; then
	if "${SUDO[@]}" eject "$TARGET"; then
		printf '设备已弹出，现在可以安全拔出 TF 卡。\n'
	else
		printf '无法自动弹出设备，但数据已同步，可以安全拔出 TF 卡。\n'
	fi
else
	printf '数据已同步，现在可以安全拔出 TF 卡。\n'
fi
