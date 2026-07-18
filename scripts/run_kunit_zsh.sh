#!/bin/sh
set -eu

project_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
kernel_release=$(uname -r)
config=/boot/config-$kernel_release

if ! test -r "$config" ||
   ! grep -Eq '^CONFIG_KUNIT=(y|m)$' "$config"; then
	echo "gpioctl KUnit: SKIP (CONFIG_KUNIT is disabled for $kernel_release)"
	echo "The same pure production helpers are covered by userspace logic_zsh."
	exit 0
fi

make -C "/lib/modules/$kernel_release/build" \
	M="$project_dir/tests/kunit" modules
echo "gpioctl KUnit module built: tests/kunit/gpioctl_logic_kunit_zsh.ko"
echo "Load it as root and inspect KUnit TAP output for suite gpioctl-logic-zsh."
