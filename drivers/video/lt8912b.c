// SPDX-License-Identifier: GPL-2.0

#include <common.h>
#include <dm.h>
#include <dm/device_compat.h>
#include <mipi_dsi.h>
#include <panel.h>
#include <asm/gpio.h>
#include <i2c.h>
#include <linux/err.h>
#include <linux/delay.h>

/*
 * lt8912b have three i2c module inside.
 * lt8912b i2c address: [0x48, 0x49, 0x4a]
 */

#define I2C_1ST		0
#define I2C_2ND		1
#define I2C_3RD		2

struct lt8912b_priv {
	struct udevice *dev;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	unsigned long mode_flags;
	struct gpio_desc reset_gpiod;
	struct udevice *udev[3];
};

#include "lt8912b-reg.c"

static const struct display_timing default_timing = {
	.pixelclock.typ		= 148500000,
	.hactive.typ		= 1920,
	.hfront_porch.typ	= 88,
	.hback_porch.typ	= 148,
	.hsync_len.typ		= 44,
	.vactive.typ		= 1080,
	.vfront_porch.typ	= 4,
	.vback_porch.typ	= 36,
	.vsync_len.typ		= 5,
};

static int lt8912b_enable_backlight(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);
	struct mipi_dsi_device *device = plat->device;
	int ret;

	ret = mipi_dsi_attach(device);
	if (ret < 0)
		return ret;

	return 0;
}

static int lt8912b_get_display_timing(struct udevice *dev,
					    struct display_timing *timings)
{
	struct mipi_dsi_panel_plat *plat = dev_get_plat(dev);
	struct mipi_dsi_device *device = plat->device;
	struct lt8912b_priv *priv = dev_get_priv(dev);

	memcpy(timings, &default_timing, sizeof(*timings));

	/* fill characteristics of DSI data link */
	if (device) {
		device->lanes = priv->lanes;
		device->format = priv->format;
		device->mode_flags = priv->mode_flags;
	}

	return 0;
}

static int lt8912b_probe(struct udevice *dev)
{
	struct lt8912b_priv *priv = dev_get_priv(dev);
	int ret;

	printf("\n%s: ...\n", __func__);

	priv->dev = dev;

	priv->lanes = 4;
	priv->format = MIPI_DSI_FMT_RGB888;
	priv->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_VIDEO_HSE;
	
	ret = gpio_request_by_name(dev, "reset-gpios", 0, &priv->reset_gpiod,
						GPIOD_IS_OUT);
	if (ret) {
		printf("failed to get reset-gpios\n");
		return -1;
	}
	
	dm_gpio_set_value(&priv->reset_gpiod, 0);
	mdelay(200);
	dm_gpio_set_value(&priv->reset_gpiod, 1);

	ret = dm_i2c_probe(dev_get_parent(dev), 0x48, 0, &priv->udev[0]);
	if (ret) {
		printf("failed to allocate udev[0], ret=%d\n", ret);
		return -ENODEV;
	}
	
	ret = dm_i2c_probe(dev_get_parent(dev), 0x49, 0, &priv->udev[1]);
	if (ret) {
		printf("failed to allocate udev[1], ret=%d\n", ret);
		return -ENODEV;
	}
	
	ret = dm_i2c_probe(dev_get_parent(dev), 0x4a, 0, &priv->udev[2]);
	if (ret) {
		printf("failed to allocate udev[2], ret=%d\n", ret);
		return -ENODEV;
	}
	
	ret = lt8912_hw_init(priv);
	if (ret) {
		return -1;
	}

	printf("%s: done\n", __func__);

	return 0;
}

static const struct panel_ops lt8912b_ops = {
	.enable_backlight = lt8912b_enable_backlight,
	.get_display_timing = lt8912b_get_display_timing,
};

static const struct udevice_id lt8912b_ids[] = {
	{ .compatible = "lontium,lt8912b" },
	{ }
};

U_BOOT_DRIVER(lt8912b_mipi2hdmi) = {
	.name			  = "lt8912b_mipi2hdmi",
	.id			  = UCLASS_PANEL,
	.of_match		  = lt8912b_ids,
	.ops			  = &lt8912b_ops,
	.probe			  = lt8912b_probe,
	.plat_auto = sizeof(struct mipi_dsi_panel_plat),
	.priv_auto	= sizeof(struct lt8912b_priv),
};
