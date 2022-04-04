ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

# Some files needed by the dw-edma drivers are not exported and are locally
# copied in kernel/x.y/ subdirectory depending on kernel API changes.
# These files are drivers/dma/dmaengine.h and drivers/dma/virt-dma.h
AKIDA_KERNEL_VERSION_RANK := $(shell \
	printf "$(VERSION).$(PATCHLEVEL)\n5.3\n5.4\n5.6\n5.7\n5.16\n5.17\n" | \
	sort -V )

ifneq ($(word 1,$(AKIDA_KERNEL_VERSION_RANK)), 5.3)
$(error Kernel $(VERSION).$(PATCHLEVEL) too old)
else ifneq ($(word 2,$(AKIDA_KERNEL_VERSION_RANK)), 5.4)
ccflags-y += -I$(src)/kernel/5.3/drivers/dma
else ifneq ($(word 3,$(AKIDA_KERNEL_VERSION_RANK)), 5.6)
ccflags-y += -I$(src)/kernel/5.4/drivers/dma
else ifneq ($(word 4,$(AKIDA_KERNEL_VERSION_RANK)), 5.7)
ccflags-y += -I$(src)/kernel/5.6/drivers/dma
else ifneq ($(word 5,$(AKIDA_KERNEL_VERSION_RANK)), 5.16)
ccflags-y += -I$(src)/kernel/5.7/drivers/dma
else ifneq ($(word 6,$(AKIDA_KERNEL_VERSION_RANK)), 5.17)
ccflags-y += -I$(src)/kernel/5.16/drivers/dma
else
$(error Kernel $(VERSION).$(PATCHLEVEL) not supported. Some incompatibilities can be present)
endif

ifeq ($(CONFIG_ARCH_BCM2835),y)
# Kernel built to support a Raspberry Pi CM4 -> Force 32bit PCIe accesses
ccflags-y += -DAKIDA_DW_EDMA_FORCE_32BIT
endif

obj-m := akida-pcie.o

akida-pcie-y += akida-pcie-core.o
CFLAGS_akida-pcie-core.o += -I$(src)/akida-dw-edma

akida-pcie-y += akida-dw-edma/dw-edma-core.o
akida-pcie-y += akida-dw-edma/dw-edma-v0-core.o
akida-pcie-y += akida-dw-edma/dw-edma-v0-debugfs.o

else
# normal makefile
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_PATH ?= /lib/modules/$(KERNEL_VERSION)/build

all:
	$(MAKE) -C $(KERNEL_PATH) M=$$PWD modules

clean:
	$(MAKE) -C $(KERNEL_PATH) M=$$PWD clean

endif