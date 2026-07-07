/*
 * hid_mouse.h
 *
 * USB HID mouse interface using the ESP-IDF TinyUSB stack.
 *
 * The mouse report has 4 bytes:
 *   byte 0 : button mask  (bit 0 = left, bit 1 = right, bit 2 = middle)
 *   byte 1 : X movement   (-127 .. +127)
 *   byte 2 : Y movement   (-127 .. +127)
 *   byte 3 : scroll wheel (-127 .. +127, positive = scroll up)
 *
 * Axis directions follow the USB HID convention (positive X = right,
 * positive Y = down).
 *
 * Note: esp_tinyusb v2.x manages its own internal tud_task() loop.
 * No external USB task is needed.
 */
#pragma once

#include <stdint.h>

/* Button bitmask constants */
#define HID_BTN_LEFT    (1u << 0)
#define HID_BTN_RIGHT   (1u << 1)
#define HID_BTN_MIDDLE  (1u << 2)

/*
 * hid_mouse_init
 *
 * Initialise and start the TinyUSB stack in HID device mode.
 * Must be called once before any send functions.
 * Returns 0 on success, non-zero on error.
 */
int hid_mouse_init(void);

/*
 * hid_mouse_send
 *
 * Enqueue a single HID mouse report.
 *
 *   buttons  - bitmask of HID_BTN_* flags (0 = all released)
 *   dx       - horizontal movement  (-127..+127)
 *   dy       - vertical movement    (-127..+127)
 *   scroll   - scroll-wheel delta   (-127..+127)
 *
 * Returns 0 if the report was accepted, -1 if the USB endpoint was busy.
 */
int hid_mouse_send(uint8_t buttons, int8_t dx, int8_t dy, int8_t scroll);
