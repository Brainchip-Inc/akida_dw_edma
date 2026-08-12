#!/bin/sh
# Installs the udev rule and reloads udev after a DKMS build/install.
#
# DKMS's working directory when it runs POST_INSTALL is not guaranteed to be
# the module source tree (it varies by dkms version and by whether the hook
# runs from a per-kernel build copy). Resolve the rule file relative to this
# script's own location instead of relying on cwd - dkms invokes hook scripts
# by absolute path, so dirname "$0" reliably points at the source tree.
set -e

script_dir="$(cd "$(dirname "$0")" && pwd)"

cp -f "$script_dir/99-akida-pcie.rules" /etc/udev/rules.d/
udevadm control --reload-rules
udevadm trigger
