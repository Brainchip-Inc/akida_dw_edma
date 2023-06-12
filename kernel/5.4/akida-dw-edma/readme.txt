This directory (akida-dw-edma) contains a copy/paste of kernel v5.16 sources
drivers/dma/dw-edma/ files.

This files are used to embed the dw-edma engine in the akida driver and so
avoid the usage of the kernel dw-edma module.

Some minor modifications were done:
- Change include paths to avoid including files from the parent directory
  (ie '#include "../xxxx.h"')
- Rename exported symbols to avoid name collision with upstream module
- Add akida-edma.h to declare renamed symbols

In order to update this directory from files updated in an upstream kernel,
perform the following steps:
- Copy/paste files from the upstream kernel
- Change the include path
- Rename the exported symbols
- Update akida-edma.h if needed
