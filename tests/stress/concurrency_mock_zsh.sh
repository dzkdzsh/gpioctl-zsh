#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "concurrency_mock_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cli=${GPIOCTL_ZSH_CLI:-$project_dir/build/userspace/gpioctl_zsh}
config=${GPIOCTL_ZSH_CONFIG:-$project_dir/config/phytium-pi-v1.conf}
iterations=${GPIOCTL_ZSH_ITERATIONS:-25}
holder_pid=
injector_pid=
event_log=

cleanup_zsh()
{
	for pid in "$holder_pid" "$injector_pid"; do
		if [ -n "$pid" ]; then
			kill "$pid" 2>/dev/null || true
			wait "$pid" 2>/dev/null || true
		fi
	done
	rm -f "$event_log"
}
trap cleanup_zsh EXIT HUP INT TERM

wait_for_leases_zsh()
{
	expected=$1
	i=0
	while [ "$(cat /sys/class/gpioctl_zsh/gpio0_zsh/active_leases)" -ne "$expected" ]; do
		i=$((i + 1))
		if [ "$i" -ge 100 ]; then
			echo "active_leases did not reach $expected" >&2
			exit 1
		fi
		sleep 0.02
	done
}

run_worker_zsh()
{
	offset=$1
	i=0
	while [ "$i" -lt "$iterations" ]; do
		value=$((i % 2))
		"$cli" --config "$config" set "/dev/gpio0_zsh:$offset" \
			"$value" >/dev/null
		i=$((i + 1))
	done
}

for workers in 1 2 4 8; do
	pids=
	w=0
	while [ "$w" -lt "$workers" ]; do
		run_worker_zsh "$w" &
		pids="$pids $!"
		w=$((w + 1))
	done
	status=0
	for pid in $pids; do
		wait "$pid" || status=1
	done
	if [ "$status" -ne 0 ]; then
		echo "$workers-worker different-line run failed" >&2
		exit 1
	fi
	wait_for_leases_zsh 0
	echo "concurrency different-lines workers=$workers: PASS"
done

printf '%s\n' \
	'acquire /dev/gpio0_zsh:12 out 0' \
	'sleep 2000' \
	'release /dev/gpio0_zsh:12' |
	"$cli" --strict --config "$config" run - >/dev/null &
holder_pid=$!
wait_for_leases_zsh 1
pids=
w=0
while [ "$w" -lt 8 ]; do
	(
		if "$cli" --config "$config" set /dev/gpio0_zsh:12 1 \
			>/dev/null 2>&1; then
			exit 1
		fi
	) &
	pids="$pids $!"
	w=$((w + 1))
done
status=0
for pid in $pids; do
	wait "$pid" || status=1
done
if [ "$status" -ne 0 ]; then
	echo "same-line contention unexpectedly acquired held line" >&2
	exit 1
fi
if rmmod gpioctl_mock_zsh 2>/dev/null; then
	echo "mock backend unloaded with an active file/lease" >&2
	exit 1
fi
wait "$holder_pid"
holder_pid=
wait_for_leases_zsh 0
echo "concurrency same-line workers=8 and unload exclusion: PASS"

printf '%s\n' \
	'acquire /dev/gpio0_zsh:13 out 1' \
	'sleep 60000' \
	'release /dev/gpio0_zsh:13' |
	"$cli" --strict --config "$config" run - >/dev/null &
holder_pid=$!
wait_for_leases_zsh 1
kill -KILL "$holder_pid"
wait "$holder_pid" 2>/dev/null || true
holder_pid=
wait_for_leases_zsh 0
"$cli" --config "$config" get /dev/gpio0_zsh:13 >/dev/null
echo "SIGKILL lease cleanup: PASS"

event_log=$(mktemp)
"$cli" --config "$config" watch /dev/gpio0_zsh:5 both 60000 100000 \
	>"$event_log" 2>&1 &
holder_pid=$!
wait_for_leases_zsh 1
(
	i=0
	while [ "$i" -lt 500 ]; do
		printf '5\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset \
			2>/dev/null || true
		i=$((i + 1))
	done
) &
injector_pid=$!
sleep 0.05
kill -TERM "$holder_pid"
wait "$holder_pid" 2>/dev/null || true
holder_pid=
wait "$injector_pid" 2>/dev/null || true
injector_pid=
wait_for_leases_zsh 0
echo "event/close race: PASS"

"$cli" --config "$config" stats /dev/gpio0_zsh |
	grep -q 'active_leases=0'
echo "concurrency_mock_zsh.sh: PASS"
