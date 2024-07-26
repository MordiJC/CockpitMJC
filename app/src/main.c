
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

struct semihalfino_7x7_image {
	union {
		struct {
			uint8_t c1: 1, c2: 1, c3: 1, c4: 1, c5: 1, c6: 1, c7: 1;

		} r[7];
		uint8_t row[7];
	};
};

#define SH_IMAGE(_rows...) { .r = { _rows } }

struct semihalfino_7x7_image image = SH_IMAGE({1, 0, 0, 0, 0, 0, 0},
					      {0, 1, 0, 0, 0, 0, 0},
					      {0, 0, 1, 0, 0, 0, 0},
					      {0, 0, 0, 1, 0, 0, 0},
					      {0, 0, 0, 0, 1, 0, 0},
					      {0, 0, 0, 0, 0, 1, 0},
					      {0, 0, 0, 0, 0, 0, 1});

int main(void)
{
	LOG_INIT();

	const struct device *display_dev;
	struct display_capabilities capabilities;
	struct display_buffer_descriptor buf_desc;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	device_init(display_dev);
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device %s not found. Aborting sample.", display_dev->name);
#ifdef CONFIG_ARCH_POSIX
		posix_exit_main(1);
#else
		return 0;
#endif
	}

	buf_desc.buf_size = sizeof(image);
	buf_desc.pitch = 8;
	buf_desc.width = 7;
	buf_desc.height = 7;

	LOG_INF("Display sample for %s", display_dev->name);
	display_get_capabilities(display_dev, &capabilities);

	display_write(display_dev, 0, 0, &buf_desc, &image);

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
