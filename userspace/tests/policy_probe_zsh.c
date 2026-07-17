// SPDX-License-Identifier: GPL-2.0-only
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "gpioctl_zsh.h"

static void fail_zsh(const char *operation)
{
	perror(operation);
	exit(EXIT_FAILURE);
}

static void expect_errno_zsh(int result, int expected, const char *operation)
{
	if (result != -1 || errno != expected) {
		fprintf(stderr, "%s: expected errno=%d, got result=%d errno=%d\n",
			operation, expected, result, errno);
		exit(EXIT_FAILURE);
	}
}

static void expect_policy_zsh(const struct gpioctl_zsh_line_policy *policy,
			      uint32_t expected_flags, const char *operation)
{
	if (policy->flags != expected_flags ||
	    policy->safe_direction != GPIOCTL_ZSH_DIRECTION_INPUT ||
	    policy->safe_value != 0 ||
	    policy->safe_bias != GPIOCTL_ZSH_BIAS_DISABLE) {
		fprintf(stderr,
			"%s: flags=0x%08x safe_direction=%u safe_value=%u safe_bias=%u\n",
			operation, policy->flags, policy->safe_direction,
			policy->safe_value, policy->safe_bias);
		exit(EXIT_FAILURE);
	}
}

static void expect_iopad_zsh(const struct gpioctl_zsh_iopad_config *config,
			      uint32_t bias, uint32_t drive,
			      uint32_t mux, const char *operation)
{
	const uint32_t all_flags = GPIOCTL_ZSH_IOPAD_APPLY_BIAS |
		GPIOCTL_ZSH_IOPAD_APPLY_DRIVE | GPIOCTL_ZSH_IOPAD_APPLY_MUX;

	if (config->flags != all_flags || config->bias != bias ||
	    config->drive_level != drive || config->mux_state != mux) {
		fprintf(stderr,
			"%s: flags=0x%08x bias=%u drive=%u mux=%u\n",
			operation, config->flags, config->bias,
			config->drive_level, config->mux_state);
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	struct gpioctl_zsh_handle *handle;
	struct gpioctl_zsh_iopad_config iopad;
	struct gpioctl_zsh_line_policy policy;
	uint32_t offset;

	if (argc != 2) {
		fprintf(stderr, "usage: %s DEVICE\n", argv[0]);
		return EXIT_FAILURE;
	}
	handle = gpioctl_zsh_open(argv[1]);
	if (!handle)
		fail_zsh("open");
	if (gpioctl_zsh_get_line_policy(handle, 10, &policy))
		fail_zsh("get policy 10");
	expect_policy_zsh(&policy, GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED |
			  GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED, "policy 10");
	if (gpioctl_zsh_get_line_policy(handle, 14, &policy))
		fail_zsh("get policy 14");
	expect_policy_zsh(&policy, 0, "policy 14");
	if (gpioctl_zsh_get_line_policy(handle, 15, &policy))
		fail_zsh("get policy 15");
	expect_policy_zsh(&policy, GPIOCTL_ZSH_POLICY_RESERVED, "policy 15");

	offset = 10;
	if (gpioctl_zsh_iopad_get_config(handle, offset, &iopad))
		fail_zsh("initial IOPAD query");
	expect_iopad_zsh(&iopad, GPIOCTL_ZSH_BIAS_DISABLE, 0,
			  GPIOCTL_ZSH_MUX_GPIO, "initial IOPAD state");
	if (gpioctl_zsh_lease(handle, &offset, 1,
			      GPIOCTL_ZSH_LEASE_INPUT_ONLY))
		fail_zsh("input-only lease");
	if (gpioctl_zsh_iopad_config(handle, offset, GPIOCTL_ZSH_BIAS_PULL_UP,
				     7, GPIOCTL_ZSH_MUX_GPIO,
				     GPIOCTL_ZSH_IOPAD_APPLY_BIAS |
				     GPIOCTL_ZSH_IOPAD_APPLY_DRIVE |
				     GPIOCTL_ZSH_IOPAD_APPLY_MUX))
		fail_zsh("IOPAD configure");
	if (gpioctl_zsh_iopad_get_config(handle, offset, &iopad))
		fail_zsh("configured IOPAD query");
	expect_iopad_zsh(&iopad, GPIOCTL_ZSH_BIAS_PULL_UP, 7,
			  GPIOCTL_ZSH_MUX_GPIO, "configured IOPAD state");
	expect_errno_zsh(gpioctl_zsh_config(handle, offset,
					    GPIOCTL_ZSH_DIRECTION_OUTPUT, 1,
					    GPIOCTL_ZSH_BIAS_AS_IS, 0, 0),
			  EPERM, "input-only output upgrade");
	if (gpioctl_zsh_config(handle, offset, GPIOCTL_ZSH_DIRECTION_INPUT, 0,
			       GPIOCTL_ZSH_BIAS_AS_IS, 0, 0))
		fail_zsh("input-only configure");
	if (gpioctl_zsh_release(handle, &offset, 1))
		fail_zsh("input-only release");
	if (gpioctl_zsh_iopad_get_config(handle, offset, &iopad))
		fail_zsh("released IOPAD query");
	expect_iopad_zsh(&iopad, GPIOCTL_ZSH_BIAS_DISABLE, 7,
			  GPIOCTL_ZSH_MUX_GPIO, "released IOPAD state");

	offset = 14;
	if (gpioctl_zsh_lease(handle, &offset, 1, 0))
		fail_zsh("privileged line 14 lease");
	expect_errno_zsh(gpioctl_zsh_config(handle, offset,
					    GPIOCTL_ZSH_DIRECTION_OUTPUT, 1,
					    GPIOCTL_ZSH_BIAS_AS_IS, 0, 0),
			  EPERM, "unknown output safety");
	if (gpioctl_zsh_release(handle, &offset, 1))
		fail_zsh("line 14 release");

	offset = 15;
	expect_errno_zsh(gpioctl_zsh_lease(handle, &offset, 1, 0), EPERM,
			  "reserved lease");
	gpioctl_zsh_close(handle);
	puts("policy_probe_zsh: PASS");
	return EXIT_SUCCESS;
}
