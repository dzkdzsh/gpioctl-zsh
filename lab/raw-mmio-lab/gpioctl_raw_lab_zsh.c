// SPDX-License-Identifier: GPL-2.0-only
#include <linux/bits.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define GPIOCTL_RAW_DR_ZSH 0x00
#define GPIOCTL_RAW_DDR_ZSH 0x04
#define GPIOCTL_RAW_VALID_MASK_ZSH GENMASK(15, 0)

struct gpioctl_raw_lab_zsh {
	void __iomem *base;
	spinlock_t lock;
};

static bool allow_write_zsh;
static uint selftest_mask_zsh;
static uint selftest_value_zsh;
module_param_named(allow_write, allow_write_zsh, bool, 0400);
MODULE_PARM_DESC(allow_write,
	"Permit isolated probe-time RMW self-test; default false");
module_param_named(selftest_mask, selftest_mask_zsh, uint, 0400);
MODULE_PARM_DESC(selftest_mask, "16-bit line mask for isolated self-test");
module_param_named(selftest_value, selftest_value_zsh, uint, 0400);
MODULE_PARM_DESC(selftest_value, "Masked DR value for isolated self-test");

static int gpioctl_raw_selftest_zsh(struct platform_device *pdev,
				    struct gpioctl_raw_lab_zsh *lab)
{
	unsigned long flags;
	u32 old_ddr, old_dr, test_dr, readback;
	u32 mask = selftest_mask_zsh;
	int ret = 0;

	if (!allow_write_zsh)
		return 0;
	if (!device_property_read_bool(&pdev->dev, "zsh,isolation-confirmed"))
		return dev_err_probe(&pdev->dev, -EPERM,
			"write refused without isolation-confirmed property\n");
	if (!mask || mask & ~GPIOCTL_RAW_VALID_MASK_ZSH ||
	    selftest_value_zsh & ~mask)
		return dev_err_probe(&pdev->dev, -EINVAL,
			"invalid self-test mask/value\n");

	spin_lock_irqsave(&lab->lock, flags);
	old_ddr = readl(lab->base + GPIOCTL_RAW_DDR_ZSH);
	old_dr = readl(lab->base + GPIOCTL_RAW_DR_ZSH);
	/* Prevent a temporary external transition while changing the data latch. */
	writel(old_ddr & ~mask, lab->base + GPIOCTL_RAW_DDR_ZSH);
	readl(lab->base + GPIOCTL_RAW_DDR_ZSH);
	test_dr = (old_dr & ~mask) | selftest_value_zsh;
	writel(test_dr, lab->base + GPIOCTL_RAW_DR_ZSH);
	readback = readl(lab->base + GPIOCTL_RAW_DR_ZSH);
	if ((readback & mask) != (test_dr & mask))
		ret = -EIO;
	/* The lab never leaves a changed latch or direction behind. */
	writel(old_dr, lab->base + GPIOCTL_RAW_DR_ZSH);
	readl(lab->base + GPIOCTL_RAW_DR_ZSH);
	writel(old_ddr, lab->base + GPIOCTL_RAW_DDR_ZSH);
	readl(lab->base + GPIOCTL_RAW_DDR_ZSH);
	spin_unlock_irqrestore(&lab->lock, flags);
	return ret ? dev_err_probe(&pdev->dev, ret,
				   "RMW readback failed; snapshot restored\n") : 0;
}

static int gpioctl_raw_probe_zsh(struct platform_device *pdev)
{
	struct gpioctl_raw_lab_zsh *lab;
	int ret;

	lab = devm_kzalloc(&pdev->dev, sizeof(*lab), GFP_KERNEL);
	if (!lab)
		return -ENOMEM;
	/* Includes request_mem_region ownership; an active gpiochip yields EBUSY. */
	lab->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(lab->base))
		return dev_err_probe(&pdev->dev, PTR_ERR(lab->base),
				     "exclusive MMIO claim failed\n");
	spin_lock_init(&lab->lock);
	platform_set_drvdata(pdev, lab);
	ret = gpioctl_raw_selftest_zsh(pdev, lab);
	if (ret)
		return ret;
	dev_info(&pdev->dev, "isolated raw-MMIO lab claimed in %s mode\n",
		 allow_write_zsh ? "self-test" : "read-only");
	return 0;
}

static const struct of_device_id gpioctl_raw_of_match_zsh[] = {
	{ .compatible = "zsh,phytium-gpio-raw-lab" },
	{ }
};
MODULE_DEVICE_TABLE(of, gpioctl_raw_of_match_zsh);

static struct platform_driver gpioctl_raw_driver_zsh = {
	.probe = gpioctl_raw_probe_zsh,
	.driver = {
		.name = "gpioctl-raw-lab-zsh",
		.of_match_table = gpioctl_raw_of_match_zsh,
	},
};
module_platform_driver(gpioctl_raw_driver_zsh);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("zsh");
MODULE_DESCRIPTION("Isolated Phytium GPIO raw-MMIO safety lab");
MODULE_VERSION("0.1.0");
