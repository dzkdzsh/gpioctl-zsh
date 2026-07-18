#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "unload_zsh.sh must run as root" >&2
	exit 1
fi

overlay_dir=/sys/kernel/config/device-tree/overlays/gpioctl_zsh
remove_overlay=0

case ${1:-} in
	"") ;;
	--remove-overlay) remove_overlay=1 ;;
	*)
		echo "usage: $0 [--remove-overlay]" >&2
		exit 2
		;;
esac

modprobe -r gpioctl_backend_phytium_zsh 2>/dev/null || true
modprobe -r gpioctl_backend_gpiolib_zsh
if [ "$remove_overlay" -eq 1 ] && [ -d "$overlay_dir" ]; then
	rmdir "$overlay_dir"
fi
modprobe -r gpioctl_core_zsh
if [ -d "$overlay_dir" ]; then
	echo "gpioctl_zsh unloaded; overlay=preserved"
else
	echo "gpioctl_zsh unloaded; overlay=absent"
fi
