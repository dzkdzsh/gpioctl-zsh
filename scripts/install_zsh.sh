#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "install_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
kernel_release=${KERNEL_RELEASE:-$(uname -r)}
module_dir="/lib/modules/$kernel_release/kernel/test/course_design_zsh"
legacy_module_dir="/lib/modules/$kernel_release/extra/gpioctl_zsh"

for artifact in \
	gpioctl_core_zsh.ko \
	gpioctl_backend_gpiolib_zsh.ko \
	gpioctl_backend_phytium_zsh.ko \
	gpioctl_mock_zsh.ko
do
	test -f "$project_dir/build/kernel/$artifact" || {
		echo "missing build/kernel/$artifact; run make first" >&2
		exit 1
	}
done
test -x "$project_dir/build/userspace/gpioctl_zsh"
test -f "$project_dir/build/dts/phytium-pi-gpioctl-zsh.dtbo"

install -d -m 0755 "$module_dir"
install -m 0644 "$project_dir"/build/kernel/*.ko "$module_dir/"
rm -f "$legacy_module_dir/"gpioctl_core_zsh.ko \
	"$legacy_module_dir/"gpioctl_backend_gpiolib_zsh.ko \
	"$legacy_module_dir/"gpioctl_backend_phytium_zsh.ko \
	"$legacy_module_dir/"gpioctl_mock_zsh.ko
rmdir "$legacy_module_dir" 2>/dev/null || true
install -D -m 0755 "$project_dir/build/userspace/gpioctl_zsh" \
	/usr/local/bin/gpioctl_zsh
install -D -m 0644 "$project_dir/build/userspace/libgpioctl_zsh.a" \
	/usr/local/lib/libgpioctl_zsh.a
install -D -m 0644 "$project_dir/include/gpioctl_zsh.h" \
	/usr/local/include/gpioctl_zsh.h
install -D -m 0644 "$project_dir/include/uapi/gpioctl_zsh.h" \
	/usr/local/include/uapi/gpioctl_zsh.h
install -D -m 0644 "$project_dir/config/phytium-pi-v1.conf" \
	/etc/gpioctl_zsh/board.conf
install -D -m 0644 "$project_dir/build/dts/phytium-pi-gpioctl-zsh.dtbo" \
	/usr/lib/gpioctl_zsh/phytium-pi-gpioctl-zsh.dtbo
install -D -m 0644 "$project_dir/packaging/70-gpioctl-zsh.rules" \
	/etc/udev/rules.d/70-gpioctl-zsh.rules
install -D -m 0755 "$project_dir/scripts/load_zsh.sh" \
	/usr/libexec/gpioctl-zsh/load_zsh.sh
install -D -m 0755 "$project_dir/scripts/unload_zsh.sh" \
	/usr/libexec/gpioctl-zsh/unload_zsh.sh
install -D -m 0755 "$project_dir/scripts/autostart_zsh.sh" \
	/usr/local/sbin/gpioctl-zsh-autostart
install -D -m 0644 "$project_dir/packaging/gpioctl-zsh.service" \
	/etc/systemd/system/gpioctl-zsh.service

if ! getent group gpio >/dev/null 2>&1; then
	groupadd --system gpio
fi
depmod -a "$kernel_release"
udevadm control --reload-rules
udevadm trigger --subsystem-match=gpioctl_zsh 2>/dev/null || true
systemctl daemon-reload
systemctl reset-failed gpioctl-zsh.service 2>/dev/null || true
systemctl start gpioctl-zsh.service
echo "gpioctl_zsh installed and started for kernel $kernel_release"
echo "boot autostart remains disabled; enable it with: sudo gpioctl-zsh-autostart enable"
