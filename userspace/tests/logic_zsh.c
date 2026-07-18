// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>

#include "gpioctl_logic_zsh.h"

static void require_zsh(bool condition, const char *what)
{
	if (!condition) {
		fprintf(stderr, "logic_zsh: FAIL: %s\n", what);
		exit(EXIT_FAILURE);
	}
}

int main(void)
{
	gpioctl_logic_u32_zsh reserved[3] = { 0, 0, 0 };
	gpioctl_logic_u32_zsh head = 0, tail = 0, count = 0, slot;
	bool dropped;

	require_zsh(gpioctl_reserved_zero_logic_zsh(reserved, 3),
		"zero reserved fields");
	reserved[1] = 1;
	require_zsh(!gpioctl_reserved_zero_logic_zsh(reserved, 3),
		"nonzero reserved field");
	require_zsh(gpioctl_validate_header_logic_zsh(
		GPIOCTL_ZSH_ABI_VERSION, 48, 48) == 0, "valid ABI header");
	require_zsh(gpioctl_validate_header_logic_zsh(
		GPIOCTL_ZSH_ABI_VERSION + 1U, 48, 48) == -EPROTO,
		"bad ABI version");
	require_zsh(gpioctl_validate_header_logic_zsh(
		GPIOCTL_ZSH_ABI_VERSION, 47, 48) == -EINVAL,
		"bad ABI size");

	require_zsh(gpioctl_validate_policy_logic_zsh(
		GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED,
		GPIOCTL_ZSH_DIRECTION_OUTPUT, 0,
		GPIOCTL_ZSH_BIAS_DISABLE) == 0, "valid output policy");
	require_zsh(gpioctl_validate_policy_logic_zsh(
		0, GPIOCTL_ZSH_DIRECTION_OUTPUT, 0,
		GPIOCTL_ZSH_BIAS_DISABLE) == -EINVAL,
		"output policy without permission");
	require_zsh(gpioctl_validate_policy_logic_zsh(
		GPIOCTL_ZSH_POLICY_RESERVED |
		GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED,
		GPIOCTL_ZSH_DIRECTION_INPUT, 0,
		GPIOCTL_ZSH_BIAS_DISABLE) == -EINVAL,
		"mixed reserved policy");

	require_zsh(gpioctl_active_low_value_logic_zsh(1, false) == 1,
		"active-high conversion");
	require_zsh(gpioctl_active_low_value_logic_zsh(1, true) == 0,
		"active-low conversion");

	slot = gpioctl_ring_push_logic_zsh(&head, &tail, &count, 2, &dropped);
	require_zsh(slot == 0 && head == 1 && tail == 0 && count == 1 &&
		!dropped, "ring first push");
	slot = gpioctl_ring_push_logic_zsh(&head, &tail, &count, 2, &dropped);
	require_zsh(slot == 1 && head == 0 && tail == 0 && count == 2 &&
		!dropped, "ring wrap push");
	slot = gpioctl_ring_push_logic_zsh(&head, &tail, &count, 2, &dropped);
	require_zsh(slot == 0 && head == 1 && tail == 1 && count == 2 &&
		dropped, "ring overflow drops oldest");

	require_zsh(gpioctl_debounce_accept_logic_zsh(500, 1000000, 1499999) ==
		false, "debounce filters early event");
	require_zsh(gpioctl_debounce_accept_logic_zsh(500, 1000000, 1500000),
		"debounce accepts boundary event");
	require_zsh(!gpioctl_debounce_accept_logic_zsh(500, 1000000, 999999),
		"debounce rejects time reversal");

	puts("logic_zsh: PASS");
	return EXIT_SUCCESS;
}
