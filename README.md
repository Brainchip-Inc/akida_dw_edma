# Akida PCIe driver
## Installing driver
Prerequisite: having gcc & build tools available, kernel headers available,
and DKMS.

On Ubuntu:
```
sudo apt install dkms build-essential linux-headers-$(uname -r)
```

The driver is packaged for DKMS (Dynamic Kernel Module Support), so once
installed it is automatically rebuilt and reinstalled every time you update
your kernel — no manual step required after the initial install.

### Quick install

Run the install script from the current directory:
```
./install.sh
```
It registers the driver with DKMS, builds and installs it for the running
kernel, and cleans up any leftovers from a pre-DKMS install of this driver
(a `.ko` copied directly into `/lib/modules/.../kernel/drivers` and the
corresponding `/etc/modules` line).

The udev rule installed alongside the module gives read/write access on
`/dev/akida*` to __every__ user by default. If you want to control
permissions, edit `99-akida-pcie.rules` before installing, or in
`/etc/udev/rules.d/99-akida-pcie.rules` afterwards, then run
`sudo udevadm control --reload-rules && sudo udevadm trigger`.

The driver loads automatically via udev when a device is present — no
`/etc/modules` entry is needed.

### Manual install

If you'd rather drive DKMS directly instead of using `install.sh`:
```
sudo git clone https://github.com/Brainchip-Inc/akida_dw_edma /usr/src/akida-pcie-1.0
sudo dkms add -m akida-pcie -v 1.0
sudo dkms build -m akida-pcie -v 1.0
sudo dkms install -m akida-pcie -v 1.0
```

### Uninstalling

```
sudo dkms remove -m akida-pcie -v 1.0 --all
sudo rm -rf /usr/src/akida-pcie-1.0
```

### Non-default DMA RAM PHY configuration

If you build with a non-default `CFG_AKIDA_DMA_RAM_PHY_FILE` (see
`cfg_dma_ram_phy_*.mk`), be aware that DKMS's automatic rebuild on kernel
upgrade (`AUTOINSTALL="yes"`, run from a kernel postinst hook) always
builds with the default configuration — there is no way to persist that
variable into an unattended, kernel-triggered rebuild. After a kernel
upgrade, re-run the build manually with the variable exported so it reaches
DKMS's environment:
```
sudo -E env CFG_AKIDA_DMA_RAM_PHY_FILE=cfg_dma_ram_phy_4MB.mk \
    dkms install -m akida-pcie -v 1.0 --force
```

### Known limitation: kernel 6.9 and newer

The vendored DMA headers in this repo only support kernel 5.4 through 6.8
(see the version guard in `Makefile`). `dkms.conf` restricts the build to
that range, so on 6.9+ DKMS reports the module as "not applicable" instead
of failing — but the driver will not build or load on those kernels.
Support for 6.9+ against the kernel's in-tree `dw-edma` is tracked
separately in issue #23; this is not fixed here.

## Enable CMA in the kernel

Some systems, e.g.: Ubuntu on x86_64, do not come with CMA (contiguous memory
allocation) support enabled in the kernel. This is required to use AKD1500
devices through PCIe if you want to use larger amounts of memory to program big
models or make full usage of the pipeline. To build the kernel packages that
include such support you can run the dedicated script, tailored for the Ubuntu
distribution:

```
./build_kernel_w_cma.sh
```

> Note: this command might take long time because parallel build is not set by default. To enable that you can export this environment variable before launching the build to use a number of jobs equivalent to the number of available cores:
>
> `export MAKEFLAGS="-j $(nproc)"`


The script will create the packages and explain how to install and boot on the
new kernel.

Note that this will not prevent the upgrade and install of new kernel versions
on your system. If you want to do that on Ubuntu, you can run:

```
sudo apt-mark hold `uname -r`
```

Note that this will prevent security updates on current kernels, and that might
not be safe in some environments.
If you want to go back to an old kernel that does not contain the CMA feature,
you can grep the installed kernels that were configured in grub:

```
sudo grub-mkconfig | grep -iE "menuentry 'Ubuntu, with Linux" | awk '{print i++ " : "$1, $2, $3, $4, $5, $6, $7}'
```

This will print a list of options prefixed by a number. If you want to select
for example the item 2, modify the `/etc/default/grub` file from:
`GRUB_DEFAULT=0` to: `GRUB_DEFAULT="1>2"`.

Once done, you will need to invoke `update-grub` and reboot:

```
sudo update-grub
sudo reboot
```

Source: https://askubuntu.com/questions/82140/how-can-i-boot-with-an-older-kernel-version


## Support
Please visit:
- https://doc.brainchipinc.com/ for akida documentation, examples and tutorials
- https://brainchipinc.com/support/ for support requests
