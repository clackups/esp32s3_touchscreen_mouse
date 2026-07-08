/*
 * display.c
 *
 * RGB display driver and 2-D drawing functions.
 *
 * Initialisation flow:
 *   1. Configure LEDC backlight output.
 *   2. Create the ESP-IDF RGB panel and start DMA output.
 *   3. Return a pointer to the framebuffer for direct pixel access.
 */

#include "display.h"
#include "device_config.h"
#include "app_config.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/ledc.h"

static const char *TAG = "display";

/* Panel handle returned by ESP-IDF RGB driver */
static esp_lcd_panel_handle_t s_panel = NULL;

/* Direct pointer to the DMA framebuffer in PSRAM */
static uint16_t *s_fb = NULL;

/* ======================================================================
 * NOTE (VIEWE UEDX80480043E-WB-A):
 * This board uses an 800x480 RGB panel and does not expose a separate
 * 3-wire command channel for controller register programming in this project.
 * The panel is driven directly by the ESP-IDF RGB peripheral configuration.
 * ====================================================================== */

/* ======================================================================
 * Backlight
 * ====================================================================== */

static void backlight_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .timer_num        = LEDC_TIMER_0,
        .duty_resolution  = LEDC_TIMER_10_BIT,
        .freq_hz          = 5000,
        .clk_cfg          = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .timer_sel      = LEDC_TIMER_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = LCD_BL_GPIO,
        .duty           = 800,
        .hpoint         = 0,
    };
    ledc_channel_config(&ledc_channel);
}

/* ======================================================================
 * Public: display_init
 * ====================================================================== */

int display_init(void)
{
    ESP_LOGI(TAG, "Initialising display (%dx%d)", LCD_H_RES, LCD_V_RES);

    /* Bring up backlight */
    backlight_init();

    /* Create the ESP-IDF RGB panel */
    esp_lcd_rgb_panel_config_t panel_cfg = {
        .data_width            = 16,
        .num_fbs               = 1,
        /*
         * bounce_buffer_size_px: allocate a small SRAM bounce buffer so the
         * DMA engine does not read directly from PSRAM over the AHB bus.
         * Direct PSRAM DMA can stall under heavy PSRAM traffic (QSPI flash
         * + framebuffer reads at the same time), producing garbled display
         * lines.  10 lines of buffer (9600 B) fits comfortably in internal
         * SRAM and eliminates these artefacts.
         */
        .bounce_buffer_size_px = LCD_H_RES * 10,
        .clk_src               = LCD_CLK_SRC_DEFAULT,
        .disp_gpio_num    = -1,
        .pclk_gpio_num    = LCD_PCLK_GPIO,
        .vsync_gpio_num   = LCD_VSYNC_GPIO,
        .hsync_gpio_num   = LCD_HSYNC_GPIO,
        .de_gpio_num      = LCD_DE_GPIO,
        .data_gpio_nums   = {
            LCD_DATA_GPIO_B0, LCD_DATA_GPIO_B1, LCD_DATA_GPIO_B2,
            LCD_DATA_GPIO_B3, LCD_DATA_GPIO_B4,
            LCD_DATA_GPIO_G0, LCD_DATA_GPIO_G1, LCD_DATA_GPIO_G2,
            LCD_DATA_GPIO_G3, LCD_DATA_GPIO_G4, LCD_DATA_GPIO_G5,
            LCD_DATA_GPIO_R0, LCD_DATA_GPIO_R1, LCD_DATA_GPIO_R2,
            LCD_DATA_GPIO_R3, LCD_DATA_GPIO_R4,
        },
        .timings = {
            .pclk_hz            = LCD_PIXEL_CLOCK_HZ,
            .h_res              = LCD_H_RES,
            .v_res              = LCD_V_RES,
            .hsync_back_porch   = LCD_HBP,
            .hsync_front_porch  = LCD_HFP,
            .hsync_pulse_width  = LCD_HSW,
            .vsync_back_porch   = LCD_VBP,
            .vsync_front_porch  = LCD_VFP,
            .vsync_pulse_width  = LCD_VSW,
            .flags = {
                .hsync_idle_low  = LCD_HSYNC_IDLE_LOW,
                .vsync_idle_low  = LCD_VSYNC_IDLE_LOW,
                .pclk_active_neg = LCD_PCLK_ACTIVE_NEG,
            },
        },
        .flags = {
            .fb_in_psram = 1,
        },
    };

    esp_err_t err = esp_lcd_new_rgb_panel(&panel_cfg, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel reset failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "panel init failed: %s", esp_err_to_name(err));
        return -1;
    }

    /*
     * esp_lcd_panel_disp_on_off is not supported by the RGB panel driver
     * when disp_gpio_num = -1.  Display enable is handled by the DE/HSYNC/
     * VSYNC signals, so no explicit on/off call is needed here.
     */

    /* Obtain framebuffer pointer */
    err = esp_lcd_rgb_panel_get_frame_buffer(s_panel, 1, (void **)&s_fb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "get frame buffer failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Clear screen to background color */
    draw_fill(COLOR_BACKGROUND);
    display_flush();

    ESP_LOGI(TAG, "Display ready, fb=%p", (void *)s_fb);
    return 0;
}

uint16_t *display_get_fb(void)
{
    return s_fb;
}

void display_flush(void)
{
    if (s_panel != NULL) {
        esp_lcd_panel_draw_bitmap(s_panel, 0, 0, LCD_H_RES, LCD_V_RES, s_fb);
    }
}

/* ======================================================================
 * Helper: clip and write a single pixel
 * ====================================================================== */

static inline void put_pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x < LCD_H_RES && (unsigned)y < LCD_V_RES) {
        s_fb[y * LCD_H_RES + x] = color;
    }
}

/* ======================================================================
 * Drawing primitives
 * ====================================================================== */

void draw_fill(uint16_t color)
{
    int total = LCD_H_RES * LCD_V_RES;
    for (int i = 0; i < total; i++) {
        s_fb[i] = color;
    }
}

void draw_rect(int x, int y, int w, int h, uint16_t color)
{
    for (int row = y; row < y + h; row++) {
        for (int col = x; col < x + w; col++) {
            put_pixel(col, row, color);
        }
    }
}

void draw_rect_outline(int x, int y, int w, int h, int thickness,
                       uint16_t color)
{
    /* Top and bottom edges */
    draw_rect(x, y,             w, thickness, color);
    draw_rect(x, y + h - thickness, w, thickness, color);
    /* Left and right edges */
    draw_rect(x,             y + thickness, thickness, h - 2 * thickness, color);
    draw_rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
}

void draw_line(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx  =  abs(x1 - x0);
    int dy  = -abs(y1 - y0);
    int sx  = (x0 < x1) ? 1 : -1;
    int sy  = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (1) {
        put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void draw_circle_filled(int cx, int cy, int r, uint16_t color)
{
    for (int y = -r; y <= r; y++) {
        int half_w = (int)__builtin_sqrt((double)((r * r) - (y * y)));
        draw_rect(cx - half_w, cy + y, 2 * half_w + 1, 1, color);
    }
}

void draw_circle_outline(int cx, int cy, int r, int thickness, uint16_t color)
{
    for (int t = 0; t < thickness; t++) {
        int rr = r - t;
        if (rr < 0) break;
        /* Midpoint circle algorithm */
        int x = rr, y = 0, err = 0;
        while (x >= y) {
            put_pixel(cx + x, cy + y, color);
            put_pixel(cx + y, cy + x, color);
            put_pixel(cx - y, cy + x, color);
            put_pixel(cx - x, cy + y, color);
            put_pixel(cx - x, cy - y, color);
            put_pixel(cx - y, cy - x, color);
            put_pixel(cx + y, cy - x, color);
            put_pixel(cx + x, cy - y, color);
            y++;
            err += 1 + 2 * y;
            if (2 * (err - x) + 1 > 0) { x--; err += 1 - 2 * x; }
        }
    }
}

/* ======================================================================
 * Minimal 8x8 bitmap font (ASCII 0x20-0x7E)
 *
 * Each character is encoded as 8 bytes, one byte per row (top to bottom).
 * Within each byte, bit 7 is the leftmost pixel.
 * ====================================================================== */

/* 95 printable characters: index = ascii - 0x20 */
static const uint8_t s_font8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20 SPACE */
    {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, /* 0x21 ! */
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, /* 0x22 " */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 0x23 # */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 0x24 $ */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 0x25 % */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 0x26 & */
    {0x18,0x18,0x08,0x00,0x00,0x00,0x00,0x00}, /* 0x27 ' */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 0x28 ( */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 0x29 ) */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 0x2A * */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 0x2B + */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 0x2C , */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 0x2D - */
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, /* 0x2E . */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 0x2F / */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 0x30 0 */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 0x31 1 */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 0x32 2 */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 0x33 3 */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 0x34 4 */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 0x35 5 */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 0x36 6 */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 0x37 7 */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 0x38 8 */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 0x39 9 */
    {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, /* 0x3A : */
    {0x00,0x18,0x18,0x00,0x18,0x18,0x0C,0x00}, /* 0x3B ; */
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, /* 0x3C < */
    {0x00,0x00,0x3F,0x00,0x3F,0x00,0x00,0x00}, /* 0x3D = */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 0x3E > */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 0x3F ? */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 0x40 @ */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 0x41 A */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 0x42 B */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 0x43 C */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 0x44 D */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 0x45 E */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 0x46 F */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 0x47 G */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 0x48 H */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x49 I */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 0x4A J */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 0x4B K */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 0x4C L */
    {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, /* 0x4D M */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 0x4E N */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 0x4F O */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 0x50 P */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 0x51 Q */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 0x52 R */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 0x53 S */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x54 T */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 0x55 U */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 0x56 V */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 0x57 W */
    {0x63,0x63,0x36,0x1C,0x36,0x63,0x63,0x00}, /* 0x58 X */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 0x59 Y */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 0x5A Z */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 0x5B [ */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 0x5C \ */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 0x5D ] */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 0x5E ^ */
    {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}, /* 0x5F _ */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 0x60 ` */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 0x61 a */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 0x62 b */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 0x63 c */
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, /* 0x64 d */
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, /* 0x65 e */
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, /* 0x66 f */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 0x67 g */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 0x68 h */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x69 i */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 0x6A j */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 0x6B k */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x6C l */
    {0x00,0x00,0x37,0x7F,0x6B,0x6B,0x63,0x00}, /* 0x6D m */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 0x6E n */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 0x6F o */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 0x70 p */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 0x71 q */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 0x72 r */
    {0x00,0x00,0x1E,0x03,0x1E,0x30,0x1F,0x00}, /* 0x73 s */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 0x74 t */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 0x75 u */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 0x76 v */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 0x77 w */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 0x78 x */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 0x79 y */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 0x7A z */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 0x7B { */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 0x7C | */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 0x7D } */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x7E ~ */
};

void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *glyph = s_font8x8[(uint8_t)c - 0x20];
    for (int row = 0; row < 8; row++) {
        uint8_t line = glyph[row];
        for (int col = 0; col < 8; col++) {
            put_pixel(x + col, y + row,
                      (line & (0x80u >> col)) ? fg : bg);
        }
    }
}

void draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg)
{
    int cx = x;
    while (*str) {
        draw_char(cx, y, *str, fg, bg);
        cx += 9; /* 8 px glyph + 1 px spacing */
        str++;
    }
}

/* ======================================================================
 * Vector icons
 * ====================================================================== */

/*
 * draw_arrow_head
 *
 * Draw a filled triangle (arrowhead) pointing in direction dy_sign
 * (-1 = up, +1 = down) centred horizontally at cx, tip at (cx, tip_y).
 */
static void draw_arrow_head(int cx, int tip_y, int half_base, int height,
                            int dy_sign, uint16_t color)
{
    for (int i = 0; i < height; i++) {
        int half_w = (i * half_base) / height;
        int row = tip_y + dy_sign * i;
        draw_rect(cx - half_w, row, 2 * half_w + 1, 1, color);
    }
}

void draw_icon_drag(int x, int y, int w, int h, uint16_t color)
{
    /*
     * Icon: a cursor arrow (pointing up-left) plus a small drag arrow
     * beneath it.  Drawn with lines inside the bounding box.
     */
    int cx = x + w / 2;
    int cy = y + h / 2;
    int sz = (w < h ? w : h) / 2 - 4;

    /* Cursor arrow shaft (diagonal) */
    draw_line(cx - sz, cy - sz, cx + sz / 3, cy + sz / 3, color);
    /* Arrow tip decorations */
    draw_line(cx - sz, cy - sz, cx - sz + sz / 2, cy - sz, color);
    draw_line(cx - sz, cy - sz, cx - sz,           cy - sz + sz / 2, color);
    /* Small horizontal drag arrows */
    draw_line(cx - sz / 2, cy + sz / 2, cx + sz / 2, cy + sz / 2, color);
    draw_arrow_head(cx + sz / 2, cy + sz / 2, sz / 3, sz / 3,  0, color);
    draw_arrow_head(cx - sz / 2, cy + sz / 2, sz / 3, sz / 3,  0, color);
}

void draw_icon_scroll(int x, int y, int w, int h, uint16_t color)
{
    /*
     * Icon: two arrowheads, one pointing up and one pointing down,
     * separated by a vertical line.
     */
    int cx = x + w / 2;
    int mid = y + h / 2;
    int half = h / 4 - 2;
    int ab  = w / 4;   /* arrowhead half-base */
    int ah  = h / 5;   /* arrowhead height   */

    /* Vertical divider */
    draw_line(cx, mid - half, cx, mid + half, color);
    /* Up arrow */
    draw_arrow_head(cx, mid - half, ab, ah, -1, color);
    /* Down arrow */
    draw_arrow_head(cx, mid + half, ab, ah, +1, color);
}

void draw_icon_rclick(int x, int y, int w, int h, uint16_t color)
{
    /*
     * Icon: simplified mouse body outline, with the right half of the
     * top (right button) filled in.
     */
    int mx = x + w / 2;
    int my = y + h / 2;
    int bw = w * 2 / 5;
    int bh = h * 3 / 5;
    int btn_h = bh / 3;

    /* Mouse body outline */
    draw_rect_outline(mx - bw, my - bh / 2, 2 * bw, bh, 2, color);

    /* Right button filled */
    draw_rect(mx, my - bh / 2 + 2, bw - 2, btn_h, color);

    /* Button divider line */
    draw_line(mx, my - bh / 2 + 2, mx, my - bh / 2 + btn_h, color);
}
