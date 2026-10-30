// SPDX-License-Identifier: GPL-2.0-only
/*
 * Awinic AW21009 9-channel constant-current LED driver.
 *
 * One LED class device per channel, named from the device tree, so each LED
 * keeps its own label. Single-byte 8-bit PWM (GCR2.SBMD): one BR byte per
 * channel is the brightness, SLx is held at full scale, and the full-scale
 * current is the board's ISET resistor. Registers from Awinic's aw210xx vendor driver; the
 * group, pattern and open/short detection blocks are not used.
 */

#include <linux/i2c.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/of.h>

#define AW21009_REG_GCR		0x20	/* CHIPEN bit 0; 16 MHz, 8-bit PWM at 0 */
#define AW21009_REG_BR0	0x21	/* + channel: SBMD packs BRxL (vendor aw210xx_singleled_set) */
#define AW21009_REG_UPDATE	0x45	/* write 0: latch BR/SL */
#define AW21009_REG_SL0		0x46	/* + channel */
#define AW21009_REG_GCCR	0x58	/* global current */
#define AW21009_REG_GCR2	0x61	/* SBMD bit 1 */
#define AW21009_REG_RESET	0x70	/* read: chip id */
#define AW21009_REG_GCFG	0x8b

#define AW21009_CHIP_ID		0x12
#define AW21009_NUM_LEDS	9
#define AW21009_GCR_CHIPEN	BIT(0)
#define AW21009_GCR2_SBMD	BIT(1)
#define AW21009_GCFG_GROUP_OFF	0x40	/* Awinic's AW21009_GROUP_DISABLE */

struct aw21009_led {
	struct led_classdev cdev;
	struct i2c_client *client;
	u8 channel;
};

static int aw21009_brightness_set(struct led_classdev *cdev,
				  enum led_brightness brightness)
{
	struct aw21009_led *led = container_of(cdev, struct aw21009_led, cdev);
	int ret;

	ret = i2c_smbus_write_byte_data(led->client,
					AW21009_REG_BR0 + led->channel,
					brightness);
	if (ret)
		return ret;

	return i2c_smbus_write_byte_data(led->client, AW21009_REG_UPDATE, 0);
}

static int aw21009_init_chip(struct i2c_client *client)
{
	static const u8 init[][2] = {
		{ AW21009_REG_GCR, AW21009_GCR_CHIPEN },
		{ AW21009_REG_GCR2, AW21009_GCR2_SBMD },
		{ AW21009_REG_GCFG, AW21009_GCFG_GROUP_OFF },
		{ AW21009_REG_GCCR, 0xff },
	};
	int ret, i;

	ret = i2c_smbus_read_byte_data(client, AW21009_REG_RESET);
	if (ret < 0)
		return ret;
	if (ret != AW21009_CHIP_ID)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(init); i++) {
		ret = i2c_smbus_write_byte_data(client, init[i][0], init[i][1]);
		if (ret)
			return ret;
	}

	for (i = 0; i < AW21009_NUM_LEDS; i++) {
		ret = i2c_smbus_write_byte_data(client, AW21009_REG_SL0 + i, 0xff);
		if (ret)
			return ret;
		ret = i2c_smbus_write_byte_data(client, AW21009_REG_BR0 + i, 0);
		if (ret)
			return ret;
	}

	return i2c_smbus_write_byte_data(client, AW21009_REG_UPDATE, 0);
}

static int aw21009_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct fwnode_handle *child;
	int ret;

	ret = aw21009_init_chip(client);
	if (ret)
		return dev_err_probe(dev, ret, "chip init failed\n");

	/* reg is the chip's pin number, LED1..LED9 */
	device_for_each_child_node(dev, child) {
		struct led_init_data init_data = { .fwnode = child };
		struct aw21009_led *led;
		u32 pin;

		ret = fwnode_property_read_u32(child, "reg", &pin);
		if (ret || pin < 1 || pin > AW21009_NUM_LEDS) {
			fwnode_handle_put(child);
			dev_err(dev, "invalid led reg property\n");
			return -EINVAL;
		}

		led = devm_kzalloc(dev, sizeof(*led), GFP_KERNEL);
		if (!led) {
			fwnode_handle_put(child);
			return -ENOMEM;
		}

		led->client = client;
		led->channel = pin - 1;
		led->cdev.max_brightness = 255;
		led->cdev.brightness_set_blocking = aw21009_brightness_set;

		ret = devm_led_classdev_register_ext(dev, &led->cdev, &init_data);
		if (ret) {
			fwnode_handle_put(child);
			return dev_err_probe(dev, ret, "led register failed\n");
		}
	}

	return 0;
}

static void aw21009_remove(struct i2c_client *client)
{
	i2c_smbus_write_byte_data(client, AW21009_REG_GCR, 0);
}

static const struct of_device_id aw21009_of_match[] = {
	{ .compatible = "awinic,aw21009" },
	{}
};
MODULE_DEVICE_TABLE(of, aw21009_of_match);

static struct i2c_driver aw21009_driver = {
	.driver = {
		.name = "aw21009-led",
		.of_match_table = aw21009_of_match,
	},
	.probe = aw21009_probe,
	.remove = aw21009_remove,
};
module_i2c_driver(aw21009_driver);

MODULE_DESCRIPTION("Awinic AW21009 LED driver");
MODULE_LICENSE("GPL");
