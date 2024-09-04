#
# Copyright (C) 2009-2011 The Android-x86 Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0


ifneq ($(strip $(TARGET_NO_KERNEL)),true)
ifeq ($(filter abc123, $(TARGET_DEVICE)),)
KERNEL_DIR := $(call my-dir)
ROOTDIR := $(abspath $(TOP))

ifndef INSTALLED_KERNEL_TARGET
INSTALLED_KERNEL_TARGET := $(PRODUCT_OUT)/kernel
endif

KERNEL_OUT := $(TARGET_OUT_INTERMEDIATES)/KERNEL_OBJ
ifeq ($(TARGET_ARCH),arm)
TARGET_PREBUILT_KERNEL := $(KERNEL_OUT)/arch/arm/boot/zImage
endif
ifeq ($(TARGET_ARCH),arm64)
TARGET_PREBUILT_KERNEL := $(KERNEL_OUT)/arch/arm64/boot/Image
endif
TARGET_KERNEL_CONFIG := $(KERNEL_OUT)/.config
KERNEL_HEADERS_INSTALL := $(KERNEL_OUT)/usr
KERNEL_MODULES_OUT := $(TARGET_OUT)/lib/modules
KERNEL_MODULES_SYMBOLS_OUT := $(PRODUCT_OUT)/symbols/system/lib/modules
SMATCH_PATH := $(ROOTDIR)/prebuilts/misc/linux-x86/smatch/bin/smatch
SMATCH_DATA_PATH := $(ROOTDIR)/prebuilts/misc/linux-x86/smatch/share/smatch/smatch_data/
KERNEL_CHECK_TOOL := "$(SMATCH_PATH) -p=kernel --data=$(SMATCH_DATA_PATH)"

ifeq ($(shell $(SMATCH_PATH) --version &> /dev/null; echo $$?),0)
KERNEL_CHECK_LEVEL := 1
else
KERNEL_CHECK_LEVEL := 0
endif

ifneq ($(TARGET_BUILD_VARIANT),user)
KERNEL_DEFCONFIG ?= $(TARGET_DEVICE)_debug_defconfig
else
KERNEL_DEFCONFIG ?= $(TARGET_DEVICE)_defconfig
endif

ifeq ($(KERNEL_CROSS_COMPILE),)
ifeq ($(TARGET_ARCH),arm)
KERNEL_CROSS_COMPILE := $(abspath $(TOPDIR)prebuilts/gcc/$(HOST_PREBUILT_TAG)/arm/arm-eabi-$(TARGET_GCC_VERSION)/bin/arm-eabi-)
endif
ifeq ($(TARGET_ARCH),arm64)
KERNEL_CROSS_COMPILE := $(abspath $(TARGET_TOOLS_PREFIX))
endif
endif

ifneq ($(USE_CCACHE),)
KERNEL_CROSS_COMPILE := "$(ROOTDIR)/prebuilts/misc/$(CCACHE_HOST_TAG)/ccache/ccache $(KERNEL_CROSS_COMPILE)"
endif

KERNEL_MAKEFLAGS := -C $(KERNEL_DIR) O=$(ROOTDIR)/$(KERNEL_OUT) ARCH=$(TARGET_ARCH) CROSS_COMPILE=$(KERNEL_CROSS_COMPILE)
ifneq ($(strip $(SHOW_COMMANDS)),)
KERNEL_MAKEFLAGS += V=1
endif

# install kernel header before building android
header_install := $(shell mkdir -p $(ROOTDIR)/$(KERNEL_OUT) && make $(KERNEL_MAKEFLAGS) headers_install)

define mv-modules
mv `find $(1)/lib -type f -name *.ko` $(1)
endef

define clean-module-folder
rm -rf $(1)/lib
endef

$(KERNEL_OUT):
	mkdir -p $@

$(KERNEL_MODULES_OUT):
	mkdir -p $@

.PHONY: kernel kernel-defconfig kernel-menuconfig kernel-modules clean-kernel kernel-check
kernel-menuconfig: | $(KERNEL_OUT)
	$(MAKE) $(KERNEL_MAKEFLAGS) menuconfig
	$(MAKE) $(KERNEL_MAKEFLAGS) savedefconfig
	cp $(KERNEL_OUT)/defconfig $(KERNEL_DIR)/arch/arm/configs/$(KERNEL_DEFCONFIG)

kernel-savedefconfig: | $(KERNEL_OUT)
	$(MAKE) $(KERNEL_MAKEFLAGS) savedefconfig
	cp $(KERNEL_OUT)/defconfig $(KERNEL_DIR)/arch/$(TARGET_ARCH)/configs/$(KERNEL_DEFCONFIG)

kernel: $(INSTALLED_KERNEL_TARGET)

kernel-defconfig: $(TARGET_KERNEL_CONFIG)

$(TARGET_KERNEL_CONFIG): | $(KERNEL_OUT) $(KERNEL_OUT)/include/generated/trapz_generated_kernel.h
	mkdir -p $(KERNEL_OUT)
	$(MAKE) $(KERNEL_MAKEFLAGS) $(KERNEL_DEFCONFIG)
# ACOS_MOD_BEGIN {internal_membo}
ifeq ($(USER_DEBUG_PVA), 1)
	cat $(KERNEL_DIR)/arch/arm/configs/trapz_pva.config >> $@
else
	cat $(KERNEL_DIR)/arch/arm/configs/trapz.config >> $@
endif
# ACOS_MOD_END {internal_membo}
	$(MAKE) $(KERNEL_MAKEFLAGS) oldconfig

$(KERNEL_HEADERS_INSTALL): $(TARGET_KERNEL_CONFIG) | $(KERNEL_OUT)
	$(MAKE) $(KERNEL_MAKEFLAGS) headers_install

$(TARGET_PREBUILT_KERNEL): $(TARGET_KERNEL_CONFIG) FORCE | $(KERNEL_OUT)
	$(MAKE) $(KERNEL_MAKEFLAGS)

kernel-modules: kernel | $(KERNEL_MODULES_OUT)
	$(MAKE) $(KERNEL_MAKEFLAGS) modules
	$(MAKE) $(KERNEL_MAKEFLAGS) INSTALL_MOD_PATH=$(ROOTDIR)/$(KERNEL_MODULES_SYMBOLS_OUT) modules_install
	$(call mv-modules,$(ROOTDIR)/$(KERNEL_MODULES_SYMBOLS_OUT))
	$(call clean-module-folder,$(ROOTDIR)/$(KERNEL_MODULES_SYMBOLS_OUT))
	$(MAKE) $(KERNEL_MAKEFLAGS) INSTALL_MOD_PATH=$(ROOTDIR)/$(KERNEL_MODULES_OUT) INSTALL_MOD_STRIP=1 modules_install
	$(call mv-modules,$(ROOTDIR)/$(KERNEL_MODULES_OUT))
	$(call clean-module-folder,$(ROOTDIR)/$(KERNEL_MODULES_OUT))

$(INSTALLED_KERNEL_TARGET): $(TARGET_PREBUILT_KERNEL) | $(ACP)
	$(copy-file-to-target)

droidcore all_modules systemimage: kernel-modules

kernel-check: KERNEL_MAKEFLAGS += C=$(KERNEL_CHECK_LEVEL) CHECK=$(KERNEL_CHECK_TOOL)
kernel-check: kernel

clean-kernel:
	@rm -rf $(KERNEL_OUT)
	@rm -rf $(KERNEL_MODULES_OUT)

endif # not abc123 64bit
endif #TARGET_NO_KERNEL
