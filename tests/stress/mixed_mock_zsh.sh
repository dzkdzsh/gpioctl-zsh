#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "mixed_mock_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)
cli=${GPIOCTL_ZSH_CLI:-$project_dir/build/userspace/gpioctl_zsh}
config=${GPIOCTL_ZSH_CONFIG:-$project_dir/config/phytium-pi-v1.conf}
duration=${GPIOCTL_ZSH_STRESS_SECONDS:-3600}
report_interval=${GPIOCTL_ZSH_REPORT_SECONDS:-60}
tmp_dir=
worker_pids=

case $duration:$report_interval in
	*[!0-9:]* | :* | *:) echo "duration/report interval must be integers" >&2; exit 2 ;;
esac
if [ "$duration" -lt 1 ] || [ "$duration" -gt 86400 ] ||
   [ "$report_interval" -lt 1 ] || [ "$report_interval" -gt "$duration" ]; then
	echo "duration must be 1..86400 and report interval 1..duration" >&2
	exit 2
fi

cleanup_zsh()
{
	status=$?
	trap - EXIT HUP INT TERM
	for pid in $worker_pids; do
		kill "$pid" 2>/dev/null || true
		wait "$pid" 2>/dev/null || true
	done
	modprobe -r gpioctl_mock_zsh 2>/dev/null || true
	modprobe -r gpioctl_core_zsh 2>/dev/null || true
	if [ -n "$tmp_dir" ]; then
		case $tmp_dir in
			/tmp/*) rm -rf -- "$tmp_dir" ;;
		esac
	fi
	"$project_dir/scripts/load_zsh.sh" >/dev/null 2>&1 || true
	exit "$status"
}
trap cleanup_zsh EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

kernel_bad_count_zsh()
{
	dmesg | grep -Ec 'BUG:|Oops:|general protection fault|KASAN:|UBSAN:|WARNING:.*gpioctl' || true
}

"$project_dir/scripts/unload_zsh.sh" >/dev/null
insmod "$project_dir/build/kernel/gpioctl_core_zsh.ko"
insmod "$project_dir/build/kernel/gpioctl_mock_zsh.ko"
udevadm trigger --subsystem-match=gpioctl_zsh 2>/dev/null || true
udevadm settle 2>/dev/null || true
tmp_dir=$(mktemp -d /tmp/gpioctl-stress-zsh.XXXXXX)
bad_before=$(kernel_bad_count_zsh)
start=$(date +%s)
deadline=$((start + duration))

(
	i=0
	while [ "$(date +%s)" -lt "$deadline" ]; do
		"$cli" --config "$config" set /dev/gpio0_zsh:0 $((i % 2)) \
			>/dev/null
		i=$((i + 1))
	done
	echo "$i" >"$tmp_dir/set.count"
) >"$tmp_dir/set.log" 2>&1 &
worker_pids="$worker_pids $!"

(
	i=0
	while [ "$(date +%s)" -lt "$deadline" ]; do
		if [ $((i % 2)) -eq 0 ]; then
			values='1=1 2=0'
		else
			values='1=0 2=1'
		fi
		# shellcheck disable=SC2086
		"$cli" --config "$config" batch-set /dev/gpio0_zsh 0 $values \
			>/dev/null
		i=$((i + 1))
	done
	echo "$i" >"$tmp_dir/batch.count"
) >"$tmp_dir/batch.log" 2>&1 &
worker_pids="$worker_pids $!"

(
	i=0
	while [ "$(date +%s)" -lt "$deadline" ]; do
		"$cli" --config "$config" get /dev/gpio0_zsh:3 >/dev/null
		i=$((i + 1))
	done
	echo "$i" >"$tmp_dir/get.count"
) >"$tmp_dir/get.log" 2>&1 &
worker_pids="$worker_pids $!"

(
	i=0
	while [ "$(date +%s)" -lt "$deadline" ]; do
		if "$cli" --config "$config" set /dev/gpio0_zsh:4 \
			$((i % 2)) 5 >/dev/null 2>&1; then
			:
		else
			status=$?
			if [ "$status" -ge 128 ]; then
				exit "$status"
			fi
		fi
		i=$((i + 1))
	done
	echo "$i" >"$tmp_dir/contend-a.count"
) >"$tmp_dir/contend-a.log" 2>&1 &
worker_pids="$worker_pids $!"

(
	i=0
	while [ "$(date +%s)" -lt "$deadline" ]; do
		if "$cli" --config "$config" set /dev/gpio0_zsh:4 \
			$(((i + 1) % 2)) 5 >/dev/null 2>&1; then
			:
		else
			status=$?
			if [ "$status" -ge 128 ]; then
				exit "$status"
			fi
		fi
		i=$((i + 1))
	done
	echo "$i" >"$tmp_dir/contend-b.count"
) >"$tmp_dir/contend-b.log" 2>&1 &
worker_pids="$worker_pids $!"

(
	i=0
	event_log="$tmp_dir/event-iteration.log"
	event_pid=
	# shellcheck disable=SC2317
	event_cleanup_zsh()
	{
		if [ -n "$event_pid" ]; then
			kill "$event_pid" 2>/dev/null || true
			wait "$event_pid" 2>/dev/null || true
		fi
	}
	trap event_cleanup_zsh EXIT
	trap 'exit 129' HUP
	trap 'exit 130' INT
	trap 'exit 143' TERM
	while [ "$(date +%s)" -lt "$deadline" ]; do
		: >"$event_log"
		"$project_dir/build/userspace/event_epoll_zsh" \
			/dev/gpio0_zsh 6 500 1 0 >"$event_log" 2>&1 &
		event_pid=$!
		ready=0
		while [ "$ready" -lt 100 ] && ! grep -q '^READY$' "$event_log"; do
			if ! kill -0 "$event_pid" 2>/dev/null; then
				wait "$event_pid"
				exit 1
			fi
			ready=$((ready + 1))
			sleep 0.01
		done
		if [ "$ready" -ge 100 ]; then
			kill "$event_pid" 2>/dev/null || true
			wait "$event_pid" 2>/dev/null || true
			exit 1
		fi
		j=0
		while [ "$j" -lt 20 ]; do
			printf '6\n' > /sys/module/gpioctl_mock_zsh/parameters/inject_offset
			j=$((j + 1))
			sleep 0.02
		done
		wait "$event_pid"
		event_pid=
		i=$((i + 1))
	done
	echo "$i" >"$tmp_dir/event.count"
) >"$tmp_dir/event.log" 2>&1 &
worker_pids="$worker_pids $!"

next_report=$((start + report_interval))
while [ "$(date +%s)" -lt "$deadline" ]; do
	now=$(date +%s)
	if [ "$now" -ge "$next_report" ]; then
		active=$(cat /sys/class/gpioctl_zsh/gpio0_zsh/active_leases)
		operations=$(cat /sys/class/gpioctl_zsh/gpio0_zsh/operations)
		events=$(cat /sys/class/gpioctl_zsh/gpio0_zsh/events)
		drops=$(cat /sys/class/gpioctl_zsh/gpio0_zsh/event_drops)
		echo "stress elapsed=$((now - start))s active=$active operations=$operations events=$events drops=$drops"
		next_report=$((next_report + report_interval))
	fi
	sleep 1
done

status=0
for pid in $worker_pids; do
	wait "$pid" || status=1
done
worker_pids=
if [ "$status" -ne 0 ]; then
	echo "mixed worker failed" >&2
	for log in "$tmp_dir"/*.log; do
		echo "--- $log" >&2
		tail -n 20 "$log" >&2 || true
	done
	exit 1
fi

test "$(cat /sys/class/gpioctl_zsh/gpio0_zsh/active_leases)" -eq 0
bad_after=$(kernel_bad_count_zsh)
if [ "$bad_before" -ne "$bad_after" ]; then
	echo "new kernel BUG/Oops/WARNING detected: $bad_before -> $bad_after" >&2
	exit 1
fi

printf 'mixed_mock_zsh.sh: PASS duration=%ss set=%s batch=%s get=%s contend=%s+%s event_rounds=%s\n' \
	"$duration" \
	"$(cat "$tmp_dir/set.count")" \
	"$(cat "$tmp_dir/batch.count")" \
	"$(cat "$tmp_dir/get.count")" \
	"$(cat "$tmp_dir/contend-a.count")" \
	"$(cat "$tmp_dir/contend-b.count")" \
	"$(cat "$tmp_dir/event.count")"
