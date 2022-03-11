This driver handles data transfers to/from the Akida PCIe board.
The transfers are done using DMA transfers.

The driver embeds the dw-edma engine (Cf. akida-dw-edma/readme.txt).
This engine depends on virt-dma. The virt-dma module must be loaded before
this module driver is loaded.

Some kernel DMA engine private files are needed to compile.
These files are copied from the upstream kernel without any modifications in
kernel/x.y (x.y refering to the kernel version).
In the driver Makefile, ccflags-y is set in order to point to the kernel
compatible copy according to the kernel version we compile against.

To add a newer kernel set of files, follow the following steps:
- Copy the new files in the new kernel/x.y directory
- Update the Makefile to point to this new directory (ie update the
  AKIDA_KERNEL_VERSION_RANK variable and the ccflags-y setting).

If the new kernel is compatible with the previous one (ie no API changes).
For instance 5.y release contains no change compared to the previous one, the
following modification should be done in the Makefile:
    ---- 8< ----
    # copied in kernel/x.y/ subdirectory depending on kernel API changes.
    # These files are drivers/dma/dmaengine.h and drivers/dma/virt-dma.h
    AKIDA_KERNEL_VERSION_RANK := $(shell \
   -       printf "$(VERSION).$(PATCHLEVEL)\n5.4\n5.6\n5.7\n5.16\n5.17\n" | \
   +       printf "$(VERSION).$(PATCHLEVEL)\n5.4\n5.6\n5.7\n5.16\n5.y\n" | \
           sort -V )
   ...
    ifneq ($(word 4,$(AKIDA_KERNEL_VERSION_RANK)), 5.16)
    ccflags-y += -I$(src)/kernel/5.7/drivers/dma
    else
   -ifneq ($(word 5,$(AKIDA_KERNEL_VERSION_RANK)), 5.17)
   +ifneq ($(word 5,$(AKIDA_KERNEL_VERSION_RANK)), 5.y)
    ccflags-y += -I$(src)/kernel/5.16/drivers/dma
    else
    $(error Kernel $(VERSION).$(PATCHLEVEL) not supported. Some incompatibilities can be present)
    ---- 8< ----

If the new kernel is not compatible with the previous one (ie API changes).
For instance 5.y release contains changes compared to the previous one, the
following modification should be done in th Makefile:
    ---- 8< ----
     # copied in kernel/x.y/ subdirectory depending on kernel API changes.
     # These files are drivers/dma/dmaengine.h and drivers/dma/virt-dma.h
     AKIDA_KERNEL_VERSION_RANK := $(shell \
    -       printf "$(VERSION).$(PATCHLEVEL)\n5.4\n5.6\n5.7\n5.16\n5.17\n" | \
    +       printf "$(VERSION).$(PATCHLEVEL)\n5.4\n5.6\n5.7\n5.16\n5.y\n5.z\n" | \
            sort -V )
    ...
     ifneq ($(word 4,$(AKIDA_KERNEL_VERSION_RANK)), 5.16)
     ccflags-y += -I$(src)/kernel/5.7/drivers/dma
     else
    -ifneq ($(word 5,$(AKIDA_KERNEL_VERSION_RANK)), 5.17)
    +ifneq ($(word 5,$(AKIDA_KERNEL_VERSION_RANK)), 5.y)
     ccflags-y += -I$(src)/kernel/5.16/drivers/dma
     else
    +ifneq ($(word 6,$(AKIDA_KERNEL_VERSION_RANK)), 5.z)
    +ccflags-y += -I$(src)/kernel/5.y/drivers/dma
    +else
     $(error Kernel $(VERSION).$(PATCHLEVEL) not supported. Some incompatibilities can be present)
     endif
     endif
     endif
     endif
     endif
    +endif
    ---- 8< ----
Note the 5.z witch is set the first kernel not tested (probably 5.y+1).
