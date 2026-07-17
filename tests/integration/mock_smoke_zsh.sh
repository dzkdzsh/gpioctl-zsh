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
event_log=
overflow_log=

cleanup_zsh()
{
	if [ -n "$holder_pid" ]; then
		kill "$holder_pid" 2>/dev/null || true
		wait "$holder_pid" 2>/dev/null || true
	fi
	rm -f "$event_log" "$overflow_log"
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
insmod "$project_dir/build/kernel/gpioctl_core_zsh.ko"
insmod "$project_dir/build/kernel/gpioctl_mock_zsh.ko" fail_offset=-1
udevadm trigger --subsystem-match=gpioctl_zsh 2>/dev/null || true
udevadm settle 2>/dev/null || true

"$cli" --config "$config" list | grep -q '/dev/gpio0_zsh'
test "$(cat /sys/class/gpioctl_zsh/gpio0_zsh/allowlisted_lines)" -eq 14
test "$(cat /sys/class/gpioctl_zsh/gpio0_zsh/output_lines)" -eq 14
test "$(cat /sys/class/gpioctl_zsh/gpio0_zsh/reserved_lines)" -eq 1
"$project_dir/build/userspace/policy_probe_zsh" /dev/gpio0_zsh
runuser -u zsh -- "$cli" --config "$config" get /dev/gpio0_zsh:10 \
	>/dev/null
if runuser -u zsh -- "$cli" --config "$config" get /dev/gpio0_zsh:14 \
	>/dev/null 2>&1; then
	echo "unprivileged non-allowlisted lease unexpectedly succeeded" >&2
	exit 1
fi
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

transaction_output=$(
	printf '%s\n' \
		'transaction /dev/gpio0_zsh' \
		'tx-line 7 out 1' \
		'tx-line 8 out 0 active-low' \
		'commit 0' |
		"$cli" --json --strict --config "$config" run -
)
printf '%s\n' "$transaction_output" |
	grep -q '"ok":true,"operation":"commit"'
"$cli" --config "$config" stats /dev/gpio0_zsh |
	grep -q 'active_leases=0'

if printf '%s\n' \
	'transaction /dev/gpio0_zsh' \
	'tx-line 7 out 1' \
	'tx-line 7 out 0' \
	'commit 0' |
	"$cli" --json --strict --config "$config" run - >/dev/null 2>&1; then
	echo "duplicate transaction offset unexpectedly succeeded" >&2
	exit 1
fi
"$cli" --config "$config" stats /dev/gpio0_zsh |
	grep -q 'active_leases=0'

printf '8\n' > /sys/module/gpioctl_mock_zsh/parameters/fail_offset
if printf '%s\n' \
	'transaction /dev/gpio0_zsh' \
	'tx-line 7 out 0' \
	'tx-line 8 out 1' \
	'commit 0' |
	"$cli" --json --strict --config "$config" run - >/dev/null 2>&1; then
	echo "fault-injected transaction unexpectedly succeeded" >&2
	exit 1
fi
printf '%s\n' '-1' > /sys/module/gpioctl_mock_zsh/parameters/fail_offset
"$cli" --config "$config" stats /dev/gpio0_zsh |
	grep -q 'active_leases=0'

if printf '%s\n' \
	'transaction /dev/gpio0_zsh' \
	'tx-line 9 out 1' |
	"$cli" --json --strict --config "$config" run - >/dev/null 2>&1; then
	echo "uncommitted transaction unexpectedly succeeded" >&2
	exit 1
fi
"$cli" --config "$config" stats /dev/gpio0_zsh |
	grep -q 'active_leases=0'

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

event_log=$(mktemp)
"$cli" --config "$config" watch /dev/gpio0_zsh:5 both 10000 3 \
	>"$event_log" &
holder_pid=$!
sleep 1
printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
wait "$holder_pid"
holder_pid=
test "$(grep -c '^event ' "$event_log")" -eq 3
grep -q 'sequence=1 ' "$event_log"
grep -q 'sequence=3 ' "$event_log"

: >"$event_log"
"$cli" --json --config "$config" watch /dev/gpio0_zsh:5 both \
	10000 2 500000 >"$event_log" &
holder_pid=$!
sleep 1
printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
sleep 0.6
printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
wait "$holder_pid"
holder_pid=
test "$(grep -c '\"type\":\"event\"' "$event_log")" -eq 2
grep -q '\"sequence\":1' "$event_log"
grep -q '\"sequence\":2' "$event_log"

overflow_log=$(mktemp)
"$project_dir/build/userspace/event_epoll_zsh" \
	/dev/gpio0_zsh 6 5000 200 1 >"$overflow_log" &
holder_pid=$!
i=0
while ! grep -q '^READY$' "$overflow_log"; do
	i=$((i + 1))
	if [ "$i" -ge 50 ]; then
		echo "event epoll probe did not become ready" >&2
		exit 1
	fi
	sleep 0.1
done
exec 9> /sys/module/gpioctl_mock_zsh/parameters/inject_offset
i=0
while [ "$i" -lt 300 ]; do
	printf '6\n' >&9
	i=$((i + 1))
done
exec 9>&-
wait "$holder_pid"
holder_pid=
grep -q 'event_epoll_zsh: PASS' "$overflow_log"
grep -q 'events=256 ' "$overflow_log"
grep -q 'overflow=44 drops=44 ' "$overflow_log"
grep -q 'first_sequence=45 last_sequence=300' "$overflow_log"
tail -n 1 "$overflow_log"

echo "mock_smoke_zsh: PASS"
