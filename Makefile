ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

# Some files needed by the dw-edma drivers are not exported and are locally
# copied in kernel/x.y/ subdirectory depending on kernel API changes.
# These files are drivers/dma/dmaengine.h and drivers/dma/virt-dma.h
AKIDA_KERNEL_VERSION_RANK := $(shell \
	printf "$(VERSION).$(PATCHLEVEL)\n5.4\n5.6\n5.7\n5.9\n5.16\n6.4\n" | \
	sort -V )

ifneq ($(word 1,$(AKIDA_KERNEL_VERSION_RANK)), 5.4)
$(error Kernel $(VERSION).$(PATCHLEVEL) too old)
else ifneq ($(word 2,$(AKIDA_KERNEL_VERSION_RANK)), 5.6)
ccflags-y += -I$(src)/kernel/5.4/drivers/dma
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-v0-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-v0-debugfs.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-hdma-v0-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-hdma-v0-debugfs.o
else ifneq ($(word 3,$(AKIDA_KERNEL_VERSION_RANK)), 5.7)
ccflags-y += -I$(src)/kernel/5.6/drivers/dma
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-v0-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-v0-debugfs.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-hdma-v0-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-hdma-v0-debugfs.o
else ifneq ($(word 4,$(AKIDA_KERNEL_VERSION_RANK)), 5.9)
ccflags-y += -I$(src)/kernel/5.7/drivers/dma
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-v0-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-edma-v0-debugfs.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-hdma-v0-core.o
akida-pcie-y += kernel/5.4/akida-dw-edma/dw-hdma-v0-debugfs.o
else ifneq ($(word 5,$(AKIDA_KERNEL_VERSION_RANK)), 5.16)
ccflags-y += -I$(src)/kernel/5.7/drivers/dma
akida-pcie-y += akida-dw-edma/dw-edma-core.o
akida-pcie-y += akida-dw-edma/dw-edma-v0-core.o
akida-pcie-y += akida-dw-edma/dw-edma-v0-debugfs.o
akida-pcie-y += akida-dw-edma/dw-hdma-v0-core.o
akida-pcie-y += akida-dw-edma/dw-hdma-v0-debugfs.o
else ifneq ($(word 6,$(AKIDA_KERNEL_VERSION_RANK)), 6.4)
ccflags-y += -I$(src)/kernel/5.16/drivers/dma
akida-pcie-y += akida-dw-edma/dw-edma-core.o
akida-pcie-y += akida-dw-edma/dw-edma-v0-core.o
akida-pcie-y += akida-dw-edma/dw-edma-v0-debugfs.o
akida-pcie-y += akida-dw-edma/dw-hdma-v0-core.o
akida-pcie-y += akida-dw-edma/dw-hdma-v0-debugfs.o
else
$(error Kernel $(VERSION).$(PATCHLEVEL) not supported. Some incompatibilities can be present)
endif

# Use the edma.h header exported from last version of edma driver
# Use include option to preempt the edma header inclusion from linux
ccflags-y += -include $(src)/kernel/common/include/edma.h

ifeq ($(CONFIG_ARCH_BCM2835),y)
# Kernel built to support a Raspberry Pi CM4 -> Force 32bit PCIe accesses
ccflags-y += -DAKIDA_DW_EDMA_FORCE_32BIT
endif

obj-m := akida-pcie.o

akida-pcie-y += akida-pcie-core.o
CFLAGS_akida-pcie-core.o += -I$(src)/akida-dw-edma

ifneq ($(CFG_AKIDA_DMA_RAM_PHY_FILE),)
# makefile for AKIDA_DMA_RAM_PHY_* variable provided.
# include it and activate CFG_AKIDA_DMA_RAM_PHY
include $(CFG_AKIDA_DMA_RAM_PHY_FILE)
CFG_AKIDA_DMA_RAM_PHY = 1
endif

ifneq ($(CFG_AKIDA_DMA_RAM_PHY),)
# CFG_AKIDA_DMA_RAM_PHY activated -> Set needed 'define'

define AKIDA_DEFINE_IF_SET
$(if $($(strip $(1))),-D'$(strip $(1))=$($(strip $(1)))')
endef

CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_ADDR)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_OFFSET)

CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX0_LL_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX0_LL_SIZE)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX1_LL_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX1_LL_SIZE)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX0_LL_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX0_LL_SIZE)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX1_LL_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX1_LL_SIZE)

CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX0_DT_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX0_DT_SIZE)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX1_DT_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_TX1_DT_SIZE)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX0_DT_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX0_DT_SIZE)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX1_DT_OFFSET)
CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_RX1_DT_SIZE)

CFLAGS_akida-pcie-core.o += $(call AKIDA_DEFINE_IF_SET, AKIDA_DMA_RAM_PHY_SIZE)

# Activate CFG_AKIDA_DMA_RAM_PHY at source level
CFLAGS_akida-pcie-core.o += -DCFG_AKIDA_DMA_RAM_PHY
endif

else
# normal makefile
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_PATH ?= /lib/modules/$(KERNEL_VERSION)/build

all:
	$(MAKE) -C $(KERNEL_PATH) M=$$PWD modules

clean:
	$(MAKE) -C $(KERNEL_PATH) M=$$PWD clean

endif
