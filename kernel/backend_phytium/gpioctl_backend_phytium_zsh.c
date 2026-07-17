// SPDX-License-Identifier: GPL-2.0-only
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include "gpioctl_hal_zsh.h"

#define PHYTIUM_IOPAD_FUNC_MASK_ZSH GENMASK(2, 0)
#define PHYTIUM_IOPAD_DRIVE_MASK_ZSH GENMASK(7, 4)
#define PHYTIUM_IOPAD_BIAS_MASK_ZSH GENMASK(9, 8)

struct phytium_pad_zsh {
	u16 register_offset;
	u8 gpio_function;
};

#define PHYTIUM_PAD_ZSH(offset, function) \
	{ .register_offset = (offset), .gpio_function = (function) }

static const struct phytium_pad_zsh phytium_pads_zsh[6][16] = {
	[0] = {
		PHYTIUM_PAD_ZSH(0x0000, 5), PHYTIUM_PAD_ZSH(0x019c, 6),
		PHYTIUM_PAD_ZSH(0x01a0, 6), PHYTIUM_PAD_ZSH(0x01a4, 6),
		PHYTIUM_PAD_ZSH(0x01a8, 6), PHYTIUM_PAD_ZSH(0x01ac, 6),
		PHYTIUM_PAD_ZSH(0x01b0, 6), PHYTIUM_PAD_ZSH(0x0038, 5),
		PHYTIUM_PAD_ZSH(0x003c, 5), PHYTIUM_PAD_ZSH(0x0040, 5),
		PHYTIUM_PAD_ZSH(0x0044, 5), PHYTIUM_PAD_ZSH(0x0048, 5),
		PHYTIUM_PAD_ZSH(0x004c, 5), PHYTIUM_PAD_ZSH(0x0050, 5),
		PHYTIUM_PAD_ZSH(0x0054, 5), PHYTIUM_PAD_ZSH(0x0058, 5),
	},
	[1] = {
		PHYTIUM_PAD_ZSH(0x005c, 5), PHYTIUM_PAD_ZSH(0x0060, 5),
		PHYTIUM_PAD_ZSH(0x0064, 5), PHYTIUM_PAD_ZSH(0x0068, 5),
		PHYTIUM_PAD_ZSH(0x006c, 5), PHYTIUM_PAD_ZSH(0x0070, 5),
		PHYTIUM_PAD_ZSH(0x0074, 5), PHYTIUM_PAD_ZSH(0x0078, 5),
		PHYTIUM_PAD_ZSH(0x01f8, 6), PHYTIUM_PAD_ZSH(0x01fc, 6),
		PHYTIUM_PAD_ZSH(0x0200, 6), PHYTIUM_PAD_ZSH(0x0088, 5),
		PHYTIUM_PAD_ZSH(0x008c, 5), PHYTIUM_PAD_ZSH(0x0090, 5),
		PHYTIUM_PAD_ZSH(0x0094, 6), PHYTIUM_PAD_ZSH(0x0098, 6),
	},
	[2] = {
		PHYTIUM_PAD_ZSH(0x0210, 0), PHYTIUM_PAD_ZSH(0x0214, 0),
		PHYTIUM_PAD_ZSH(0x0218, 0), PHYTIUM_PAD_ZSH(0x00a8, 6),
		PHYTIUM_PAD_ZSH(0x00ac, 6), PHYTIUM_PAD_ZSH(0x0224, 0),
		PHYTIUM_PAD_ZSH(0x0228, 0), PHYTIUM_PAD_ZSH(0x022c, 0),
		PHYTIUM_PAD_ZSH(0x00bc, 6), PHYTIUM_PAD_ZSH(0x00c0, 6),
		PHYTIUM_PAD_ZSH(0x00c4, 6), PHYTIUM_PAD_ZSH(0x023c, 0),
		PHYTIUM_PAD_ZSH(0x0240, 0), PHYTIUM_PAD_ZSH(0x0244, 0),
		PHYTIUM_PAD_ZSH(0x0248, 0), PHYTIUM_PAD_ZSH(0x024c, 0),
	},
	[3] = {
		PHYTIUM_PAD_ZSH(0x00dc, 6), PHYTIUM_PAD_ZSH(0x00e0, 6),
		PHYTIUM_PAD_ZSH(0x00e4, 6), PHYTIUM_PAD_ZSH(0x00e8, 6),
		PHYTIUM_PAD_ZSH(0x00ec, 6), PHYTIUM_PAD_ZSH(0x00f0, 6),
		PHYTIUM_PAD_ZSH(0x00f4, 6), PHYTIUM_PAD_ZSH(0x00f8, 6),
		PHYTIUM_PAD_ZSH(0x00fc, 6), PHYTIUM_PAD_ZSH(0x0100, 6),
		PHYTIUM_PAD_ZSH(0x0104, 6), PHYTIUM_PAD_ZSH(0x0108, 6),
		PHYTIUM_PAD_ZSH(0x010c, 6), PHYTIUM_PAD_ZSH(0x0110, 6),
		PHYTIUM_PAD_ZSH(0x0114, 6), PHYTIUM_PAD_ZSH(0x0118, 6),
	},
	[4] = {
		PHYTIUM_PAD_ZSH(0x011c, 6), PHYTIUM_PAD_ZSH(0x0120, 6),
		PHYTIUM_PAD_ZSH(0x0124, 6), PHYTIUM_PAD_ZSH(0x0128, 6),
		PHYTIUM_PAD_ZSH(0x012c, 6), PHYTIUM_PAD_ZSH(0x0130, 6),
		PHYTIUM_PAD_ZSH(0x0134, 6), PHYTIUM_PAD_ZSH(0x0138, 6),
		PHYTIUM_PAD_ZSH(0x013c, 6), PHYTIUM_PAD_ZSH(0x0140, 6),
		PHYTIUM_PAD_ZSH(0x0144, 6), PHYTIUM_PAD_ZSH(0x0148, 6),
		PHYTIUM_PAD_ZSH(0x014c, 6), PHYTIUM_PAD_ZSH(0x0150, 6),
		PHYTIUM_PAD_ZSH(0x0154, 6), PHYTIUM_PAD_ZSH(0x0158, 6),
	},
	[5] = {
		PHYTIUM_PAD_ZSH(0x015c, 6), PHYTIUM_PAD_ZSH(0x0160, 6),
		PHYTIUM_PAD_ZSH(0x0164, 6), PHYTIUM_PAD_ZSH(0x0168, 6),
		PHYTIUM_PAD_ZSH(0x016c, 6), PHYTIUM_PAD_ZSH(0x0170, 6),
		PHYTIUM_PAD_ZSH(0x0174, 6), PHYTIUM_PAD_ZSH(0x0178, 6),
		PHYTIUM_PAD_ZSH(0x017c, 6), PHYTIUM_PAD_ZSH(0x0180, 6),
		PHYTIUM_PAD_ZSH(0x0184, 6), PHYTIUM_PAD_ZSH(0x0188, 6),
		PHYTIUM_PAD_ZSH(0x018c, 6), PHYTIUM_PAD_ZSH(0x0190, 6),
		PHYTIUM_PAD_ZSH(0x0194, 6), PHYTIUM_PAD_ZSH(0x0198, 6),
	},
};

#undef PHYTIUM_PAD_ZSH

static const char *const phytium_controller_keys_zsh[6] = {
	"28034000.gpio", "28035000.gpio", "28036000.gpio",
	"28037000.gpio", "28038000.gpio", "28039000.gpio",
};

struct phytium_iopad_zsh {
	void __iomem *base;
	spinlock_t lock;
	struct gpioctl_iopad_provider_zsh *provider;
};

static int phytium_controller_index_zsh(const char *hardware_key)
{
	unsigned int i;

	if (!hardware_key)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(phytium_controller_keys_zsh); i++)
		if (!strcmp(hardware_key, phytium_controller_keys_zsh[i]))
			return i;
	return -ENODEV;
}

static bool phytium_iopad_supports_zsh(void *priv, const char *hardware_key,
				      unsigned int offset)
{
	return phytium_controller_index_zsh(hardware_key) >= 0 && offset < 16;
}

static int phytium_iopad_get_caps_zsh(void *priv, const char *hardware_key,
				     unsigned int offset,
				     struct gpioctl_zsh_line_caps *caps)
{
	if (!phytium_iopad_supports_zsh(priv, hardware_key, offset))
		return -EOPNOTSUPP;
	caps->capabilities |= GPIOCTL_ZSH_CAP_IOPAD |
		GPIOCTL_ZSH_CAP_BIAS_DISABLE | GPIOCTL_ZSH_CAP_BIAS_PULL_UP |
		GPIOCTL_ZSH_CAP_BIAS_PULL_DOWN;
	caps->drive_level_min = 0;
	caps->drive_level_max = 15;
	return 0;
}

static int phytium_bias_bits_zsh(u32 bias, u32 *bits)
{
	switch (bias) {
	case GPIOCTL_ZSH_BIAS_DISABLE:
		*bits = 0;
		return 0;
	case GPIOCTL_ZSH_BIAS_PULL_DOWN:
		*bits = BIT(8);
		return 0;
	case GPIOCTL_ZSH_BIAS_PULL_UP:
		*bits = BIT(9);
		return 0;
	default:
		return -EINVAL;
	}
}

static int phytium_iopad_configure_zsh(
	void *priv, const char *hardware_key, unsigned int offset,
	const struct gpioctl_zsh_iopad_config *config)
{
	struct phytium_iopad_zsh *iopad = priv;
	const struct phytium_pad_zsh *pad;
	void __iomem *reg;
	unsigned long irq_flags;
	u32 old_value, new_value, readback, bias_bits;
	int controller, ret;

	controller = phytium_controller_index_zsh(hardware_key);
	if (controller < 0 || offset >= 16)
		return -EOPNOTSUPP;
	if ((config->flags & GPIOCTL_ZSH_IOPAD_APPLY_DRIVE) &&
	    config->drive_level > 15)
		return -ERANGE;
	if ((config->flags & GPIOCTL_ZSH_IOPAD_APPLY_MUX) &&
	    config->mux_state != GPIOCTL_ZSH_MUX_GPIO)
		return -EINVAL;
	if (config->flags & GPIOCTL_ZSH_IOPAD_APPLY_BIAS) {
		ret = phytium_bias_bits_zsh(config->bias, &bias_bits);
		if (ret)
			return ret;
	} else {
		bias_bits = 0;
	}

	pad = &phytium_pads_zsh[controller][offset];
	reg = iopad->base + pad->register_offset;
	spin_lock_irqsave(&iopad->lock, irq_flags);
	old_value = readl(reg);
	new_value = old_value;
	if (config->flags & GPIOCTL_ZSH_IOPAD_APPLY_BIAS) {
		new_value &= ~PHYTIUM_IOPAD_BIAS_MASK_ZSH;
		new_value |= bias_bits;
	}
	if (config->flags & GPIOCTL_ZSH_IOPAD_APPLY_DRIVE) {
		new_value &= ~PHYTIUM_IOPAD_DRIVE_MASK_ZSH;
		new_value |= config->drive_level << 4;
	}
	if (config->flags & GPIOCTL_ZSH_IOPAD_APPLY_MUX) {
		new_value &= ~PHYTIUM_IOPAD_FUNC_MASK_ZSH;
		new_value |= pad->gpio_function;
	}
	writel(new_value, reg);
	readback = readl(reg);
	if (readback != new_value) {
		writel(old_value, reg);
		readl(reg);
		ret = -EIO;
	} else {
		ret = 0;
	}
	spin_unlock_irqrestore(&iopad->lock, irq_flags);
	return ret;
}

static const struct gpioctl_iopad_ops_zsh phytium_iopad_ops_zsh = {
	.abi_version = GPIOCTL_ZSH_HAL_ABI_VERSION,
	.struct_size = sizeof(phytium_iopad_ops_zsh),
	.supports = phytium_iopad_supports_zsh,
	.get_caps = phytium_iopad_get_caps_zsh,
	.configure = phytium_iopad_configure_zsh,
};

static int phytium_iopad_probe_zsh(struct platform_device *pdev)
{
	struct phytium_iopad_zsh *iopad;
	struct gpioctl_iopad_provider_desc_zsh desc;
	int ret;

	iopad = devm_kzalloc(&pdev->dev, sizeof(*iopad), GFP_KERNEL);
	if (!iopad)
		return -ENOMEM;
	iopad->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(iopad->base))
		return PTR_ERR(iopad->base);
	spin_lock_init(&iopad->lock);
	desc = (struct gpioctl_iopad_provider_desc_zsh) {
		.abi_version = GPIOCTL_ZSH_HAL_ABI_VERSION,
		.struct_size = sizeof(desc),
		.name = "phytium-iopad",
		.ops = &phytium_iopad_ops_zsh,
		.priv = iopad,
		.owner = THIS_MODULE,
	};
	ret = gpioctl_register_iopad_provider_zsh(&desc, &iopad->provider);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, iopad);
	dev_info(&pdev->dev, "registered protected Phytium IOPAD provider\n");
	return 0;
}

static void phytium_iopad_remove_zsh(struct platform_device *pdev)
{
	struct phytium_iopad_zsh *iopad = platform_get_drvdata(pdev);

	gpioctl_unregister_iopad_provider_zsh(iopad->provider);
}

static const struct of_device_id phytium_iopad_of_match_zsh[] = {
	{ .compatible = "phytium,gpioctl-iopad-zsh" },
	{ }
};
MODULE_DEVICE_TABLE(of, phytium_iopad_of_match_zsh);

static struct platform_driver phytium_iopad_driver_zsh = {
	.probe = phytium_iopad_probe_zsh,
	.remove_new = phytium_iopad_remove_zsh,
	.driver = {
		.name = "gpioctl_backend_phytium_zsh",
		.of_match_table = phytium_iopad_of_match_zsh,
	},
};
module_platform_driver(phytium_iopad_driver_zsh);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zsh");
MODULE_DESCRIPTION("Device-tree-backed Phytium IOPAD provider for gpioctl_zsh");
MODULE_VERSION("0.1.0");
MODULE_SOFTDEP("pre: gpioctl_core_zsh");

