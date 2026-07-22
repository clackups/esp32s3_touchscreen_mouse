/*
 * ui.c
 *
 * UI state machine and rendering for the touchscreen mouse.
 *
 * Coordinate conventions
 * ----------------------
 * All coordinates are in screen pixels with origin at the top-left
 * corner.  X increases to the right, Y increases downward.
 *
 * State machine
 * -------------
 *   STATE_IDLE      : no finger on screen
 *   STATE_TAP_WAIT  : finger down near nav center; waiting to confirm
 *                     click vs drag start
 *   STATE_NAVIGATE  : finger moving in nav area; generating mouse motion
 *   STATE_DRAG      : drag mode is latched; left button held down
 *   STATE_SCROLL    : scroll mode is latched; wheel events generated
 */

#include "ui.h"
#include "display.h"
#include "hid_mouse.h"
#include "app_config.h"
#include "device_config.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";

/* ======================================================================
 * Layout calculation
 * ====================================================================== */

/*
 * Computed at init time from LCD_H_RES, LCD_V_RES and the chosen panel
 * side.  The button panel strip is PANEL_WIDTH_PX wide (or tall for
 * top/bottom placement).
 */
static int s_nav_cx;      /* Navigation circle centre X */
static int s_nav_cy;      /* Navigation circle centre Y */
static int s_nav_r;       /* Navigation circle radius   */

/* Bounding box of the three buttons (common side dimension) */
static int s_btn_x;       /* left edge of button area */
static int s_btn_y;       /* top edge of button area  */
static int s_btn_w;       /* width  of button area    */
static int s_btn_h;       /* height of button area    */

/* Per-button bounding boxes (3 buttons) */
typedef struct { int x, y, w, h; } rect_t;
static rect_t s_buttons[PANEL_NUM_BUTTONS];

/* Button indices */
#define BTN_DRAG    0
#define BTN_SCROLL  1
#define BTN_RCLICK  2

/* ======================================================================
 * Application state
 * ====================================================================== */

typedef enum {
    STATE_IDLE = 0,
    STATE_TAP_WAIT,
    STATE_NAVIGATE,
    STATE_DRAG,
    STATE_SCROLL,
} app_state_t;

static app_state_t s_state = STATE_IDLE;

/* Whether each latchable mode is currently active */
static int s_drag_active   = 0;
static int s_scroll_active = 0;

/* Position of the finger dot drawn in the nav area */
static int s_finger_x = -1;
static int s_finger_y = -1;

/* Timestamp when the current touch began (us from esp_timer) */
static int64_t s_touch_start_us = 0;

/* Position at touch-down, used for tap/move discrimination */
static int s_touch_down_x = 0;
static int s_touch_down_y = 0;

/* ======================================================================
 * Layout helpers
 * ====================================================================== */

/*
 * compute_layout
 *
 * Fill the layout variables based on panel position.
 */
static void compute_layout(void)
{
#if PANEL_ON_RIGHT
    s_btn_x = LCD_H_RES - PANEL_WIDTH_PX;
    s_btn_y = 0;
    s_btn_w = PANEL_WIDTH_PX;
    s_btn_h = LCD_V_RES;

    /* Nav area occupies the left portion */
    int nav_area_w = LCD_H_RES - PANEL_WIDTH_PX;
    int nav_area_h = LCD_V_RES;
    s_nav_cx = nav_area_w / 2;
    s_nav_cy = nav_area_h / 2;
    s_nav_r  = (nav_area_w < nav_area_h ? nav_area_w : nav_area_h) / 2 - 10;

#elif PANEL_ON_LEFT
    s_btn_x = 0;
    s_btn_y = 0;
    s_btn_w = PANEL_WIDTH_PX;
    s_btn_h = LCD_V_RES;

    int nav_area_w = LCD_H_RES - PANEL_WIDTH_PX;
    s_nav_cx = PANEL_WIDTH_PX + nav_area_w / 2;
    s_nav_cy = LCD_V_RES / 2;
    s_nav_r  = (nav_area_w < LCD_V_RES ? nav_area_w : LCD_V_RES) / 2 - 10;

#elif PANEL_ON_TOP
    s_btn_x = 0;
    s_btn_y = 0;
    s_btn_w = LCD_H_RES;
    s_btn_h = PANEL_WIDTH_PX;

    int nav_area_h = LCD_V_RES - PANEL_WIDTH_PX;
    s_nav_cx = LCD_H_RES / 2;
    s_nav_cy = PANEL_WIDTH_PX + nav_area_h / 2;
    s_nav_r  = (LCD_H_RES < nav_area_h ? LCD_H_RES : nav_area_h) / 2 - 10;

#else /* PANEL_ON_BOTTOM */
    s_btn_x = 0;
    s_btn_y = LCD_V_RES - PANEL_WIDTH_PX;
    s_btn_w = LCD_H_RES;
    s_btn_h = PANEL_WIDTH_PX;

    int nav_area_h = LCD_V_RES - PANEL_WIDTH_PX;
    s_nav_cx = LCD_H_RES / 2;
    s_nav_cy = nav_area_h / 2;
    s_nav_r  = (LCD_H_RES < nav_area_h ? LCD_H_RES : nav_area_h) / 2 - 10;
#endif

    /* Divide the button strip into three equal sections */
    for (int i = 0; i < PANEL_NUM_BUTTONS; i++) {
#if PANEL_ON_RIGHT || PANEL_ON_LEFT
        s_buttons[i].x = s_btn_x;
        s_buttons[i].w = s_btn_w;
        s_buttons[i].y = s_btn_y + i * (s_btn_h / PANEL_NUM_BUTTONS);
        s_buttons[i].h = s_btn_h / PANEL_NUM_BUTTONS;
#else
        s_buttons[i].y = s_btn_y;
        s_buttons[i].h = s_btn_h;
        s_buttons[i].x = s_btn_x + i * (s_btn_w / PANEL_NUM_BUTTONS);
        s_buttons[i].w = s_btn_w / PANEL_NUM_BUTTONS;
#endif
    }

    ESP_LOGI(TAG, "Layout: nav circle centre=(%d,%d) r=%d  panel=(%d,%d,%d,%d)",
             s_nav_cx, s_nav_cy, s_nav_r,
             s_btn_x, s_btn_y, s_btn_w, s_btn_h);
}

/* ======================================================================
 * Hit testing
 * ====================================================================== */

/*
 * point_in_nav_circle
 *
 * Returns 1 if (px, py) is inside the navigation circle.
 */
static int point_in_nav_circle(int px, int py)
{
    int dx = px - s_nav_cx;
    int dy = py - s_nav_cy;
    return (dx * dx + dy * dy) <= (s_nav_r * s_nav_r);
}

/*
 * point_in_tap_zone
 *
 * Returns 1 if (px, py) is within TAP_RADIUS of the nav centre.
 */
static int point_in_tap_zone(int px, int py)
{
    int dx = px - s_nav_cx;
    int dy = py - s_nav_cy;
    int r  = TAP_RADIUS;
    return (dx * dx + dy * dy) <= (r * r);
}

/*
 * hit_button
 *
 * Returns the button index (0-2) if (px, py) is inside one of the
 * buttons, or -1 if none.
 */
static int hit_button(int px, int py)
{
    for (int i = 0; i < PANEL_NUM_BUTTONS; i++) {
        if (px >= s_buttons[i].x && px < s_buttons[i].x + s_buttons[i].w &&
            py >= s_buttons[i].y && py < s_buttons[i].y + s_buttons[i].h) {
            return i;
        }
    }
    return -1;
}

/* ======================================================================
 * Mouse motion calculation
 * ====================================================================== */

/*
 * clamp8
 *
 * Clamp value to [-127, 127] (valid range for signed 8-bit HID fields).
 */
static inline int8_t clamp8(int v)
{
    if (v >  127) return  127;
    if (v < -127) return -127;
    return (int8_t)v;
}

/*
 * compute_mouse_delta
 *
 * Map finger offset from nav centre to mouse movement units.
 *
 * Linear mapping:
 *   offset 0        -> 0  units
 *   offset s_nav_r  -> MOUSE_SPEED_MAX units
 */
static void compute_mouse_delta(int fx, int fy, int8_t *dx, int8_t *dy)
{
    int ox = fx - s_nav_cx;
    int oy = fy - s_nav_cy;

    /* Scale factor: MOUSE_SPEED_MAX / nav_radius */
    int mx = (ox * MOUSE_SPEED_MAX) / s_nav_r;
    int my = (oy * MOUSE_SPEED_MAX) / s_nav_r;

    *dx = clamp8(mx);
    *dy = clamp8(my);
}

/* ======================================================================
 * Rendering
 * ====================================================================== */

/*
 * draw_button
 *
 * Draw one button cell.  active != 0 means the button is toggled on.
 */
static void draw_button(int idx, int active)
{
    rect_t *b = &s_buttons[idx];
    uint16_t face  = active ? COLOR_ACTIVE : COLOR_BACKGROUND;
    uint16_t edge  = COLOR_OUTLINE;
    uint16_t icon_color = active ? COLOR_BACKGROUND : COLOR_TEXT;

    draw_rect(b->x, b->y, b->w, b->h, face);
    draw_rect_outline(b->x, b->y, b->w, b->h, OUTLINE_WIDTH, edge);

    /* Draw icon inside the button with a small margin */
    int margin = OUTLINE_WIDTH + 4;
    int ix = b->x + margin;
    int iy = b->y + margin;
    int iw = b->w - 2 * margin;
    int ih = b->h - 2 * margin;

    switch (idx) {
        case BTN_DRAG:
            draw_icon_drag(ix, iy, iw, ih, icon_color);
            break;
        case BTN_SCROLL:
            draw_icon_scroll(ix, iy, iw, ih, icon_color);
            break;
        case BTN_RCLICK:
            draw_icon_rclick(ix, iy, iw, ih, icon_color);
            break;
        default:
            break;
    }

    /* Text label below the icon (small font) */
    static const char *labels[PANEL_NUM_BUTTONS] = {"DRAG", "SCRL", "RCLK"};
    int lx = b->x + (b->w - (int)strlen(labels[idx]) * 9) / 2;
    int ly = b->y + b->h - 12;
    if (lx < b->x) lx = b->x + 2;
    draw_string(lx, ly, labels[idx], active ? COLOR_BACKGROUND : COLOR_TEXT,
                face);
}

/*
 * draw_nav_area
 *
 * Draw the navigation circle and the crosshair lines at the centre.
 */
static void draw_nav_area(void)
{
    /* Fill inside the nav circle with background */
    draw_circle_filled(s_nav_cx, s_nav_cy, s_nav_r, COLOR_BACKGROUND);

    /* Circle outline */
    draw_circle_outline(s_nav_cx, s_nav_cy, s_nav_r, OUTLINE_WIDTH, COLOR_OUTLINE);

    /* Small centre crosshair */
    int cross = 10;
    draw_line(s_nav_cx - cross, s_nav_cy, s_nav_cx + cross, s_nav_cy, COLOR_OUTLINE);
    draw_line(s_nav_cx, s_nav_cy - cross, s_nav_cx, s_nav_cy + cross, COLOR_OUTLINE);
}

/*
 * draw_finger_dot
 *
 * Draw the finger position indicator.  Pass (-1, -1) to erase it.
 */
static void draw_finger_dot(int fx, int fy, uint16_t color)
{
    if (fx >= 0 && fy >= 0) {
        /* Direction line from centre to finger */
        draw_line(s_nav_cx, s_nav_cy, fx, fy, COLOR_OUTLINE);
        /* Dot */
        draw_circle_filled(fx, fy, FINGER_DOT_RADIUS, color);
    }
}

/*
 * erase_finger_dot
 *
 * Redraw the parts of the nav area that were obscured by the finger dot
 * at the given position.
 */
static void erase_finger_dot(int fx, int fy)
{
    if (fx < 0 || fy < 0) return;

    int r = FINGER_DOT_RADIUS + 1;

    /* Erase dot by refilling a bounding square with background */
    draw_rect(fx - r, fy - r, 2 * r + 1, 2 * r + 1, COLOR_BACKGROUND);

    /* Restore outline pixels that might have been erased */
    draw_circle_outline(s_nav_cx, s_nav_cy, s_nav_r, OUTLINE_WIDTH, COLOR_OUTLINE);

    /* Restore the centre crosshair if it overlapped */
    int cross = 10;
    draw_line(s_nav_cx - cross, s_nav_cy, s_nav_cx + cross, s_nav_cy, COLOR_OUTLINE);
    draw_line(s_nav_cx, s_nav_cy - cross, s_nav_cx, s_nav_cy + cross, COLOR_OUTLINE);
}

/* ======================================================================
 * Public API
 * ====================================================================== */

void ui_init(void)
{
    compute_layout();

    /* Full background */
    draw_fill(COLOR_BACKGROUND);

    /* Navigation circle */
    draw_nav_area();

    /* All three buttons (initially inactive) */
    for (int i = 0; i < PANEL_NUM_BUTTONS; i++) {
        draw_button(i, 0);
    }

    display_flush();

    ESP_LOGI(TAG, "UI initialised");
}

void ui_process_touch(const touch_data_t *td)
{
    if (td == NULL) return;

    int px = td->x;
    int py = td->y;
    int in_nav    = point_in_nav_circle(px, py);
    int in_tap    = point_in_tap_zone(px, py);
    int btn_idx   = hit_button(px, py);
    int64_t now_us = esp_timer_get_time();

    switch (td->event) {

    /* ---- Finger placed on screen ------------------------------------ */
    case TOUCH_EVENT_DOWN:
        s_touch_start_us = now_us;
        s_touch_down_x   = px;
        s_touch_down_y   = py;

        if (btn_idx >= 0) {
            /* Button tapped: handle immediately on DOWN */
            switch (btn_idx) {
                case BTN_DRAG:
                    s_drag_active = !s_drag_active;
                    if (s_drag_active) {
                        s_scroll_active = 0;
                        draw_button(BTN_SCROLL, 0);
                    } else {
                        /* Release left button if drag ended */
                        hid_mouse_send(0, 0, 0, 0);
                    }
                    draw_button(BTN_DRAG,   s_drag_active);
                    draw_button(BTN_SCROLL, s_scroll_active);
                    break;

                case BTN_SCROLL:
                    s_scroll_active = !s_scroll_active;
                    if (s_scroll_active) {
                        s_drag_active = 0;
                        hid_mouse_send(0, 0, 0, 0); /* release any held btn */
                        draw_button(BTN_DRAG, 0);
                    }
                    draw_button(BTN_SCROLL, s_scroll_active);
                    break;

                case BTN_RCLICK:
                    /* Right-click: press then release */
                    hid_mouse_send(HID_BTN_RIGHT, 0, 0, 0);
                    vTaskDelay(pdMS_TO_TICKS(30));
                    hid_mouse_send(0, 0, 0, 0);
                    break;
            }
            s_state = STATE_IDLE;
            return;
        }

        if (in_nav) {
            if (in_tap) {
                s_state = STATE_TAP_WAIT;
            } else {
                s_state = STATE_NAVIGATE;
                s_finger_x = px;
                s_finger_y = py;
                draw_finger_dot(s_finger_x, s_finger_y, COLOR_FINGER);
            }
        }
        break;

    /* ---- Finger moved ----------------------------------------------- */
    case TOUCH_EVENT_MOVE:
        if (s_state == STATE_TAP_WAIT) {
            /* Check if the finger has moved beyond the tap zone */
            if (!in_tap) {
                s_state = (s_drag_active || s_scroll_active)
                          ? (s_drag_active ? STATE_DRAG : STATE_SCROLL)
                          : STATE_NAVIGATE;
                s_finger_x = px;
                s_finger_y = py;
                draw_finger_dot(s_finger_x, s_finger_y, COLOR_FINGER);
            }
            break;
        }

        if (s_state == STATE_NAVIGATE || s_state == STATE_DRAG ||
            s_state == STATE_SCROLL) {
            int8_t dx, dy;

            if (in_nav) {
                erase_finger_dot(s_finger_x, s_finger_y);
                s_finger_x = px;
                s_finger_y = py;
                draw_finger_dot(s_finger_x, s_finger_y, COLOR_FINGER);
                compute_mouse_delta(px, py, &dx, &dy);
            } else {
                /* Finger left the nav area: stop movement */
                dx = 0;
                dy = 0;
                if (s_drag_active) {
                    hid_mouse_send(0, 0, 0, 0);
                    s_drag_active = 0;
                    draw_button(BTN_DRAG, 0);
                    s_state = STATE_IDLE;
                }
                if (s_scroll_active) {
                    s_scroll_active = 0;
                    draw_button(BTN_SCROLL, 0);
                    s_state = STATE_IDLE;
                }
                erase_finger_dot(s_finger_x, s_finger_y);
                s_finger_x = -1;
                s_finger_y = -1;
                break;
            }

            if (s_state == STATE_SCROLL) {
                /* Scroll mode: vertical movement -> wheel */
                hid_mouse_send(0, 0, 0, (int8_t)(-dy));
            } else {
                uint8_t btns = s_drag_active ? HID_BTN_LEFT : 0;
                hid_mouse_send(btns, dx, dy, 0);
            }
        }
        break;

    /* ---- Finger lifted ---------------------------------------------- */
    case TOUCH_EVENT_UP:
        if (s_state == STATE_TAP_WAIT) {
            int64_t elapsed_ms = (now_us - s_touch_start_us) / 1000;
            if (elapsed_ms <= TAP_MAX_MS) {
                /* Short tap in nav centre: left click */
                hid_mouse_send(HID_BTN_LEFT, 0, 0, 0);
                vTaskDelay(pdMS_TO_TICKS(30));
                hid_mouse_send(0, 0, 0, 0);
                ESP_LOGD(TAG, "Click sent (%lld ms)", (long long)elapsed_ms);
            }
        }

        if (s_state == STATE_NAVIGATE || s_state == STATE_DRAG) {
            hid_mouse_send(0, 0, 0, 0);
            if (s_drag_active) {
                /* Drag mode: release left button, end drag */
                s_drag_active = 0;
                draw_button(BTN_DRAG, 0);
            }
        }

        if (s_state == STATE_SCROLL) {
            s_scroll_active = 0;
            draw_button(BTN_SCROLL, 0);
        }

        erase_finger_dot(s_finger_x, s_finger_y);
        s_finger_x = -1;
        s_finger_y = -1;
        s_state = STATE_IDLE;
        break;

    default:
        break;
    }
}

void ui_refresh(void)
{
    display_flush();
}
