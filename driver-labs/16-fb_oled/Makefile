ifneq ($(KERNELRELEASE),)

obj-m := fboled.o

else

PROJECT_ROOT := $(abspath $(CURDIR)/../..)
KERNELDIR ?= $(PROJECT_ROOT)/output-wukongpi/build/linux-6.12.3
ARCH ?= arm
CROSS_COMPILE ?= $(PROJECT_ROOT)/output-wukongpi/host/bin/arm-buildroot-linux-gnueabihf-
APP_CFLAGS ?= -O2 -Wall -Wextra

.DEFAULT_GOAL := all
.PHONY: all modules check-config clean

all: modules fbapp

# 把缺少内核配置的原因提前说明，避免直接出现大量 MODPOST 未定义符号。
check-config:
	@grep -q '^CONFIG_FB=y$$' "$(KERNELDIR)/.config" && \
	 grep -q '^CONFIG_FB_DEVICE=y$$' "$(KERNELDIR)/.config" && \
	 grep -q '^CONFIG_FB_DEFERRED_IO=y$$' "$(KERNELDIR)/.config" || \
	 { echo 'Enable CONFIG_FB=y, CONFIG_FB_DEVICE=y, and CONFIG_FB_DEFERRED_IO=y, then rebuild/boot the kernel.'; exit 1; }
	@if grep -q '^CONFIG_FRAMEBUFFER_CONSOLE=y$$' "$(KERNELDIR)/.config"; then \
	 echo 'This userspace-only lesson requires CONFIG_FRAMEBUFFER_CONSOLE=n (see README.md).'; exit 1; fi

modules: check-config
	$(MAKE) -C "$(KERNELDIR)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M="$(CURDIR)" modules

fbapp: fbapp.c
	$(CROSS_COMPILE)gcc $(APP_CFLAGS) -o "$@" "$<"

clean:
	$(MAKE) -C "$(KERNELDIR)" ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) M="$(CURDIR)" clean
	$(RM) fbapp

endif
