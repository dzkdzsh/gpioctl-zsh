#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "test_conflict_zsh.sh must run as root" >&2
	exit 1
fi

lab_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
project_dir=$(CDPATH='' cd -- "$lab_dir/../.." && pwd)
overlay_name=gpioctl-raw-conflict-test-zsh
overlay_dir=/sys/kernel/config/device-tree/overlays/$overlay_name
dtbo=$project_dir/build/raw-mmio-lab/conflict-probe-zsh.dtbo
module=$lab_dir/gpioctl_raw_lab_zsh.ko

cleanup_zsh()
{
	status=$?
	trap - EXIT HUP INT TERM
	if [ -d "$overlay_dir" ]; then
		rmdir "$overlay_dir" 2>/dev/null || true
	fi
	rmmod gpioctl_raw_lab_zsh 2>/dev/null || true
	exit "$status"
}
trap cleanup_zsh EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

test -f "$dtbo"
test -f "$module"
test -d /sys/kernel/config/device-tree/overlays
test -e /dev/gpio4_zsh
rmmod gpioctl_raw_lab_zsh 2>/dev/null || true
log_marker="gpioctl-raw-conflict-start-zsh-$$"
overlay_warnings_before=$(dmesg |
	grep -Ec 'OF: overlay: WARNING: memory leak' || true)
insmod "$module" allow_write=0
printf '<6>%s\n' "$log_marker" > /dev/kmsg
mkdir "$overlay_dir"
dd if="$dtbo" of="$overlay_dir/dtbo" status=none
test "$(cat "$overlay_dir/status")" = applied
sleep 0.2

if find /sys/bus/platform/drivers/gpioctl-raw-lab-zsh -maxdepth 1 \
	-type l -name '*28038000*' | grep -q .; then
	echo "raw lab bound despite active GPIO4 resource" >&2
	exit 1
fi
new_log=$(dmesg | sed -n "/$log_marker/,\$p")
printf '%s\n' "$new_log" | grep -q 'gpioctl-raw.*exclusive MMIO claim failed'
printf '%s\n' "$new_log" | grep -Eq 'EBUSY|error -16'
test -e /dev/gpio4_zsh
rmdir "$overlay_dir"
overlay_warnings_after=$(dmesg |
	grep -Ec 'OF: overlay: WARNING: memory leak' || true)
test "$overlay_warnings_before" -eq "$overlay_warnings_after"
echo "raw-mmio conflict test: PASS active GPIO4 rejected duplicate claim with EBUSY"
