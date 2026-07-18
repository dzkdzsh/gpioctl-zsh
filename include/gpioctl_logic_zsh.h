/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GPIOCTL_LOGIC_ZSH_H
#define GPIOCTL_LOGIC_ZSH_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
typedef u32 gpioctl_logic_u32_zsh;
typedef u64 gpioctl_logic_u64_zsh;
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint32_t gpioctl_logic_u32_zsh;
typedef uint64_t gpioctl_logic_u64_zsh;
#endif

#include "uapi/gpioctl_zsh.h"

static inline bool gpioctl_reserved_zero_logic_zsh(
	const gpioctl_logic_u32_zsh *reserved, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		if (reserved[i])
			return false;
	return true;
}

static inline int gpioctl_validate_header_logic_zsh(
	gpioctl_logic_u32_zsh version, gpioctl_logic_u32_zsh size,
	size_t expected)
{
	if (version != GPIOCTL_ZSH_ABI_VERSION)
		return -EPROTO;
	if (size != expected)
		return -EINVAL;
	return 0;
}

static inline int gpioctl_validate_policy_logic_zsh(
	gpioctl_logic_u32_zsh flags, gpioctl_logic_u32_zsh safe_direction,
	gpioctl_logic_u32_zsh safe_value, gpioctl_logic_u32_zsh safe_bias)
{
	const gpioctl_logic_u32_zsh known_flags =
		GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED |
		GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED |
		GPIOCTL_ZSH_POLICY_RESERVED;

	if (flags & ~known_flags ||
	    safe_direction > GPIOCTL_ZSH_DIRECTION_OUTPUT ||
	    safe_value > 1 ||
	    safe_bias < GPIOCTL_ZSH_BIAS_DISABLE ||
	    safe_bias > GPIOCTL_ZSH_BIAS_PULL_DOWN)
		return -EINVAL;
	if ((flags & GPIOCTL_ZSH_POLICY_RESERVED) &&
	    (flags != GPIOCTL_ZSH_POLICY_RESERVED ||
	     safe_direction != GPIOCTL_ZSH_DIRECTION_INPUT || safe_value))
		return -EINVAL;
	if (safe_direction == GPIOCTL_ZSH_DIRECTION_OUTPUT &&
	    !(flags & GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED))
		return -EINVAL;
	return 0;
}

static inline int gpioctl_active_low_value_logic_zsh(int value,
						      bool active_low)
{
	return (!!value) ^ active_low;
}

static inline gpioctl_logic_u32_zsh gpioctl_ring_next_logic_zsh(
	gpioctl_logic_u32_zsh index, gpioctl_logic_u32_zsh capacity)
{
	return index + 1U == capacity ? 0U : index + 1U;
}

/* Reserve one producer slot, dropping the oldest entry when already full. */
static inline gpioctl_logic_u32_zsh gpioctl_ring_push_logic_zsh(
	gpioctl_logic_u32_zsh *head, gpioctl_logic_u32_zsh *tail,
	gpioctl_logic_u32_zsh *count, gpioctl_logic_u32_zsh capacity,
	bool *dropped)
{
	gpioctl_logic_u32_zsh slot = *head;

	*dropped = *count == capacity;
	if (*dropped) {
		*tail = gpioctl_ring_next_logic_zsh(*tail, capacity);
		(*count)--;
	}
	*head = gpioctl_ring_next_logic_zsh(*head, capacity);
	(*count)++;
	return slot;
}

static inline bool gpioctl_debounce_accept_logic_zsh(
	gpioctl_logic_u32_zsh debounce_us, gpioctl_logic_u64_zsh last_ns,
	gpioctl_logic_u64_zsh now_ns)
{
	gpioctl_logic_u64_zsh interval_ns;

	if (!debounce_us || !last_ns)
		return true;
	if (now_ns < last_ns)
		return false;
	interval_ns = (gpioctl_logic_u64_zsh)debounce_us * 1000U;
	return now_ns - last_ns >= interval_ns;
}

#endif
