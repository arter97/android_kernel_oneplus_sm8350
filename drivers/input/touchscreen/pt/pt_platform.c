/*
 * pt_platform.c
 * Parade TrueTouch(TM) Standard Product Platform Module.
 * For use with Parade touchscreen controllers.
 * Supported parts include:
 * TMA5XX
 * TMA448
 * TMA445A
 * TT21XXX
 * TT31XXX
 * TT4XXXX
 * TT7XXX
 * TC3XXX
 *
 * Copyright (C) 2015-2020 Parade Technologies
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2, and only version 2, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Contact Parade Technologies at www.paradetech.com <ttdrivers@paradetech.com>
 */

#include "pt_regs.h"
#include <linux/pt_platform.h>
#include <linux/regulator/consumer.h>
#ifdef PT_PTSBC_SUPPORT
#include <linux/init-input.h>
#endif

#ifdef CONFIG_TOUCHSCREEN_PARADE_PLATFORM_FW_UPGRADE
/* FW for Panel ID = 0x00 */
#include "pt_fw_pid00.h"
static struct pt_touch_firmware pt_firmware_pid00 = {
	.img = pt_img_pid00,
	.size = ARRAY_SIZE(pt_img_pid00),
	.ver = pt_ver_pid00,
	.vsize = ARRAY_SIZE(pt_ver_pid00),
	.panel_id = 0x00,
};

/* FW for Panel ID = 0x01 */
#include "pt_fw_pid01.h"
static struct pt_touch_firmware pt_firmware_pid01 = {
	.img = pt_img_pid01,
	.size = ARRAY_SIZE(pt_img_pid01),
	.ver = pt_ver_pid01,
	.vsize = ARRAY_SIZE(pt_ver_pid01),
	.panel_id = 0x01,
};

/* FW for Panel ID not enabled (legacy) */
#include "pt_fw.h"
static struct pt_touch_firmware pt_firmware = {
	.img = pt_img,
	.size = ARRAY_SIZE(pt_img),
	.ver = pt_ver,
	.vsize = ARRAY_SIZE(pt_ver),
};
#else
/* FW for Panel ID not enabled (legacy) */
static struct pt_touch_firmware pt_firmware = {
	.img = NULL,
	.size = 0,
	.ver = NULL,
	.vsize = 0,
};
#endif

#ifdef CONFIG_TOUCHSCREEN_PARADE_PLATFORM_TTCONFIG_UPGRADE
/* TT Config for Panel ID = 0x00 */
#include "pt_params_pid00.h"
static struct touch_settings pt_sett_param_regs_pid00 = {
	.data = (uint8_t *)&pt_param_regs_pid00[0],
	.size = ARRAY_SIZE(pt_param_regs_pid00),
	.tag = 0,
};

static struct touch_settings pt_sett_param_size_pid00 = {
	.data = (uint8_t *)&pt_param_size_pid00[0],
	.size = ARRAY_SIZE(pt_param_size_pid00),
	.tag = 0,
};

static struct pt_touch_config pt_ttconfig_pid00 = {
	.param_regs = &pt_sett_param_regs_pid00,
	.param_size = &pt_sett_param_size_pid00,
	.fw_ver = ttconfig_fw_ver_pid00,
	.fw_vsize = ARRAY_SIZE(ttconfig_fw_ver_pid00),
	.panel_id = 0x00,
};

/* TT Config for Panel ID = 0x01 */
#include "pt_params_pid01.h"
static struct touch_settings pt_sett_param_regs_pid01 = {
	.data = (uint8_t *)&pt_param_regs_pid01[0],
	.size = ARRAY_SIZE(pt_param_regs_pid01),
	.tag = 0,
};

static struct touch_settings pt_sett_param_size_pid01 = {
	.data = (uint8_t *)&pt_param_size_pid01[0],
	.size = ARRAY_SIZE(pt_param_size_pid01),
	.tag = 0,
};

static struct pt_touch_config pt_ttconfig_pid01 = {
	.param_regs = &pt_sett_param_regs_pid01,
	.param_size = &pt_sett_param_size_pid01,
	.fw_ver = ttconfig_fw_ver_pid01,
	.fw_vsize = ARRAY_SIZE(ttconfig_fw_ver_pid01),
	.panel_id = 0x01,
};

/* TT Config for Panel ID not enabled (legacy)*/
#include "pt_params.h"
static struct touch_settings pt_sett_param_regs = {
	.data = (uint8_t *)&pt_param_regs[0],
	.size = ARRAY_SIZE(pt_param_regs),
	.tag = 0,
};

static struct touch_settings pt_sett_param_size = {
	.data = (uint8_t *)&pt_param_size[0],
	.size = ARRAY_SIZE(pt_param_size),
	.tag = 0,
};

static struct pt_touch_config pt_ttconfig = {
	.param_regs = &pt_sett_param_regs,
	.param_size = &pt_sett_param_size,
	.fw_ver = ttconfig_fw_ver,
	.fw_vsize = ARRAY_SIZE(ttconfig_fw_ver),
};
#else
/* TT Config for Panel ID not enabled (legacy)*/
static struct pt_touch_config pt_ttconfig = {
	.param_regs = NULL,
	.param_size = NULL,
	.fw_ver = NULL,
	.fw_vsize = 0,
};
#endif

static struct pt_touch_firmware *pt_firmwares[] = {
#ifdef CONFIG_TOUCHSCREEN_PARADE_PLATFORM_FW_UPGRADE
	&pt_firmware_pid00,
	&pt_firmware_pid01,
#endif
	NULL, /* Last item should always be NULL */
};

static struct pt_touch_config *pt_ttconfigs[] = {
#ifdef CONFIG_TOUCHSCREEN_PARADE_PLATFORM_TTCONFIG_UPGRADE
	&pt_ttconfig_pid00,
	&pt_ttconfig_pid01,
#endif
	NULL, /* Last item should always be NULL */
};

struct pt_loader_platform_data _pt_loader_platform_data = {
	.fw = &pt_firmware,
	.ttconfig = &pt_ttconfig,
	.fws = pt_firmwares,
	.ttconfigs = pt_ttconfigs,
	.flags = PT_LOADER_FLAG_NONE,
};

/*******************************************************************************
 * FUNCTION: pt_xres
 *
 * SUMMARY: Toggles the reset gpio (TP_XRES) to perform a HW reset
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 ******************************************************************************/
int pt_xres(struct pt_core_platform_data *pdata,
		struct device *dev)
{
	int rst_gpio = pdata->rst_gpio;
	int rc = 0;
	int ddi_rst_gpio = pdata->ddi_rst_gpio;

	pt_debug(dev, DL_WARN, "%s: 20ms HARD RESET on gpio=%d\n",
		__func__, pdata->rst_gpio);

	/* Toggling only TP_XRES as DDI_XRES resets the entire part */
	gpio_set_value(rst_gpio, 1);
	if (ddi_rst_gpio)
		gpio_set_value(ddi_rst_gpio, 1);
	usleep_range(3000, 4000);
	gpio_set_value(rst_gpio, 0);
	usleep_range(6000, 7000);
	gpio_set_value(rst_gpio, 1);
	if (ddi_rst_gpio)
		gpio_set_value(ddi_rst_gpio, 1);

	/* Sleep to allow the DUT to boot */
	usleep_range(3000, 4000);
	return rc;
}

#ifdef PT_PINCTRL_EN
/*******************************************************************************
 * FUNCTION: pt_pinctrl_init
 *
 * SUMMARY: Pinctrl method to obtain pin state handler for TP_RST, IRQ
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_pinctrl_init(struct pt_core_platform_data *pdata,
			   struct device *dev)
{
	int ret = 0;

	pdata->pinctrl = devm_pinctrl_get(dev);
	if (IS_ERR_OR_NULL(pdata->pinctrl)) {
		pt_debug(dev, DL_ERROR,
			 "Failed to get pinctrl, please check dts");
		ret = PTR_ERR(pdata->pinctrl);
		goto err_pinctrl_get;
	}

	pdata->pins_active =
	    pinctrl_lookup_state(pdata->pinctrl, "pmx_ts_active");
	if (IS_ERR_OR_NULL(pdata->pins_active)) {
		pt_debug(dev, DL_ERROR, "pmx_ts_active not found");
		ret = PTR_ERR(pdata->pins_active);
		goto err_pinctrl_lookup;
	}

	pdata->pins_suspend =
	    pinctrl_lookup_state(pdata->pinctrl, "pmx_ts_suspend");
	if (IS_ERR_OR_NULL(pdata->pins_suspend)) {
		pt_debug(dev, DL_ERROR, "pmx_ts_suspend not found");
		ret = PTR_ERR(pdata->pins_suspend);
		goto err_pinctrl_lookup;
	}

	pdata->pins_release =
	    pinctrl_lookup_state(pdata->pinctrl, "pmx_ts_release");
	if (IS_ERR_OR_NULL(pdata->pins_release)) {
		pt_debug(dev, DL_ERROR, "pmx_ts_release not found");
		ret = PTR_ERR(pdata->pins_release);
		goto err_pinctrl_lookup;
	}

	return 0;

err_pinctrl_lookup:
	devm_pinctrl_put(pdata->pinctrl);

err_pinctrl_get:
	pdata->pinctrl = NULL;
	pdata->pins_release = NULL;
	pdata->pins_suspend = NULL;
	pdata->pins_active = NULL;
	return ret;
}

/*******************************************************************************
 * FUNCTION: pt_pinctrl_select_normal
 *
 * SUMMARY: Pinctrl method to configure drive mode for TP_RST, IRQ - normal
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_pinctrl_select_normal(struct pt_core_platform_data *pdata,
				    struct device *dev)
{
	int ret = 0;

	if (pdata->pinctrl && pdata->pins_active) {
		ret = pinctrl_select_state(pdata->pinctrl, pdata->pins_active);
		if (ret < 0) {
			pt_debug(dev, DL_ERROR, "Set normal pin state error=%d",
				 ret);
		}
	}

	return ret;
}

/*******************************************************************************
 * FUNCTION: pt_pinctrl_select_suspend
 *
 * SUMMARY:  Pinctrl method to configure drive mode for TP_RST, IRQ - suspend
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_pinctrl_select_suspend(struct pt_core_platform_data *pdata,
				     struct device *dev)
{
	int ret = 0;

	if (pdata->pinctrl && pdata->pins_suspend) {
		ret = pinctrl_select_state(pdata->pinctrl, pdata->pins_suspend);
		if (ret < 0) {
			pt_debug(dev, DL_ERROR,
				 "Set suspend pin state error=%d", ret);
		}
	}

	return ret;
}

/*******************************************************************************
 * FUNCTION: pt_pinctrl_select_release
 *
 * SUMMARY:  Pinctrl method to configure drive mode for TP_RST, IRQ - release
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_pinctrl_select_release(struct pt_core_platform_data *pdata,
				     struct device *dev)
{
	int ret = 0;

	if (pdata->pinctrl) {
		if (IS_ERR_OR_NULL(pdata->pins_release)) {
			devm_pinctrl_put(pdata->pinctrl);
			pdata->pinctrl = NULL;
		} else {
			ret = pinctrl_select_state(pdata->pinctrl,
						   pdata->pins_release);
			if (ret < 0)
				pt_debug(dev, DL_ERROR,
					 "Set gesture pin state error=%d", ret);
		}
	}

	return ret;
}
#endif /* PT_PINCTRL_EN */

#ifdef PT_REGULATOR_EN
#define PT_VCC_MIN_UV (2800000)
#define PT_VCC_MAX_UV (3300000)
#define PT_VDDI_MIN_UV (1800000)
#define PT_VDDI_MAX_UV (1900000)
/*******************************************************************************
 * FUNCTION: pt_regulator_init
 *
 * SUMMARY: With regulator framework to get regulator handle and setup voltage
 *  level.
 *
 * NOTE: The function only contains setup for VCC and VDDI since AVDD AVEE is
 *  usually used by TDDI products while the power is setup on other driver.
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_regulator_init(struct pt_core_platform_data *pdata,
			     struct device *dev)
{
	int rc = 0;

	pdata->vcc = devm_regulator_get(dev, "vcc");
	if (IS_ERR(pdata->vcc)) {
		rc = PTR_ERR(pdata->vcc);
		pt_debug(dev, DL_ERROR, "get vcc regulator failed,rc=%d", rc);
		return rc;
	}

	if (regulator_count_voltages(pdata->vcc) > 0) {
		rc = regulator_set_voltage(pdata->vcc, PT_VCC_MIN_UV,
			      PT_VCC_MAX_UV);
		if (rc) {
			pt_debug(dev, DL_ERROR,
				 "set vcc regulator failed rc=%d", rc);
			goto error_set_vcc;
		}
	}

	pdata->vddi = devm_regulator_get(dev, "vddi");
	if (IS_ERR(pdata->vddi)) {
		rc = PTR_ERR(pdata->vddi);
		pt_debug(dev, DL_ERROR, "get vddi regulator failed,rc=%d", rc);
		goto error_get_vcc;
	}

	if (regulator_count_voltages(pdata->vddi) > 0) {
		rc = regulator_set_voltage(pdata->vddi, PT_VDDI_MIN_UV,
					   PT_VDDI_MAX_UV);
		if (rc) {
			pt_debug(dev, DL_ERROR,
				 "set vddi regulator failed rc=%d", rc);
			goto error_set_vddi;
		}
	}

	return 0;

error_set_vddi:
	devm_regulator_put(pdata->vddi);
error_get_vcc:
	/*
	 * regulator_set_voltage always select minimum legal voltage between
	 * min_uV and max_uV. To set the minuV to 0 means to restore the default
	 * value of regulator. Since regulator_set_voltage is the part to
	 * release regulator source, it's not necessary to check the returned
	 * value of it.
	 */
	if (regulator_count_voltages(pdata->vcc) > 0)
		regulator_set_voltage(pdata->vcc, 0, PT_VCC_MAX_UV);
error_set_vcc:
	devm_regulator_put(pdata->vcc);

	return rc;
}

/*******************************************************************************
 * FUNCTION: pt_setup_power_by_regulator
 *
 * SUMMARY:  With regulator framework to set power on/off.
 *
 * NOTE: The function only contains setup for VCC and VDDI since AVDD AVEE is
 *  usually used by TDDI products while the power is setup on other driver.
 *
 * NOTE: The timing sequence is the EXAMPLE ONLY for TT7XXX:
 *  power up order   : VDDI, VCC
 *  power down order : VCC, VDDI
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *   on    - flag to decide power state,PT_MT_POWER_ON/PT_MT_POWER_OFF
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_setup_power_by_regulator(struct pt_core_platform_data *pdata,
				       int on, struct device *dev)
{
	int rc = 0;

	if (IS_ERR(pdata->vddi) || IS_ERR(pdata->vcc)) {
		pt_debug(dev, DL_ERROR, "vddi or vcc is not valid\n");
		return -EINVAL;
	}

	if (on == PT_MT_POWER_ON) {
		rc = regulator_enable(pdata->vddi);
		if (rc) {
			pt_debug(dev, DL_ERROR,
				 "enable vddi regulator failed,rc=%d", rc);
		}
		/* Ensure the power goes stable */
		usleep_range(3000, 4000);

		rc = regulator_enable(pdata->vcc);
		if (rc) {
			pt_debug(dev, DL_ERROR,
				 "enable vcc regulator failed,rc=%d", rc);
		}
		/* Ensure the power goes stable */
		usleep_range(3000, 4000);
	} else {
		rc = regulator_disable(pdata->vcc);
		if (rc) {
			pt_debug(dev, DL_ERROR,
				 "disable vcc regulator failed,rc=%d", rc);
		}
		rc = regulator_disable(pdata->vddi);
		if (rc) {
			pt_debug(dev, DL_ERROR,
				 "disable vddi regulator failed,rc=%d", rc);
		}
		/* Ensure the power ramp down completely */
		usleep_range(10000, 12000);
	}

	return rc;
}

/*******************************************************************************
 * FUNCTION: pt_regulator_release
 *
 * SUMMARY: With regulator framework to release regulator resource
 *
 * NOTE: The regulator MUST be disabled before this call.
 * NOTE: The function only contains setup for VCC and VDDI since AVDD AVEE is
 *  usually used by TDDI products while the power is setup on other driver.
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_regulator_release(struct pt_core_platform_data *pdata,
				struct device *dev)
{
	if (IS_ERR(pdata->vddi) || IS_ERR(pdata->vcc))
		return -EINVAL;

	/*
	 * regulator_set_voltage always select minimum legal voltage between
	 * min_uV and max_uV. To set the minuV to 0 means to restore the default
	 * value of regulator. Since regulator_set_voltage is the part to
	 * release regulator source, it's not necessary to check the returned
	 * value of it.
	 */
	if (regulator_count_voltages(pdata->vddi) > 0)
		regulator_set_voltage(pdata->vddi, 0, PT_VDDI_MAX_UV);
	devm_regulator_put(pdata->vddi);

	if (regulator_count_voltages(pdata->vcc) > 0)
		regulator_set_voltage(pdata->vcc, 0, PT_VCC_MAX_UV);
	devm_regulator_put(pdata->vcc);

	return 0;
}
#endif /* PT_REGULATOR_EN */
/*******************************************************************************
 * FUNCTION: pt_init
 *
 * SUMMARY: Set up/free gpios for TP_RST, IRQ, DDI_RST.
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *   on    - flag to set up or free gpios(0:free; !0:set up)
 *  *dev   - pointer to Device structure
 ******************************************************************************/
int pt_init(struct pt_core_platform_data *pdata,
		int on, struct device *dev)
{
	int rst_gpio     = pdata->rst_gpio;
	int irq_gpio     = pdata->irq_gpio;
	int ddi_rst_gpio = pdata->ddi_rst_gpio;
	int rc = 0;

	if (on) {
#ifdef PT_PINCTRL_EN
		rc = pt_pinctrl_init(pdata, dev);
		if (!rc) {
			pt_pinctrl_select_normal(pdata, dev);
		} else {
			pt_debug(dev, DL_ERROR,
				 "%s: Failed to request pinctrl\n", __func__);
		}
#endif
#ifdef PT_REGULATOR_EN
		rc = pt_regulator_init(pdata, dev);
		if (rc) {
			pt_debug(dev, DL_ERROR,
				 "%s: Failed requesting regulator rc=%d",
				 __func__, rc);
		}
#endif
	}

	if (on && rst_gpio) {
		/* Configure RST GPIO */
		pt_debug(dev, DL_WARN, "%s: Request RST GPIO %d",
			__func__, rst_gpio);
		rc = gpio_request(rst_gpio, NULL);
		if (rc < 0) {
			gpio_free(rst_gpio);
			rc = gpio_request(rst_gpio, NULL);
		}
		if (rc < 0) {
			pt_debug(dev, DL_ERROR,
				"%s: Failed requesting RST GPIO %d\n",
				__func__, rst_gpio);
			goto fail_rst_gpio;
		} else {
			/*
			 * Set the GPIO direction and the starting level
			 * The start level is high because the DUT needs
			 * to stay in reset during power up.
			 */
			rc = gpio_direction_output(rst_gpio, 1);
			if (rc < 0) {
				pt_debug(dev, DL_ERROR,
					"%s: Output Setup ERROR: RST GPIO %d\n",
					__func__, rst_gpio);
				goto fail_rst_gpio;
			}
		}
	}

	if (on && irq_gpio) {
		/* Configure IRQ GPIO */
		pt_debug(dev, DL_WARN, "%s: Request IRQ GPIO %d",
			__func__, irq_gpio);
		rc = gpio_request(irq_gpio, NULL);
		if (rc < 0) {
			gpio_free(irq_gpio);
			rc = gpio_request(irq_gpio, NULL);
		}
		if (rc < 0) {
			pt_debug(dev, DL_ERROR,
				"%s: Failed requesting IRQ GPIO %d\n",
				__func__, irq_gpio);
			goto fail_irq_gpio;
		} else {
			/* Set the GPIO direction */
			rc = gpio_direction_input(irq_gpio);
			if (rc < 0) {
				pt_debug(dev, DL_ERROR,
					"%s: Input Setup ERROR: IRQ GPIO %d\n",
					__func__, irq_gpio);
				goto fail_irq_gpio;
			}
		}
	}

	if (on && ddi_rst_gpio) {
		/* Configure DDI RST GPIO */
		pt_debug(dev, DL_WARN, "%s: Request DDI RST GPIO %d",
			__func__, ddi_rst_gpio);
		rc = gpio_request(ddi_rst_gpio, NULL);
		if (rc < 0) {
			gpio_free(ddi_rst_gpio);
			rc = gpio_request(ddi_rst_gpio, NULL);
		}
		if (rc < 0) {
			pt_debug(dev, DL_ERROR,
				"%s: Failed requesting DDI RST GPIO %d\n",
				__func__, ddi_rst_gpio);
			goto fail_ddi_rst_gpio;
		} else {
			/* Set the GPIO direction and the starting level */
			rc = gpio_direction_output(ddi_rst_gpio, 0);
			if (rc < 0) {
				pt_debug(dev, DL_ERROR,
					"%s: Output Setup ERROR: RST GPIO %d\n",
					__func__, ddi_rst_gpio);
				goto fail_ddi_rst_gpio;
			}
		}
	}

	if (!on) {
		/* "on" not set, therefore free all gpio's */
		if (ddi_rst_gpio)
			gpio_free(ddi_rst_gpio);
		if (irq_gpio)
			gpio_free(irq_gpio);
		if (rst_gpio)
			gpio_free(rst_gpio);
#ifdef PT_REGULATOR_EN
		pt_regulator_release(pdata, dev);
#endif
#ifdef PT_PINCTRL_EN
		pt_pinctrl_select_release(pdata, dev);
#endif
	}

	/* All GPIO's created successfully */
	goto success;

fail_ddi_rst_gpio:
	pt_debug(dev, DL_ERROR,
		"%s: ERROR - GPIO setup Failure, freeing DDI_XRES GPIO %d\n",
		__func__, ddi_rst_gpio);
	gpio_free(ddi_rst_gpio);
fail_irq_gpio:
	pt_debug(dev, DL_ERROR,
		"%s: ERROR - GPIO setup Failure, freeing IRQ GPIO %d\n",
		__func__, irq_gpio);
	gpio_free(irq_gpio);
fail_rst_gpio:
	pt_debug(dev, DL_ERROR,
		"%s: ERROR - GPIO setup Failure, freeing TP_XRES GPIO %d\n",
		__func__, rst_gpio);
	gpio_free(rst_gpio);

success:
	pt_debug(dev, DL_INFO,
		"%s: SUCCESS - Configured DDI_XRES GPIO %d, IRQ GPIO %d, TP_XRES GPIO %d\n",
		__func__, ddi_rst_gpio, irq_gpio, rst_gpio);
	return rc;
}

/*******************************************************************************
 * FUNCTION: pt_irq_stat
 *
 * SUMMARY: Obtain the level state of IRQ gpio.
 *
 * RETURN:
 *	 level state of IRQ gpio
 *
 * PARAMETERS:
 *  *pdata       - pointer to the platform data structure
 *  *dev         - pointer to Device structure
 ******************************************************************************/
int pt_irq_stat(struct pt_core_platform_data *pdata,
		struct device *dev)
{
	return gpio_get_value(pdata->irq_gpio);
}

#ifdef PT_DETECT_HW
/*******************************************************************************
 * FUNCTION: pt_detect
 *
 * SUMMARY: Detect the I2C device by reading one byte(FW sentiel) after the
 *  reset operation.
 *
 * RETURN:
 *	 0 - detected
 *  !0 - undetected
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *  *dev   - pointer to Device structure
 *   read  - pointer to the function to perform a read operation
 ******************************************************************************/
int pt_detect(struct pt_core_platform_data *pdata,
		struct device *dev, pt_platform_read read)
{
	int retry = 3;
	int rc;
	char buf[1];

	while (retry--) {
		/* Perform reset, wait for 100 ms and perform read */
		pt_debug(dev, DL_WARN, "%s: Performing a reset\n",
			__func__);
		pdata->xres(pdata, dev);
		msleep(100);
		rc = read(dev, buf, 1);
		if (!rc)
			return 0;

		pt_debug(dev, DL_ERROR, "%s: Read unsuccessful, try=%d\n",
			__func__, 3 - retry);
	}

	return rc;
}
#endif

#ifndef PT_REGULATOR_EN
/*******************************************************************************
 * FUNCTION: pt_setup_power_by_gpio
 *
 * SUMMARY:  With GPIOs to control LDO directly to set power on/off.
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata - pointer to the platform data structure
 *   on    - flag to decide power state,PT_MT_POWER_ON/PT_MT_POWER_OFF
 *  *dev   - pointer to Device structure
 ******************************************************************************/
static int pt_setup_power_by_gpio(struct pt_core_platform_data *pdata,
				       int on, struct device *dev)
{
	int rc = 0;
	int en_vcc  = pdata->vcc_gpio;
	int en_vddi = pdata->vddi_gpio;
	int en_avdd = pdata->avdd_gpio;
	int en_avee = pdata->avee_gpio;

	if (on == PT_MT_POWER_ON) {
		/*
		 * Enable GPIOs to turn on voltage regulators to pwr up DUT
		 * - TC device power up order: VDDI, VCC, AVDD, AVEE
		 * - TT device power up order: VDDI, VCC
		 * NOTE: VDDI must be stable for >10ms before XRES is released
		 */
		pt_debug(dev, DL_INFO,
		"%s: Enable defined pwr: VDDI, VCC, AVDD, AVEE\n", __func__);

		/* Turn on VDDI [Digital Interface] (+1.8v) */
		if (pdata->vddi_gpio) {
			rc = gpio_request(en_vddi, NULL);
			if (rc < 0) {
				gpio_free(en_vddi);
				rc = gpio_request(en_vddi, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting VDDI GPIO %d\n",
					__func__, en_vddi);
			}
			rc = gpio_direction_output(en_vddi, 1);
			if (rc)
				pr_err("%s: setcfg for VDDI GPIO %d failed\n",
					__func__, en_vddi);
			gpio_free(en_vddi);
			usleep_range(3000, 4000);
		}

		/* Turn on VCC */
		if (pdata->vcc_gpio) {
			rc = gpio_request(en_vcc, NULL);
			if (rc < 0) {
				gpio_free(en_vcc);
				rc = gpio_request(en_vcc, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting VCC GPIO %d\n",
					__func__, en_vcc);
			}
			rc = gpio_direction_output(en_vcc, 1);
			if (rc)
				pr_err("%s: setcfg for VCC GPIO %d failed\n",
					__func__, en_vcc);
			gpio_free(en_vcc);
			usleep_range(3000, 4000);
		}

		/* Turn on AVDD (+5.0v) */
		if (pdata->avdd_gpio) {
			rc = gpio_request(en_avdd, NULL);
			if (rc < 0) {
				gpio_free(en_avdd);
				rc = gpio_request(en_avdd, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting AVDD GPIO %d\n",
					__func__, en_avdd);
			}
			rc = gpio_direction_output(en_avdd, 1);
			if (rc)
				pr_err("%s: setcfg for AVDD GPIO %d failed\n",
					__func__, en_avdd);
			gpio_free(en_avdd);
			usleep_range(3000, 4000);
		}

		/* Turn on AVEE (-5.0v) */
		if (pdata->avee_gpio) {
			rc = gpio_request(en_avee, NULL);
			if (rc < 0) {
				gpio_free(en_avee);
				rc = gpio_request(en_avee, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting AVEE GPIO %d\n",
					__func__, en_avee);
			}
			rc = gpio_direction_output(en_avee, 1);
			if (rc)
				pr_err("%s: setcfg for AVEE GPIO %d failed\n",
					__func__, en_avee);
			gpio_free(en_avee);
			usleep_range(3000, 4000);
		}
	} else {
		/*
		 * Disable GPIOs to turn off voltage regulators to pwr down
		 * TC device The power down order is: AVEE, AVDD, VDDI
		 * TT device The power down order is: VCC, VDDI
		 *
		 * Note:Turn off some of regulators may effect display
		 * parts for TDDI chip
		 */
		pt_debug(dev, DL_INFO,
		"%s: Turn off defined pwr: VCC, AVEE, AVDD, VDDI\n", __func__);

		/* Turn off VCC */
		if (pdata->vcc_gpio) {
			rc = gpio_request(en_vcc, NULL);
			if (rc < 0) {
				gpio_free(en_vcc);
				rc = gpio_request(en_vcc, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting VCC GPIO %d\n",
					__func__, en_vcc);
			}
			rc = gpio_direction_output(en_vcc, 0);
			if (rc)
				pr_err("%s: setcfg for VCC GPIO %d failed\n",
					__func__, en_vcc);
			gpio_free(en_vcc);
		}

		/* Turn off AVEE (-5.0v) */
		if (pdata->avee_gpio) {
			rc = gpio_request(en_avee, NULL);
			if (rc < 0) {
				gpio_free(en_avee);
				rc = gpio_request(en_avee, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting AVEE GPIO %d\n",
					__func__, en_avee);
			}
			rc = gpio_direction_output(en_avee, 0);
			if (rc)
				pr_err("%s: setcfg for AVEE GPIO %d failed\n",
					__func__, en_avee);
			gpio_free(en_avee);
		}

		/* Turn off AVDD (+5.0v) */
		if (pdata->avdd_gpio) {
			rc = gpio_request(en_avdd, NULL);
			if (rc < 0) {
				gpio_free(en_avdd);
				rc = gpio_request(en_avdd, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting AVDD GPIO %d\n",
					__func__, en_avdd);
			}
			rc = gpio_direction_output(en_avdd, 0);
			if (rc)
				pr_err("%s: setcfg for AVDD GPIO %d failed\n",
					__func__, en_avdd);
			gpio_free(en_avdd);
		}

		/* Turn off VDDI [Digital Interface] (+1.8v) */
		if (pdata->vddi_gpio) {
			rc = gpio_request(en_vddi, NULL);
			if (rc < 0) {
				gpio_free(en_vddi);
				rc = gpio_request(en_vddi, NULL);
			}
			if (rc < 0) {
				pr_err("%s: Failed requesting VDDI GPIO %d\n",
					__func__, en_vddi);
			}
			rc = gpio_direction_output(en_vddi, 0);
			if (rc)
				pr_err("%s: setcfg for VDDI GPIO %d failed\n",
					__func__, en_vddi);
			gpio_free(en_vddi);
			usleep_range(10000, 12000);
		}
	}

	return rc;
}
#endif /* PT_REGULATOR_EN */
/*******************************************************************************
 * FUNCTION: pt_setup_power
 *
 * SUMMARY: Turn on/turn off voltage regulator
 *
 * RETURN:
 *	 0 = success
 *	!0 = failure
 *
 * PARAMETERS:
 *  *pdata - pointer to  core platform data
 *   on     - flag to decide power state,PT_MT_POWER_ON/PT_MT_POWER_OFF
 *  *dev   - pointer to device
 ******************************************************************************/
int pt_setup_power(struct pt_core_platform_data *pdata, int on,
		struct device *dev)
{
	int rc = 0;

	/*
	 * For TDDI parts, force part into RESET by holding DDI XRES
	 * while powering it up
	 */
	if (pdata->ddi_rst_gpio)
		gpio_set_value(pdata->ddi_rst_gpio, 0);

	/*
	 * Force part into RESET by holding XRES#(TP_XRES)
	 * while powering it up
	 */
	if (pdata->rst_gpio)
		gpio_set_value(pdata->rst_gpio, 0);

#ifdef PT_REGULATOR_EN
	rc = pt_setup_power_by_regulator(pdata, on, dev);
	if (rc) {
		pt_debug(dev, DL_ERROR,
			 "%s: Failed setup power by regulator rc=%d",
			 __func__, rc);
	}
#else /* PT_REGULATOR_EN */
	rc = pt_setup_power_by_gpio(pdata, on, dev);
	if (rc) {
		pt_debug(dev, DL_ERROR,
			 "%s: Failed setup power by gpio rc=%d",
			 __func__, rc);
	}
#endif /* PT_REGULATOR_EN */

	/* Force part out of RESET by releasing XRES#(TP_XRES) */
	if (pdata->rst_gpio)
		gpio_set_value(pdata->rst_gpio, 1);

	/* Force part out of RESET by releasing DDI XRES */
	if (pdata->ddi_rst_gpio)
		gpio_set_value(pdata->ddi_rst_gpio, 1);

	return rc;
}

/*******************************************************************************
 * FUNCTION: pt_wakeup
 *
 * SUMMARY: Resume power for "power on/off" sleep strategy which against to
 *  "deepsleep" strategy.
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata       - pointer to the platform data structure
 *  *dev         - pointer to Device structure
 *  *ignore_irq  - pointer to atomic structure to allow the host ignoring false
 *                 IRQ during power up
 ******************************************************************************/
static int pt_wakeup(struct pt_core_platform_data *pdata,
		struct device *dev, atomic_t *ignore_irq)
{
	int rc = 0;

#ifdef PT_PINCTRL_EN
	pt_pinctrl_select_normal(pdata, dev);
#endif
	rc = pt_setup_power(pdata, PT_MT_POWER_ON, dev);
	if (rc)
		pt_debug(dev, DL_ERROR, "%s: Failed setup power\n", __func__);

	return rc;
}

/*******************************************************************************
 * FUNCTION: pt_sleep
 *
 * SUMMARY: Suspend power for "power on/off" sleep strategy which against to
 *  "deepsleep" strategy.
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata       - pointer to the platform data structure
 *  *dev         - pointer to Device structure
 *  *ignore_irq  - pointer to atomic structure to allow the host ignoring false
 *                 IRQ during power down
 ******************************************************************************/
static int pt_sleep(struct pt_core_platform_data *pdata,
		struct device *dev, atomic_t *ignore_irq)
{
	int rc = 0;

	rc = pt_setup_power(pdata, PT_MT_POWER_OFF, dev);
	if (rc)
		pt_debug(dev, DL_ERROR, "%s: Failed setup power\n", __func__);

#ifdef PT_PINCTRL_EN
	pt_pinctrl_select_suspend(pdata, dev);
#endif
	return rc;
}

/*******************************************************************************
 * FUNCTION: pt_power
 *
 * SUMMARY: Wrapper function to resume/suspend power with function
 *  pt_wakeup()/pt_sleep().
 *
 * RETURN:
 *	 0 = success
 *	!0 = fail
 *
 * PARAMETERS:
 *  *pdata       - pointer to the platform data structure
 *   on          - flag to remsume/suspend power(0:resume; 1:suspend)
 *  *dev         - pointer to Device structure
 *  *ignore_irq  - pointer to atomic structure to allow the host ignoring false
 *                 IRQ during power up/down
 ******************************************************************************/
int pt_power(struct pt_core_platform_data *pdata,
		int on, struct device *dev, atomic_t *ignore_irq)
{
	if (on)
		return pt_wakeup(pdata, dev, ignore_irq);

	return pt_sleep(pdata, dev, ignore_irq);
}

#ifdef PT_PTSBC_SUPPORT

static struct workqueue_struct *parade_wq;
static u32 int_handle;

/*******************************************************************************
 * FUNCTION: pt_irq_work_function
 *
 * SUMMARY: Work function for queued IRQ activity
 *
 * RETURN: Void
 *
 * PARAMETERS:
 *	*work - pointer to work structure
 ******************************************************************************/
static void pt_irq_work_function(struct work_struct *work)
{
	struct pt_core_data *cd = container_of(work,
			struct pt_core_data, irq_work);

	pt_irq(cd->irq, (void *)cd);
}

/*******************************************************************************
 * FUNCTION: pt_irq_wrapper
 *
 * SUMMARY: Wrapper function for IRQ to queue the irq_work function
 *
 * RETURN:
 *	 0 = success
 *	!0 = failure
 *
 * PARAMETERS:
 *	*handle - void pointer to contain the core_data pointer
 ******************************************************************************/
peint_handle *pt_irq_wrapper(void *handle)
{
	struct pt_core_data *cd = (struct pt_core_data *)handle;

	queue_work(parade_wq, &cd->irq_work);
	return 0;
}
#endif /* PT_PTSBC_SUPPORT */

/*******************************************************************************
 * FUNCTION: pt_setup_irq
 *
 * SUMMARY: Configure the IRQ GPIO used by the TT DUT
 *
 * RETURN:
 *	 0 = success
 *	!0 = failure
 *
 * PARAMETERS:
 *	*pdata - pointer to core platform data
 *	 on    - flag to setup interrupt process work(PT_MT_IRQ_FREE/)
 *	*dev   - pointer to device
 ******************************************************************************/
int pt_setup_irq(struct pt_core_platform_data *pdata, int on,
	struct device *dev)
{
	struct pt_core_data *cd = dev_get_drvdata(dev);
	unsigned long irq_flags;
	int rc = 0;

	if (on == PT_MT_IRQ_REG) {
		/*
		 * When TTDL has direct access to the GPIO the irq_stat function
		 * will be defined and the gpio_to_irq conversion must be
		 * performed. e.g. For CHROMEOS this is not the case, the irq is
		 * passed in directly.
		 */
		if (pdata->irq_stat) {
			/* Initialize IRQ */
			dev_vdbg(dev, "%s: Value Passed to gpio_to_irq =%d\n",
				__func__, pdata->irq_gpio);
			cd->irq = gpio_to_irq(pdata->irq_gpio);
			dev_vdbg(dev,
				"%s: Value Returned from gpio_to_irq =%d\n",
				__func__, cd->irq);
		}
		if (cd->irq < 0)
			return -EINVAL;

		cd->irq_enabled = true;

		pt_debug(dev, DL_INFO, "%s: initialize threaded irq=%d\n",
			__func__, cd->irq);

		if (pdata->level_irq_udelay > 0)
#ifdef PT_PTSBC_SUPPORT
			/* use level triggered interrupts */
			irq_flags = TRIG_LEVL_LOW;
		else
			/* use edge triggered interrupts */
			irq_flags = TRIG_EDGE_NEGATIVE;
#else
			/* use level triggered interrupts */
			irq_flags = IRQF_TRIGGER_LOW | IRQF_ONESHOT;
		else
			/* use edge triggered interrupts */
			irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
#endif /* PT_PTSBC_SUPPORT */

#ifdef PT_PTSBC_SUPPORT
		/* Adding new work queue to cd struct */
		INIT_WORK(&cd->irq_work, pt_irq_work_function);

		parade_wq = create_singlethread_workqueue("parade_wq");
		if (!parade_wq)
			pt_debug(dev, DL_ERROR, "%s Create workqueue failed.\n",
				__func__);

		int_handle = sw_gpio_irq_request(pdata->irq_gpio, irq_flags,
				(peint_handle)pt_irq_wrapper, cd);
		if (!int_handle) {
			pt_debug(dev, DL_ERROR,
				"%s: PARADE could not request irq\n", __func__);
			rc = -1;
		} else {
			rc = 0;
			/* clk=0: 32Khz; clk=1: 24Mhz*/
			ctp_set_int_port_rate(pdata->irq_gpio, 1);
			/*
			 * Debounce INT Line by clock divider: 2^n. E.g. The
			 * para:0x03 means the period of interrupt controller is
			 * 0.33 us = (2^3)/24. It has ability to measure the
			 * high/low width of the pulse bigger than 1 us.
			 */
			ctp_set_int_port_deb(pdata->irq_gpio, 0x03);
			pt_debug(cd->dev, DL_INFO,
				"%s: Parade sw_gpio_irq_request SUCCESS\n",
				__func__);
		}
#else
		rc = request_threaded_irq(cd->irq, NULL, pt_irq,
			irq_flags, dev_name(dev), cd);
		if (rc < 0)
			pt_debug(dev, DL_ERROR,
				"%s: Error, could not request irq\n", __func__);
#endif /* PT_PTSBC_SUPPORT */
	} else {
		disable_irq_nosync(cd->irq);
#ifndef PT_PTSBC_SUPPORT
		free_irq(cd->irq, cd);
#else
		sw_gpio_irq_free(int_handle);
		cancel_work_sync(&cd->irq_work);
		destroy_workqueue(parade_wq);
#endif /* PT_PTSBC_SUPPORT */
	}
	return rc;
}
