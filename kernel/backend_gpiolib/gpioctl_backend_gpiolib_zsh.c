// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/module.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/sort.h>

#include "gpioctl_hal_zsh.h"

#define GPIOCTL_GPIOLIB_ZSH_MAX_CHIPS 64

struct gpioctl_gpiolib_chip_zsh {
	struct gpio_device *gdev;
	struct gpio_chip *chip;
	struct gpioctl_controller_zsh *controller;
	char name[GPIOCTL_ZSH_BACKEND_NAME_MAX + 1];
};

struct gpioctl_find_context_zsh {
	struct gpioctl_gpiolib_chip_zsh *chips;
	unsigned int count;
	struct gpio_chip *found;
};

static struct gpioctl_gpiolib_chip_zsh gpioctl_chips_zsh[
	GPIOCTL_GPIOLIB_ZSH_MAX_CHIPS];
static unsigned int gpioctl_chip_count_zsh;

static bool gpioctl_chip_seen_zsh(struct gpioctl_find_context_zsh *context,
				  struct gpio_chip *chip)
{
	unsigned int i;

	for (i = 0; i < context->count; i++)
		if (context->chips[i].chip == chip)
			return true;
	return false;
}

static int gpioctl_match_unseen_chip_zsh(struct gpio_chip *chip, void *data)
{
	struct gpioctl_find_context_zsh *context = data;

	if (!chip || !chip->ngpio || gpioctl_chip_seen_zsh(context, chip))
		return 0;
	context->found = chip;
	return 1;
}

static int gpioctl_compare_chip_zsh(const void *left, const void *right)
{
	const struct gpioctl_gpiolib_chip_zsh *a = left;
	const struct gpioctl_gpiolib_chip_zsh *b = right;

	if (a->chip->base < b->chip->base)
		return -1;
	if (a->chip->base > b->chip->base)
		return 1;
	return 0;
}

static int gpioctl_gpiolib_request_zsh(void *priv, unsigned int offset,
				       void **line_priv)
{
	struct gpioctl_gpiolib_chip_zsh *entry = priv;
	struct gpio_desc *desc;

	if (offset >= entry->chip->ngpio)
		return -EINVAL;
	desc = gpiochip_request_own_desc(entry->chip, offset, "gpioctl_zsh",
					 GPIO_LOOKUP_FLAGS_DEFAULT, GPIOD_ASIS);
	if (IS_ERR(desc))
		return PTR_ERR(desc);
	*line_priv = desc;
	return 0;
}

static void gpioctl_gpiolib_release_zsh(void *priv, void *line_priv)
{
	gpiochip_free_own_desc(line_priv);
}

static int gpioctl_gpiolib_direction_input_zsh(void *priv, void *line_priv)
{
	return gpiod_direction_input(line_priv);
}

static int gpioctl_gpiolib_direction_output_zsh(void *priv, void *line_priv,
						int value)
{
	return gpiod_direction_output(line_priv, !!value);
}

static int gpioctl_gpiolib_get_value_zsh(void *priv, void *line_priv)
{
	return gpiod_get_value(line_priv);
}

static int gpioctl_gpiolib_set_value_zsh(void *priv, void *line_priv, int value)
{
	gpiod_set_value(line_priv, !!value);
	return 0;
}

static int gpioctl_gpiolib_set_bias_zsh(void *priv, void *line_priv,
					enum gpioctl_zsh_bias bias)
{
	unsigned long config;

	switch (bias) {
	case GPIOCTL_ZSH_BIAS_DISABLE:
		config = pinconf_to_config_packed(PIN_CONFIG_BIAS_DISABLE, 0);
		break;
	case GPIOCTL_ZSH_BIAS_PULL_UP:
		config = pinconf_to_config_packed(PIN_CONFIG_BIAS_PULL_UP, 1);
		break;
	case GPIOCTL_ZSH_BIAS_PULL_DOWN:
		config = pinconf_to_config_packed(PIN_CONFIG_BIAS_PULL_DOWN, 1);
		break;
	default:
		return -EINVAL;
	}
	return gpiod_set_config(line_priv, config);
}

static int gpioctl_gpiolib_set_debounce_zsh(void *priv, void *line_priv,
					    u32 debounce_us)
{
	unsigned long config = pinconf_to_config_packed(PIN_CONFIG_INPUT_DEBOUNCE,
							  debounce_us);

	return gpiod_set_config(line_priv, config);
}

static int gpioctl_gpiolib_to_irq_zsh(void *priv, void *line_priv)
{
	return gpiod_to_irq(line_priv);
}

static int gpioctl_gpiolib_get_iopad_caps_zsh(
	void *priv, unsigned int offset, struct gpioctl_zsh_line_caps *caps)
{
	/* Standard pinconf operations are capability-probed when applied. */
	caps->capabilities |= GPIOCTL_ZSH_CAP_BIAS_DISABLE |
		GPIOCTL_ZSH_CAP_BIAS_PULL_UP | GPIOCTL_ZSH_CAP_BIAS_PULL_DOWN;
	return 0;
}

static const struct gpioctl_hal_ops_zsh gpioctl_gpiolib_ops_zsh = {
	.abi_version = GPIOCTL_ZSH_HAL_ABI_VERSION,
	.struct_size = sizeof(gpioctl_gpiolib_ops_zsh),
	.request = gpioctl_gpiolib_request_zsh,
	.release = gpioctl_gpiolib_release_zsh,
	.direction_input = gpioctl_gpiolib_direction_input_zsh,
	.direction_output = gpioctl_gpiolib_direction_output_zsh,
	.get_value = gpioctl_gpiolib_get_value_zsh,
	.set_value = gpioctl_gpiolib_set_value_zsh,
	.set_bias = gpioctl_gpiolib_set_bias_zsh,
	.set_debounce = gpioctl_gpiolib_set_debounce_zsh,
	.to_irq = gpioctl_gpiolib_to_irq_zsh,
	.get_iopad_caps = gpioctl_gpiolib_get_iopad_caps_zsh,
};

static void gpioctl_release_discovered_chips_zsh(void)
{
	unsigned int i;

	for (i = 0; i < gpioctl_chip_count_zsh; i++) {
		if (gpioctl_chips_zsh[i].gdev)
			gpio_device_put(gpioctl_chips_zsh[i].gdev);
		gpioctl_chips_zsh[i].gdev = NULL;
		gpioctl_chips_zsh[i].chip = NULL;
	}
	gpioctl_chip_count_zsh = 0;
}

static int gpioctl_discover_chips_zsh(void)
{
	struct gpioctl_find_context_zsh context = {
		.chips = gpioctl_chips_zsh,
	};

	while (context.count < GPIOCTL_GPIOLIB_ZSH_MAX_CHIPS) {
		struct gpio_device *gdev;

		context.found = NULL;
		gdev = gpio_device_find(&context, gpioctl_match_unseen_chip_zsh);
		if (!gdev)
			break;
		if (!context.found) {
			gpio_device_put(gdev);
			break;
		}
		gpioctl_chips_zsh[context.count].gdev = gdev;
		gpioctl_chips_zsh[context.count].chip = context.found;
		context.count++;
	}
	gpioctl_chip_count_zsh = context.count;
	if (!gpioctl_chip_count_zsh)
		return -ENODEV;
	sort(gpioctl_chips_zsh, gpioctl_chip_count_zsh,
	     sizeof(gpioctl_chips_zsh[0]), gpioctl_compare_chip_zsh, NULL);
	return 0;
}

static int __init gpioctl_gpiolib_init_zsh(void)
{
	unsigned int i;
	int ret;

	ret = gpioctl_discover_chips_zsh();
	if (ret)
		return ret;
	for (i = 0; i < gpioctl_chip_count_zsh; i++) {
		struct gpioctl_gpiolib_chip_zsh *entry = &gpioctl_chips_zsh[i];
		struct gpioctl_backend_desc_zsh desc;
		const char *label = entry->chip->label ?: dev_name(entry->chip->parent);

		snprintf(entry->name, sizeof(entry->name), "gpiolib:%s", label);
		desc = (struct gpioctl_backend_desc_zsh) {
			.abi_version = GPIOCTL_ZSH_HAL_ABI_VERSION,
			.struct_size = sizeof(desc),
			.name = entry->name,
			.hardware_key = dev_name(entry->chip->parent),
			.line_count = entry->chip->ngpio,
			.capabilities = GPIOCTL_ZSH_CAP_INPUT |
				GPIOCTL_ZSH_CAP_OUTPUT |
				GPIOCTL_ZSH_CAP_BIAS_DISABLE |
				GPIOCTL_ZSH_CAP_BIAS_PULL_UP |
				GPIOCTL_ZSH_CAP_BIAS_PULL_DOWN |
				GPIOCTL_ZSH_CAP_EDGE_RISING |
				GPIOCTL_ZSH_CAP_EDGE_FALLING |
				GPIOCTL_ZSH_CAP_DEBOUNCE |
				GPIOCTL_ZSH_CAP_BATCH,
			.ops = &gpioctl_gpiolib_ops_zsh,
			.priv = entry,
			.owner = THIS_MODULE,
		};

		ret = gpioctl_register_backend_zsh(&desc, &entry->controller);
		if (ret)
			goto rollback;
	}
	pr_info("gpioctl_backend_gpiolib_zsh: registered %u gpiochips\n",
		gpioctl_chip_count_zsh);
	return 0;

rollback:
	while (i) {
		i--;
		gpioctl_unregister_backend_zsh(gpioctl_chips_zsh[i].controller);
		gpioctl_chips_zsh[i].controller = NULL;
	}
	gpioctl_release_discovered_chips_zsh();
	return ret;
}

static void __exit gpioctl_gpiolib_exit_zsh(void)
{
	unsigned int i = gpioctl_chip_count_zsh;

	while (i) {
		int ret;

		i--;
		ret = gpioctl_unregister_backend_zsh(gpioctl_chips_zsh[i].controller);
		if (ret)
			pr_err("gpioctl_backend_gpiolib_zsh: unregister controller %u failed: %d\n",
			       i, ret);
		gpioctl_chips_zsh[i].controller = NULL;
	}
	gpioctl_release_discovered_chips_zsh();
	pr_info("gpioctl_backend_gpiolib_zsh: unloaded\n");
}

module_init(gpioctl_gpiolib_init_zsh);
module_exit(gpioctl_gpiolib_exit_zsh);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zsh");
MODULE_DESCRIPTION("Generic gpiolib backend for gpioctl_zsh");
MODULE_VERSION("0.1.0");
MODULE_SOFTDEP("pre: gpioctl_core_zsh");
