#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

cli=${GPIOCTL_ZSH_CLI:-/usr/local/bin/gpioctl_zsh}
config=${GPIOCTL_ZSH_CONFIG:-/etc/gpioctl_zsh/board.conf}
cycles=${GPIOCTL_ZSH_CYCLES:-3}
interval_ms=${GPIOCTL_ZSH_INTERVAL_MS:-1000}

test -x "$cli"
test "$(cat /sys/kernel/config/device-tree/overlays/gpioctl_zsh/status)" = applied

"$cli" --config "$config" resolve LED20
"$cli" --config "$config" resolve GPIO1_11
"$cli" --config "$config" resolve GPIO4_7

# The core selects each pad's GPIO mux for the lease and restores the previous
# mux/bias/drive on release; the smoke test must not leave a manual mux change.
"$cli" --config "$config" blink LED20 "$cycles" "$interval_ms" "$interval_ms"
"$cli" --config "$config" pair-blink GPIO1_11 GPIO4_7 \
	"$cycles" "$interval_ms"
"$cli" --config "$config" stats LED20 | grep -q 'active_leases=0'
"$cli" --config "$config" stats GPIO4_7 | grep -q 'active_leases=0'

echo "phytium_led_smoke_zsh: PASS"
