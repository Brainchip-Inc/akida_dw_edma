This directory (akida-dw-edma) contains a copy/paste of kernel v6.4 sources
drivers/dma/dw-edma/ files with few fixes send mainline.

This files are used to embed the dw-edma engine in the akida driver and so
avoid the usage of the kernel dw-edma module.

Some minor modifications were done:
- Change include paths to avoid including files from the parent directory
  (ie '#include "../xxxx.h"')
- Rename exported symbols to avoid name collision with upstream module
- Add akida-edma.h to declare renamed symbols

The following upstream fixes were backported on top of the snapshot
(all in dw-hdma-v0-core.c, HDMA native mode / AKD1500 only):
- 383baf5c8f06 ("dmaengine: dw-edma: Fix unmasking STOP and ABORT interrupts
  for HDMA") - v6.12
- 9f646ff25c09 ("dmaengine: dw-edma: Do not enable watermark interrupts for
  HDMA") - v6.12
- 3f63297ff61a ("dmaengine: dw-edma: Fix multiple times setting of
  CYCLE_STATE/CYCLE_BIT for HDMA") - v7.1

In order to update this directory from files updated in an upstream kernel,
perform the following steps:
- Copy/paste files from the upstream kernel
- Change the include path
- Rename the exported symbols
- Update akida-edma.h if needed
- Update kernel/common/include/edma.h if needed
