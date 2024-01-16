# Akida PCIe driver
## Installing driver
Prerequisite: having gcc & build tools available, and kernel headers available

On Ubuntu:
```
sudo apt install build-essential linux-headers-$(uname -r)
```

Then simply run the install script from current directory.
```
./install.sh
```
It will build the driver, remove old installed driver versions if any,
and load the new one.
It will also configure modules to load it at every boot, and give read/write
access on `/dev/akida*` to __every__ user.

If you want to control permissions, edit the udev rules with your own
preferences in `99-akida-pcie.rules`

If you don't want the driver to load on every boot, edit the `/etc/modules`
file after installation and remove `akida_pcie` line.

***After a kernel update you need to run the install script again.***

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
