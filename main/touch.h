/*
 * touch.h
 *
 * GT911 capacitive touch controller driver over I2C.
 *
 * The driver auto-detects the board's GT911 address, polls one touch point,
 * and converts the raw controller state into DOWN / MOVE / UP events.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Touch event types */
typedef enum {
    TOUCH_EVENT_NONE = 0, /* No touch active */
    TOUCH_EVENT_DOWN,     /* Finger placed on screen  */
    TOUCH_EVENT_MOVE,     /* Finger moved while down  */
    TOUCH_EVENT_UP,       /* Finger lifted            */
} touch_event_t;

/* Single-point touch report */
typedef struct {
    touch_event_t event; /* What happened              */
    int           x;     /* X coordinate (0..LCD_H_RES-1) */
    int           y;     /* Y coordinate (0..LCD_V_RES-1) */
} touch_data_t;

/*
 * touch_init
 *
 * Initialise the I2C bus and the GT911 touch controller.
 * Must be called once before touch_read().
 * Returns 0 on success, non-zero on error.
 */
int touch_init(void);

/*
 * touch_read
 *
 * Read the current touch state from the GT911.
 * Fills *out with the latest event and coordinates.
 * Returns 0 on success, -1 on I2C error (previous data in *out is
 * left unchanged on error).
 */
int touch_read(touch_data_t *out);
