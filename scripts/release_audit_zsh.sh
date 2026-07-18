#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cli=$project_dir/build/userspace/gpioctl_zsh
library=$project_dir/build/userspace/libgpioctl_zsh.a

fail_zsh()
{
	echo "release audit: FAIL: $*" >&2
	exit 1
}

for file in \
	README.md DEPENDENCY.md plan.md \
	docs/architecture-zsh.md docs/uapi-zsh.md docs/user-guide-zsh.md \
	docs/developer-guide-zsh.md docs/deployment-guide-zsh.md \
	docs/device-tree-policy-zsh.md docs/threat-model-zsh.md \
	docs/traceability-zsh.md docs/test-report-zsh.md \
	docs/performance-zsh.md; do
	test -s "$project_dir/$file" || fail_zsh "missing document $file"
done
test -x "$cli" || fail_zsh "production CLI is not built"
test -f "$library" || fail_zsh "production library is not built"

if ldd "$cli" | grep -qi libgpiod; then
	fail_zsh "production CLI links libgpiod"
fi
if nm -u "$library" | grep -qi gpiod; then
	fail_zsh "production static library references libgpiod symbols"
fi
if grep -R -n -E '#[[:space:]]*include[[:space:]]*[<\"]gpiod\.h' \
	"$project_dir/kernel" "$project_dir/include" \
	"$project_dir/userspace/libgpioctl_zsh" \
	"$project_dir/userspace/gpioctl_zsh"; then
	fail_zsh "production source includes gpiod.h"
fi
if grep -R -n -E \
	'0x(2803[4-9]000|32[bB]30000)|LED20|GPIO1_11|GPIO4_7|E37|BA49|W53' \
	"$project_dir/kernel/core" "$project_dir/include"; then
	fail_zsh "generic core/header contains board-specific address or pin"
fi
if grep -R -n -E \
	'(192\.168\.[0-9]+\.[0-9]+|10\.122\.[0-9]+\.[0-9]+)' \
	"$project_dir/scripts" "$project_dir/tests" \
	"$project_dir/benchmarks"; then
	fail_zsh "automation hardcodes a board IP instead of phytiumPi"
fi
if grep -R -n --exclude=release_audit_zsh.sh -E \
	'(/dev/mem|devmem[[:space:]])' \
	"$project_dir/kernel" "$project_dir/userspace" \
	"$project_dir/scripts" "$project_dir/tests"; then
	fail_zsh "production/test path contains a raw /dev/mem bypass"
fi

if [ -d /sys/class/gpioctl_zsh ]; then
	for controller in /sys/class/gpioctl_zsh/gpio*_zsh; do
		test -d "$controller" || continue
		test "$(cat "$controller/active_leases")" -eq 0 ||
			fail_zsh "active lease remains at $controller"
	done
fi

printf 'release audit: PASS\n'
printf 'production_cli_sha256='
sha256sum "$cli" | awk '{print $1}'
printf 'production_library_sha256='
sha256sum "$library" | awk '{print $1}'
