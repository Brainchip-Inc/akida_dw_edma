#!/usr/bin/env bash

# This script installs the akida-pcie driver via DKMS, so it is automatically
# rebuilt and reinstalled on every kernel update instead of only once, now.
#
# If a previous version of this script was used on this machine, it will
# have copied akida-pcie.ko straight into the kernel/drivers directory and
# added an /etc/modules entry. That path conflicts with DKMS, which installs
# into .../updates instead, so this script first removes any such leftovers.

set -euo pipefail

PACKAGE_NAME="akida-pcie"
PACKAGE_VERSION="1.0"
SRC_DIR="/usr/src/${PACKAGE_NAME}-${PACKAGE_VERSION}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v dkms >/dev/null 2>&1; then
    echo "dkms is not installed. Install it first, e.g. on Ubuntu/Debian:" >&2
    echo "  sudo apt install dkms build-essential linux-headers-\$(uname -r)" >&2
    exit 1
fi

echo "Cleaning up any pre-DKMS install"

# Old pedd_bc driver, predating akida-pcie entirely.
sudo rmmod pedd_bc 2> /dev/null || true
legacy_pedd="/lib/modules/$(uname -r)/kernel/drivers/pedd-bc.ko"
if [[ -f "$legacy_pedd" ]]; then
    echo "Removing old pedd-bc driver"
    sudo rm -f "$legacy_pedd"
fi

# akida-pcie.ko copied directly by a previous version of this script.
legacy_ko="/lib/modules/$(uname -r)/kernel/drivers/akida-pcie.ko"
if [[ -f "$legacy_ko" ]]; then
    echo "Removing stale module: $legacy_ko"
    sudo rmmod akida_pcie 2> /dev/null || true
    sudo rm -f "$legacy_ko"
    sudo sed -E -i '/^akida_pcie$/d' /etc/modules
    sudo depmod
fi

echo "Staging sources under $SRC_DIR for DKMS"
if [[ "$SCRIPT_DIR" != "$SRC_DIR" ]]; then
    sudo mkdir -p "$SRC_DIR"
    sudo rsync -a --delete "$SCRIPT_DIR"/ "$SRC_DIR"/ 2>/dev/null || sudo cp -a "$SCRIPT_DIR"/. "$SRC_DIR"/
fi

if sudo dkms status "${PACKAGE_NAME}/${PACKAGE_VERSION}" 2>/dev/null | grep -q .; then
    echo "Removing previously registered DKMS module to rebuild it cleanly"
    sudo dkms remove -m "$PACKAGE_NAME" -v "$PACKAGE_VERSION" --all
fi

sudo dkms add -m "$PACKAGE_NAME" -v "$PACKAGE_VERSION"
sudo dkms build -m "$PACKAGE_NAME" -v "$PACKAGE_VERSION"
sudo dkms install -m "$PACKAGE_NAME" -v "$PACKAGE_VERSION"
sudo udevadm trigger

echo "akida-pcie was installed via DKMS and will be rebuilt automatically on kernel updates."
echo "To remove it: sudo dkms remove -m $PACKAGE_NAME -v $PACKAGE_VERSION --all"
