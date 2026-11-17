/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   (10*6*1000)
#define MAX_NUMBER_FACT 10

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
#define SW0_NODE DT_ALIAS(sw0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(SW0_NODE, gpios);

static struct gpio_callback button_cb_data;

void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int i;
	long int factorial = 1;

	printk("Calculating the factorials of numbers from 1 to %d:\n", MAX_NUMBER_FACT);
	for (i = 1; i <= MAX_NUMBER_FACT; i++) {
		factorial = factorial * i;
		printk("The factorial of %2d = %ld\n", i, factorial);
	}
	printk("_______________________________________________________\n");
	/*Important note!
	Code in ISR runs at a high priority, therefore, it should be written with timing in mind.
	Too lengthy or too complex tasks should not be performed by an ISR, they should be deferred
	to a thread.
	*/
}

int main(void)
{
	int ret;

	if (!gpio_is_ready_dt(&led) || !gpio_is_ready_dt(&button)) {
		printf("LED or button GPIO is not ready\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		printf("LED configuration failed: %d\n", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		printf("Button configuration failed: %d\n", ret);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret < 0) {
		printf("Button callback registration failed: %d\n", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		printf("Button interrupt configuration failed: %d\n", ret);
		return ret;
	}

	printf("Button interrupt ready\n");

	while (1) {
		k_msleep(SLEEP_TIME_MS);
	}
	return 0;
}
