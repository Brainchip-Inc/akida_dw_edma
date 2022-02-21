ifneq ($(KERNELRELEASE),)
# kbuild part of makefile

# Some files needed by the dw-edma drivers are not exported and are locally
# copied in kernel/x.y/ subdirectory depending on kernel API changes.
# These files are drivers/dma/dmaengine.h and drivers/dma/virt-dma.h
AKIDA_KERNEL_VERSION_RANK := $(shell \
	printf "$(VERSION).$(PATCHLEVEL)\n5.4\n5.5\n" | \
	sort -V )

ifneq ($(word 1,$(AKIDA_KERNEL_VERSION_RANK)), 5.4)
$(error Kernel $(VERSION).$(PATCHLEVEL) too old)
else
ifneq ($(word 2,$(AKIDA_KERNEL_VERSION_RANK)), 5.5)
ccflags-y += -I$(src)/kernel/5.4/drivers/dma
else
$(error Kernel $(VERSION).$(PATCHLEVEL) not supported. Some incompatibilities can be present)
endif
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