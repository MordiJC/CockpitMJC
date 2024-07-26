/*
 * Copyright (c) 2024 Jakub Czapiga
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT led_multiplex_matrix

#include "zephyr/sys/util.h"
#include "zephyr/sys/util_macro.h"
#include <soc.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/gpio/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(led_multiplex_matrix, CONFIG_DISPLAY_LOG_LEVEL);

struct led_multiplex_matrix_config {
	uint32_t rows;
	const struct gpio_dt_spec *row_gpios;
	uint32_t cols;
	const struct gpio_dt_spec *col_gpios;
	uint32_t refresh_frequency;
};

struct led_multiplex_matrix_data {
	struct k_timer timer;
	uint8_t *framebuf;
	uint8_t current_row;
	bool blanking;
};

static int api_blanking_on(const struct device *dev)
{
	const struct led_multiplex_matrix_config *config = dev->config;
	struct led_multiplex_matrix_data *data = dev->data;

	if (!data->blanking) {
		k_timer_stop(&data->timer);
		for (int i = 0; i < config->cols; ++i) {
			const struct gpio_dt_spec *c = &config->col_gpios[i];
			gpio_pin_set(c->port, c->pin, 0);
		}
		for (int i = 0; i < config->rows; ++i) {
			const struct gpio_dt_spec *r = &config->row_gpios[i];
			gpio_pin_set(r->port, r->pin, 0);
		}

		data->blanking = true;
	}

	return 0;
}

static int api_blanking_off(const struct device *dev)
{
	const struct led_multiplex_matrix_config *config = dev->config;
	struct led_multiplex_matrix_data *data = dev->data;

	if (data->blanking) {
		data->current_row = config->rows - 1;

		const k_timeout_t tval =
			(k_timeout_t)K_SECONDS(1.0f / (config->refresh_frequency * config->rows));
		k_timer_start(&data->timer, tval, tval);

		data->blanking = false;
	}

	return 0;
}

static void *api_get_framebuffer(const struct device *dev)
{
	struct led_multiplex_matrix_data *data = dev->data;

	return data->framebuf;
}

static void api_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	const struct led_multiplex_matrix_config *config = dev->config;

	caps->x_resolution = config->cols;
	caps->y_resolution = config->rows;
	caps->supported_pixel_formats = PIXEL_FORMAT_MONO01;
	caps->screen_info = 0;
	caps->current_pixel_format = PIXEL_FORMAT_MONO01;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int api_set_pixel_format(const struct device *dev, const enum display_pixel_format format)
{
	switch (format) {
	case PIXEL_FORMAT_MONO01:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int api_set_orientation(const struct device *dev, const enum display_orientation orientation)
{
	switch (orientation) {
	case DISPLAY_ORIENTATION_NORMAL:
		return 0;
	default:
		return -ENOTSUP;
	}
}

static inline void move_to_next_pixel(uint8_t *mask, uint8_t *data, const uint8_t **byte_buf)
{
	*mask <<= 1;
	if (!*mask) {
		*mask = 0x01;
		*data = *(*byte_buf)++;
	}
}

static int api_write(const struct device *dev, const uint16_t x, const uint16_t y,
		     const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct led_multiplex_matrix_config *config = dev->config;
	struct led_multiplex_matrix_data *data = dev->data;
	uint16_t end_x = x + desc->width;
	uint16_t end_y = y + desc->height;

	if (x >= config->cols || end_x > config->cols || y >= config->rows ||
	    end_y > config->rows) {
		return -EINVAL;
	}

	if (desc->pitch < desc->width) {
		return -EINVAL;
	}

	uint16_t to_skip = desc->pitch - desc->width;
	const uint8_t *byte_buf = buf;
	uint8_t mask = 0;
	uint8_t value = 0;

	for (uint16_t py = y; py < end_y; ++py) {
		for (uint16_t px = x; px < end_x; ++px) {
			move_to_next_pixel(&mask, &value, &byte_buf);
			/*const size_t bit_offset = px + py * config->cols; // Select bit in bitmap
			const size_t byte_offset = bit_offset / NUM_BITS(uint8_t);
			const uint8_t x_mask = BIT(bit_offset % NUM_BITS(uint8_t));
			data->framebuf[byte_offset] &= (value & mask) ? x_mask : ~x_mask;*/
			data->framebuf[py * config->cols + px] = (value & mask) ? 1 : 0;
		}

		for (uint16_t i = 0; i < to_skip; ++i) {
			move_to_next_pixel(&mask, &value, &byte_buf);
		}
	}

	return 0;
}

const struct display_driver_api led_multiplex_matrix_display_api = {
	.blanking_on = api_blanking_on,
	.blanking_off = api_blanking_off,
	.get_framebuffer = api_get_framebuffer,
	.get_capabilities = api_get_capabilities,
	.set_pixel_format = api_set_pixel_format,
	.set_orientation = api_set_orientation,
	.write = api_write,
};

#define calculate_previous_row(i, rows) ((i) == 0 ? (rows - 1) : (i - 1))

static void led_multiplex_matrix_draw(struct k_timer *timer)
{
	const struct device *dev = k_timer_user_data_get(timer);
	const struct led_multiplex_matrix_config *config = dev->config;
	struct led_multiplex_matrix_data *data = dev->data;

	// Disable previous row
	const uint32_t previous_row = calculate_previous_row(data->current_row, config->rows);
	const struct gpio_dt_spec *p = &config->row_gpios[previous_row];
	gpio_pin_set_dt(p, 0);

	// Enable current row.
	for (int i = 0; i < config->cols; ++i) {
		/*const size_t bit_offset =
			i + data->current_row * config->cols; // Select bit in bitmap
		const size_t byte_offset = bit_offset / NUM_BITS(uint8_t);
		const uint8_t mask = BIT(bit_offset % NUM_BITS(uint8_t));
		const struct gpio_dt_spec *c = &config->col_gpios[i];
		gpio_pin_set_dt(c, data->framebuf[byte_offset] & mask ? 1 : 0);*/

		const struct gpio_dt_spec *c = &config->col_gpios[i];
		gpio_pin_set_dt(c, data->framebuf[data->current_row * config->cols + i] ? 1 : 0);
	}
	const struct gpio_dt_spec *c = &config->row_gpios[data->current_row];
	gpio_pin_set_dt(c, 1);

	// Advance row
	data->current_row = (data->current_row + 1) % config->rows;
}

static int init_gpio_list(const struct gpio_dt_spec *gpios, uint32_t n)
{
	int ret = 0;

	for (int i = 0; i < n; i++) {
		const struct gpio_dt_spec *gpio = &gpios[i];
		if (!gpio_is_ready_dt(gpio)) {
			return -ENODEV;
		}

		if (gpio->dt_flags & GPIO_ACTIVE_LOW) {
			ret = gpio_pin_configure_dt(gpio, GPIO_OUTPUT_ACTIVE);
		} else {
			ret = gpio_pin_configure_dt(gpio, GPIO_OUTPUT_INACTIVE);
		}

		if (ret < 0) {
			return ret;
		}
	}

	return ret;
}

static int led_multiplex_matrix_init(const struct device *dev)
{
	const struct led_multiplex_matrix_config *config = dev->config;
	struct led_multiplex_matrix_data *data = dev->data;

	int ret = init_gpio_list(config->row_gpios, config->rows);
	if (ret < 0) {
		return ret;
	}
	ret = init_gpio_list(config->col_gpios, config->cols);
	if (ret < 0) {
		return ret;
	}

	k_timer_init(&data->timer, led_multiplex_matrix_draw, NULL);
	k_timer_user_data_set(&data->timer, (void *)dev);

	const k_timeout_t tval =
		(k_timeout_t)K_SECONDS(1.0f / (config->refresh_frequency * config->rows));
	k_timer_start(&data->timer, tval, tval);

	return 0;
}

/*static uint8_t led_multiplex_matrix_framebuf_##n[DIV_ROUND_UP(                             \
	DT_INST_PROP(n, width) * DT_INST_PROP(n, height), NUM_BITS(uint8_t))];             \*/
#define LED_MULTIPLEX_MATRIX_INIT(n)                                                               \
	static uint8_t led_multiplex_matrix_framebuf_##n[DT_INST_PROP(n, width) *                  \
							 DT_INST_PROP(n, height)] = {};            \
	static struct led_multiplex_matrix_data led_multiplex_matrix_data_##n = {                  \
		.framebuf = led_multiplex_matrix_framebuf_##n,                                     \
	};                                                                                         \
	static const struct gpio_dt_spec led_multiplex_matrix_row_gpios_##n[DT_INST_PROP_LEN(      \
		n, row_gpios)] = {                                                                 \
		DT_INST_FOREACH_PROP_ELEM_SEP(n, row_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, ))};       \
	static const struct gpio_dt_spec led_multiplex_matrix_col_gpios_##n[DT_INST_PROP_LEN(      \
		n, col_gpios)] = {                                                                 \
		DT_INST_FOREACH_PROP_ELEM_SEP(n, col_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, ))};       \
                                                                                                   \
	static const struct led_multiplex_matrix_config led_multiplex_matrix_config_##n = {        \
		.rows = DT_INST_PROP(n, height),                                                   \
		.row_gpios = led_multiplex_matrix_row_gpios_##n,                                   \
		.cols = DT_INST_PROP(n, width),                                                    \
		.col_gpios = led_multiplex_matrix_col_gpios_##n,                                   \
		.refresh_frequency = DT_INST_PROP(n, refresh_frequency),                           \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, &led_multiplex_matrix_init, NULL, &led_multiplex_matrix_data_##n, \
			      &led_multiplex_matrix_config_##n, POST_KERNEL,                       \
			      CONFIG_DISPLAY_INIT_PRIORITY, &led_multiplex_matrix_display_api);

DT_INST_FOREACH_STATUS_OKAY(LED_MULTIPLEX_MATRIX_INIT)
