/*
 * app_config.h
 *
 * Application-level constants derived from Kconfig (menuconfig) settings.
 * All values here are available at compile time.
 *
 * Color encoding: 16-bit RGB565
 *   bits 15-11 : red   (5 bits)
 *   bits 10-5  : green (6 bits)
 *   bits  4-0  : blue  (5 bits)
 */
#pragma once

#include "sdkconfig.h"

/* ---- Color theme (from menuconfig) ------------------------------------ */

#define COLOR_BACKGROUND   ((uint16_t)CONFIG_COLOR_BACKGROUND)
#define COLOR_OUTLINE      ((uint16_t)CONFIG_COLOR_OUTLINE)
#define COLOR_ACTIVE       ((uint16_t)CONFIG_COLOR_ACTIVE)
#define COLOR_FINGER       ((uint16_t)CONFIG_COLOR_FINGER)
#define COLOR_TEXT         ((uint16_t)CONFIG_COLOR_TEXT)

/* ---- Behaviour knobs (from menuconfig) --------------------------------- */

/* Maximum mouse movement units per HID report, per axis */
#define MOUSE_SPEED_MAX    CONFIG_MOUSE_SPEED_MAX

/* Radius (screen px) around nav-center that counts as a tap */
#define TAP_RADIUS         CONFIG_TAP_RADIUS

/* Max touch duration (ms) that still counts as a click */
#define TAP_MAX_MS         CONFIG_TAP_MAX_MS

/* ---- Layout constants ------------------------------------------------- */

/*
 * Navigation area: a circle drawn in the area of the screen that is NOT
 * occupied by the side-button panel.
 *
 * Button panel: a strip along one edge of the screen containing 3 buttons
 * stacked perpendicular to that edge.
 *
 * The values below are computed for the default 480x480 display.
 * device_config.h provides LCD_H_RES and LCD_V_RES.
 */

/* Width (or height, when on top/bottom) of the button panel strip */
#define PANEL_WIDTH_PX     90

/* Number of mode buttons in the panel */
#define PANEL_NUM_BUTTONS  3

/* Finger dot radius (px) drawn at the current touch position */
#define FINGER_DOT_RADIUS  8

/* Outline stroke width for circles and button borders */
#define OUTLINE_WIDTH      3

/* ---- Panel-side shortcuts --------------------------------------------- */

#if defined(CONFIG_PANEL_SIDE_RIGHT)
#   define PANEL_ON_RIGHT   1
#   define PANEL_ON_LEFT    0
#   define PANEL_ON_TOP     0
#   define PANEL_ON_BOTTOM  0
#elif defined(CONFIG_PANEL_SIDE_LEFT)
#   define PANEL_ON_RIGHT   0
#   define PANEL_ON_LEFT    1
#   define PANEL_ON_TOP     0
#   define PANEL_ON_BOTTOM  0
#elif defined(CONFIG_PANEL_SIDE_TOP)
#   define PANEL_ON_RIGHT   0
#   define PANEL_ON_LEFT    0
#   define PANEL_ON_TOP     1
#   define PANEL_ON_BOTTOM  0
#else /* bottom */
#   define PANEL_ON_RIGHT   0
#   define PANEL_ON_LEFT    0
#   define PANEL_ON_TOP     0
#   define PANEL_ON_BOTTOM  1
#endif
