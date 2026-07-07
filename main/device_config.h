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
 * Guition ESP32-4848S040C_I
 *   4-inch 480x480 TFT
 *   Display controller : ST7701S (16-bit RGB parallel interface)
 *   Touch controller   : GT911   (I2C)
 *
 * References:
 *   https://www.guition.com/esp32-display-module/4-inch-esp32s3-display-module
 *   https://devices.esphome.io/devices/guition-esp32-s3-4848s040
 *   https://github.com/alaltitov/Guition-ESP32-S3-4848S040
 * ====================================================================== */
#if defined(CONFIG_DEVICE_GUITION_4848S040)

/* ---- Display resolution ----------------------------------------------- */
#define LCD_H_RES          480
#define LCD_V_RES          480

/* ---- RGB timing (pixel clock ~12 MHz with board-tested porch values) --- */
#define LCD_PIXEL_CLOCK_HZ (12 * 1000 * 1000)
#define LCD_HBP            50   /* horizontal back porch  */
#define LCD_HFP            20   /* horizontal front porch */
#define LCD_HSW            8    /* horizontal sync width  */
#define LCD_VBP            20   /* vertical back porch    */
#define LCD_VFP            10   /* vertical front porch   */
#define LCD_VSW            8    /* vertical sync width    */

/* ---- RGB parallel interface GPIOs ------------------------------------- */
#define LCD_PCLK_GPIO      21
#define LCD_VSYNC_GPIO     17
#define LCD_HSYNC_GPIO     16
#define LCD_DE_GPIO        18

/*
 * 16 data lines in bit order B0..B4, G0..G5, R0..R4.
 * (index 0 = LSB of the 16-bit RGB565 word on the bus)
 */
#define LCD_DATA_GPIO_B0   4
#define LCD_DATA_GPIO_B1   5
#define LCD_DATA_GPIO_B2   6
#define LCD_DATA_GPIO_B3   7
#define LCD_DATA_GPIO_B4   15
#define LCD_DATA_GPIO_G0   8
#define LCD_DATA_GPIO_G1   20
#define LCD_DATA_GPIO_G2   3
#define LCD_DATA_GPIO_G3   46
#define LCD_DATA_GPIO_G4   9
#define LCD_DATA_GPIO_G5   10
#define LCD_DATA_GPIO_R0   11
#define LCD_DATA_GPIO_R1   12
#define LCD_DATA_GPIO_R2   13
#define LCD_DATA_GPIO_R3   14
#define LCD_DATA_GPIO_R4   0

/* ---- Backlight -------------------------------------------------------- */
#define LCD_BL_GPIO        38

/* ---- ST7701S init-command SPI (3-wire, bit-banged) -------------------- */
/*
 * The ST7701S initialisation commands are clocked in over a separate
 * 9-bit 3-wire SPI bus BEFORE the RGB interface is active.
 */
#define LCD_SPI_CS_GPIO    39
#define LCD_SPI_SCK_GPIO   48
#define LCD_SPI_MOSI_GPIO  47

/* ---- Touch (GT911 over I2C) ------------------------------------------- */
#define TOUCH_I2C_PORT     0
#define TOUCH_I2C_SDA_GPIO 19
#define TOUCH_I2C_SCL_GPIO 45
#define TOUCH_I2C_ADDR_1   0x5D
#define TOUCH_I2C_ADDR_2   0x14
#define TOUCH_I2C_FREQ_HZ  100000

#else
#   error "No target device selected. Run: idf.py menuconfig -> TOUCHSCREEN MOUSE"
#endif
