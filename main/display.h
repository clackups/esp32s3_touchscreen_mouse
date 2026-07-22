/*
 * display.h
 *
 * Display initialisation and 2-D drawing primitives for the target
 * RGB parallel LCD panel.
 *
 * All drawing functions operate on an internal framebuffer that lives in
 * PSRAM.  The buffer is flushed to the display automatically by the ESP-IDF
 * RGB panel DMA engine every frame.
 *
 * Coordinate origin (0, 0) is the top-left corner.
 * X increases to the right; Y increases downward.
 * Colors are 16-bit RGB565 values.
 */
#pragma once

#include <stdint.h>

/*
 * display_init
 *
 * Initialise the RGB panel and bring up the RGB interface.
 * Must be called once before any drawing function.
 * Returns 0 on success, non-zero on error.
 */
int display_init(void);

/*
 * display_get_fb
 *
 * Return a pointer to the start of the framebuffer.  Callers may write
 * RGB565 pixels directly; the DMA engine reads from this buffer.
 * The buffer is (LCD_H_RES * LCD_V_RES * 2) bytes.
 */
uint16_t *display_get_fb(void);

/*
 * display_flush
 *
 * Explicitly request a framebuffer refresh (useful after a burst of
 * direct framebuffer writes).  May be a no-op on boards where the
 * DMA engine is always running.
 */
void display_flush(void);

/* ---- Drawing primitives ----------------------------------------------- */

/* Fill the entire screen with color. */
void draw_fill(uint16_t color);

/* Fill a rectangle with color.  Clips to screen bounds. */
void draw_rect(int x, int y, int w, int h, uint16_t color);

/*
 * Draw the outline of a rectangle with line thickness 1.
 * Clips to screen bounds.
 */
void draw_rect_outline(int x, int y, int w, int h, int thickness,
                       uint16_t color);

/* Draw a line from (x0,y0) to (x1,y1) using Bresenham's algorithm. */
void draw_line(int x0, int y0, int x1, int y1, uint16_t color);

/* Draw a filled circle centred at (cx, cy) with radius r. */
void draw_circle_filled(int cx, int cy, int r, uint16_t color);

/*
 * Draw a circle outline centred at (cx, cy).
 * thickness : number of concentric circles drawn (pixel count).
 */
void draw_circle_outline(int cx, int cy, int r, int thickness, uint16_t color);

/*
 * Draw a single 8x8 bitmap character from the built-in font.
 * Only printable ASCII characters 0x20-0x7E are supported.
 * bg_color is used for the background pixels of the glyph cell.
 */
void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg);

/*
 * Draw a null-terminated ASCII string using draw_char.
 * Characters are drawn left-to-right, spaced 9 pixels apart.
 */
void draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg);

/* ---- Vector icons ----------------------------------------------------- */

/*
 * draw_icon_drag
 *
 * Draws a cursor-plus-arrow icon indicating "drag mode" inside the
 * bounding box (x, y, w, h).
 */
void draw_icon_drag(int x, int y, int w, int h, uint16_t color);

/*
 * draw_icon_scroll
 *
 * Draws up/down arrows indicating "scroll mode" inside (x, y, w, h).
 */
void draw_icon_scroll(int x, int y, int w, int h, uint16_t color);

/*
 * draw_icon_rclick
 *
 * Draws a mouse silhouette with the right button highlighted inside
 * the bounding box (x, y, w, h).
 */
void draw_icon_rclick(int x, int y, int w, int h, uint16_t color);
