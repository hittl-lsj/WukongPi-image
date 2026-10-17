ifneq ($(KERNELRELEASE),)

# 这一段由 Linux Kbuild 读取，只描述要编译哪些模块。
obj-m := ioctrl.o

else

# 这一段由我们在实验目录中直接执行 make 时读取。
KERNELDIR := /home/hitle/桌面/H3/output-wukongpi/build/linux-6.12.3
ARCH := arm
CROSS_COMPILE := /home/hitle/桌面/H3/output-wukongpi/host/bin/arm-buildroot-linux-gnueabihf-
CURRENT_PATH := $(CURDIR)

.DEFAULT_GOAL := build
.PHONY: build kernel_modules clean

build: kernel_modules

kernel_modules:
	$(MAKE) -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURRENT_PATH) modules

clean:
	$(MAKE) -C $(KERNELDIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M=$(CURRENT_PATH) clean

endif
