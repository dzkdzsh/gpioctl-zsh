// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include "gpioctl_logic_zsh.h"

static void gpioctl_header_test_zsh(struct kunit *test)
{
	gpioctl_logic_u32_zsh reserved[3] = { 0, 0, 0 };

	KUNIT_EXPECT_TRUE(test,
		gpioctl_reserved_zero_logic_zsh(reserved, ARRAY_SIZE(reserved)));
	reserved[2] = 1;
	KUNIT_EXPECT_FALSE(test,
		gpioctl_reserved_zero_logic_zsh(reserved, ARRAY_SIZE(reserved)));
	KUNIT_EXPECT_EQ(test, 0, gpioctl_validate_header_logic_zsh(
		GPIOCTL_ZSH_ABI_VERSION, 48, 48));
	KUNIT_EXPECT_EQ(test, -EPROTO, gpioctl_validate_header_logic_zsh(
		GPIOCTL_ZSH_ABI_VERSION + 1U, 48, 48));
	KUNIT_EXPECT_EQ(test, -EINVAL, gpioctl_validate_header_logic_zsh(
		GPIOCTL_ZSH_ABI_VERSION, 47, 48));
}

static void gpioctl_policy_test_zsh(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 0, gpioctl_validate_policy_logic_zsh(
		GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED,
		GPIOCTL_ZSH_DIRECTION_OUTPUT, 0,
		GPIOCTL_ZSH_BIAS_DISABLE));
	KUNIT_EXPECT_EQ(test, -EINVAL, gpioctl_validate_policy_logic_zsh(
		0, GPIOCTL_ZSH_DIRECTION_OUTPUT, 0,
		GPIOCTL_ZSH_BIAS_DISABLE));
	KUNIT_EXPECT_EQ(test, -EINVAL, gpioctl_validate_policy_logic_zsh(
		GPIOCTL_ZSH_POLICY_RESERVED |
		GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED,
		GPIOCTL_ZSH_DIRECTION_INPUT, 0,
		GPIOCTL_ZSH_BIAS_DISABLE));
}

static void gpioctl_active_low_test_zsh(struct kunit *test)
{
	KUNIT_EXPECT_EQ(test, 1,
		gpioctl_active_low_value_logic_zsh(1, false));
	KUNIT_EXPECT_EQ(test, 0,
		gpioctl_active_low_value_logic_zsh(1, true));
	KUNIT_EXPECT_EQ(test, 1,
		gpioctl_active_low_value_logic_zsh(0, true));
}

static void gpioctl_ring_test_zsh(struct kunit *test)
{
	gpioctl_logic_u32_zsh head = 0, tail = 0, count = 0, slot;
	bool dropped;

	slot = gpioctl_ring_push_logic_zsh(&head, &tail, &count, 2, &dropped);
	KUNIT_EXPECT_EQ(test, (gpioctl_logic_u32_zsh)0, slot);
	KUNIT_EXPECT_FALSE(test, dropped);
	slot = gpioctl_ring_push_logic_zsh(&head, &tail, &count, 2, &dropped);
	KUNIT_EXPECT_EQ(test, (gpioctl_logic_u32_zsh)1, slot);
	KUNIT_EXPECT_FALSE(test, dropped);
	slot = gpioctl_ring_push_logic_zsh(&head, &tail, &count, 2, &dropped);
	KUNIT_EXPECT_EQ(test, (gpioctl_logic_u32_zsh)0, slot);
	KUNIT_EXPECT_TRUE(test, dropped);
	KUNIT_EXPECT_EQ(test, (gpioctl_logic_u32_zsh)1, head);
	KUNIT_EXPECT_EQ(test, (gpioctl_logic_u32_zsh)1, tail);
	KUNIT_EXPECT_EQ(test, (gpioctl_logic_u32_zsh)2, count);
}

static void gpioctl_debounce_test_zsh(struct kunit *test)
{
	KUNIT_EXPECT_FALSE(test,
		gpioctl_debounce_accept_logic_zsh(500, 1000000, 1499999));
	KUNIT_EXPECT_TRUE(test,
		gpioctl_debounce_accept_logic_zsh(500, 1000000, 1500000));
	KUNIT_EXPECT_FALSE(test,
		gpioctl_debounce_accept_logic_zsh(500, 1000000, 999999));
}

static struct kunit_case gpioctl_logic_cases_zsh[] = {
	KUNIT_CASE(gpioctl_header_test_zsh),
	KUNIT_CASE(gpioctl_policy_test_zsh),
	KUNIT_CASE(gpioctl_active_low_test_zsh),
	KUNIT_CASE(gpioctl_ring_test_zsh),
	KUNIT_CASE(gpioctl_debounce_test_zsh),
	{}
};

static struct kunit_suite gpioctl_logic_suite_zsh = {
	.name = "gpioctl-logic-zsh",
	.test_cases = gpioctl_logic_cases_zsh,
};

kunit_test_suite(gpioctl_logic_suite_zsh);

MODULE_LICENSE("GPL");
