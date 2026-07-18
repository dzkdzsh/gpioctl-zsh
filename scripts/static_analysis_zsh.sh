#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -u

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
kdir=${KDIR:-/lib/modules/$(uname -r)/build}
timestamp=$(date +%Y%m%d-%H%M%S)
result_dir=${GPIOCTL_ZSH_STATIC_RESULT_DIR:-$project_dir/build/results/static-$timestamp}
summary=$result_dir/summary.tsv
failed=0

mkdir -p "$result_dir"
printf 'check\tstatus\texit_code\n' > "$summary"

record_skip_zsh()
{
	name=$1
	reason=$2
	printf '%s\tSKIP\t-\n' "$name" >> "$summary"
	printf '%s: SKIP: %s\n' "$name" "$reason" |
		tee "$result_dir/$name.txt"
}

run_zsh()
{
	name=$1
	shift
	if "$@" > "$result_dir/$name.txt" 2>&1; then
		status=PASS
		code=0
	else
		code=$?
		status=FAIL
		failed=1
	fi
	printf '%s\t%s\t%s\n' "$name" "$status" "$code" >> "$summary"
	printf '%s: %s (exit=%s)\n' "$name" "$status" "$code"
	cat "$result_dir/$name.txt"
}

run_checkpatch_zsh()
{
	name=$1
	shift
	output=$result_dir/$name.txt
	if "$@" > "$output" 2>&1; then
		raw_code=0
	else
		raw_code=$?
	fi
	if grep -Eq '^(ERROR|WARNING):' "$output"; then
		status=FAIL
		code=$raw_code
		failed=1
	else
		status=PASS
		code=0
	fi
	printf '%s\t%s\t%s\n' "$name" "$status" "$code" >> "$summary"
	printf '%s: %s (strict raw exit=%s; CHECK findings are advisory)\n' \
		"$name" "$status" "$raw_code"
	cat "$output"
}

run_smatch_zsh()
{
	name=$1
	shift
	output=$result_dir/$name.txt
	if "$@" > "$output" 2>&1; then
		raw_code=0
	else
		raw_code=$?
	fi
	if [ "$raw_code" -ne 0 ] || grep -Eq ' (warn|error):' "$output"; then
		status=FAIL
		code=$raw_code
		[ "$code" -ne 0 ] || code=1
		failed=1
	else
		status=PASS
		code=0
	fi
	printf '%s\t%s\t%s\n' "$name" "$status" "$code" >> "$summary"
	printf '%s: %s (raw exit=%s; diagnostics are release blockers)\n' \
		"$name" "$status" "$raw_code"
	cat "$output"
}

{
	date -Ins
	uname -a
	printf 'project=%s\n' "$project_dir"
	printf 'kdir=%s\n' "$kdir"
	for tool in gcc make sparse spatch smatch shellcheck cppcheck; do
		if command -v "$tool" >/dev/null 2>&1; then
			printf '\n[%s]\n' "$tool"
			"$tool" --version 2>&1 | head -n 4 || true
		else
			printf '\n[%s]\nMISSING\n' "$tool"
		fi
	done
} > "$result_dir/environment.txt" 2>&1

if [ ! -d "$kdir" ]; then
	record_skip_zsh kernel_w1 "kernel build directory not found: $kdir"
	record_skip_zsh raw_lab_w1 "kernel build directory not found: $kdir"
	record_skip_zsh sparse "kernel build directory not found: $kdir"
	record_skip_zsh raw_lab_sparse "kernel build directory not found: $kdir"
	record_skip_zsh coccinelle "kernel build directory not found: $kdir"
	record_skip_zsh raw_lab_coccinelle "kernel build directory not found: $kdir"
	record_skip_zsh smatch "kernel build directory not found: $kdir"
	record_skip_zsh raw_lab_smatch "kernel build directory not found: $kdir"
	record_skip_zsh checkpatch "kernel build directory not found: $kdir"
else
	run_zsh kernel_w1 make -C "$kdir" M="$project_dir/kernel" W=1 modules
	run_zsh raw_lab_w1 make -C "$kdir" \
		M="$project_dir/lab/raw-mmio-lab" W=1 modules
	if command -v sparse >/dev/null 2>&1; then
		run_zsh sparse make -C "$kdir" M="$project_dir/kernel" \
			C=2 CHECK=sparse CF=-D__CHECK_ENDIAN__ modules
		run_zsh raw_lab_sparse make -C "$kdir" \
			M="$project_dir/lab/raw-mmio-lab" C=2 CHECK=sparse \
			CF=-D__CHECK_ENDIAN__ modules
	else
		record_skip_zsh sparse "sparse is not installed"
		record_skip_zsh raw_lab_sparse "sparse is not installed"
	fi
	if command -v spatch >/dev/null 2>&1; then
		mkdir -p "$result_dir/coccinelle-kernel-tmp" \
			"$result_dir/coccinelle-raw-tmp"
		run_zsh coccinelle env \
			SPFLAGS="--tmp-dir $result_dir/coccinelle-kernel-tmp" \
			make -C "$kdir" M="$project_dir/kernel" \
			coccicheck MODE=report
		run_zsh raw_lab_coccinelle env \
			SPFLAGS="--tmp-dir $result_dir/coccinelle-raw-tmp" \
			make -C "$kdir" \
			M="$project_dir/lab/raw-mmio-lab" coccicheck MODE=report
	else
		record_skip_zsh coccinelle "spatch is not installed"
		record_skip_zsh raw_lab_coccinelle "spatch is not installed"
	fi
	if command -v smatch >/dev/null 2>&1; then
		run_smatch_zsh smatch make -C "$kdir" M="$project_dir/kernel" \
			C=2 CHECK="smatch -p=kernel" modules
		run_smatch_zsh raw_lab_smatch make -C "$kdir" \
			M="$project_dir/lab/raw-mmio-lab" C=2 \
			CHECK="smatch -p=kernel" modules
	else
		record_skip_zsh smatch "smatch is not installed"
		record_skip_zsh raw_lab_smatch "smatch is not installed"
	fi
	if [ -x "$kdir/scripts/checkpatch.pl" ]; then
		# Keep strict diagnostics while excluding three kernel-only style
		# rules that conflict with the ISO C userspace sources in this scan.
		# Generated Kbuild *.mod.c files are never source-review inputs.
		# shellcheck disable=SC2046
		run_checkpatch_zsh checkpatch "$kdir/scripts/checkpatch.pl" \
			--no-tree --strict \
			--ignore PREFER_KERNEL_TYPES,SPLIT_STRING,ELSE_AFTER_BRACE \
			--show-types -f $(find "$project_dir/kernel" \
			"$project_dir/include" "$project_dir/userspace" \
			"$project_dir/tests/kunit" "$project_dir/benchmarks" \
			"$project_dir/lab/raw-mmio-lab" -type f \
			\( -name '*.c' -o -name '*.h' \) \
			! -name '*.mod.c' -print)
	else
		record_skip_zsh checkpatch "kernel checkpatch.pl is unavailable"
	fi
fi

if command -v gcc >/dev/null 2>&1; then
	run_zsh gcc_analyzer make -C "$project_dir/userspace" \
		BUILD_DIR="$result_dir/analyzer-build" \
		CFLAGS="-O2 -g -fanalyzer" clean all check benchmark \
		benchmark-libgpiod
else
	record_skip_zsh gcc_analyzer "gcc is not installed"
fi

if command -v cppcheck >/dev/null 2>&1; then
	run_zsh cppcheck cppcheck --enable=warning,style,performance,portability \
		--error-exitcode=1 --inline-suppr --std=c11 \
		-I "$project_dir/include" "$project_dir/userspace" \
		"$project_dir/benchmarks"
else
	record_skip_zsh cppcheck "cppcheck is not installed"
fi

if command -v shellcheck >/dev/null 2>&1; then
	# shellcheck disable=SC2046
	run_zsh shellcheck shellcheck -x $(find "$project_dir/scripts" \
		"$project_dir/tests" "$project_dir/benchmarks" \
		"$project_dir/lab" -type f -name '*.sh' -print)
else
	record_skip_zsh shellcheck "shellcheck is not installed"
fi

printf '\nStatic analysis summary: %s\n' "$summary"
cat "$summary"
exit "$failed"
