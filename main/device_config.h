/*
 * device_config.h
 *
 * GPIO pin assignments and display timing parameters for each supported
 * target device.  Select the device via:
 *   idf.py menuconfig -> TOUCHSCREEN MOUSE -> Target device
 *
 * To add a new device:
 *   1. Add a CONFIG_DEVICE_xxx option in Kconfig.projbuild.
 *   2. Add a matching #elif block below with the correct pin numbers.
 */
#pragma once

#include "sdkconfig.h"

/* ======================================================================
 * VIEWE UEDX80480043E-WB-A
 *   4.3-inch 800x480 TFT
 *   Display controller : ST7262E43-G4 (16-bit RGB565 parallel interface)
 *   Touch controller   : GT911 (I2C)
 *
 * References:
 *   https://viewedisplay.com/product/esp32-4-3-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/
 *   https://github.com/VIEWESMART/UEDX80480043ESP32-4.3inch-Touch-Display
 * ====================================================================== */
#if defined(CONFIG_DEVICE_VIEWE_UEDX80480043E_WB_A)

/* ---- Display resolution ----------------------------------------------- */
#define LCD_H_RES          800
#define LCD_V_RES          480

/* ---- RGB timing (vendor reference) ------------------------------------- */
#define LCD_PIXEL_CLOCK_HZ (15 * 1000 * 1000)
#define LCD_HBP            42   /* horizontal back porch  */
#define LCD_HFP            20   /* horizontal front porch */
#define LCD_HSW            1    /* horizontal sync width  */
#define LCD_VBP            12   /* vertical back porch    */
#define LCD_VFP            4    /* vertical front porch   */
#define LCD_VSW            10   /* vertical sync width    */

/* ---- RGB timing polarity flags ---------------------------------------- */
/* HSYNC and VSYNC are active-low (idle high) on this panel.
 * PCLK data is captured on the falling edge (active-neg). */
#define LCD_HSYNC_IDLE_LOW  0
#define LCD_VSYNC_IDLE_LOW  0
#define LCD_PCLK_ACTIVE_NEG 1

/* ---- RGB parallel interface GPIOs ------------------------------------- */
#define LCD_PCLK_GPIO      42
#define LCD_VSYNC_GPIO     41
#define LCD_HSYNC_GPIO     39
#define LCD_DE_GPIO        40

/*
 * 16 data lines in bit order B0..B4, G0..G5, R0..R4.
 * (index 0 = LSB of the 16-bit RGB565 word on the bus)
 */
#define LCD_DATA_GPIO_B0   8
#define LCD_DATA_GPIO_B1   3
#define LCD_DATA_GPIO_B2   46
#define LCD_DATA_GPIO_B3   9
#define LCD_DATA_GPIO_B4   1
#define LCD_DATA_GPIO_G0   5
#define LCD_DATA_GPIO_G1   6
#define LCD_DATA_GPIO_G2   7
#define LCD_DATA_GPIO_G3   15
#define LCD_DATA_GPIO_G4   16
#define LCD_DATA_GPIO_G5   4
#define LCD_DATA_GPIO_R0   45
#define LCD_DATA_GPIO_R1   48
#define LCD_DATA_GPIO_R2   47
#define LCD_DATA_GPIO_R3   21
#define LCD_DATA_GPIO_R4   14

/* ---- Backlight -------------------------------------------------------- */
#define LCD_BL_GPIO        2

/* ---- Touch (GT911 over I2C) ------------------------------------------- */
#define TOUCH_I2C_PORT     0
#define TOUCH_I2C_SDA_GPIO 19
#define TOUCH_I2C_SCL_GPIO 20
#define TOUCH_I2C_ADDR_1   0x5D
#define TOUCH_I2C_ADDR_2   0x14
#define TOUCH_I2C_FREQ_HZ  400000

#else
#   error "No target device selected. Run: idf.py menuconfig -> TOUCHSCREEN MOUSE"
#endif
