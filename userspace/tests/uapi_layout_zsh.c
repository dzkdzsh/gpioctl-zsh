// SPDX-License-Identifier: GPL-2.0-only
#include <assert.h>
#include <stdio.h>

#include "uapi/gpioctl_zsh.h"

_Static_assert(sizeof(struct gpioctl_zsh_event) == 48, "event ABI drift");
_Static_assert(sizeof(struct gpioctl_zsh_batch_op) == 32, "batch op ABI drift");
_Static_assert(sizeof(struct gpioctl_zsh_abi_info) == 32, "ABI info drift");
_Static_assert(sizeof(struct gpioctl_zsh_iopad_config) == 48,
	       "IOPAD config ABI drift");
_Static_assert(sizeof(struct gpioctl_zsh_line_policy) == 48,
	       "line policy ABI drift");

int main(void)
{
	assert(_IOC_TYPE(GPIOCTL_ZSH_IOC_GET_ABI) == GPIOCTL_ZSH_IOC_MAGIC);
	assert(_IOC_SIZE(GPIOCTL_ZSH_IOC_GET_ABI) ==
	       sizeof(struct gpioctl_zsh_abi_info));
	assert(_IOC_SIZE(GPIOCTL_ZSH_IOC_BATCH_EXEC) ==
	       sizeof(struct gpioctl_zsh_batch));
	assert(_IOC_SIZE(GPIOCTL_ZSH_IOC_GET_LINE_POLICY) ==
	       sizeof(struct gpioctl_zsh_line_policy));
	assert(_IOC_SIZE(GPIOCTL_ZSH_IOC_IOPAD_GET_CONFIG) ==
	       sizeof(struct gpioctl_zsh_iopad_config));
	puts("uapi_layout_zsh: PASS");
	return 0;
}
