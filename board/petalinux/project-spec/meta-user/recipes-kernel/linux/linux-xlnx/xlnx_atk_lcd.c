// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx FPGA atk LCD Tx Subsystem driver.
 *
 * Copyright (c) 2023 Xilinx Pvt., Ltd
 *
 * Authors:
 * CX <2568365021@qq.com>
 */

#include <linux/clk.h>
#include <linux/pwm.h>
#include <linux/component.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/math64.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>
#include <video/of_videomode.h>

#include <drm/drmP.h>
#include <drm/drm_of.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_print.h>

/**
 * struct atk_dpi - Core configuration atk Tx subsystem device structure
 * @encoder: DRM encoder structure
 * @connector: DRM connector structure
 * @dev: device structure
 * @vm: videomode structure
 * @pclk: Video Clock
 */
struct atk_dpi {
	struct device *dev;
	struct drm_encoder encoder;
	struct drm_connector connector;

	struct videomode *vm;
	struct clk *pclk;
	struct pwm_device *backlight_pwm;
};

#define connector_to_dpi(c) container_of(c, struct atk_dpi, connector)
#define encoder_to_dpi(e) container_of(e, struct atk_dpi, encoder)
#define ATK_RGB_MSB_GPIO_COUNT 3U

static enum drm_connector_status
atk_dpi_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void atk_dpi_connector_destroy(struct drm_connector *connector)
{
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static const struct drm_connector_funcs atk_dpi_connector_funcs = {
	.detect = atk_dpi_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.reset = drm_atomic_helper_connector_reset,
	.destroy = atk_dpi_connector_destroy,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int lcd_get_modes(struct drm_connector *connector)
{
	struct atk_dpi *dpi = connector_to_dpi(connector);

	if (dpi->vm) {
		struct drm_display_mode *mode;

		mode = drm_mode_create(connector->dev);
		if (!mode) {
			DRM_DEV_ERROR(dpi->dev,
				      "failed to create a new display mode\n");
			return 0;
		}
		drm_display_mode_from_videomode(dpi->vm, mode);
		drm_mode_set_name(mode);
		mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
		drm_mode_probed_add(connector, mode);
		return 1;
	}

	return 0;
}

static int lcd_mode_valid(struct drm_connector *connector,
			       struct drm_display_mode *mode)
{
	struct atk_dpi *dpi = connector_to_dpi(connector);
	u64 requested_rate = (u64)mode->clock * 1000ULL;

	if (!dpi->vm || requested_rate != dpi->vm->pixelclock)
		return MODE_CLOCK_RANGE;

	return MODE_OK;
}

static struct drm_connector_helper_funcs atk_dpi_connector_helper_funcs = {
	.get_modes = lcd_get_modes,
	.mode_valid = lcd_mode_valid,
};

static void atk_dpi_mode_set(struct drm_encoder *encoder,
				struct drm_display_mode *mode,
				struct drm_display_mode *adjusted_mode)
{
	struct atk_dpi *dpi = encoder_to_dpi(encoder);
	u64 requested_rate = (u64)mode->clock * 1000ULL;

	/*
	 * The Clock Wizard feeds the complete video pipeline. Its product rate is
	 * configured once in probe, before the DRM component is registered. A
	 * second dynamic reconfiguration here can desynchronise VTC/DMA/video IP.
	 */
	if (!dpi->vm || requested_rate != dpi->vm->pixelclock)
		DRM_DEV_ERROR(dpi->dev,
			      "refusing runtime pclk change to %llu Hz\n",
			      (unsigned long long)requested_rate);
}

static void atk_dpi_enable(struct drm_encoder *encoder)
{
	struct atk_dpi *dpi = encoder_to_dpi(encoder);

	if (clk_prepare_enable(dpi->pclk))
		DRM_DEV_ERROR(dpi->dev,
			      "Cannot enable lcd pclk clock!\n");
}

static void atk_dpi_disable(struct drm_encoder *encoder)
{
	struct atk_dpi *dpi = encoder_to_dpi(encoder);

	clk_disable_unprepare(dpi->pclk);
}

static const struct drm_encoder_helper_funcs atk_dpi_encoder_helper_funcs = {
	.mode_set	= atk_dpi_mode_set,
	.enable = atk_dpi_enable,
	.disable = atk_dpi_disable,
};

static const struct drm_encoder_funcs atk_dpi_encoder_funcs = {
	.destroy = drm_encoder_cleanup,
};

static int atk_dpi_bind(struct device *dev, struct device *master,
			 void *data)
{
	struct atk_dpi *dpi = dev_get_drvdata(dev);
	struct drm_encoder *encoder = &dpi->encoder;
	struct drm_device *drm_dev = data;
	struct device_node *port;
	int ret;

	/*
	 * TODO: The possible CRTCs are 1 now as per current implementation of
	 * atk tx drivers. DRM framework can support more than one CRTCs and
	 * atk driver can be enhanced for that.
	 */
	encoder->possible_crtcs = 1;

	for_each_child_of_node(dev->of_node, port) {
		if (!port->name || of_node_cmp(port->name, "ports")) {
			DRM_DEBUG_DRIVER("port name is null or node name is not ports!\n");
			continue;
		}
		encoder->possible_crtcs |= drm_of_find_possible_crtcs(drm_dev, port);
	}

	/* initialize encoder */
	drm_encoder_init(drm_dev, encoder, &atk_dpi_encoder_funcs,
			 DRM_MODE_ENCODER_DPI, NULL);
	drm_encoder_helper_add(encoder, &atk_dpi_encoder_helper_funcs);

	/* initialize connector */
	ret = drm_connector_init(drm_dev, &dpi->connector,
				 &atk_dpi_connector_funcs, DRM_MODE_CONNECTOR_DPI);
	if (ret) {
		DRM_DEV_ERROR(dpi->dev, "Failed to initialize connector with drm\n");
		drm_encoder_cleanup(encoder);
		return ret;
	}

	drm_connector_helper_add(&dpi->connector, &atk_dpi_connector_helper_funcs);
	drm_connector_register(&dpi->connector);
	drm_connector_attach_encoder(&dpi->connector, encoder);

	return ret;
}

static void atk_dpi_unbind(struct device *dev, struct device *master,
			    void *data)
{
	struct atk_dpi *dpi = dev_get_drvdata(dev);

	drm_encoder_cleanup(&dpi->encoder);
	drm_connector_cleanup(&dpi->connector);
}

static const struct component_ops atk_dpi_component_ops = {
	.bind	= atk_dpi_bind,
	.unbind	= atk_dpi_unbind,
};

static int atk_dpi_drive_rgb_msb_lanes(struct device *dev)
{
	unsigned int i;

	for (i = 0; i < ATK_RGB_MSB_GPIO_COUNT; i++) {
		int gpio;
		int ret;

		gpio = of_get_named_gpio(dev->of_node, "rgb-msb-gpios", i);
		if (!gpio_is_valid(gpio)) {
			DRM_DEV_ERROR(dev, "invalid RGB-MSB gpio[%u]: %d\n", i, gpio);
			return gpio < 0 ? gpio : -EINVAL;
		}
		ret = devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_LOW,
					    "lcd rgb msb");
		if (ret) {
			DRM_DEV_ERROR(dev, "failed to drive RGB-MSB gpio[%u] low: %d\n",
				      i, ret);
			return ret;
		}
	}

	return 0;
}

static int atkencoder_init_dt(struct atk_dpi *dpi)
{
	struct device *dev = dpi->dev;
	struct device_node *dn = dev->of_node;
	struct device_node *np;
	int ret;

	np = of_get_child_by_name(dn, "display-timings");
	if (np) {
		struct videomode *vm;
		of_node_put(np);

		vm = devm_kzalloc(dev, sizeof(*dpi->vm), GFP_KERNEL);
		if (!vm)
			return -ENOMEM;

		ret = of_get_videomode(dn, vm, OF_USE_NATIVE_MODE);
		if (ret < 0) {
			DRM_DEV_ERROR(dev, "Failed to get videomode from DT\n");
			devm_kfree(dev, vm);
			return ret;
		}

		dpi->vm = vm;

		return 0;
	}

	DRM_DEV_ERROR(dev, "Missing display-timings in DT\n");
	return -EINVAL;
}

static int atk_dpi_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct atk_dpi *dpi;
	struct pwm_device *pwm;
	struct pwm_args pwm_args;
	struct pwm_state pwm_state;
	u32 duty_percent;
	int ret;

	dpi = devm_kzalloc(dev, sizeof(*dpi), GFP_KERNEL);
	if (!dpi)
		return -ENOMEM;

	dpi->dev = dev;
	ret = atk_dpi_drive_rgb_msb_lanes(dev);
	if (ret)
		return ret;

	dpi->pclk = devm_clk_get(dev, "lcd_pclk");
	if (IS_ERR(dpi->pclk)) {
		ret = PTR_ERR(dpi->pclk);
		DRM_DEV_ERROR(dev, "failed to get lcd_pclk %d\n", ret);
		return ret;
	}

	clk_disable_unprepare(dpi->pclk);		// 鍏堢姝㈡椂閽熻緭鍑?
	/* 鍒濆鍖杋nfo鍙橀噺 */
	ret = atkencoder_init_dt(dpi);
	if (ret < 0) {
		devm_kfree(dev, dpi);
		goto out1;
	}

	ret = clk_set_rate(dpi->pclk, dpi->vm->pixelclock);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to set pclk %d\n", ret);
		goto out1;
	}

	ret = clk_prepare_enable(dpi->pclk);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to enable pclk %d\n", ret);
		goto out1;
	}

	/* 鎵撳紑LCD鑳屽厜 */
	pwm = devm_pwm_get(dev, NULL);
	if (IS_ERR(pwm)) {
		ret = PTR_ERR(pwm);
		DRM_DEV_ERROR(dev, "failed devm_pwm_get- %d\n", ret);
		goto err_disable_pclk;
	}

	ret = of_property_read_u32(dev->of_node, "backlight-duty-percent",
				   &duty_percent);
	if (ret || !duty_percent || duty_percent > 100) {
		DRM_DEV_ERROR(dev, "invalid backlight-duty-percent in DT\n");
		ret = ret ? ret : -EINVAL;
		goto err_disable_pclk;
	}

	pwm_get_args(pwm, &pwm_args);
	if (!pwm_args.period) {
		DRM_DEV_ERROR(dev, "missing backlight PWM period in DT\n");
		ret = -EINVAL;
		goto err_disable_pclk;
	}

	pwm_init_state(pwm, &pwm_state);
	pwm_state.duty_cycle = DIV_ROUND_CLOSEST_ULL(
		(u64)pwm_args.period * duty_percent, 100);
	pwm_state.enabled = true;
	ret = pwm_apply_state(pwm, &pwm_state);
	if (ret) {
		DRM_DEV_ERROR(dev, "failed to apply DT backlight PWM %d\n", ret);
		goto err_disable_pclk;
	}
	dpi->backlight_pwm = pwm;

	platform_set_drvdata(pdev, dpi);

	ret = component_add(dev, &atk_dpi_component_ops);
	if (ret < 0){
		DRM_DEV_ERROR(dev, "failed component_add- %d\n", ret);
		goto err_disable_backlight;
	}

	dev_info(&pdev->dev, "Atk encoder driver probed\n");
	return ret;

err_disable_backlight:
	pwm_disable(dpi->backlight_pwm);

err_disable_pclk:
	clk_disable_unprepare(dpi->pclk);

out1:
	printk("atk lcd failed\n");

	return ret;
}

static int atk_dpi_remove(struct platform_device *pdev)
{
	struct atk_dpi *dpi = platform_get_drvdata(pdev);

	component_del(&pdev->dev, &atk_dpi_component_ops);
	pwm_disable(dpi->backlight_pwm);
	clk_disable_unprepare(dpi->pclk);

	return 0;
}

static const struct of_device_id atk_dpi_of_match[] = {
	{ .compatible = "atk,atk_dpi" },
	{ }
};
MODULE_DEVICE_TABLE(of, atk_dpi_of_match);

static struct platform_driver atk_dpi_driver = {
	.probe = atk_dpi_probe,
	.remove = atk_dpi_remove,
	.driver = {
		.name = "atk-dpi",
		.of_match_table = atk_dpi_of_match,
	},
};
module_platform_driver(atk_dpi_driver);

MODULE_AUTHOR("CX <saurabhs@xilinx.com>");
MODULE_DESCRIPTION("Alientek FPGA LCd Tx Driver");
MODULE_LICENSE("GPL v2");
