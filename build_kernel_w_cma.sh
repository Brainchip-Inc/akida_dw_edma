#!/bin/bash

# This scripts builds the kernel with CMA enabled with 64MB by default, adapted
# for usage with AKD1500.
#
# Info on how to build from:
# - https://wiki.ubuntu.com/Kernel/BuildYourOwnKernel
# - https://askubuntu.com/questions/1435591/correct-way-to-build-kernel-with-hardware-support-fix-patches-ubuntu-22-04-lts

POSTFIX=-cma-akd1500
DIRNAME=linux-kernel$POSTFIX

# Check if CMA is already enabled
grep "CONFIG_CMA=y" /boot/config-$(uname -r)
ret=$?
if [ $ret -eq 0 ]; then
    echo "CONFIG_CMA=y is present in your current kernel config, no need to"
    echo "rebuild a new kernel."
    exit 0
fi

mkdir -p $DIRNAME
cd $DIRNAME

sudo sed -i 's/# deb-src/deb-src/' /etc/apt/sources.list

sudo apt update

# Instead of:
# sudo apt-get build-dep linux linux-image-$(uname -r)
# This seems to work:
sudo apt -y build-dep linux-image-generic

sudo apt-get install libncurses-dev gawk flex bison openssl libssl-dev dkms libelf-dev libudev-dev libpci-dev libiberty-dev autoconf llvm

sudo apt-get install git

apt-get source linux-image-unsigned-$(uname -r)

linux_dir=`ls -d */`

cd $linux_dir

chmod a+x debian/rules
chmod a+x debian/scripts/*
chmod a+x debian/scripts/misc/*

cp /boot/config-$(uname -r) ./.config

# Modify configuration to enable CMA
MBYTES_CMA=64
echo "CONFIG_CMA=y" >> ./.config
echo "CONFIG_CMA_SIZE_MBYTES=$MBYTES_CMA" >> ./.config
echo "CONFIG_CMA_SIZE_SEL_MBYTES=y" >> ./.config

# update config for new kernel with default options for other settings
make olddefconfig

echo
echo
echo "👉 Kernel will now be built, this might take a while."
echo
echo

make deb-pkg LOCALVERSION=-cma-akd1500

echo
echo
echo "🎉 Kernel is now built! Availables packages are in $DIRNAME:"
echo
ls $DIRNAME/*.deb
echo
echo "You can install those with these commands:"
echo
echo "sudo dpkg -i $DIRNAME/*.deb"
echo "sudo update-grub"
echo "sudo reboot"
echo
