/*
 * platform_camera.c: Camera platform library file
 *
 * (C) Copyright 2008 Intel Corporation
 * Author:
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/lnw_gpio.h>
#include <linux/atomisp_platform.h>
#include <linux/module.h>
#include <media/v4l2-subdev.h>
#include <asm/intel-mid.h>
#include "platform_camera.h"
#ifdef CONFIG_CRYSTAL_COVE
#include <linux/mfd/intel_mid_pmic.h>
#endif
/*
 * TODO: Check whether we can move this info to OEM table or
 *       set this info in the platform data of each sensor
 */
const struct intel_v4l2_subdev_id v4l2_ids[] = {
	//{"mt9e013", RAW_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	//{"ov8830", RAW_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	//{"imx175", RAW_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	//{"imx135", RAW_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	//{"imx134", RAW_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	//{"imx132", RAW_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	//{"ov9724", RAW_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	//{"ov2722", RAW_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	//{"mt9d113", SOC_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	{"ov5693", SOC_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	{"ar0543", SOC_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	{"mt9m114", SOC_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	{"ar0543_raw", RAW_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	{"imx111_raw", RAW_CAMERA, ATOMISP_CAMERA_PORT_PRIMARY},
	{"mt9m114_raw", RAW_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	{"hm2056_raw", RAW_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	{"gc0339_raw", RAW_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	//{"mt9v113", SOC_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	//{"s5k8aay", SOC_CAMERA, ATOMISP_CAMERA_PORT_SECONDARY},
	//{"lm3554", LED_FLASH, -1},
	//{"lm3559", LED_FLASH, -1},
	{},
};

/*
 * One-time gpio initialization.
 * @name: gpio name: coded in SFI table
 * @gpio: gpio pin number (bypass @name)
 * @dir: GPIOF_DIR_IN or GPIOF_DIR_OUT
 * @value: if dir = GPIOF_DIR_OUT, this is the init value for output pin
 * if dir = GPIOF_DIR_IN, this argument is ignored
 * return: a positive pin number if succeeds, otherwise a negative value
 */
int camera_sensor_gpio(int gpio, char *name, int dir, int value)
{
	int ret, pin;

	if (gpio == -1) {
		pin = get_gpio_by_name(name);
		if (pin == -1) {
			pr_err("%s: failed to get gpio(name: %s)\n",
						__func__, name);
			return -EINVAL;
		}
		pr_info("camera pdata: gpio: %s: %d\n", name, pin);
	} else {
		pin = gpio;
	}

	ret = gpio_request(pin, name);
	if (ret) {
		pr_err("%s: failed to request gpio(pin %d)\n", __func__, pin);
		return -EINVAL;
	}

	if (dir == GPIOF_DIR_OUT)
		ret = gpio_direction_output(pin, value);
	else
		ret = gpio_direction_input(pin);

	if (ret) {
		pr_err("%s: failed to set gpio(pin %d) direction\n",
							__func__, pin);
		gpio_free(pin);
	}

	return ret ? ret : pin;
}

/*
 * Configure MIPI CSI physical parameters.
 * @port: ATOMISP_CAMERA_PORT_PRIMARY or ATOMISP_CAMERA_PORT_SECONDARY
 * @lanes: for ATOMISP_CAMERA_PORT_PRIMARY, there could be 2 or 4 lanes
 * for ATOMISP_CAMERA_PORT_SECONDARY, there is only one lane.
 * @format: MIPI CSI pixel format, see include/linux/atomisp_platform.h
 * @bayer_order: MIPI CSI bayer order, see include/linux/atomisp_platform.h
 */
int camera_sensor_csi(struct v4l2_subdev *sd, u32 port,
			u32 lanes, u32 format, u32 bayer_order, int flag)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct camera_mipi_info *csi = NULL;

	if (flag) {
		csi = kzalloc(sizeof(*csi), GFP_KERNEL);
		if (!csi) {
			dev_err(&client->dev, "out of memory\n");
			return -ENOMEM;
		}
		csi->port = port;
		csi->num_lanes = lanes;
		csi->input_format = format;
		csi->raw_bayer_order = bayer_order;
		v4l2_set_subdev_hostdata(sd, (void *)csi);
		dev_info(&client->dev,
			 "camera pdata: port: %d lanes: %d order: %8.8x\n",
			 port, lanes, bayer_order);
	} else {
		csi = v4l2_get_subdev_hostdata(sd);
		kfree(csi);
	}

	return 0;
}
static int no_v4l2_dev_ids(void)
{
	const struct intel_v4l2_subdev_id *v4l2_ids_ptr = v4l2_ids;
	int no_v4l2_ids = 0;

	while (v4l2_ids_ptr->name[0]) {
		no_v4l2_ids++;
		v4l2_ids_ptr++;
	}

	return no_v4l2_ids;
}
static const struct intel_v4l2_subdev_id *get_v4l2_ids(int *n_subdev)
{
	if (n_subdev && v4l2_ids)
		*n_subdev = no_v4l2_dev_ids();
	return v4l2_ids;
}

static struct atomisp_platform_data *atomisp_platform_data;

void intel_register_i2c_camera_device(struct sfi_device_table_entry *pentry,
					struct devs_id *dev)
{
	struct i2c_board_info i2c_info;
	struct i2c_board_info *idev = &i2c_info;
	int bus = pentry->host_num;
	void *pdata = NULL;
	int n_subdev;
	const struct intel_v4l2_subdev_id *vdev = get_v4l2_ids(&n_subdev);
	struct intel_v4l2_subdev_i2c_board_info *info;
	static struct intel_v4l2_subdev_table *subdev_table;
	enum intel_v4l2_subdev_type type = 0;
	enum atomisp_camera_port port;
	static int i;

	if (vdev == NULL) {
		pr_info("ERROR: camera vdev list is NULL\n");
		return;
	}

	memset(&i2c_info, 0, sizeof(i2c_info));
	strncpy(i2c_info.type, pentry->name, SFI_NAME_LEN);
	i2c_info.irq = ((pentry->irq == (u8)0xff) ? 0 : pentry->irq);
	i2c_info.addr = pentry->addr;
	pr_info("camera pdata: I2C bus = %d, name = %16.16s, irq = 0x%2x, addr = 0x%x\n",
		pentry->host_num, i2c_info.type, i2c_info.irq, i2c_info.addr);

	if (!dev->get_platform_data)
		return;
	pdata = dev->get_platform_data(&i2c_info);
	i2c_info.platform_data = pdata;

	//Peter++
	//read HW ID
	if (HW_ID == 0xFF){
		HW_ID = Read_HW_ID();
	}
	//printk("HW ID:%d\n", HW_ID);
	if ((!strcmp(i2c_info.type, "ov5693")) || (!strcmp(i2c_info.type, "ar0543"))){ //rear camera
		i2c_info.addr = 0x3C;
		printk("name:%s, addr=0x%x", i2c_info.type, i2c_info.addr);
	}

	if (!strcmp(i2c_info.type, "mt9m114")){
		i2c_info.addr = 0x48; //fake value for adding node
		printk("name:%s, addr=0x%x", i2c_info.type, i2c_info.addr);
	}
	
	if (!strcmp(i2c_info.type, "ar0543_raw")){
		i2c_info.addr = 0x36;
		printk("name:%s, addr=0x%x", i2c_info.type, i2c_info.addr);
	}
	
	if (!strcmp(i2c_info.type, "imx111_raw")){
		i2c_info.addr = 0x1a;
		printk("name:%s, addr=0x%x", i2c_info.type, i2c_info.addr);
	}	
	
	if (!strcmp(i2c_info.type, "mt9m114_raw")){
		i2c_info.addr = 0x48;
		printk("name:%s, addr=0x%x", i2c_info.type, i2c_info.addr);
	}

	if (!strcmp(i2c_info.type, "hm2056_raw")){
		i2c_info.addr = 0x24;
		printk("name:%s, addr=0x%x", i2c_info.type, i2c_info.addr);
	}

	if (!strcmp(i2c_info.type, "gc0339_raw")){
                i2c_info.addr = 0x21; //fake value for adding node
                printk("name:%s, addr=0x%x", i2c_info.type, i2c_info.addr);
        }
	//Peter--

	while (vdev->name[0]) {
		if (!strncmp(vdev->name, idev->type, 16)) {
			/* compare name */
			type = vdev->type;
			port = vdev->port;
			break;
		}
		vdev++;
	}

	if (!type) /* not found */
		return;

	info = kzalloc(sizeof(struct intel_v4l2_subdev_i2c_board_info),
		       GFP_KERNEL);
	if (!info) {
		pr_err("MRST: fail to alloc mem for ignored i2c dev %s\n",
		       idev->type);
		return;
	}

	info->i2c_adapter_id = bus;
	/* set platform data */
	memcpy(&info->board_info, idev, sizeof(*idev));

	if (atomisp_platform_data == NULL) {
		subdev_table = kzalloc(sizeof(struct intel_v4l2_subdev_table)
			* n_subdev, GFP_KERNEL);

		if (!subdev_table) {
			pr_err("MRST: fail to alloc mem for v4l2_subdev_table %s\n",
			       idev->type);
			kfree(info);
			return;
		}

		atomisp_platform_data = kzalloc(
			sizeof(struct atomisp_platform_data), GFP_KERNEL);
		if (!atomisp_platform_data) {
			pr_err("%s: fail to alloc mem for atomisp_platform_data %s\n",
			       __func__, idev->type);
			kfree(info);
			kfree(subdev_table);
			return;
		}
		atomisp_platform_data->subdevs = subdev_table;
	}

	memcpy(&subdev_table[i].v4l2_subdev, info, sizeof(*info));
	subdev_table[i].type = type;
	subdev_table[i].port = port;
	i++;
	kfree(info);
	return;
}

#ifdef CONFIG_ACPI
#if 0
static int match_name(struct device *dev, void *data)
{
	const char *name = data;
	struct i2c_client *client = to_i2c_client(dev);
	return !strncmp(client->name, name, strlen(client->name));
}

struct i2c_client *i2c_find_client_by_name(char *name)
{
	struct device *dev = bus_find_device(&i2c_bus_type, NULL,
						name, match_name);
	return dev ? to_i2c_client(dev) : NULL;
}
#endif
/*
 * In BTY, ACPI enumination will register all the camera i2c devices
 * which will cause v4l2_i2c_new_subdev_board() failed called in atomisp
 * driver.
 * Here we unregister the devices registered by ACPI
 */
static void atomisp_unregister_acpi_devices(struct atomisp_platform_data *pdata)
{
	const char *subdev_name[] = {
		"3-0053",	/* FFRD8 lm3554 */
		"4-0036",	/* ov2722 */
		"4-0010",	/* imx1xx Sensor*/
		"4-0053",	/* FFRD10 lm3554 */
		"4-0054",	/* imx1xx EEPROM*/
		"4-000c",	/* imx1xx driver*/
#if 0
		"INTCF0B:00",	/* From ACPI ov2722 */
		"INTCF1A:00",	/* From ACPI imx175 */
		"INTCF1C:00",	/* From ACPI lm3554 */
#endif
	};
	struct device *dev;
	struct i2c_client *client;
	struct i2c_board_info board_info;
	int i;
	/* search by device name */
	for (i = 0; i < ARRAY_SIZE(subdev_name); i++) {
		dev = bus_find_device_by_name(&i2c_bus_type, NULL,
					      subdev_name[i]);
		if (dev) {
			client = to_i2c_client(dev);
			board_info.flags = client->flags;
			board_info.addr = client->addr;
			board_info.irq = client->irq;
			strlcpy(board_info.type, client->name,
				sizeof(client->name));
			i2c_unregister_device(client);
		}
	}
#if 0
	/* search by client name */
	for (i = 0; i < ARRAY_SIZE(subdev_name); i++) {
		client = i2c_find_client_by_name(subdev_name[i]);
		if (client) {
			board_info.flags = client->flags;
			board_info.addr = client->addr;
			board_info.irq = client->irq;
			strlcpy(board_info.type, client->name,
				sizeof(client->name));
			i2c_unregister_device(client);
		}
	}
#endif
}
#endif
const struct atomisp_platform_data *atomisp_get_platform_data(void)
{
	if (atomisp_platform_data) {
#ifdef CONFIG_ACPI
		atomisp_unregister_acpi_devices(atomisp_platform_data);
#endif
		atomisp_platform_data->spid = &spid;
		return atomisp_platform_data;
	} else {
		pr_err("%s: no camera device in the SFI table\n", __func__);
		return NULL;
	}
}
EXPORT_SYMBOL_GPL(atomisp_get_platform_data);

static int camera_af_power_gpio = -1;

static int camera_af_power_ctrl(struct v4l2_subdev *sd, int flag)
{
	return gpio_direction_output(camera_af_power_gpio, flag);
}

const struct camera_af_platform_data *camera_get_af_platform_data(void)
{
	static const int GP_CORE = 96;
	static const int GPIO_DEFAULT = GP_CORE + 76;
	static const char gpio_name[] = "CAM_0_AF_EN";
	static const struct camera_af_platform_data platform_data = {
		.power_ctrl = camera_af_power_ctrl
	};
	int gpio, r;

	if (camera_af_power_gpio == -1) {
		gpio = get_gpio_by_name(gpio_name);
		if (gpio < 0) {
			pr_err("%s: can not find gpio `%s', using default\n",
				__func__, gpio_name);
			gpio = GPIO_DEFAULT;
		}
		pr_info("camera pdata: af gpio: %d\n", gpio);
		r = gpio_request(gpio, gpio_name);
		if (r)
			return NULL;
		r = gpio_direction_output(gpio, 0);
		if (r)
			return NULL;
		camera_af_power_gpio = gpio;
	}

	return &platform_data;
}
EXPORT_SYMBOL_GPL(camera_get_af_platform_data);

#ifdef CONFIG_CRYSTAL_COVE
/*
 * WA for BTY as simple VRF management
 */
int camera_set_pmic_power(enum camera_pmic_pin pin, bool flag)
{
	u8 reg_addr[CAMERA_POWER_NUM] = {VPROG_1P8V, VPROG_2P8V};
	u8 reg_value[2] = {VPROG_DISABLE, VPROG_ENABLE};
	static struct vprog_status status[CAMERA_POWER_NUM];
	static DEFINE_MUTEX(mutex_power);
	int ret = 0;

	mutex_lock(&mutex_power);
	/*
	 * only set power at:
	 * first to power on
	 * last to power off
	 */
	if ((flag && status[pin].user == 0)
	    || (!flag && status[pin].user == 1))
		ret = intel_mid_pmic_writeb(reg_addr[pin], reg_value[flag]);

	/* no update counter if config failed */
	if (ret)
		goto done;

	if (flag)
		status[pin].user++;
	else
		if (status[pin].user)
			status[pin].user--;
done:
	mutex_unlock(&mutex_power);
	return ret;
}
EXPORT_SYMBOL_GPL(camera_set_pmic_power);
#endif
