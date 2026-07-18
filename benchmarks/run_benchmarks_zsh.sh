#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

if [ "$(id -u)" -ne 0 ]; then
	echo "run_benchmarks_zsh.sh must run as root" >&2
	exit 1
fi

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
iterations=${GPIOCTL_ZSH_BENCH_ITERATIONS:-10000}
warmup=${GPIOCTL_ZSH_BENCH_WARMUP:-1000}
cpuset=${GPIOCTL_ZSH_BENCH_CPUSET:-0-3}
timestamp=$(date +%Y%m%d-%H%M%S)
result_dir=${GPIOCTL_ZSH_BENCH_RESULT_DIR:-$project_dir/build/results/benchmark-$timestamp}
own=$project_dir/build/userspace/gpioctl_benchmark_zsh
baseline=$project_dir/build/userspace/libgpiod_baseline_zsh
raw=$result_dir/raw.csv
time_data=$result_dir/cpu-time.txt
single_own_device=${GPIOCTL_ZSH_BENCH_OWN_DEVICE:-/dev/gpio1_zsh}
single_baseline_device=${GPIOCTL_ZSH_BENCH_GPIOD_DEVICE:-/dev/gpiochip1}
single_offset=${GPIOCTL_ZSH_BENCH_OFFSET:-11}
batch_own_device=${GPIOCTL_ZSH_BENCH_BATCH_OWN_DEVICE:-/dev/gpio4_zsh}
batch_baseline_device=${GPIOCTL_ZSH_BENCH_BATCH_GPIOD_DEVICE:-/dev/gpiochip4}
batch_offsets=${GPIOCTL_ZSH_BENCH_BATCH_OFFSETS:-"6 7 8 9 10 11 12 13"}
temporary=
cli=$project_dir/build/userspace/gpioctl_zsh
config=$project_dir/config/phytium-pi-v1.conf

cleanup_zsh()
{
	trap - EXIT HUP INT TERM
	rm -f "$temporary"
	rm -f "$result_dir"/.case-*.csv "$result_dir"/.concurrent-*.csv
	if [ -x "$cli" ] && [ -e /dev/gpio4_zsh ]; then
		for offset in $batch_offsets; do
			"$cli" --config "$config" get \
				"/dev/gpio4_zsh:$offset" >/dev/null 2>&1 || true
		done
		"$cli" --config "$config" set GPIO1_11 0 >/dev/null 2>&1 || true
		"$cli" --config "$config" set GPIO4_7 0 >/dev/null 2>&1 || true
	fi
}
trap cleanup_zsh EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

case $iterations:$warmup in
	*[!0-9:]*|0:*|*:0)
		echo "iterations and warmup must be positive integers" >&2
		exit 1
		;;
esac
test -x "$own"
test -x "$baseline"
test -x "$cli"
command -v taskset >/dev/null
command -v python3 >/dev/null
command -v /usr/bin/time >/dev/null
mkdir -p "$result_dir"
bad_before=$(dmesg | grep -Ec \
	'BUG:|Oops:|general protection fault|KASAN:|UBSAN:|WARNING:.*gpioctl' || true)
printf 'implementation,metric,line_count,workers,worker,iteration,latency_ns\n' > "$raw"
: > "$time_data"

if ldd "$project_dir/build/userspace/gpioctl_zsh" |
	grep -qi libgpiod; then
	echo "production CLI unexpectedly links libgpiod" >&2
	exit 1
fi

{
	date -Ins
	uname -a
	printf 'source_commit=%s\n' "${GPIOCTL_ZSH_SOURCE_COMMIT:-unknown}"
	printf 'iterations=%s\nwarmup=%s\ncpuset=%s\n' \
		"$iterations" "$warmup" "$cpuset"
	printf 'single_own=%s:%s\nsingle_libgpiod=%s:%s\n' \
		"$single_own_device" "$single_offset" \
		"$single_baseline_device" "$single_offset"
	printf 'batch_offsets=%s\n' "$batch_offsets"
	printf '\n[product ldd]\n'
	ldd "$project_dir/build/userspace/gpioctl_zsh"
	printf '\n[baseline ldd]\n'
	ldd "$baseline"
	printf '\n[libgpiod]\n'
	gpiodetect --version 2>&1 || true
	gpiodetect 2>&1 || true
	printf '\n[gpioctl mapping]\n'
	"$cli" --config "$config" info GPIO1_11
	"$cli" --config "$config" info GPIO4_7
} > "$result_dir/environment.txt" 2>&1

run_case_zsh()
{
	implementation=$1
	workers=$2
	worker=$3
	metric=$4
	device=$5
	count=$6
	shift 6
	if [ "$implementation" = gpioctl_zsh ]; then
		binary=$own
	else
		binary=$baseline
	fi
	temporary=$result_dir/.case-$$-$implementation-$metric-$count-$workers-$worker.csv
	/usr/bin/time -f \
		"implementation=$implementation metric=$metric lines=$count workers=$workers worker=$worker wall_s=%e user_s=%U system_s=%S cpu_percent=%P" \
		-o "$time_data" -a \
		env GPIOCTL_ZSH_BENCH_WORKERS="$workers" \
		GPIOCTL_ZSH_BENCH_WORKER="$worker" \
		taskset -c "$cpuset" "$binary" "$metric" "$device" \
		"$iterations" "$@" > "$temporary"
	cat "$temporary" >> "$raw"
	rm -f "$temporary"
	temporary=
}

warmup_case_zsh()
{
	implementation=$1
	metric=$2
	device=$3
	shift 3
	if [ "$implementation" = gpioctl_zsh ]; then
		binary=$own
	else
		binary=$baseline
	fi
	env GPIOCTL_ZSH_BENCH_WORKERS=1 GPIOCTL_ZSH_BENCH_WORKER=0 \
		taskset -c "$cpuset" "$binary" "$metric" "$device" \
		"$warmup" "$@" >/dev/null
}

for implementation in gpioctl_zsh libgpiod; do
	if [ "$implementation" = gpioctl_zsh ]; then
		device=$single_own_device
	else
		device=$single_baseline_device
	fi
	for metric in set get lease; do
		warmup_case_zsh "$implementation" "$metric" "$device" "$single_offset"
		run_case_zsh "$implementation" 1 0 "$metric" "$device" 1 \
			"$single_offset"
	done
done

for count in 1 2 4 8; do
	# Word splitting is intentional: the configuration is a validated list
	# of numeric offsets, not filenames or arbitrary shell input.
	# shellcheck disable=SC2086
	selected=$(printf '%s\n' $batch_offsets | head -n "$count" | tr '\n' ' ')
	test "$(printf '%s' "$selected" | wc -w)" -eq "$count"
	for implementation in gpioctl_zsh libgpiod; do
		if [ "$implementation" = gpioctl_zsh ]; then
			device=$batch_own_device
		else
			device=$batch_baseline_device
		fi
		# shellcheck disable=SC2086
		warmup_case_zsh "$implementation" batch "$device" $selected
		# shellcheck disable=SC2086
		run_case_zsh "$implementation" 1 0 batch "$device" "$count" $selected
	done
done

for workers in 1 2 4 8; do
	for implementation in gpioctl_zsh libgpiod; do
		if [ "$implementation" = gpioctl_zsh ]; then
			device=$batch_own_device
		else
			device=$batch_baseline_device
		fi
		worker=0
		pids=
		for offset in $batch_offsets; do
			[ "$worker" -lt "$workers" ] || break
			temporary=$result_dir/.concurrent-$implementation-$workers-$worker.csv
			/usr/bin/time -f \
				"implementation=$implementation metric=set lines=1 workers=$workers worker=$worker wall_s=%e user_s=%U system_s=%S cpu_percent=%P" \
				-o "$time_data" -a \
				env GPIOCTL_ZSH_BENCH_WORKERS="$workers" \
				GPIOCTL_ZSH_BENCH_WORKER="$worker" \
				taskset -c "$cpuset" \
				"$([ "$implementation" = gpioctl_zsh ] && printf %s "$own" || printf %s "$baseline")" \
				set "$device" "$iterations" "$offset" > "$temporary" &
			pids="$pids $!:$temporary"
			worker=$((worker + 1))
		done
		for item in $pids; do
			pid=${item%%:*}
			file=${item#*:}
			wait "$pid"
			cat "$file" >> "$raw"
			rm -f "$file"
		done
	done
done

python3 "$project_dir/benchmarks/analyze_benchmark_zsh.py" \
	"$raw" "$result_dir/summary.csv"
for controller in /sys/class/gpioctl_zsh/gpio*_zsh/active_leases; do
	test "$(cat "$controller")" -eq 0
done
bad_after=$(dmesg | grep -Ec \
	'BUG:|Oops:|general protection fault|KASAN:|UBSAN:|WARNING:.*gpioctl' || true)
test "$bad_before" -eq "$bad_after"
printf 'benchmark: PASS results=%s\n' "$result_dir"
