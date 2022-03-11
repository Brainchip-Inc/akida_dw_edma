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
