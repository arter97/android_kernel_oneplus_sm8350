/*
 * pt_i2c.c
 * Parade TrueTouch(TM) Standard Product I2C Module.
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

#include <linux/i2c.h>
#include <linux/version.h>

#define PT_I2C_DATA_SIZE  (2 * 256)

#ifdef TTDL_PTVIRTDUT_SUPPORT
#define VIRT_DUT_BUF_SIZE  2048
static unsigned char pt_dut_cmd_buf[VIRT_DUT_BUF_SIZE];
static unsigned char pt_dut_out_buf[VIRT_DUT_BUF_SIZE];
static int pt_dut_cmd_len;
static int pt_dut_out_len;
DEFINE_MUTEX(virt_i2c_lock);

/*******************************************************************************
 * FUNCTION: virt_i2c_transfer
 *
 * SUMMARY: Copies the current i2c output message to the temporary buffer
 *	used by the dut_cmd sysfs node
 *
 * RETURN VALUE:
 *	Number of messages transferred which in this function will be 1
 *
 * PARAMETERS:
 *      *buf - pointer to i2c command
 *	 len - length of command in the buffer
 ******************************************************************************/
static int virt_i2c_transfer(u8 *buf, int len)
{
	int rc = 0;

	mutex_lock(&virt_i2c_lock);
	if (len <= sizeof(pt_dut_cmd_buf)) {
		memcpy(pt_dut_cmd_buf, buf, len);
		pt_dut_cmd_len = len;
		rc = 1;
	} else
		rc = 0;
	mutex_unlock(&virt_i2c_lock);

	return rc;
}

/*******************************************************************************
 * FUNCTION: virt_i2c_master_recv
 *
 * SUMMARY: Copies the i2c input message from the dut_out sysfs node into a
 *	temporary buffer.
 *
 * RETURN VALUE:
 *	Length of data transferred
 *
 * PARAMETERS:
 *      *dev  - pointer to device struct
 *      *buf  - pointer to i2c incoming report
 *       size - size to be read
 ******************************************************************************/
static int virt_i2c_master_recv(struct device *dev, u8 *buf, int size)
{
#ifndef PT_POLL_RESP_BY_BUS
	struct pt_core_data *cd = dev_get_drvdata(dev);
	int i = 0;
#endif

	mutex_lock(&virt_i2c_lock);
	memcpy(buf, pt_dut_out_buf, size);

	/* Set "empty buffer" */
	pt_dut_out_buf[1] = 0xFF;
	pt_dut_out_len = 0;
	mutex_unlock(&virt_i2c_lock);

#ifndef PT_POLL_RESP_BY_BUS
	if (cd->bl_with_no_int) {
		/*
		 * Wait for IRQ gpio to be released, make read operation
		 * synchronize with PtVirtDut tool.
		 * Safety net: Exit after 500ms (50us * 10000 loops = 500ms)
		 */
		while (i < VIRT_MAX_IRQ_RELEASE_TIME_US &&
		       !gpio_get_value(cd->cpdata->irq_gpio)) {
			pt_debug(dev, DL_INFO, "%s: %d IRQ still Enabled\n",
				__func__, i);
			usleep_range(50, 60);
			i += 50;
		}
	}
#endif

	pt_debug(dev, DL_INFO,
		"%s: Copy msg from dut_out to i2c buffer, size=%d\n",
		__func__, size);

	return size;
}

/*******************************************************************************
 * FUNCTION: pt_dut_cmd_show
 *
 * SUMMARY: The show function for the dut_cmd sysfs node. Provides read access
 *	to the pt_dut_cmd_buf and clears it after it has been read.
 *
 * RETURN VALUE:
 *	Number of bytes transferred
 *
 * PARAMETERS:
 *      *dev  - pointer to device structure
 *      *attr - pointer to device attributes
 *	*buf  - pointer to output buffer
 ******************************************************************************/
static ssize_t pt_dut_cmd_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int i;
	int index = 0;

	/* Only print to sysfs if the buffer has data */
	mutex_lock(&virt_i2c_lock);
	if (pt_dut_cmd_len > 0) {
		for (i = 0; i < pt_dut_cmd_len; i++)
			index += scnprintf(buf + index, strlen(buf), "%02X",
				pt_dut_cmd_buf[i]);
		index += scnprintf(buf + index, strlen(buf), "\n");
	}
	pt_dut_cmd_len = 0;
	mutex_unlock(&virt_i2c_lock);
	return index;
}
static DEVICE_ATTR(dut_cmd, 0444, pt_dut_cmd_show, NULL);

/*******************************************************************************
 * FUNCTION: pt_dut_out_store
 *
 * SUMMARY: The store function for the dut_out sysfs node. Provides write
 *	access to the pt_dut_out_buf. The smallest valid PIP response is 2
 *	bytes so don't update buffer if only 1 byte passed in.
 *
 * RETURN VALUE:
 *	Number of bytes read from virtual DUT
 *
 * PARAMETERS:
 *	*dev  - pointer to device structure
 *	*attr - pointer to device attributes
 *	*buf  - pointer to buffer that hold the command parameters
 *	 size - size of buf
 ******************************************************************************/
static ssize_t pt_dut_out_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t size)
{
	int loop_max = ARRAY_SIZE(pt_dut_out_buf);
	int hex_str_len = strlen(buf)/2;
	int i;
	const char *pos = buf;
	struct pt_core_data *cd = dev_get_drvdata(dev);

	/* Clear out the last message */
	mutex_lock(&virt_i2c_lock);
	memset(pt_dut_out_buf, 0, VIRT_DUT_BUF_SIZE);
	pt_dut_out_len = 0;

	/* Only update the dut_out buffer if at least 2 byte payload */
	if (size >= 2 && hex_str_len <= loop_max) {
		/* Convert string of hex values to byte array */
		for (i = 0; i < hex_str_len; i++) {
			pt_dut_out_buf[i] = ((HEXOF(*pos)) << 4) +
					     HEXOF(*(pos + 1));
			pos += 2;
		}
		/*
		 * In HID we send the full string, non HID we send based
		 * on the 2 byte length.
		 */
		if (cd->dut_status.protocol_mode == PT_PROTOCOL_MODE_HID)
			pt_dut_out_len = hex_str_len;
		else
			pt_dut_out_len = get_unaligned_le16(&pt_dut_out_buf[0]);
	} else if (size >= PT_PIP_1P7_EMPTY_BUF) {
		/* Message too large, set to 'empty buffer' message */
		pt_dut_out_buf[1] = 0xFF;
		pt_dut_out_len = 0;
	}
	mutex_unlock(&virt_i2c_lock);

	return size;
}
static DEVICE_ATTR(dut_out, 0200, NULL, pt_dut_out_store);
#endif /* TTDL_PTVIRTDUT_SUPPORT */

/*******************************************************************************
 * FUNCTION: pt_i2c_read_default
 *
 * SUMMARY: Read a certain number of bytes from the I2C bus
 *
 * PARAMETERS:
 *      *dev  - pointer to Device structure
 *      *buf  - pointer to buffer where the data read will be stored
 *       size - size to be read
 ******************************************************************************/
static int pt_i2c_read_default(struct device *dev, void *buf, int size)
{
#ifdef TTDL_PTVIRTDUT_SUPPORT
	struct pt_core_data *cd = dev_get_drvdata(dev);
#endif /* TTDL_PTVIRTDUT_SUPPORT */
	struct i2c_client *client = to_i2c_client(dev);
	int rc;
	int read_size = size;

	if (!buf || !size || size > VIRT_DUT_BUF_SIZE)
		return -EINVAL;

#ifdef TTDL_PTVIRTDUT_SUPPORT
	if (cd->route_bus_virt_dut)
		rc = virt_i2c_master_recv(dev, buf, read_size);
	else
		rc = i2c_master_recv(client, buf, read_size);
#else
	rc = i2c_master_recv(client, buf, read_size);
#endif /* TTDL_PTVIRTDUT_SUPPORT */

	return (rc < 0) ? rc : rc != read_size ? -EIO : 0;
}

/*******************************************************************************
 * FUNCTION: pt_i2c_read_default_nosize
 *
 * SUMMARY: Read from the I2C bus in two transactions first reading the HID
 *	packet size (2 bytes) followed by reading the rest of the packet based
 *	on the size initially read.
 *	NOTE: The empty buffer 'size' was redefined in PIP version 1.7.
 *
 * PARAMETERS:
 *      *dev  - pointer to Device structure
 *      *buf  - pointer to buffer where the data read will be stored
 *       max  - max size that can be read
 ******************************************************************************/
static int pt_i2c_read_default_nosize(struct device *dev, u8 *buf, u32 max)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct i2c_msg msgs[2];
	u8 msg_count = 1;
	int rc;
	u32 size;
#ifdef TTDL_PTVIRTDUT_SUPPORT
	struct pt_core_data *cd = dev_get_drvdata(dev);

	if (cd->route_bus_virt_dut) {
		mutex_lock(&virt_i2c_lock);
		size = pt_dut_out_len;
		mutex_unlock(&virt_i2c_lock);
		pt_debug(dev, DL_INFO, "%s: pt_dut_out_len=%d\n",
			__func__, pt_dut_out_len);
		/* Only copy 2 bytes for "empty buffer" or "FW sentinel" */
		if (!size || size == 2 || size >= PT_PIP_1P7_EMPTY_BUF)
			size = 2;
		goto skip_read_len;
	}

#endif /* TTDL_PTVIRTDUT_SUPPORT */

	if (!buf)
		return -EINVAL;

	msgs[0].addr = client->addr;
	msgs[0].flags = (client->flags & I2C_M_TEN) | I2C_M_RD;
	msgs[0].len = 2;
	msgs[0].buf = buf;
	rc = i2c_transfer(client->adapter, msgs, msg_count);
	if (rc < 0 || rc != msg_count)
		return (rc < 0) ? rc : -EIO;

	size = get_unaligned_le16(&buf[0]);
	if (!size || size == 2 || size >= PT_PIP_1P7_EMPTY_BUF)
		/*
		 * Before PIP 1.7, empty buffer is 0x0002;
		 * From PIP 1.7, empty buffer is 0xFFXX
		 */
		return 0;

	if (size > max)
		return -EINVAL;

#ifdef TTDL_PTVIRTDUT_SUPPORT
skip_read_len:
	if (cd->route_bus_virt_dut)
		rc = virt_i2c_master_recv(dev, buf, size);
	else
		rc = i2c_master_recv(client, buf, size);

	pt_debug(dev, DL_INFO, "%s: rc = %d\n", __func__, rc);
#else
	rc = i2c_master_recv(client, buf, size);
#endif /* TTDL_PTVIRTDUT_SUPPORT */
	return (rc < 0) ? rc : rc != (int)size ? -EIO : 0;
}

/*******************************************************************************
 * FUNCTION: pt_i2c_write_read_specific
 *
 * SUMMARY: Write the contents of write_buf to the I2C device and then read
 *  the response using pt_i2c_read_default_nosize()
 *
 * PARAMETERS:
 *  *dev       - pointer to Device structure
 *   write_len - length of data buffer write_buf
 *  *write_buf - pointer to buffer to write
 *  *read_buf  - pointer to buffer to read response into
 *   read_len  - length to read, 0 to use pt_i2c_read_default_nosize
 ******************************************************************************/
static int pt_i2c_write_read_specific(struct device *dev, u16 write_len,
		u8 *write_buf, u8 *read_buf, u16 read_len)
{
	struct i2c_client *client = to_i2c_client(dev);
#ifdef TTDL_PTVIRTDUT_SUPPORT
	struct pt_core_data *cd = dev_get_drvdata(dev);
#endif /* TTDL_PTVIRTDUT_SUPPORT */
	struct i2c_msg msgs[2];
	u8 msg_count = 1;
	int rc;

	/* Ensure no packet larger than what the PIP spec allows */
	if (write_len > PT_MAX_PIP2_MSG_SIZE)
		return -EINVAL;

	if (!write_buf || !write_len) {
		if (!write_buf)
			pt_debug(dev, DL_ERROR,
				"%s write_buf is NULL", __func__);
		if (!write_len)
			pt_debug(dev, DL_ERROR,
				"%s write_len is NULL", __func__);
		return -EINVAL;
	}

	msgs[0].addr = client->addr;
	msgs[0].flags = client->flags & I2C_M_TEN;
	msgs[0].len = write_len;
	msgs[0].buf = write_buf;
#ifdef TTDL_PTVIRTDUT_SUPPORT
	if (cd->route_bus_virt_dut) {
		rc = virt_i2c_transfer(msgs[0].buf, msgs[0].len);
		pt_debug(dev, DL_DEBUG, "%s: Virt transfer size = %d",
			__func__, msgs[0].len);
	} else
		rc = i2c_transfer(client->adapter, msgs, msg_count);
#else
	rc = i2c_transfer(client->adapter, msgs, msg_count);
#endif /* TTDL_PTVIRTDUT_SUPPORT */

	if (rc < 0 || rc != msg_count)
		return (rc < 0) ? rc : -EIO;

	rc = 0;

	if (read_buf) {
#ifdef TTDL_PTVIRTDUT_SUPPORT
		if (cd->route_bus_virt_dut &&
		    cd->dut_status.protocol_mode == PT_PROTOCOL_MODE_HID) {
			/* Simulate clock stretch */
			usleep_range(3000, 4000);
		}
#endif /* TTDL_PTVIRTDUT_SUPPORT */
		if (read_len > 0)
			rc = pt_i2c_read_default(dev, read_buf, read_len);
		else
			rc = pt_i2c_read_default_nosize(dev, read_buf,
				PT_I2C_DATA_SIZE);
	}

	return rc;
}

static struct pt_bus_ops pt_i2c_bus_ops = {
	.bustype = BUS_I2C,
	.read_default = pt_i2c_read_default,
	.read_default_nosize = pt_i2c_read_default_nosize,
	.write_read_specific = pt_i2c_write_read_specific,
};

#ifdef CONFIG_TOUCHSCREEN_PARADE_DEVICETREE_SUPPORT
static const struct of_device_id pt_i2c_of_match[] = {
	{ .compatible = "parade,pt_i2c_adapter", },
	{ }
};
MODULE_DEVICE_TABLE(of, pt_i2c_of_match);
#endif


/*******************************************************************************
 * FUNCTION: pt_i2c_probe
 *
 * SUMMARY: Probe functon for the I2C module
 *
 * PARAMETERS:
 *      *client - pointer to i2c client structure
 *      *i2c_id - pointer to i2c device structure
 ******************************************************************************/
static int pt_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *i2c_id)
{
	struct device *dev = &client->dev;
#ifdef CONFIG_TOUCHSCREEN_PARADE_DEVICETREE_SUPPORT
	const struct of_device_id *match;
#endif
	int rc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pt_debug(dev, DL_ERROR, "I2C functionality not Supported\n");
		return -EIO;
	}

#ifdef CONFIG_TOUCHSCREEN_PARADE_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(pt_i2c_of_match), dev);
	if (match) {
		rc = pt_devtree_create_and_get_pdata(dev);
		if (rc < 0)
			return rc;
	}
#endif

#ifdef TTDL_PTVIRTDUT_SUPPORT
	/*
	 * When using the virtual DUT these files must be created before
	 * pt_probe is called.
	 */
	mutex_lock(&virt_i2c_lock);
	device_create_file(dev, &dev_attr_dut_cmd);
	device_create_file(dev, &dev_attr_dut_out);
	mutex_unlock(&virt_i2c_lock);
#endif /* TTDL_PTVIRTDUT_SUPPORT */

	rc = pt_probe(&pt_i2c_bus_ops, &client->dev, client->irq,
			  PT_I2C_DATA_SIZE);

#ifdef CONFIG_TOUCHSCREEN_PARADE_DEVICETREE_SUPPORT
	if (rc && match)
		pt_devtree_clean_pdata(dev);
#endif
#ifdef TTDL_PTVIRTDUT_SUPPORT
	if (rc) {
		mutex_lock(&virt_i2c_lock);
		device_remove_file(dev, &dev_attr_dut_cmd);
		device_remove_file(dev, &dev_attr_dut_out);
		mutex_unlock(&virt_i2c_lock);
	}
#endif /* TTDL_PTVIRTDUT_SUPPORT */

	return rc;
}

/*******************************************************************************
 * FUNCTION: pt_i2c_remove
 *
 * SUMMARY: Remove functon for the I2C module
 *
 * PARAMETERS:
 *      *client - pointer to i2c client structure
 ******************************************************************************/
static int pt_i2c_remove(struct i2c_client *client)
{
#ifdef CONFIG_TOUCHSCREEN_PARADE_DEVICETREE_SUPPORT
	const struct of_device_id *match;
#endif
	struct device *dev = &client->dev;
	struct pt_core_data *cd = i2c_get_clientdata(client);

	pt_release(cd);
#ifdef TTDL_PTVIRTDUT_SUPPORT
	mutex_lock(&virt_i2c_lock);
	device_remove_file(dev, &dev_attr_dut_cmd);
	device_remove_file(dev, &dev_attr_dut_out);
	mutex_unlock(&virt_i2c_lock);
#endif /* TTDL_PTVIRTDUT_SUPPORT */

#ifdef CONFIG_TOUCHSCREEN_PARADE_DEVICETREE_SUPPORT
	match = of_match_device(of_match_ptr(pt_i2c_of_match), dev);
	if (match)
		pt_devtree_clean_pdata(dev);
#endif

	return 0;
}

static const struct i2c_device_id pt_i2c_id[] = {
	{ PT_I2C_NAME, 0, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pt_i2c_id);

static struct i2c_driver pt_i2c_driver = {
	.driver = {
		.name = PT_I2C_NAME,
		.owner = THIS_MODULE,
		.pm = &pt_pm_ops,
#ifdef CONFIG_TOUCHSCREEN_PARADE_DEVICETREE_SUPPORT
		.of_match_table = pt_i2c_of_match,
#endif
	},
	.probe = pt_i2c_probe,
	.remove = pt_i2c_remove,
	.id_table = pt_i2c_id,
};

#if (KERNEL_VERSION(3, 3, 0) <= LINUX_VERSION_CODE)
module_i2c_driver(pt_i2c_driver);
#else
/*******************************************************************************
 * FUNCTION: pt_i2c_init
 *
 * SUMMARY: Initialize function to register i2c module to kernel.
 *
 * RETURN:
 *	 0 = success
 *	!0 = failure
 ******************************************************************************/
static int __init pt_i2c_init(void)
{
	int rc = i2c_add_driver(&pt_i2c_driver);

	pr_info("%s: Parade TTDL I2C Driver (Build %s) rc=%d\n",
			__func__, PT_DRIVER_VERSION, rc);
	return rc;
}
module_init(pt_i2c_init);

/*******************************************************************************
 * FUNCTION: pt_i2c_exit
 *
 * SUMMARY: Exit function to unregister i2c module from kernel.
 *
 ******************************************************************************/
static void __exit pt_i2c_exit(void)
{
	i2c_del_driver(&pt_i2c_driver);
}
module_exit(pt_i2c_exit);
#endif

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Parade TrueTouch(R) Standard Product I2C driver");
MODULE_AUTHOR("Parade Technologies <ttdrivers@paradetech.com>");
