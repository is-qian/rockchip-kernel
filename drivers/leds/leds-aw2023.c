// SPDX-License-Identifier: GPL-2.0-only
/*
 * Awinic AW2023 3-channel constant-current LED driver.
 *
 * Channels run in manual mode (LCFGx.MD=0): PWMx sets brightness directly.
 * Pattern/breathing engine is not exposed.
 */

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>

#define AW2023_REG_RSTR		0x00	/* read: chip id 0x09; write 0x55: reset */
#define AW2023_REG_GCR1		0x01
#define AW2023_REG_GCR2		0x04	/* IMAX in bits [1:0] */
#define AW2023_REG_LCTR		0x30
#define AW2023_REG_LCFG0	0x31	/* + channel */
#define AW2023_REG_PWM0		0x34	/* + channel */

#define AW2023_CHIP_ID		0x09
#define AW2023_NUM_LEDS		3
#define AW2023_GCR1_CHIPEN	BIT(0)
#define AW2023_LCTR_LE_ALL	0x07
#define AW2023_LCFG_MANUAL_CUR_MAX	0x0f	/* MD=0, CUR=15 */

struct aw2023_led {
	struct led_classdev cdev;
	struct i2c_client *client;
	u8 channel;
};

struct aw2023 {
	struct i2c_client *client;
	struct mutex lock;
};

static int aw2023_write(struct i2c_client *client, u8 reg, u8 val)
{
	return i2c_smbus_write_byte_data(client, reg, val);
}

static int aw2023_brightness_set(struct led_classdev *cdev,
				 enum led_brightness brightness)
{
	struct aw2023_led *led = container_of(cdev, struct aw2023_led, cdev);
	struct aw2023 *chip = i2c_get_clientdata(led->client);
	int ret;

	mutex_lock(&chip->lock);
	ret = aw2023_write(led->client, AW2023_REG_PWM0 + led->channel,
			   brightness);
	mutex_unlock(&chip->lock);

	return ret;
}

static int aw2023_init_chip(struct i2c_client *client, u32 imax)
{
	int ret, i;

	ret = i2c_smbus_read_byte_data(client, AW2023_REG_RSTR);
	if (ret < 0)
		return ret;
	if (ret != AW2023_CHIP_ID)
		return -ENODEV;

	/* only GCR1 is writable while in standby */
	ret = aw2023_write(client, AW2023_REG_GCR1, AW2023_GCR1_CHIPEN);
	if (ret)
		return ret;

	ret = aw2023_write(client, AW2023_REG_GCR2, imax & 0x3);
	if (ret)
		return ret;

	for (i = 0; i < AW2023_NUM_LEDS; i++) {
		ret = aw2023_write(client, AW2023_REG_LCFG0 + i,
				   AW2023_LCFG_MANUAL_CUR_MAX);
		if (ret)
			return ret;
		ret = aw2023_write(client, AW2023_REG_PWM0 + i, 0);
		if (ret)
			return ret;
	}

	return aw2023_write(client, AW2023_REG_LCTR, AW2023_LCTR_LE_ALL);
}

static int aw2023_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct aw2023 *chip;
	u32 imax = 1;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->client = client;
	mutex_init(&chip->lock);
	i2c_set_clientdata(client, chip);

	of_property_read_u32(dev->of_node, "awinic,imax", &imax);

	ret = aw2023_init_chip(client, imax);
	if (ret)
		return dev_err_probe(dev, ret, "chip init failed\n");

	device_for_each_child_node_scoped(dev, child) {
		struct led_init_data init_data = { .fwnode = child };
		struct aw2023_led *led;
		u32 channel;

		ret = fwnode_property_read_u32(child, "reg", &channel);
		if (ret || channel >= AW2023_NUM_LEDS) {
			dev_err(dev, "invalid led reg property\n");
			return -EINVAL;
		}

		led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
		if (!led)
			return -ENOMEM;

		led->client = client;
		led->channel = channel;
		led->cdev.max_brightness = 255;
		led->cdev.brightness_set_blocking = aw2023_brightness_set;

		ret = devm_led_classdev_register_ext(dev, &led->cdev, &init_data);
		if (ret)
			return dev_err_probe(dev, ret, "led register failed\n");
	}

	return 0;
}

static void aw2023_remove(struct i2c_client *client)
{
	aw2023_write(client, AW2023_REG_LCTR, 0);
	aw2023_write(client, AW2023_REG_GCR1, 0);
}

static const struct of_device_id aw2023_of_match[] = {
	{ .compatible = "awinic,aw2023" },
	{}
};
MODULE_DEVICE_TABLE(of, aw2023_of_match);

static struct i2c_driver aw2023_driver = {
	.driver = {
		.name = "aw2023-led",
		.of_match_table = aw2023_of_match,
	},
	.probe = aw2023_probe,
	.remove = aw2023_remove,
};
module_i2c_driver(aw2023_driver);

MODULE_DESCRIPTION("Awinic AW2023 LED driver");
MODULE_LICENSE("GPL");
