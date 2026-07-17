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
core_new=0
gpiolib_new=0
phytium_new=0
overlay_new=0

module_loaded_zsh()
{
	grep -q "^$1 " /proc/modules
}

cleanup_zsh()
{
	status=$?
	trap - 0 1 2 15
	if [ "$status" -ne 0 ]; then
		[ "$phytium_new" -eq 0 ] || modprobe -r gpioctl_backend_phytium_zsh 2>/dev/null || true
		[ "$gpiolib_new" -eq 0 ] || modprobe -r gpioctl_backend_gpiolib_zsh 2>/dev/null || true
		[ "$overlay_new" -eq 0 ] || rmdir "$overlay_dir" 2>/dev/null || true
		[ "$core_new" -eq 0 ] || modprobe -r gpioctl_core_zsh 2>/dev/null || true
	fi
	exit "$status"
}
trap cleanup_zsh 0
trap 'exit 129' 1
trap 'exit 130' 2
trap 'exit 143' 15

if ! module_loaded_zsh gpioctl_core_zsh; then
	modprobe gpioctl_core_zsh
	core_new=1
fi

if ! mountpoint -q /sys/kernel/config; then
	mount -t configfs configfs /sys/kernel/config
fi
if [ ! -d "$overlay_root" ]; then
	echo "device-tree overlay configfs is unavailable" >&2
	exit 1
fi
if [ ! -d "$overlay_dir" ]; then
	mkdir "$overlay_dir"
	overlay_new=1
	tee "$overlay_dir/dtbo" < "$dtbo" >/dev/null
fi
status=$(cat "$overlay_dir/status")
if [ "$status" != applied ]; then
	echo "device-tree overlay status is '$status'" >&2
	exit 1
fi
if ! module_loaded_zsh gpioctl_backend_gpiolib_zsh; then
	modprobe gpioctl_backend_gpiolib_zsh
	gpiolib_new=1
fi
if ! module_loaded_zsh gpioctl_backend_phytium_zsh; then
	modprobe gpioctl_backend_phytium_zsh
	phytium_new=1
fi
udevadm trigger --subsystem-match=gpioctl_zsh 2>/dev/null || true
trap - 0 1 2 15
echo "gpioctl_zsh loaded; overlay=$status"
