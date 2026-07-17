#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "mock_smoke_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cli=${GPIOCTL_ZSH_CLI:-$project_dir/build/userspace/gpioctl_zsh}
config=${GPIOCTL_ZSH_CONFIG:-$project_dir/config/phytium-pi-v1.conf}
holder_pid=

cleanup_zsh()
{
	if [ -n "$holder_pid" ]; then
		kill "$holder_pid" 2>/dev/null || true
		wait "$holder_pid" 2>/dev/null || true
	fi
	modprobe -r gpioctl_mock_zsh 2>/dev/null || true
	modprobe -r gpioctl_core_zsh 2>/dev/null || true
	if [ -x "$project_dir/scripts/load_zsh.sh" ] &&
	   [ -f /usr/lib/gpioctl_zsh/phytium-pi-gpioctl-zsh.dtbo ]; then
		"$project_dir/scripts/load_zsh.sh" >/dev/null 2>&1 || true
	fi
}
trap cleanup_zsh EXIT HUP INT TERM

test -x "$cli"
if [ -x "$project_dir/scripts/unload_zsh.sh" ]; then
	"$project_dir/scripts/unload_zsh.sh"
fi
modprobe gpioctl_core_zsh
modprobe gpioctl_mock_zsh fail_offset=-1

"$cli" --config "$config" list | grep -q '/dev/gpio0_zsh'
printf '%s\n' \
	'acquire /dev/gpio0_zsh:1 out 0' \
	'value /dev/gpio0_zsh:1 1' \
	'value /dev/gpio0_zsh:1' \
	'release /dev/gpio0_zsh:1' |
	"$cli" --strict --config "$config" run - |
	grep -q '/dev/gpio0_zsh:1=1'

printf '2\n' > /sys/module/gpioctl_mock_zsh/parameters/fail_offset
if "$cli" --config "$config" batch-set /dev/gpio0_zsh 0 1=1 2=1; then
	echo "fault-injected batch unexpectedly succeeded" >&2
	exit 1
fi
"$cli" --config "$config" stats /dev/gpio0_zsh |
	grep -q 'active_leases=0'
printf '%s\n' '-1' > /sys/module/gpioctl_mock_zsh/parameters/fail_offset

printf '%s\n' \
	'acquire /dev/gpio0_zsh:3 out 0' \
	'sleep 1500' \
	'release /dev/gpio0_zsh:3' |
	"$cli" --strict --config "$config" run - >/dev/null &
holder_pid=$!
sleep 1
if "$cli" --config "$config" set /dev/gpio0_zsh:3 1; then
	echo "exclusive lease conflict unexpectedly succeeded" >&2
	exit 1
fi
wait "$holder_pid"
holder_pid=
"$cli" --config "$config" stats /dev/gpio0_zsh |
	grep -q 'active_leases=0'

echo "mock_smoke_zsh: PASS"
