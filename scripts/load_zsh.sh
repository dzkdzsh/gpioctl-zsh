#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "load_zsh.sh must run as root" >&2
	exit 1
fi

overlay_root=/sys/kernel/config/device-tree/overlays
overlay_dir="$overlay_root/gpioctl_zsh"
dtbo=${GPIOCTL_ZSH_DTBO:-/usr/lib/gpioctl_zsh/phytium-pi-gpioctl-zsh.dtbo}

modprobe gpioctl_core_zsh
modprobe gpioctl_backend_gpiolib_zsh
modprobe gpioctl_backend_phytium_zsh

if ! mountpoint -q /sys/kernel/config; then
	mount -t configfs configfs /sys/kernel/config
fi
if [ ! -d "$overlay_root" ]; then
	echo "device-tree overlay configfs is unavailable" >&2
	exit 1
fi
if [ ! -d "$overlay_dir" ]; then
	mkdir "$overlay_dir"
	tee "$overlay_dir/dtbo" < "$dtbo" >/dev/null
fi
status=$(cat "$overlay_dir/status")
if [ "$status" != applied ]; then
	echo "device-tree overlay status is '$status'" >&2
	exit 1
fi
udevadm trigger --subsystem-match=gpioctl_zsh 2>/dev/null || true
echo "gpioctl_zsh loaded; overlay=$status"
