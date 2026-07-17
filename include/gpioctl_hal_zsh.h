/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GPIOCTL_HAL_ZSH_H
#define GPIOCTL_HAL_ZSH_H

#include <linux/types.h>

#include "uapi/gpioctl_zsh.h"

#define GPIOCTL_ZSH_HAL_ABI_VERSION 1U
#define GPIOCTL_ZSH_BACKEND_NAME_MAX 31U

struct gpioctl_controller_zsh;
struct module;

struct gpioctl_hal_ops_zsh {
	u32 abi_version;
	u32 struct_size;
	int (*request)(void *priv, unsigned int offset, void **line_priv);
	void (*release)(void *priv, void *line_priv);
	int (*direction_input)(void *priv, void *line_priv);
	int (*direction_output)(void *priv, void *line_priv, int value);
	int (*get_value)(void *priv, void *line_priv);
	int (*set_value)(void *priv, void *line_priv, int value);
	int (*set_bias)(void *priv, void *line_priv, enum gpioctl_zsh_bias bias);
	int (*set_debounce)(void *priv, void *line_priv, u32 debounce_us);
	int (*to_irq)(void *priv, void *line_priv);
	int (*get_iopad_caps)(void *priv, unsigned int offset,
			      struct gpioctl_zsh_line_caps *caps);
	int (*set_iopad)(void *priv, void *line_priv,
			 const struct gpioctl_zsh_iopad_config *config);
};

struct gpioctl_backend_desc_zsh {
	u32 abi_version;
	u32 struct_size;
	const char *name;
	unsigned int line_count;
	u64 capabilities;
	const struct gpioctl_hal_ops_zsh *ops;
	void *priv;
	struct module *owner;
};

int gpioctl_register_backend_zsh(const struct gpioctl_backend_desc_zsh *desc,
				 struct gpioctl_controller_zsh **controller);
int gpioctl_unregister_backend_zsh(struct gpioctl_controller_zsh *controller);

#endif /* GPIOCTL_HAL_ZSH_H */
