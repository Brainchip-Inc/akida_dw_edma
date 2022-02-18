ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

# Some parts of dw-edma API are not exported and are locally copied.
# The missing files are dw-edma-core.h and its dependencies.
# This file is needed for the struct dw_edma_chip definition
# passed to dw_edma_probe() and dw_edma_remove()
CFLAGS_akida-pcie-core.o += -I$(src)/kernel/5.4/drivers/dma/dw-edma

obj-m := akida-pcie.o

akida-pcie-y += akida-pcie-core.o


else
# normal makefile
KERNEL_VERSION ?= $(shell uname -r)
KERNEL_PATH ?= /lib/modules/$(KERNEL_VERSION)/build

all:
	$(MAKE) -C $(KERNEL_PATH) M=$$PWD modules

clean:
	$(MAKE) -C $(KERNEL_PATH) M=$$PWD clean

endif