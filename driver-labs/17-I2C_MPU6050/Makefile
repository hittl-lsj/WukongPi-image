ifneq ($(KERNELRELEASE),)

# 这一段由 Linux Kbuild 读取，只描述要编译哪些模块。
obj-m := i2c_dir_dev.o

else

# 这一段由我们在实验目录中直接执行 make 时读取。
KERNELDIR := /home/hitle/桌面/H3/output-wukongpi/build/linux-6.12.3
ARCH := arm
CROSS_COMPILE := /home/hitle/桌面/H3/output-wukongpi/host/bin/arm-buildroot-linux-gnueabihf-
CURRENT_PATH := $(CURDIR)
APP_CFLAGS ?= -O2 -Wall -Wextra -std=c11

.DEFAULT_GOAL := build
.PHONY: build kernel_modules app clean

build: kernel_modules app

kernel_modules:
	$(MAKE) -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURRENT_PATH) modules

# 用户态程序只需要交叉编译器和 C 库，不通过内核 Kbuild 编译。
app: mpu6050app

mpu6050app: MPU6050APP.c
	$(CROSS_COMPILE)gcc $(APP_CFLAGS) -o "$@" "$<"

clean:
	$(MAKE) -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURRENT_PATH) clean
	$(RM) mpu6050app

endif
