// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2021, The Linux Foundation. All rights reserved. */

#define pr_fmt(fmt) "max20411-reg: %s: " fmt, __func__

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/of_platform.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/proxy-consumer.h>
#include <linux/gpio/consumer.h>

#define MAX20411_UV_STEP		6250
#define MAX20411_BASE_UV		243750
#define MAX20411_MIN_UV			500000
#define MAX20411_MAX_UV			1275000
#define MAX20411_VID_OFFSET		0x7
#define MAX20411_VID_MASK		0xFF
#define MAX20411_LINEAR_MIN_SEL		41
#define MAX20411_SLEW_OFFSET		0x6
#define MAX20411_SLEW_DVS_MASK		0xC
#define MAX20411_SLEW_DVS_SHIFT		0x2
#define MAX20411_SLEW_SR_MASK		0x3
#define MAX20411_ETRSLLEN_OFFSET	0xE

struct max20411_vreg {
	struct device		*dev;
	struct device_node	*of_node;
	struct regulator_desc	rdesc;
	struct regulator_dev	*rdev;
	struct regmap		*regmap;
};

unsigned int max20411_slew_rates[] = { 13100, 6600, 3300, 1600 };

static int max20411_enable_time(struct regulator_dev *rdev)
{
	int voltage, rate, ret;
	unsigned int val;

	/* get voltage */
	ret = regmap_read(rdev->regmap, rdev->desc->vsel_reg, &val);
	if (ret)
		return ret;

	val &= rdev->desc->vsel_mask;
	voltage = regulator_list_voltage_linear(rdev, val);

	/* get rate */
	ret = regmap_read(rdev->regmap, MAX20411_SLEW_OFFSET, &val);
	if (ret)
		return ret;

	val = (val & MAX20411_SLEW_SR_MASK);
	rate = max20411_slew_rates[val];

	return DIV_ROUND_UP(voltage, rate);
}

static const struct regmap_config max20411_regmap_config = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= 0xE,
};

static const struct regulator_ops max20411_ops = {
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable_time		= max20411_enable_time,
};

static int max20411_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct max20411_vreg *vreg;
	struct regulator_config reg_config = {};
	struct regulator_init_data *init_data;
	struct gpio_desc *ena_gpiod;
	enum gpiod_flags gflags;
	int ret, linear_min_sel, n_voltages;
	unsigned int val;

	vreg = devm_kzalloc(&client->dev, sizeof(*vreg), GFP_KERNEL);
	if (!vreg)
		return -ENOMEM;

	vreg->regmap = devm_regmap_init_i2c(client, &max20411_regmap_config);
	if (IS_ERR(vreg->regmap)) {
		ret = PTR_ERR(vreg->regmap);
		dev_err(&client->dev, "regmap init failed, err %d\n", ret);
		return ret;
	}

	vreg->dev		= &client->dev;
	vreg->of_node		= client->dev.of_node;
	vreg->rdesc.ops		= &max20411_ops;
	vreg->rdesc.owner	= THIS_MODULE;
	vreg->rdesc.type	= REGULATOR_VOLTAGE;

	ret = of_property_read_string(vreg->of_node, "regulator-name", &vreg->rdesc.name);
	if (ret) {
		dev_err(&client->dev, "could not read regulator-name property, ret=%d\n", ret);
		return ret;
	}

	init_data = of_get_regulator_init_data(vreg->dev,
						vreg->of_node, &vreg->rdesc);
	if (init_data == NULL)
		return -ENODATA;

	/* Update ramp up delay */
	ret = regmap_read(vreg->regmap, MAX20411_SLEW_OFFSET, &val);
	if (ret)
		return ret;

	val = (val & MAX20411_SLEW_DVS_MASK) >> MAX20411_SLEW_DVS_SHIFT;
	init_data->constraints.ramp_delay = max20411_slew_rates[val];

	/* Supported range is from 0.5V to 1.275V */
	init_data->constraints.min_uV	= max(init_data->constraints.min_uV, MAX20411_MIN_UV);
	init_data->constraints.min_uV	= min(init_data->constraints.min_uV, MAX20411_MAX_UV);
	init_data->constraints.max_uV	= max(init_data->constraints.max_uV, MAX20411_MIN_UV);
	init_data->constraints.max_uV	= min(init_data->constraints.max_uV, MAX20411_MAX_UV);

	/*
	 * VOUT = 0.24375 + VID[7:0] * 6.25mV
	 * valid VID[7:0] is 41 to 165 (0.5V to 1.275V)
	 */
	linear_min_sel = MAX20411_LINEAR_MIN_SEL +
		((init_data->constraints.min_uV - MAX20411_MIN_UV) / MAX20411_UV_STEP);

	n_voltages = ((init_data->constraints.max_uV - init_data->constraints.min_uV)
			/ MAX20411_UV_STEP) + 1 + linear_min_sel;

	vreg->rdesc.uV_step		= MAX20411_UV_STEP;
	vreg->rdesc.linear_min_sel	= linear_min_sel;
	vreg->rdesc.min_uV		= init_data->constraints.min_uV;
	vreg->rdesc.n_voltages		= n_voltages;
	vreg->rdesc.vsel_reg		= MAX20411_VID_OFFSET;
	vreg->rdesc.vsel_mask		= MAX20411_VID_MASK;

	reg_config.dev			= vreg->dev;
	reg_config.init_data		= init_data;
	reg_config.of_node		= vreg->of_node;
	reg_config.driver_data		= vreg;

	if (init_data->constraints.boot_on || init_data->constraints.always_on)
		gflags = GPIOD_OUT_HIGH;
	else
		gflags = GPIOD_OUT_LOW;

	ena_gpiod = gpiod_get(vreg->dev, "enable", gflags);
	if (IS_ERR(ena_gpiod))
		return PTR_ERR(ena_gpiod);

	reg_config.ena_gpiod = ena_gpiod;

	vreg->rdev = devm_regulator_register(vreg->dev, &vreg->rdesc, &reg_config);
	if (IS_ERR(vreg->rdev)) {
		ret = PTR_ERR(vreg->rdev);
		dev_err(vreg->dev, "Failed to register regulator: %d\n", ret);
		return ret;
	}

	/* Disable Enhanced Transient Response (ETR) */
	ret = regmap_update_bits(vreg->regmap, MAX20411_ETRSLLEN_OFFSET, 0xF, 0x0);
	if (ret) {
		dev_err(vreg->dev, "Failed to disable ETR, ret=%d\n", ret);
		return ret;
	}

	ret = devm_regulator_proxy_consumer_register(vreg->dev, vreg->of_node);
	if (ret) {
		dev_err(vreg->dev, "failed to register proxy consumer, ret=%d\n",
			ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id of_max20411_match_tbl[] = {
	{ .compatible = "maxim,max20411", },
	{ },
};
MODULE_DEVICE_TABLE(of, of_max20411_match_tbl);

static const struct i2c_device_id max20411_id[] = {
	{ "max20411", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, max20411_id);

static struct i2c_driver max20411_i2c_driver = {
	.driver		= {
		.name		= "max20411",
		.of_match_table	= of_max20411_match_tbl,
		.sync_state	= regulator_proxy_consumer_sync_state,
	},
	.probe		= max20411_probe,
	.id_table	= max20411_id,
};

module_i2c_driver(max20411_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:max20411-regulator");
