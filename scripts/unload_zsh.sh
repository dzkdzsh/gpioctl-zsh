#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "unload_zsh.sh must run as root" >&2
	exit 1
fi

overlay_dir=/sys/kernel/config/device-tree/overlays/gpioctl_zsh
if [ -d "$overlay_dir" ]; then
	rmdir "$overlay_dir"
fi
modprobe -r gpioctl_backend_phytium_zsh 2>/dev/null || true
modprobe -r gpioctl_backend_gpiolib_zsh
modprobe -r gpioctl_core_zsh
echo "gpioctl_zsh unloaded"
