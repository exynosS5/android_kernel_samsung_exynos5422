#
# Makefile for the kernel mobicore drivers
#
##################################
#mobicore_security_team_add
##################################
GUD_ROOT_FOLDER := $(srctree)/drivers/gud/gud-exynos5422
##################################
# add our modules to kernel.
obj-$(CONFIG_MOBICORE_API) += mcKernelApi.o
obj-$(CONFIG_MOBICORE_DRIVER) += mcDrvModule.o

mcDrvModule-objs := MobiCoreDriver/logging.o \
		MobiCoreDriver/ops.o \
		MobiCoreDriver/mem.o \
		MobiCoreDriver/api.o \
		MobiCoreDriver/pm.o \
		MobiCoreDriver/main.o

mcKernelApi-objs := MobiCoreKernelApi/main.o \
		MobiCoreKernelApi/clientlib.o \
		MobiCoreKernelApi/device.o \
		MobiCoreKernelApi/session.o \
		MobiCoreKernelApi/connection.o

obj-$(CONFIG_SECURE_OS_CONTROL) += sec-os-ctrl/

# Release mode by default
ccflags-y := -DNDEBUG -include $(GUD_ROOT_FOLDER)/build_tag.h
ccflags-y += -Wno-declaration-after-statement

ccflags-$(CONFIG_MOBICORE_DEBUG) += -DDEBUG
ccflags-$(CONFIG_MOBICORE_VERBOSE) += -DDEBUG_VERBOSE

# Choose one platform from the folder
MOBICORE_PLATFORM := EXYNOS_5422_STD
    ccflags-y += -DMC_NETLINK_COMPAT_V37

# Use the available platform folder
ccflags-y += -I$(GUD_ROOT_FOLDER)/MobiCoreDriver/platforms/$(MOBICORE_PLATFORM)
# MobiCore Driver includes
ccflags-y += -I$(GUD_ROOT_FOLDER)/MobiCoreDriver/public
# MobiCore KernelApi required incldes
ccflags-y += -I$(GUD_ROOT_FOLDER)/MobiCoreKernelApi/include \
		-I$(GUD_ROOT_FOLDER)/MobiCoreKernelApi/public

# TUI Module
obj-$(CONFIG_TRUSTONIC_TRUSTED_UI) += mcDrvTui.o
mcDrvTui-objs := \
	t-base-tui/trustedui.o \
	t-base-tui/main.o \
	t-base-tui/tlcTui.o

ccflags-$(CONFIG_TRUSTONIC_TRUSTED_UI) += -Idrivers/gud/t-base-tui
ccflags-$(CONFIG_TRUSTONIC_TRUSTED_UI) += -Idrivers/video/decon_display
ccflags-$(CONFIG_TRUSTONIC_TRUSTED_UI) += -Idrivers/gud/MobiCoreKernelApi/public
ccflags-$(CONFIG_TRUSTONIC_TRUSTED_UI) += -Idrivers/gud/MobiCoreKernelApi/include

ccflags-$(CONFIG_MOBICORE_DEBUG) += -DDEBUG
ccflags-$(CONFIG_MOBICORE_VERBOSE) += -DDEBUG_VERBOSE
