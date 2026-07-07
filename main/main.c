/*
 * main.c
 *
 * Entry point for the ESP32-S3 touchscreen mouse project.
 *
 * Startup sequence:
 *   1. Initialise USB HID mouse (TinyUSB).
 *   2. Initialise the display (ST7701S RGB panel).
 *   3. Draw the initial UI.
 *   4. Initialise the touch controller (GT911 I2C).
 *   5. Enter the main touch polling loop.
 *
 * Note: the TinyUSB event loop is managed internally by esp_tinyusb v2.x.
 * No separate USB task needs to be created by the application.
 */

#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "hid_mouse.h"
#include "display.h"
#include "touch.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "ESP32-S3 Touchscreen Mouse starting");

    /* ---- USB HID ---- */
    if (hid_mouse_init() != 0) {
        ESP_LOGE(TAG, "HID mouse init failed");
        return;
    }

    /* ---- Display ---- */
    if (display_init() != 0) {
        ESP_LOGE(TAG, "Display init failed");
        return;
    }

    /* ---- UI ---- */
    ui_init();

    /* ---- Touch ---- */
    bool touch_ready = (touch_init() == 0);
    if (!touch_ready) {
        ESP_LOGE(TAG, "Touch init failed, continuing without touch input");
    }

    ESP_LOGI(TAG, "All subsystems ready.  Entering touch loop.");

    /* ---- Main loop: poll touch at ~60 Hz ---- */
    touch_data_t td = { .event = TOUCH_EVENT_NONE, .x = 0, .y = 0 };

    while (1) {
        if (touch_ready && touch_read(&td) == 0) {
            ui_process_touch(&td);
        }

        ui_refresh();

        vTaskDelay(pdMS_TO_TICKS(16)); /* ~60 Hz */
    }
}
