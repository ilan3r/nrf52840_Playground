/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(Less5_Exer1, LOG_LEVEL_DBG);

static const struct device *const uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec leds[] = {
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios),
	GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios),
};
static struct gpio_callback button_cb_data;

static void print_button_press(struct k_work *work)
{
	ARG_UNUSED(work);
	printk("hello from interrupt\n");
}

K_WORK_DEFINE(button_work, print_button_press);

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
			   uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	k_work_submit(&button_work);
}

static void uart_cb(const struct device *dev, void *user_data)
{
	uint8_t received[10];
	int count;

	ARG_UNUSED(user_data);

	if (uart_irq_update(dev) <= 0 || uart_irq_rx_ready(dev) <= 0) {
		return;
	}

	while ((count = uart_fifo_read(dev, received, sizeof(received))) > 0) {
		for (int i = 0; i < count; i++) {
			if (received[i] >= '1' && received[i] <= '3') {
				gpio_pin_toggle_dt(&leds[received[i] - '1']);
			}
		}
	}
}

int main(void)
{
	int ret;
	uint32_t dtr = 0;

	if (!device_is_ready(uart) || !gpio_is_ready_dt(&button)) {
		printk("UART or button GPIO is not ready\n");
		return -ENODEV;
	}

	for (size_t i = 0; i < ARRAY_SIZE(leds); i++) {
		if (!gpio_is_ready_dt(&leds[i])) {
			printk("LED %u GPIO is not ready\n", (unsigned int)i + 1);
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&leds[i], GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			printk("LED %u configuration failed: %d\n", (unsigned int)i + 1, ret);
			return ret;
		}
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret < 0) {
		printk("Button configuration failed: %d\n", ret);
		return ret;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	ret = gpio_add_callback(button.port, &button_cb_data);
	if (ret < 0) {
		printk("Button callback registration failed: %d\n", ret);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		printk("Button interrupt configuration failed: %d\n", ret);
		return ret;
	}

	ret = uart_irq_callback_user_data_set(uart, uart_cb, NULL);
	if (ret < 0) {
		printk("UART callback registration failed: %d\n", ret);
		return ret;
	}

	uart_irq_rx_enable(uart);

	/* Wait for the terminal to open before printing the instructions. */
	while (uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr) == 0 && dtr == 0) {
		k_msleep(100);
	}

	LOG_INF("nRF Connect SDK Fundamentals Course");
	printk("Press 1-3 to toggle the three external LEDs.\r\n");

	return 0;
}
