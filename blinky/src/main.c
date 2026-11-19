/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 /*
 
 connect BNO055 to the nice!nano board instead of the BME280 board they use in the exercise 
 BNO055  | nice!nano board 
 VIN | VCC/3V3
 GND | GND
 SDA | D2/P0.17
 SCL | D3/P0.20 
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>

#define REG_ACC_DATA_X_LSB 0x08
#define SLEEP_TIME_MS 1000

/* BNO055 register addresses */
#define REG_CHIP_ID      0x00
#define REG_PAGE_ID      0x07
#define REG_TEMP         0x34
#define REG_UNIT_SEL     0x3B
#define REG_OPR_MODE     0x3D
#define REG_PWR_MODE     0x3E
#define REG_TEMP_SOURCE  0x40

/* Register values */
#define BNO055_CHIP_ID   0xA0
#define MODE_CONFIG      0x00
#define MODE_ACCONLY     0x01

/* get the node identifier of the sensor, defined in nice_nano.overlay */
#define SENSOR_NODE DT_NODELABEL(bno055)

static const struct i2c_dt_spec dev_i2c =
    I2C_DT_SPEC_GET(SENSOR_NODE);


int main(void)
{
    int ret;
    uint8_t chip_id;
    uint8_t temperature_raw;

    /* Check that the I2C controller is ready. */
    if (!i2c_is_ready_dt(&dev_i2c)) {
        printk("I2C controller is not ready\n");
        return -1;
    }

    /* Allow the BNO055 time to boot after power-on. */
    k_msleep(1000);

    /* Select register page 0. */
    ret = i2c_reg_write_byte_dt(&dev_i2c, REG_PAGE_ID, 0x00);
    if (ret < 0) {
        printk("Page selection failed: %d\n", ret);
        return ret;
    }

    /* Read and verify the sensor's identity. */
    ret = i2c_reg_read_byte_dt(&dev_i2c, REG_CHIP_ID, &chip_id);
    if (ret < 0) {
        printk("Chip ID read failed: %d\n", ret);
        return ret;
    }

    printk("BNO055 chip ID: 0x%02x\n", chip_id);

    if (chip_id != BNO055_CHIP_ID) {
        printk("Unexpected chip ID; expected 0xA0\n");
        return -1;
    }

    /* Enter configuration mode before changing settings. */
    ret = i2c_reg_write_byte_dt(&dev_i2c, REG_OPR_MODE, MODE_CONFIG);
    if (ret < 0) {
        printk("Entering configuration mode failed: %d\n", ret);
        return ret;
    }

    k_msleep(30);

    /* Set normal power mode. */
    ret = i2c_reg_write_byte_dt(&dev_i2c, REG_PWR_MODE, 0x00);
    if (ret < 0) {
        printk("Power configuration failed: %d\n", ret);
        return ret;
    }

    k_msleep(10);

    /* Select default units, including degrees Celsius. */
    ret = i2c_reg_write_byte_dt(&dev_i2c, REG_UNIT_SEL, 0x00);
    if (ret < 0) {
        printk("Unit configuration failed: %d\n", ret);
        return ret;
    }

    /* Use the accelerometer's internal temperature sensor. */
    ret = i2c_reg_write_byte_dt(&dev_i2c, REG_TEMP_SOURCE, 0x00);
    if (ret < 0) {
        printk("Temperature source configuration failed: %d\n", ret);
        return ret;
    }

    /* Enable the accelerometer. */
    ret = i2c_reg_write_byte_dt(&dev_i2c, REG_OPR_MODE, MODE_ACCONLY);
    if (ret < 0) {
        printk("Starting measurement mode failed: %d\n", ret);
        return ret;
    }

    k_msleep(100);

    while (1) {
		/* Read all three axes in one I2C transaction.
		* Each axis occupies two bytes, least-significant byte first.
		*/
		uint8_t acceleration_data[6];

		ret = i2c_burst_read_dt(
			&dev_i2c,
			REG_ACC_DATA_X_LSB,
			acceleration_data,
			sizeof(acceleration_data));

		if (ret < 0) {
			printk("Acceleration read failed: %d\n", ret);
		} else {
			int16_t x_raw = (int16_t)sys_get_le16(&acceleration_data[0]);
			int16_t y_raw = (int16_t)sys_get_le16(&acceleration_data[2]);
			int16_t z_raw = (int16_t)sys_get_le16(&acceleration_data[4]);

			/* With UNIT_SEL = 0x00, 100 counts represents 1 m/s^2. */
			printk("Acceleration: X=%.2f, Y=%.2f, Z=%.2f m/s^2\n",
				x_raw / 100.0,
				y_raw / 100.0,
				z_raw / 100.0);
		}

		k_msleep(SLEEP_TIME_MS);
    }
}