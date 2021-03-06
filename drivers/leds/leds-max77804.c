/*
 * LED driver for Maxim MAX77804 - leds-max77673.c
 *
 * Copyright (C) 2011 ByungChang Cha <bc.cha@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/mfd/max77804.h>
#include <linux/mfd/max77804-private.h>
#include <linux/leds-max77804.h>
#include <linux/ctype.h>

/*
static struct max77804_led_platform_data max77804_led_pdata = {

	.num_leds = 2,

	.leds[0].name = "leds-sec1",
	.leds[0].id = MAX77803_FLASH_LED_1,
	.leds[0].timer = MAX77803_FLASH_TIME_187P5MS,
	.leds[0].timer_mode = MAX77803_TIMER_MODE_MAX_TIMER,
	.leds[0].cntrl_mode = MAX77803_LED_CTRL_BY_FLASHSTB,
	.leds[0].brightness = 0x3D,

	.leds[1].name = "torch-sec1",
	.leds[1].id = MAX77803_TORCH_LED_1,
	.leds[1].cntrl_mode = MAX77803_LED_CTRL_BY_FLASHSTB,
	.leds[1].brightness = 0x06,
};
*/
struct max77804_led_data {
	struct led_classdev led;
	struct max77804_dev *max77804;
	struct max77804_led *data;
	struct i2c_client *i2c;
	struct work_struct work;
	struct mutex lock;
	spinlock_t value_lock;
	int brightness;
	int test_brightness;
};

static u8 led_en_mask[MAX77804_LED_MAX] = {
	MAX77804_FLASH_FLED1_EN,
	MAX77804_TORCH_FLED1_EN,
};

static u8 led_en_shift[MAX77804_LED_MAX] = {
	6,
	2,
};

static u8 reg_led_timer[MAX77804_LED_MAX] = {
	MAX77804_LED_REG_FLASH_TIMER,
	MAX77804_LED_REG_ITORCHTORCHTIMER,
};

static u8 reg_led_current[MAX77804_LED_MAX] = {
	MAX77804_LED_REG_IFLASH,
	MAX77804_LED_REG_ITORCH,
};

static u8 led_current_mask[MAX77804_LED_MAX] = {
	MAX77804_FLASH_IOUT,
	MAX77804_TORCH_IOUT,
};

static u8 led_current_shift[MAX77804_LED_MAX] = {
	0,
	0,
};

#if defined(CONFIG_SOC_EXYNOS5422_REV_0) || defined(CONFIG_SOC_EXYNOS5430_REV_1)
extern struct class *camera_class; /*sys/class/camera*/
struct device *flash_dev;
#endif

static int max77804_set_bits(struct i2c_client *client, const u8 reg,
			     const u8 mask, const u8 inval)
{
	int ret;
	u8 value;

	ret = max77804_read_reg(client, reg, &value);
	if (unlikely(ret < 0))
		return ret;

	value = (value & ~mask) | (inval & mask);

	ret = max77804_write_reg(client, reg, value);

	return ret;
}

#if 0 // unused
static void print_all_reg_value(struct i2c_client *client)
{
	u8 value;
	u8 i;

	for (i = 0; i != 0x11; ++i) {
		max77804_read_reg(client, i, &value);
		printk(KERN_ERR "LEDS_MAX77804 REG(%d) = %x\n", i, value);
	}
}
#endif

static int max77804_led_get_en_value(struct max77804_led_data *led_data, int on)
{
	if (on)
		return 0x03; /*triggered via serial interface*/

#if 0
	if (led_data->data->cntrl_mode == MAX77804_LED_CTRL_BY_I2C)
		return 0x00;
#endif

	else if (reg_led_timer[led_data->data->id]
					== MAX77804_LED_REG_FLASH_TIMER)
		return 0x01;/*Flash triggered via FLASHEN*/
	else
		return 0x02;/*Torch triggered via TORCHEN*/
}

static void max77804_led_set(struct led_classdev *led_cdev,
						enum led_brightness value)
{
	unsigned long flags;
	struct max77804_led_data *led_data
		= container_of(led_cdev, struct max77804_led_data, led);

	pr_debug("[LED] %s\n", __func__);

	spin_lock_irqsave(&led_data->value_lock, flags);
	led_data->test_brightness = min((int)value, MAX77804_FLASH_IOUT);
	spin_unlock_irqrestore(&led_data->value_lock, flags);

	schedule_work(&led_data->work);
}

static void led_set(struct max77804_led_data *led_data)
{
	int ret;
	struct max77804_led *data = led_data->data;
	int id = data->id;
	u8 shift = led_current_shift[id];
	int value;

#if defined(CONFIG_V1A) || defined(CONFIG_N1A)
	/* It is for Flash Control by UART, Max77888 have internal FET structure limitation */
	/* change MUIC Control3 to 'JIG pin Hi-Impedance' */
	ret = max77804_muic_set_jigset(0x03);
	if (unlikely(ret))
		pr_err("[LED] %s : MUIC 0x0E write failed 0x03 \n", __func__);
#endif

	if (led_data->test_brightness == LED_OFF) {
		value = max77804_led_get_en_value(led_data, 0);
		ret = max77804_set_bits(led_data->i2c,
					MAX77804_LED_REG_FLASH_EN,
					led_en_mask[id],
					value << led_en_shift[id]);
		if (unlikely(ret))
			goto error_set_bits;

		ret = max77804_set_bits(led_data->i2c, reg_led_current[id],
					led_current_mask[id],
					led_data->brightness << shift);
		if (unlikely(ret))
			goto error_set_bits;
	} else {

	/* Set current */
	ret = max77804_set_bits(led_data->i2c, reg_led_current[id],
				led_current_mask[id],
				led_data->test_brightness << shift);
	if (unlikely(ret))
		goto error_set_bits;

	/* Turn on LED */
	value = max77804_led_get_en_value(led_data, 1);
	ret = max77804_set_bits(led_data->i2c, MAX77804_LED_REG_FLASH_EN,
				led_en_mask[id],
				value << led_en_shift[id]);
	if (unlikely(ret))
		goto error_set_bits;
	}
#if defined(CONFIG_V1A) || defined(CONFIG_N1A)
	/* change MUIC Control3 to 'Auto Detection' */
	ret = max77804_muic_set_jigset(0x00);
	if (unlikely(ret))
		pr_err("[LED] %s : MUIC 0x0E write failed 0x00 \n", __func__);
#endif
	return;

error_set_bits:
	pr_err("%s: can't set led level %d\n", __func__, ret);

#if defined(CONFIG_V1A) || defined(CONFIG_N1A)
	/* change MUIC Control3 to 'Auto Detection' */
	ret = max77804_muic_set_jigset(0x00);
	if (unlikely(ret))
		pr_err("[LED] %s : MUIC 0x0E write failed 0x00 \n", __func__);
#endif
	return;
}

static void max77804_led_work(struct work_struct *work)
{
	struct max77804_led_data *led_data
		= container_of(work, struct max77804_led_data, work);

	pr_debug("[LED] %s\n", __func__);

	mutex_lock(&led_data->lock);
	led_set(led_data);
	mutex_unlock(&led_data->lock);
}

static int max77804_led_setup(struct max77804_led_data *led_data)
{
	int ret = 0;
	struct max77804_led *data = led_data->data;
	int id = data->id;
	int value;

	ret |= max77804_write_reg(led_data->i2c, MAX77804_LED_REG_VOUT_CNTL,
				MAX77804_BOOST_FLASH_MODE_FLED1);

	ret |= max77804_write_reg(led_data->i2c, MAX77804_LED_REG_VOUT_FLASH,
				  MAX77804_BOOST_VOUT_FLASH_FROM_VOLT(3300));

	ret |= max77804_write_reg(led_data->i2c, MAX77804_CHG_REG_CHG_CNFG_11, 0x2C);

	ret |= max77804_write_reg(led_data->i2c,
				MAX77804_LED_REG_MAX_FLASH1, 0xBC);

	ret |= max77804_write_reg(led_data->i2c,
				MAX77804_LED_REG_MAX_FLASH2, 0x00);

	value = max77804_led_get_en_value(led_data, 0);

	ret |= max77804_set_bits(led_data->i2c, MAX77804_LED_REG_FLASH_EN,
				 led_en_mask[id],
				 value << led_en_shift[id]);

	/* Set TORCH_TMR_DUR or FLASH_TMR_DUR */
	if (reg_led_timer[id] == MAX77804_LED_REG_FLASH_TIMER) {
		ret |= max77804_write_reg(led_data->i2c, reg_led_timer[id],
					(data->timer | data->timer_mode << 7));
	} else {
		ret |= max77804_write_reg(led_data->i2c, reg_led_timer[id],
					0xC0);
	}

	/* Set current */
	ret |= max77804_set_bits(led_data->i2c, reg_led_current[id],
				led_current_mask[id],
				led_data->brightness << led_current_shift[id]);

	return ret;
}

#if defined(CONFIG_SOC_EXYNOS5422_REV_0) || defined(CONFIG_SOC_EXYNOS5430_REV_1)
static ssize_t max77804_flash(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long state = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (isspace(*after))
		count++;

	if (count == size) {
		ret = count;

		if (state > led_cdev->max_brightness)
			state = led_cdev->max_brightness;

		led_cdev->brightness = state;
		if (!(led_cdev->flags & LED_SUSPENDED))
			led_cdev->brightness_set(led_cdev, state);
	}

	return ret;
}

static DEVICE_ATTR(rear_flash, S_IWUSR|S_IWGRP,
	NULL, max77804_flash);
#endif

#if defined(CONFIG_OF)
static int of_max77804_torch_dt(struct max77804_led_platform_data *pdata)
{
	struct device_node *np, *c_np;
	int ret;
	u32 temp;
	const char *temp_str;
	int index;

	np = of_find_node_by_path("/torch");
	if (np == NULL) {
		printk("%s : error to get dt node\n", __func__);
		return -EINVAL;
	}

	pdata->num_leds = of_get_child_count(np);

	for_each_child_of_node(np, c_np) {
		ret = of_property_read_u32(c_np, "id", &temp);
		if (ret) {
			pr_info("%s failed to get a id\n", __func__);
		}
		index = temp;
		pdata->leds[index].id = temp;

		ret = of_property_read_string(c_np, "ledname", &temp_str);
		if (ret) {
			pr_info("%s failed to get a ledname\n", __func__);
		}
		pdata->leds[index].name = temp_str;
		ret = of_property_read_u32(c_np, "timer", &temp);
		if (ret) {
			pr_info("%s failed to get a timer\n", __func__);
		}
		pdata->leds[index].timer = temp;
		ret = of_property_read_u32(c_np, "timer_mode", &temp);
		if (ret) {
			pr_info("%s failed to get a timer_mode\n", __func__);
		}
		pdata->leds[index].timer_mode = temp;
		ret = of_property_read_u32(c_np, "cntrl_mode", &temp);
		if (ret) {
			pr_info("%s failed to get a cntrl_mode\n", __func__);
		}
		pdata->leds[index].cntrl_mode = temp;
		ret = of_property_read_u32(c_np, "brightness", &temp);
		if (ret) {
			pr_info("%s failed to get a brightness\n", __func__);
		}
		pdata->leds[index].brightness = temp;
	}

	return 0;
}
#endif /* CONFIG_OF */

static int max77804_led_probe(struct platform_device *pdev)
{
	int ret = 0;
	int i;
	struct max77804_dev *max77804 = dev_get_drvdata(pdev->dev.parent);
#ifndef CONFIG_OF
	struct max77804_platform_data *max77804_pdata
		= dev_get_platdata(max77804->dev);
#endif
	struct max77804_led_platform_data *pdata;
	struct max77804_led_data *led_data;
	struct max77804_led *data;
	struct max77804_led_data **led_datas;

#ifdef CONFIG_OF
	pdata = kzalloc(sizeof(struct max77804_led_platform_data), GFP_KERNEL);
	if (!pdata) {
		pr_err("%s: failed to allocate driver data\n", __func__);
		return -ENOMEM;
	}
	ret = of_max77804_torch_dt(pdata);
	if (ret < 0) {
		pr_err("max77804-torch : %s not found torch dt! ret[%d]\n",
				 __func__, ret);
		kfree(pdata);
		return -1;
	}
#else
	pdata = max77804_pdata->led_data;
	if (pdata == NULL) {
		pr_err("[LED] no platform data for this led is found\n");
		return -EFAULT;
	}
#endif

	led_datas = kzalloc(sizeof(struct max77804_led_data *)
			    * MAX77804_LED_MAX, GFP_KERNEL);
	if (unlikely(!led_datas)) {
		pr_err("[LED] memory allocation error %s", __func__);
		kfree(pdata);
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, led_datas);

	pr_info("[LED] %s %d leds\n", __func__, pdata->num_leds);

	for (i = 0; i != pdata->num_leds; ++i) {
		pr_info("%s led%d setup ...\n", __func__, i);

		data = kzalloc(sizeof(struct max77804_led), GFP_KERNEL);
		if (unlikely(!data)) {
			pr_err("[LED] memory allocation error %s\n", __func__);
			ret = -ENOMEM;
			continue;
		}

		memcpy(data, &(pdata->leds[i]), sizeof(struct max77804_led));

		led_data = kzalloc(sizeof(struct max77804_led_data),
				   GFP_KERNEL);
		led_datas[i] = led_data;
		if (unlikely(!led_data)) {
			pr_err("[LED] memory allocation error %s\n", __func__);
			ret = -ENOMEM;
			kfree(data);
			continue;
		}

		led_data->max77804 = max77804;
		led_data->i2c = max77804->i2c;
		led_data->data = data;
		led_data->led.name = data->name;
		led_data->led.brightness_set = max77804_led_set;
		led_data->led.brightness = LED_OFF;
		led_data->brightness = data->brightness;
		led_data->led.flags = 0;
		led_data->led.max_brightness =
			reg_led_timer[data->id] == MAX77804_LED_REG_FLASH_TIMER
			? MAX_FLASH_DRV_LEVEL : MAX_TORCH_DRV_LEVEL;

		mutex_init(&led_data->lock);
		spin_lock_init(&led_data->value_lock);
		INIT_WORK(&led_data->work, max77804_led_work);

		ret = led_classdev_register(&pdev->dev, &led_data->led);
		if (unlikely(ret)) {
			pr_err("unable to register LED\n");
			kfree(data);
			kfree(led_data);
			ret = -EFAULT;
			continue;
		}

		ret = max77804_led_setup(led_data);
		if (unlikely(ret)) {
			pr_err("unable to register LED\n");
			mutex_destroy(&led_data->lock);
			led_classdev_unregister(&led_data->led);
			kfree(data);
			kfree(led_data);
			ret = -EFAULT;
		}
	}
	/* print_all_reg_value(max77804->i2c); */
#if defined(CONFIG_SOC_EXYNOS5422_REV_0) || defined(CONFIG_SOC_EXYNOS5430_REV_1)
	if (camera_class == NULL) {
		camera_class = class_create(THIS_MODULE, "camera");
		if (IS_ERR(camera_class)) {
			pr_err("Failed to create class(camera)!\n");
#ifdef CONFIG_OF
			kfree(pdata);
#endif
			return PTR_ERR(camera_class);
		}
	}

	flash_dev = device_create(camera_class, NULL, 0, led_datas[1], "flash");
	if (IS_ERR(flash_dev))
		pr_err("Failed to create device(flash)!\n");

	if (device_create_file(flash_dev, &dev_attr_rear_flash) < 0) {
		pr_err("failed to create device file, %s\n",
				dev_attr_rear_flash.attr.name);
	}
#endif

#ifdef CONFIG_OF
	kfree(pdata);
#endif

	return ret;
}

static int __devexit max77804_led_remove(struct platform_device *pdev)
{
	struct max77804_led_data **led_datas = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i != MAX77804_LED_MAX; ++i) {
		if (led_datas[i] == NULL)
			continue;

		cancel_work_sync(&led_datas[i]->work);
		mutex_destroy(&led_datas[i]->lock);
		led_classdev_unregister(&led_datas[i]->led);
		kfree(led_datas[i]->data);
		kfree(led_datas[i]);
	}
	kfree(led_datas);
#if defined(CONFIG_SOC_EXYNOS5422_REV_0) || defined(CONFIG_SOC_EXYNOS5430_REV_1)
	device_remove_file(flash_dev, &dev_attr_rear_flash);
	device_destroy(camera_class, 0);
	class_destroy(camera_class);
#endif
	return 0;
}

void max77804_led_shutdown(struct device *dev)
{
	struct max77804_led_data **led_datas = dev_get_drvdata(dev);

	/* Turn off LED */
	max77804_set_bits(led_datas[1]->i2c,
		MAX77804_LED_REG_FLASH_EN,
		led_en_mask[1],
		0x02 << led_en_shift[1]);
}

static struct platform_driver max77804_led_driver = {
	.probe		= max77804_led_probe,
	.remove		= __devexit_p(max77804_led_remove),
	.driver		= {
		.name	= "max77804-led",
		.owner	= THIS_MODULE,
		.shutdown = max77804_led_shutdown,
	},
};

static int __init max77804_led_init(void)
{
	return platform_driver_register(&max77804_led_driver);
}
module_init(max77804_led_init);

static void __exit max77804_led_exit(void)
{
	platform_driver_unregister(&max77804_led_driver);
}
module_exit(max77804_led_exit);

MODULE_AUTHOR("ByungChang Cha <bc.cha@samsung.com.com>");
MODULE_DESCRIPTION("MAX77804 LED driver");
MODULE_LICENSE("GPL");
