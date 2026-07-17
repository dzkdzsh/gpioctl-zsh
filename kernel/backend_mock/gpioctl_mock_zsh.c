// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include "gpioctl_hal_zsh.h"

#define GPIOCTL_MOCK_LINES_ZSH 16

struct gpioctl_mock_line_zsh {
	unsigned int offset;
	bool requested;
	bool output;
	int value;
	u32 bias;
};

struct gpioctl_mock_zsh {
	struct mutex lock;
	struct gpioctl_mock_line_zsh lines[GPIOCTL_MOCK_LINES_ZSH];
	struct gpioctl_controller_zsh *controller;
};

static struct gpioctl_mock_zsh gpioctl_mock_zsh;
static int fail_offset_zsh = -1;
module_param_named(fail_offset, fail_offset_zsh, int, 0644);
MODULE_PARM_DESC(fail_offset, "Mock line offset that returns -EIO, or -1");

static int gpioctl_mock_maybe_fail_zsh(struct gpioctl_mock_line_zsh *line)
{
	return line->offset == fail_offset_zsh ? -EIO : 0;
}

static int gpioctl_mock_request_zsh(void *priv, unsigned int offset,
				    void **line_priv)
{
	struct gpioctl_mock_zsh *mock = priv;
	struct gpioctl_mock_line_zsh *line;
	int ret = 0;

	if (offset >= GPIOCTL_MOCK_LINES_ZSH)
		return -EINVAL;
	line = &mock->lines[offset];
	mutex_lock(&mock->lock);
	if (line->requested)
		ret = -EBUSY;
	else if (gpioctl_mock_maybe_fail_zsh(line))
		ret = -EIO;
	else {
		line->requested = true;
		*line_priv = line;
	}
	mutex_unlock(&mock->lock);
	return ret;
}

static void gpioctl_mock_release_zsh(void *priv, void *line_priv)
{
	struct gpioctl_mock_zsh *mock = priv;
	struct gpioctl_mock_line_zsh *line = line_priv;

	mutex_lock(&mock->lock);
	line->requested = false;
	line->output = false;
	line->value = 0;
	line->bias = GPIOCTL_ZSH_BIAS_DISABLE;
	mutex_unlock(&mock->lock);
}

static int gpioctl_mock_direction_input_zsh(void *priv, void *line_priv)
{
	struct gpioctl_mock_zsh *mock = priv;
	struct gpioctl_mock_line_zsh *line = line_priv;
	int ret;

	mutex_lock(&mock->lock);
	ret = gpioctl_mock_maybe_fail_zsh(line);
	if (!ret)
		line->output = false;
	mutex_unlock(&mock->lock);
	return ret;
}

static int gpioctl_mock_direction_output_zsh(void *priv, void *line_priv,
					     int value)
{
	struct gpioctl_mock_zsh *mock = priv;
	struct gpioctl_mock_line_zsh *line = line_priv;
	int ret;

	mutex_lock(&mock->lock);
	ret = gpioctl_mock_maybe_fail_zsh(line);
	if (!ret) {
		line->value = !!value;
		line->output = true;
	}
	mutex_unlock(&mock->lock);
	return ret;
}

static int gpioctl_mock_get_value_zsh(void *priv, void *line_priv)
{
	struct gpioctl_mock_zsh *mock = priv;
	struct gpioctl_mock_line_zsh *line = line_priv;
	int ret;

	mutex_lock(&mock->lock);
	ret = gpioctl_mock_maybe_fail_zsh(line);
	if (!ret)
		ret = line->value;
	mutex_unlock(&mock->lock);
	return ret;
}

static int gpioctl_mock_set_value_zsh(void *priv, void *line_priv, int value)
{
	struct gpioctl_mock_zsh *mock = priv;
	struct gpioctl_mock_line_zsh *line = line_priv;
	int ret;

	mutex_lock(&mock->lock);
	ret = gpioctl_mock_maybe_fail_zsh(line);
	if (!ret && !line->output)
		ret = -EPERM;
	if (!ret)
		line->value = !!value;
	mutex_unlock(&mock->lock);
	return ret;
}

static int gpioctl_mock_set_bias_zsh(void *priv, void *line_priv,
				     enum gpioctl_zsh_bias bias)
{
	struct gpioctl_mock_zsh *mock = priv;
	struct gpioctl_mock_line_zsh *line = line_priv;
	int ret;

	mutex_lock(&mock->lock);
	ret = gpioctl_mock_maybe_fail_zsh(line);
	if (!ret)
		line->bias = bias;
	mutex_unlock(&mock->lock);
	return ret;
}

static int gpioctl_mock_set_debounce_zsh(void *priv, void *line_priv,
					 u32 debounce_us)
{
	return gpioctl_mock_maybe_fail_zsh(line_priv);
}

static const struct gpioctl_hal_ops_zsh gpioctl_mock_ops_zsh = {
	.abi_version = GPIOCTL_ZSH_HAL_ABI_VERSION,
	.struct_size = sizeof(gpioctl_mock_ops_zsh),
	.request = gpioctl_mock_request_zsh,
	.release = gpioctl_mock_release_zsh,
	.direction_input = gpioctl_mock_direction_input_zsh,
	.direction_output = gpioctl_mock_direction_output_zsh,
	.get_value = gpioctl_mock_get_value_zsh,
	.set_value = gpioctl_mock_set_value_zsh,
	.set_bias = gpioctl_mock_set_bias_zsh,
	.set_debounce = gpioctl_mock_set_debounce_zsh,
};

static int __init gpioctl_mock_init_zsh(void)
{
	struct gpioctl_backend_desc_zsh desc = {
		.abi_version = GPIOCTL_ZSH_HAL_ABI_VERSION,
		.struct_size = sizeof(desc),
		.name = "mock",
		.line_count = GPIOCTL_MOCK_LINES_ZSH,
		.capabilities = GPIOCTL_ZSH_CAP_INPUT |
			GPIOCTL_ZSH_CAP_OUTPUT |
			GPIOCTL_ZSH_CAP_BIAS_DISABLE |
			GPIOCTL_ZSH_CAP_BIAS_PULL_UP |
			GPIOCTL_ZSH_CAP_BIAS_PULL_DOWN |
			GPIOCTL_ZSH_CAP_BATCH,
		.ops = &gpioctl_mock_ops_zsh,
		.priv = &gpioctl_mock_zsh,
		.owner = THIS_MODULE,
	};
	unsigned int i;

	mutex_init(&gpioctl_mock_zsh.lock);
	for (i = 0; i < GPIOCTL_MOCK_LINES_ZSH; i++)
		gpioctl_mock_zsh.lines[i].offset = i;
	return gpioctl_register_backend_zsh(&desc, &gpioctl_mock_zsh.controller);
}

static void __exit gpioctl_mock_exit_zsh(void)
{
	int ret = gpioctl_unregister_backend_zsh(gpioctl_mock_zsh.controller);

	if (ret)
		pr_err("gpioctl_mock_zsh: unregister failed: %d\n", ret);
}

module_init(gpioctl_mock_init_zsh);
module_exit(gpioctl_mock_exit_zsh);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zsh");
MODULE_DESCRIPTION("Fault-injectable mock backend for gpioctl_zsh");
MODULE_VERSION("0.1.0");
MODULE_SOFTDEP("pre: gpioctl_core_zsh");
