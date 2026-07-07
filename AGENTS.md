# AGENTS.md - Contributing Agent Rules

## Critical Rules

1. **ASCII only**: Every file in this repository MUST use only ASCII characters
   (code points 0x00-0x7F). No UTF-8 multi-byte characters, no Unicode symbols,
   no copyright signs, no fancy quotes, no accented letters. This applies to:
   - All C/C++ source files (.c, .h)
   - All CMake files (CMakeLists.txt, *.cmake)
   - All Kconfig files
   - All Markdown files (.md)
   - All configuration files (.defaults, .csv, .yml, .json)
   - All other text files

2. **ESP-IDF version**: Target ESP-IDF v5.2 or later. The USB TinyUSB device
   stack requires ESP-IDF >= 5.0.

3. **No external components required at build time**: All drivers (display,
   touch) are implemented inline in the main component. The project must build
   with a plain `idf.py build` after setting up the IDF environment.

## Code Style

- Use 4-space indentation.
- Keep lines under 100 characters.
- Add a blank line between logical code blocks inside functions.
- Function names use snake_case.
- Macros and constants use UPPER_SNAKE_CASE.
- Every public function must have a brief comment above it.

## Project Structure

```
esp32s3_touchscreen_mouse/
+-- AGENTS.md              (this file)
+-- CMakeLists.txt         (ESP-IDF project root)
+-- Kconfig.projbuild      (menuconfig options: device, colors)
+-- sdkconfig.defaults     (default SDK settings)
+-- partitions.csv         (flash partition table)
+-- README.md
+-- main/
    +-- CMakeLists.txt
    +-- app_config.h       (layout constants, color aliases from Kconfig)
    +-- device_config.h    (GPIO pin assignments per device)
    +-- hid_mouse.h/.c     (TinyUSB USB HID mouse)
    +-- display.h/.c       (ST7701S RGB display + drawing primitives)
    +-- touch.h/.c         (CST820 I2C touch driver)
    +-- ui.h/.c            (UI logic: navigation area, buttons, modes)
    +-- main.c             (entry point, FreeRTOS tasks)
```

## Adding a New Target Device

1. Add a new `config DEVICE_xxx` option in `Kconfig.projbuild`.
2. Add an `#elif defined(CONFIG_DEVICE_xxx)` block in `main/device_config.h`
   with the correct GPIO pin assignments and display timing parameters.
3. Adjust the ST7701S init sequence in `main/display.c` if the panel needs
   different register values.
4. Rebuild and test.

## Adding or Changing Colors

All colors are RGB565 (16-bit) integers. Change the defaults in
`Kconfig.projbuild` or override them at build time with:

    idf.py menuconfig

Navigate to "TOUCHSCREEN MOUSE" and adjust the hex values there.
