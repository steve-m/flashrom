/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2023 Steve Markgraf <steve@steve-m.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <gpiod.h>
#include "programmer.h"
#include "spi.h"
#include "flash.h"

#define CONSUMER "flashrom"

struct gpiod_spi_data {
	struct gpiod_chip *chip;
	struct gpiod_line_bulk bulk;
	struct gpiod_line *cs_line, *sck_line, *mosi_line, *miso_line;
};

static void linux_gpiod_bitbang_set_cs(int val, void *spi_data)
{
	struct gpiod_spi_data *data = spi_data;
	if (gpiod_line_set_value(data->cs_line, val) < 0)
		msg_perr("Setting cs line failed\n");
}

static void linux_gpiod_bitbang_set_sck(int val, void *spi_data)
{
	struct gpiod_spi_data *data = spi_data;
	if (gpiod_line_set_value(data->sck_line, val) < 0)
		msg_perr("Setting sck line failed\n");
}

static void linux_gpiod_bitbang_set_mosi(int val, void *spi_data)
{
	struct gpiod_spi_data *data = spi_data;
	if (gpiod_line_set_value(data->mosi_line, val) < 0)
		msg_perr("Setting sck line failed\n");
}

static int linux_gpiod_bitbang_get_miso(void *spi_data)
{
	struct gpiod_spi_data *data = spi_data;
	int r = gpiod_line_get_value(data->miso_line);
	if (r < 0)
		msg_perr("Getting miso line failed\n");
	return r;
}

static const struct bitbang_spi_master bitbang_spi_master_gpiod = {
	.set_cs			= linux_gpiod_bitbang_set_cs,
	.set_sck		= linux_gpiod_bitbang_set_sck,
	.set_mosi		= linux_gpiod_bitbang_set_mosi,
	.get_miso		= linux_gpiod_bitbang_get_miso,
};

static int linux_gpiod_spi_shutdown(void *spi_data)
{
	struct gpiod_spi_data *data = spi_data;

	if (gpiod_line_bulk_num_lines(&data->bulk) > 0)
		gpiod_line_release_bulk(&data->bulk);

	if (data->chip)
		gpiod_chip_close(data->chip);

	free(data);

	return 0;
}

static int linux_gpiod_spi_init(const struct programmer_cfg *cfg)
{
	struct gpiod_spi_data *data = NULL;
	struct gpiod_chip *chip = NULL;
	const char *param_str[] = { "cs", "sck", "mosi", "miso", "gpiochip" };
	unsigned int param_int[ARRAY_SIZE(param_str)];
	unsigned int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(param_str); i++) {
		char *param = extract_programmer_param_str(cfg, param_str[i]);
		if (param) {
			param_int[i] = atoi(param);
			free(param);
		} else {
			msg_perr("Missing required programmer parameter %s=<n>\n", param_str[i]);
			goto err_exit;
		}
	}

	data = calloc(1, sizeof(*data));
	if (!data) {
		msg_perr("Unable to allocate space for SPI master data\n");
		goto err_exit;
	}

	chip = gpiod_chip_open_by_number(param_int[4]);
	if (!chip) {
		msg_perr("Failed to open gpiochip: %s\n", strerror(errno));
		goto err_exit;
	}

	data->chip = chip;

	if (gpiod_chip_get_lines(chip, param_int, 4, &data->bulk)) {
		msg_perr("Error getting GPIO lines\n");
		goto err_exit;
	}

	data->cs_line = gpiod_line_bulk_get_line(&data->bulk, 0);
	data->sck_line = gpiod_line_bulk_get_line(&data->bulk, 1);
	data->mosi_line = gpiod_line_bulk_get_line(&data->bulk, 2);
	data->miso_line = gpiod_line_bulk_get_line(&data->bulk, 3);

	r = gpiod_line_request_output(data->cs_line, CONSUMER, 1);
	r |= gpiod_line_request_output(data->sck_line, CONSUMER, 1);
	r |= gpiod_line_request_output(data->mosi_line, CONSUMER, 1);
	r |= gpiod_line_request_input(data->miso_line, CONSUMER);

	if (r < 0) {
		msg_perr("Requesting GPIO lines failed\n");
		goto err_exit;
	}

	if (register_shutdown(linux_gpiod_spi_shutdown, data))
		goto err_exit;

	if (register_spi_bitbang_master(&bitbang_spi_master_gpiod, data))
		return 1; /* shutdown function does the cleanup */

	return 0;

err_exit:
	if (data) {
		if (gpiod_line_bulk_num_lines(&data->bulk) > 0)
			gpiod_line_release_bulk(&data->bulk);

		free(data);
	}

	if (chip)
		gpiod_chip_close(chip);
	return 1;
}

const struct programmer_entry programmer_linux_gpiod = {
	.name			= "linux_gpiod",
	.type			= OTHER,
	.devs.note		= "Device file /dev/gpiochip<n>\n",
	.init			= linux_gpiod_spi_init,
};
