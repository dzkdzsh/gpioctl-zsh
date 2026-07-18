#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "run_event_benchmark_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
iterations=${GPIOCTL_ZSH_EVENT_BENCH_ITERATIONS:-10000}
cpuset=${GPIOCTL_ZSH_BENCH_CPUSET:-0-3}
timestamp=$(date +%Y%m%d-%H%M%S)
result_dir=${GPIOCTL_ZSH_EVENT_RESULT_DIR:-$project_dir/build/results/event-$timestamp}
binary=$project_dir/build/userspace/event_benchmark_zsh
raw=$result_dir/raw.csv

cleanup_zsh()
{
	status=$?
	trap - EXIT HUP INT TERM
	modprobe -r gpioctl_mock_zsh 2>/dev/null || true
	modprobe -r gpioctl_core_zsh 2>/dev/null || true
	"$project_dir/scripts/load_zsh.sh" >/dev/null 2>&1 || true
	if [ -x "$project_dir/build/userspace/gpioctl_zsh" ]; then
		"$project_dir/build/userspace/gpioctl_zsh" --config \
			"$project_dir/config/phytium-pi-v1.conf" \
			set GPIO1_11 0 >/dev/null 2>&1 || true
		"$project_dir/build/userspace/gpioctl_zsh" --config \
			"$project_dir/config/phytium-pi-v1.conf" \
			set GPIO4_7 0 >/dev/null 2>&1 || true
	fi
	exit "$status"
}
trap cleanup_zsh EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

test -x "$binary"
mkdir -p "$result_dir"
"$project_dir/scripts/unload_zsh.sh" >/dev/null
insmod "$project_dir/build/kernel/gpioctl_core_zsh.ko"
insmod "$project_dir/build/kernel/gpioctl_mock_zsh.ko"
udevadm trigger --subsystem-match=gpioctl_zsh 2>/dev/null || true
udevadm settle 2>/dev/null || true
bad_before=$(dmesg | grep -Ec \
	'BUG:|Oops:|general protection fault|KASAN:|UBSAN:|WARNING:.*gpioctl' || true)
printf 'implementation,metric,line_count,workers,worker,iteration,latency_ns\n' > "$raw"
{
	date -Ins
	uname -a
	printf 'source_commit=%s\niterations=%s\ncpuset=%s\n' \
		"${GPIOCTL_ZSH_SOURCE_COMMIT:-unknown}" "$iterations" "$cpuset"
	echo 'generator=mock sysfs injection -> generic IRQ -> threaded handler'
} > "$result_dir/environment.txt"
/usr/bin/time -f 'wall_s=%e user_s=%U system_s=%S cpu_percent=%P' \
	-o "$result_dir/cpu-time.txt" \
	taskset -c "$cpuset" "$binary" /dev/gpio0_zsh 6 "$iterations" \
	/sys/module/gpioctl_mock_zsh/parameters/inject_offset >> "$raw"
python3 "$project_dir/benchmarks/analyze_benchmark_zsh.py" \
	"$raw" "$result_dir/summary.csv"
test "$(cat /sys/class/gpioctl_zsh/gpio0_zsh/active_leases)" -eq 0
bad_after=$(dmesg | grep -Ec \
	'BUG:|Oops:|general protection fault|KASAN:|UBSAN:|WARNING:.*gpioctl' || true)
test "$bad_before" -eq "$bad_after"
printf 'event benchmark: PASS results=%s\n' "$result_dir"
