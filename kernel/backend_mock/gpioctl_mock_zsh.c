// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
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
	unsigned int irq;
};

struct gpioctl_mock_zsh {
	struct mutex lock;
	struct gpioctl_mock_line_zsh lines[GPIOCTL_MOCK_LINES_ZSH];
	struct gpioctl_line_policy_desc_zsh policies[GPIOCTL_MOCK_LINES_ZSH];
	struct irq_domain *irq_domain;
	struct gpioctl_controller_zsh *controller;
};

static struct gpioctl_mock_zsh gpioctl_mock_zsh;
static int fail_offset_zsh = -1;
module_param_named(fail_offset, fail_offset_zsh, int, 0644);
MODULE_PARM_DESC(fail_offset, "Mock line offset that returns -EIO, or -1");

static void gpioctl_mock_irq_mask_zsh(struct irq_data *data)
{
}

static void gpioctl_mock_irq_unmask_zsh(struct irq_data *data)
{
}

static int gpioctl_mock_irq_set_type_zsh(struct irq_data *data,
					 unsigned int type)
{
	return 0;
}

static struct irq_chip gpioctl_mock_irq_chip_zsh = {
	.name = "gpioctl-mock-zsh",
	.irq_mask = gpioctl_mock_irq_mask_zsh,
	.irq_unmask = gpioctl_mock_irq_unmask_zsh,
	.irq_set_type = gpioctl_mock_irq_set_type_zsh,
};

static int gpioctl_mock_irq_map_zsh(struct irq_domain *domain,
				    unsigned int irq,
				    irq_hw_number_t hardware_irq)
{
	irq_set_chip_data(irq, domain->host_data);
	irq_set_chip_and_handler(irq, &gpioctl_mock_irq_chip_zsh,
				 handle_simple_irq);
	irq_set_noprobe(irq);
	return 0;
}

static const struct irq_domain_ops gpioctl_mock_irq_domain_ops_zsh = {
	.map = gpioctl_mock_irq_map_zsh,
};

static int gpioctl_mock_inject_set_zsh(const char *value,
				       const struct kernel_param *parameter)
{
	struct gpioctl_mock_line_zsh *line;
	unsigned int irq;
	int offset, ret;

	ret = kstrtoint(value, 0, &offset);
	if (ret)
		return ret;
	if (offset < 0 || offset >= GPIOCTL_MOCK_LINES_ZSH)
		return -ERANGE;
	line = &gpioctl_mock_zsh.lines[offset];
	mutex_lock(&gpioctl_mock_zsh.lock);
	if (!line->requested || line->output) {
		ret = -EBUSY;
	} else {
		line->value = !line->value;
		irq = line->irq;
		ret = 0;
	}
	mutex_unlock(&gpioctl_mock_zsh.lock);
	if (ret)
		return ret;
	return generic_handle_irq_safe(irq);
}

static const struct kernel_param_ops gpioctl_mock_inject_ops_zsh = {
	.set = gpioctl_mock_inject_set_zsh,
};
module_param_cb(inject_offset, &gpioctl_mock_inject_ops_zsh, NULL, 0200);
MODULE_PARM_DESC(inject_offset,
		 "Toggle a leased input line and emit its simulated IRQ");

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

static int gpioctl_mock_to_irq_zsh(void *priv, void *line_priv)
{
	struct gpioctl_mock_line_zsh *line = line_priv;

	return line->irq;
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
	.to_irq = gpioctl_mock_to_irq_zsh,
};

static int __init gpioctl_mock_init_zsh(void)
{
	struct gpioctl_backend_desc_zsh desc = {
		.abi_version = GPIOCTL_ZSH_HAL_ABI_VERSION,
		.struct_size = sizeof(desc),
		.name = "mock",
		.hardware_key = "mock0",
		.line_count = GPIOCTL_MOCK_LINES_ZSH,
		.capabilities = GPIOCTL_ZSH_CAP_INPUT |
			GPIOCTL_ZSH_CAP_OUTPUT |
			GPIOCTL_ZSH_CAP_BIAS_DISABLE |
			GPIOCTL_ZSH_CAP_BIAS_PULL_UP |
			GPIOCTL_ZSH_CAP_BIAS_PULL_DOWN |
			GPIOCTL_ZSH_CAP_EDGE_RISING |
			GPIOCTL_ZSH_CAP_EDGE_FALLING |
			GPIOCTL_ZSH_CAP_DEBOUNCE |
			GPIOCTL_ZSH_CAP_BATCH,
		.ops = &gpioctl_mock_ops_zsh,
		.line_policies = gpioctl_mock_zsh.policies,
		.priv = &gpioctl_mock_zsh,
		.owner = THIS_MODULE,
	};
	unsigned int i;
	int ret;

	mutex_init(&gpioctl_mock_zsh.lock);
	gpioctl_mock_zsh.irq_domain = irq_domain_add_linear(
		NULL, GPIOCTL_MOCK_LINES_ZSH, &gpioctl_mock_irq_domain_ops_zsh,
		&gpioctl_mock_zsh);
	if (!gpioctl_mock_zsh.irq_domain)
		return -ENOMEM;
	for (i = 0; i < GPIOCTL_MOCK_LINES_ZSH; i++) {
		gpioctl_mock_zsh.lines[i].offset = i;
		gpioctl_mock_zsh.policies[i].safe_direction =
			GPIOCTL_ZSH_DIRECTION_INPUT;
		gpioctl_mock_zsh.policies[i].safe_bias =
			GPIOCTL_ZSH_BIAS_DISABLE;
		gpioctl_mock_zsh.policies[i].flags =
			GPIOCTL_ZSH_POLICY_ALLOW_UNPRIVILEGED |
			GPIOCTL_ZSH_POLICY_OUTPUT_ALLOWED;
		gpioctl_mock_zsh.lines[i].irq = irq_create_mapping(
			gpioctl_mock_zsh.irq_domain, i);
		if (!gpioctl_mock_zsh.lines[i].irq) {
			ret = -ENOMEM;
			goto err_mappings;
		}
	}
	gpioctl_mock_zsh.policies[14].flags = 0;
	gpioctl_mock_zsh.policies[15].flags = GPIOCTL_ZSH_POLICY_RESERVED;
	ret = gpioctl_register_backend_zsh(&desc, &gpioctl_mock_zsh.controller);
	if (!ret)
		return 0;
err_mappings:
	while (i) {
		i--;
		irq_dispose_mapping(gpioctl_mock_zsh.lines[i].irq);
		gpioctl_mock_zsh.lines[i].irq = 0;
	}
	irq_domain_remove(gpioctl_mock_zsh.irq_domain);
	gpioctl_mock_zsh.irq_domain = NULL;
	return ret;
}

static void __exit gpioctl_mock_exit_zsh(void)
{
	unsigned int i;
	int ret = gpioctl_unregister_backend_zsh(gpioctl_mock_zsh.controller);

	if (ret)
		pr_err("gpioctl_mock_zsh: unregister failed: %d\n", ret);
	for (i = 0; i < GPIOCTL_MOCK_LINES_ZSH; i++)
		irq_dispose_mapping(gpioctl_mock_zsh.lines[i].irq);
	irq_domain_remove(gpioctl_mock_zsh.irq_domain);
}

module_init(gpioctl_mock_init_zsh);
module_exit(gpioctl_mock_exit_zsh);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zsh");
MODULE_DESCRIPTION("Fault-injectable mock backend for gpioctl_zsh");
MODULE_VERSION("0.1.0");
MODULE_SOFTDEP("pre: gpioctl_core_zsh");
