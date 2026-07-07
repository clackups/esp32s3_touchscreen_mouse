/*
 * ui.h
 *
 * UI logic: navigation area, side-panel buttons, touch-to-mouse mapping.
 *
 * Screen layout (default: button panel on the RIGHT side)
 * +-------------------------------------------------+----------+
 * |                                                 |  [DRAG]  |
 * |              Navigation area                    |----------|
 * |           (circle, centered in the             | [SCROLL] |
 * |             non-panel region)                   |----------|
 * |                                                 | [RCLICK] |
 * +-------------------------------------------------+----------+
 *
 * Navigation area behaviour:
 *   - The user drags a finger from the center of the circle outward.
 *   - Mouse pointer speed and direction are proportional to the
 *     displacement vector from the center to the finger.
 *   - Tapping near the center (within TAP_RADIUS px, released within
 *     TAP_MAX_MS ms) sends a left mouse button click.
 *
 * Mode buttons (side panel):
 *   1. DRAG   - while active: mouse left button is held down as the
 *               user moves in the nav area.  A second tap or releasing
 *               the finger ends drag mode.
 *   2. SCROLL - movement in the nav area generates scroll-wheel events
 *               instead of pointer movement.  Ends on tap in nav area
 *               or on the button.
 *   3. RCLICK - sends a single right mouse button click.
 */
#pragma once

#include "touch.h"

/*
 * ui_init
 *
 * Compute layout, draw the initial UI frame.
 * Must be called after display_init().
 */
void ui_init(void);

/*
 * ui_process_touch
 *
 * Feed a touch event into the UI state machine.
 * This function may call hid_mouse_send() internally.
 */
void ui_process_touch(const touch_data_t *td);

/*
 * ui_refresh
 *
 * Redraw only the parts of the screen that have changed since the
 * last call (navigation indicator, button highlight states).
 * Call periodically (e.g. every 16 ms) from the main loop.
 */
void ui_refresh(void);
