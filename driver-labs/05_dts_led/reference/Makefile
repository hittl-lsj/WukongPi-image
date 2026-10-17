ifneq ($(KERNELRELEASE),)

# Kbuild 读取这一分支：生成 dts_led.ko。
obj-m := dts_led.o

else

# 当前目录是 H3/driver-labs/05_dts_led/reference，向上三级得到 H3。
H3_ROOT := $(abspath $(CURDIR)/../../..)
KERNELDIR ?= $(H3_ROOT)/output-wukongpi/build/linux-6.12.3
ARCH ?= arm
CROSS_COMPILE ?= $(H3_ROOT)/output-wukongpi/host/bin/arm-buildroot-linux-gnueabihf-
TARGET_CC := $(CROSS_COMPILE)gcc

.DEFAULT_GOAL := build
.PHONY: build kernel_modules clean

build: kernel_modules ledapp

kernel_modules:
	$(MAKE) -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) modules

ledapp: ledapp.c
	$(TARGET_CC) -O2 -Wall -Wextra -Werror $< -o $@

clean:
	$(MAKE) -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURDIR) clean
	$(RM) ledapp

endif
