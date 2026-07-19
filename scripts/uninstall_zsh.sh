#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "uninstall_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
kernel_release=${KERNEL_RELEASE:-$(uname -r)}
systemctl disable --now gpioctl-zsh.service 2>/dev/null || true
"$project_dir/scripts/unload_zsh.sh" --remove-overlay 2>/dev/null || true
rm -f /usr/local/bin/gpioctl_zsh
rm -f /usr/local/lib/libgpioctl_zsh.a
rm -f /usr/local/include/gpioctl_zsh.h
rm -f /usr/local/include/uapi/gpioctl_zsh.h
rm -f /etc/gpioctl_zsh/board.conf
rm -f /usr/lib/gpioctl_zsh/phytium-pi-gpioctl-zsh.dtbo
rm -f /etc/udev/rules.d/70-gpioctl-zsh.rules
rm -f /usr/local/sbin/gpioctl-zsh-autostart
rm -f /etc/systemd/system/gpioctl-zsh.service
rm -f /usr/libexec/gpioctl-zsh/load_zsh.sh
rm -f /usr/libexec/gpioctl-zsh/unload_zsh.sh
rm -f "/lib/modules/$kernel_release/kernel/test/course_design_zsh/"*.ko
rm -f "/lib/modules/$kernel_release/extra/gpioctl_zsh/"*.ko
rmdir /etc/gpioctl_zsh /usr/lib/gpioctl_zsh \
	/usr/libexec/gpioctl-zsh \
	"/lib/modules/$kernel_release/kernel/test/course_design_zsh" \
	"/lib/modules/$kernel_release/extra/gpioctl_zsh" 2>/dev/null || true
depmod -a "$kernel_release"
udevadm control --reload-rules
systemctl daemon-reload
systemctl reset-failed gpioctl-zsh.service 2>/dev/null || true
echo "gpioctl_zsh uninstalled"
