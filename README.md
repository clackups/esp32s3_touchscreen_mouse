# esp32s3_touchscreen_mouse

An ESP32-S3 device with a touchscreen that presents itself as a USB HID mouse
over its native USB port.

## How it works

The user interacts with a navigation area drawn on the touchscreen:

- **Pointer movement**: pull a finger from the centre of the navigation
  circle toward the edge.  The further from the centre, the faster the
  mouse pointer moves.  The direction of the offset maps directly to the
  pointer direction.
- **Click**: tap quickly near the centre of the navigation circle (within
  the configurable tap-zone radius).
- **Side panel buttons** (three buttons along one edge of the screen):
  1. **DRAG** - latches the left mouse button down.  Moving in the
     navigation area drags whatever is under the pointer.  Tap again or
     release the finger to end drag mode.
  2. **SCROLL** - vertical movement in the navigation area generates
     scroll-wheel events instead of pointer movement.  Tap the button or
     the navigation area to exit scroll mode.
  3. **RCLICK** - sends a single right mouse button click.

## Supported hardware

| Device | Display | Touch |
|---|---|---|
| VIEWE UEDX80480043E-WB-A | ST7262 800x480 (16-bit RGB565) | GT911 (I2C) |

Links:
- https://viewedisplay.com/product/esp32-4-3-inch-800x480-rgb-ips-tft-display-touch-screen-arduino-lvgl/
- https://github.com/VIEWESMART/UEDX80480043ESP32-4.3inch-Touch-Display

## Building

Requires ESP-IDF v5.2 or later.

```bash
# Set target
idf.py set-target esp32s3

# (Optional) customise device, colors, speed
idf.py menuconfig         # navigate to "TOUCHSCREEN MOUSE"

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration (menuconfig)

All options live under the **TOUCHSCREEN MOUSE** menu:

| Option | Default | Description |
|---|---|---|
| Target device | VIEWE UEDX80480043E-WB-A | Hardware pinout selection |
| Side panel position | Right | Which screen edge holds the buttons |
| Background color | 0x0000 (black) | RGB565 hex |
| Outline color | 0x0010 (navy blue) | RGB565 hex |
| Active color | 0x87F7 (mint green) | RGB565 hex |
| Finger dot color | 0xFFFF (white) | RGB565 hex |
| Text/icon color | 0xFFFF (white) | RGB565 hex |
| Max mouse speed | 20 | HID units per report per axis |
| Tap zone radius | 30 | px from nav centre counted as a click |
| Max tap duration | 300 | ms; longer touch = movement, not click |

## Project structure

```
esp32s3_touchscreen_mouse/
+-- AGENTS.md              contributor rules (ASCII-only policy, etc.)
+-- CMakeLists.txt         ESP-IDF project root
+-- sdkconfig.defaults     default SDK settings
+-- partitions.csv         flash partition table
+-- main/
    +-- CMakeLists.txt
    +-- Kconfig.projbuild  menuconfig options
    +-- app_config.h       layout constants, color aliases
    +-- device_config.h    GPIO pin assignments per device
    +-- hid_mouse.h/.c     TinyUSB USB HID mouse
    +-- display.h/.c       RGB display + drawing primitives
    +-- touch.h/.c         GT911 I2C touch driver
    +-- ui.h/.c            navigation area, buttons, mode state machine
    +-- main.c             entry point, FreeRTOS tasks
```

## License

MIT - see LICENSE.