#!/usr/bin/env bash

# This script will build and install akida-pcie driver module
# It will write to /etc/modules file to auto load driver at boot,
# and add an udev rule to make /dev/akida* with 666 chmod
# It will also remove the pedd_bc driver if present

echo "Building akida-pcie driver module"
# Clean & build driver
make clean
make || exit 1

# Copy driver to /lib/modules folder
modules_dir="/lib/modules/$(uname -r)/kernel/drivers"
echo "Build successful! Copying driver to '$modules_dir'"
sudo cp -f akida-pcie.ko "$modules_dir" || exit 1

# Unload eventual drivers
sudo rmmod pedd_bc 2> /dev/null
sudo rmmod akida_pcie 2> /dev/null

# Remove old pedd_bc driver if it exists
if [[ -f "$modules_dir/pedd-bc.ko" ]]; then
    echo "Removing old pedd-bc driver"
    sudo rm -f "$modules_dir/pedd-bc.ko" || exit 1
fi

# Update /etc/modules file
echo "Updating /etc/modules file"
# This sed call match pedd_bc or akida_pcie and replace it by akida_pcie
# It makes sed return 0 if match was found 1 otherwise
sudo sed -E -i '/pedd_bc|akida_pcie/{s//akida_pcie/;h};${x;/./{x;q0};x;q1}' /etc/modules
# if our driver was not in /etc/modules file append it
if [[ $? -eq 1 ]]; then
    echo "akida_pcie" | sudo tee -a /etc/modules > /dev/null
fi
echo "Copying udev rule"
sudo cp 99-akida-pcie.rules /etc/udev/rules.d || exit 1

# Calling depmod, load driver & udevadm trigger
echo "Reloading modules dependencies"
sudo depmod || exit 1
echo "Loading driver"
sudo modprobe akida-pcie || exit 1
echo "Triggering udev rules"
sudo udevadm trigger || exit 1

echo "akida-pcie was succesfully installed!"
