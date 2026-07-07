/*
 * touch.c
 *
 * GT911 capacitive touch controller driver.
 *
 * The Guition ESP32-4848S040 board exposes the touch controller over I2C.
 * The controller stores its status and coordinates in a 16-bit register map.
 */

#include "touch.h"
#include "device_config.h"

#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

/* I2C read/write timeout */
#define I2C_TIMEOUT_MS          50
#define GT911_PROBE_RETRIES     10
#define GT911_PROBE_DELAY_MS    50

/* GT911 register addresses */
#define GT911_REG_PRODUCT_ID    0x8140
#define GT911_REG_STATUS        0x814E
#define GT911_REG_POINT1        0x814F

/* GT911 status bits */
#define GT911_STATUS_READY_MASK 0x80
#define GT911_STATUS_POINTS_MASK 0x0F

static uint8_t s_touch_addr = 0;
static bool s_touch_ready = false;
static i2c_master_bus_handle_t s_i2c_bus = NULL;
static i2c_master_dev_handle_t s_i2c_dev = NULL;

/*
 * i2c_read_regs16
 *
 * Read 'len' bytes from the selected touch controller starting at the
 * 16-bit register address 'reg_addr'. Returns 0 on success, -1 on error.
 */
static int i2c_read_regs16(uint8_t addr, uint16_t reg_addr,
                           uint8_t *buf, size_t len)
{
    (void)addr;
    uint8_t reg_buf[2] = {
        (uint8_t)((reg_addr >> 8) & 0xFF),
        (uint8_t)(reg_addr & 0xFF),
    };
    esp_err_t err = i2c_master_transmit_receive(
        s_i2c_dev, reg_buf, sizeof(reg_buf), buf, len, I2C_TIMEOUT_MS
    );
    return (err == ESP_OK) ? 0 : -1;
}

/*
 * i2c_write_regs16
 *
 * Write 'len' bytes to the selected touch controller starting at the
 * 16-bit register address 'reg_addr'. Returns 0 on success, -1 on error.
 */
static int i2c_write_regs16(uint8_t addr, uint16_t reg_addr,
                            const uint8_t *buf, size_t len)
{
    (void)addr;
    uint8_t write_buf[2 + len];
    write_buf[0] = (uint8_t)((reg_addr >> 8) & 0xFF);
    write_buf[1] = (uint8_t)(reg_addr & 0xFF);
    if (len > 0) {
        memcpy(&write_buf[2], buf, len);
    }

    esp_err_t err = i2c_master_transmit(
        s_i2c_dev, write_buf, sizeof(write_buf), I2C_TIMEOUT_MS
    );
    return (err == ESP_OK) ? 0 : -1;
}

/*
 * gt911_clear_status
 *
 * Clear the GT911 data-ready flag after a touch sample has been consumed.
 */
static int gt911_clear_status(void)
{
    const uint8_t clear = 0;
    return i2c_write_regs16(s_touch_addr, GT911_REG_STATUS, &clear, 1);
}

/*
 * gt911_probe_address
 *
 * Probe one possible GT911 I2C address and log the product ID on success.
 */
static int gt911_probe_address(uint8_t addr)
{
    uint8_t product_id[4];
    char product_id_text[5];

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = TOUCH_I2C_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return -1;
    }

    s_i2c_dev = dev;
    if (i2c_read_regs16(addr, GT911_REG_PRODUCT_ID, product_id,
                        sizeof(product_id)) != 0) {
        i2c_master_bus_rm_device(dev);
        s_i2c_dev = NULL;
        return -1;
    }

    memcpy(product_id_text, product_id, sizeof(product_id));
    product_id_text[4] = '\0';

    s_touch_addr = addr;
    ESP_LOGI(TAG, "GT911 product ID: %s (addr 0x%02X)", product_id_text, addr);
    return 0;
}

/* ======================================================================
 * Public API
 * ====================================================================== */

int touch_init(void)
{
    ESP_LOGI(TAG, "Initialising GT911 touch controller");

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_I2C_SDA_GPIO,
        .scl_io_num = TOUCH_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(20));

    bool detected = false;
    for (int attempt = 0; attempt < GT911_PROBE_RETRIES; attempt++) {
        if (gt911_probe_address(TOUCH_I2C_ADDR_1) == 0 ||
            gt911_probe_address(TOUCH_I2C_ADDR_2) == 0) {
            detected = true;
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(GT911_PROBE_DELAY_MS));
    }

    if (!detected) {
        ESP_LOGE(TAG, "Could not detect GT911 at 0x%02X or 0x%02X",
                 TOUCH_I2C_ADDR_1, TOUCH_I2C_ADDR_2);
        return -1;
    }

    s_touch_ready = true;

    if (gt911_clear_status() != 0) {
        ESP_LOGW(TAG, "Could not clear initial GT911 status");
    }

    ESP_LOGI(TAG, "Touch controller ready");
    return 0;
}

int touch_read(touch_data_t *out)
{
    uint8_t status;

    if (out == NULL || !s_touch_ready) {
        return -1;
    }

    if (i2c_read_regs16(s_touch_addr, GT911_REG_STATUS, &status, 1) != 0) {
        return -1;
    }

    if ((status & GT911_STATUS_READY_MASK) == 0) {
        out->event = TOUCH_EVENT_NONE;
        return 0;
    }

    uint8_t num_points = status & GT911_STATUS_POINTS_MASK;
    if (num_points == 0) {
        if (gt911_clear_status() != 0) {
            return -1;
        }

        if (out->event == TOUCH_EVENT_DOWN ||
            out->event == TOUCH_EVENT_MOVE) {
            out->event = TOUCH_EVENT_UP;
        } else {
            out->event = TOUCH_EVENT_NONE;
        }
        return 0;
    }

    uint8_t point[7];
    if (i2c_read_regs16(s_touch_addr, GT911_REG_POINT1, point,
                        sizeof(point)) != 0) {
        return -1;
    }

    if (gt911_clear_status() != 0) {
        return -1;
    }

    int x = ((int)point[2] << 8) | point[1];
    int y = ((int)point[4] << 8) | point[3];

    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= LCD_H_RES) x = LCD_H_RES - 1;
    if (y >= LCD_V_RES) y = LCD_V_RES - 1;

    out->event = (out->event == TOUCH_EVENT_NONE ||
                  out->event == TOUCH_EVENT_UP)
                 ? TOUCH_EVENT_DOWN
                 : TOUCH_EVENT_MOVE;
    out->x = x;
    out->y = y;
    return 0;
}
