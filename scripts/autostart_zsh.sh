#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

service=gpioctl-zsh.service

if [ "$(id -u)" -ne 0 ]; then
	echo "gpioctl-zsh-autostart must run as root" >&2
	exit 1
fi

usage_zsh()
{
	echo "usage: gpioctl-zsh-autostart enable|disable|status" >&2
	exit 2
}

show_status_zsh()
{
	printf 'enabled: '
	systemctl is-enabled "$service" 2>/dev/null || true
	printf 'active:  '
	systemctl is-active "$service" 2>/dev/null || true
	systemctl status "$service" --no-pager -l || true
}

case ${1:-} in
	enable)
		systemctl reset-failed "$service" 2>/dev/null || true
		systemctl enable --now "$service"
		show_status_zsh
		;;
	disable)
		# Disable only the next-boot trigger. Keep the current GPIO users alive.
		systemctl disable "$service"
		show_status_zsh
		;;
	status)
		show_status_zsh
		;;
	*)
		usage_zsh
		;;
esac
