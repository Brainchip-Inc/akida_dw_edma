ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

# Some files needed by the dw-edma drivers are not exported and are locally
# copied. These files are drivers/dma/dmaengine.h and drivers/dma/virt-dma.h
ccflags-y += -I$(src)/kernel/5.4/drivers/dma

CFLAGS_akida-pcie-core.o += -I$(src)/kernel/5.4/drivers/dma/dw-edma

obj-m := akida-pcie.o

akida-pcie-y += akida-pcie-core.o
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