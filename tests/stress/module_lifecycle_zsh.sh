#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "module_lifecycle_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cli=${GPIOCTL_ZSH_CLI:-/usr/local/bin/gpioctl_zsh}
config=${GPIOCTL_ZSH_CONFIG:-/etc/gpioctl_zsh/board.conf}
cycles=${GPIOCTL_ZSH_LIFECYCLE_CYCLES:-20}
holder_pid=

cleanup_zsh()
{
	if [ -n "$holder_pid" ]; then
		kill "$holder_pid" 2>/dev/null || true
		wait "$holder_pid" 2>/dev/null || true
	fi
	"$project_dir/scripts/load_zsh.sh" >/dev/null 2>&1 || true
}
trap cleanup_zsh EXIT HUP INT TERM

overlay_warning_count_zsh()
{
	dmesg | grep -c 'OF: overlay: WARNING: memory leak' || true
}

wait_for_gpio1_leases_zsh()
{
	expected=$1
	i=0
	while [ "$(cat /sys/class/gpioctl_zsh/gpio1_zsh/active_leases)" -ne "$expected" ]; do
		i=$((i + 1))
		if [ "$i" -ge 100 ]; then
			echo "gpio1 active_leases did not reach $expected" >&2
			exit 1
		fi
		sleep 0.02
	done
}

"$project_dir/scripts/load_zsh.sh" >/dev/null
warnings_before=$(overlay_warning_count_zsh)
printf '%s\n' \
	'acquire GPIO1_1 in' \
	'sleep 60000' \
	'release GPIO1_1' |
	"$cli" --strict --config "$config" run - >/dev/null &
holder_pid=$!
wait_for_gpio1_leases_zsh 1
if "$project_dir/scripts/unload_zsh.sh" >/dev/null 2>&1; then
	echo "module unload unexpectedly succeeded with active file/lease" >&2
	exit 1
fi
kill -KILL "$holder_pid"
wait "$holder_pid" 2>/dev/null || true
holder_pid=
wait_for_gpio1_leases_zsh 0

i=0
while [ "$i" -lt "$cycles" ]; do
	"$project_dir/scripts/unload_zsh.sh" >/dev/null
	"$project_dir/scripts/load_zsh.sh" >/dev/null
	test -e /dev/gpio1_zsh
	"$cli" --config "$config" get GPIO1_1 >/dev/null
	i=$((i + 1))
done
warnings_after=$(overlay_warning_count_zsh)
if [ "$warnings_before" -ne "$warnings_after" ]; then
	echo "overlay leak warnings changed: $warnings_before -> $warnings_after" >&2
	exit 1
fi

for active in /sys/class/gpioctl_zsh/gpio*_zsh/active_leases; do
	test "$(cat "$active")" -eq 0
done
trap - EXIT HUP INT TERM
echo "module_lifecycle_zsh.sh: PASS cycles=$cycles overlay_warnings=$warnings_after"
