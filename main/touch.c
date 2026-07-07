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
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

/* I2C read/write timeout */
#define I2C_TIMEOUT_MS          50

/* GT911 register addresses */
#define GT911_REG_PRODUCT_ID    0x8140
#define GT911_REG_STATUS        0x814E
#define GT911_REG_POINT1        0x814F

/* GT911 status bits */
#define GT911_STATUS_READY_MASK 0x80
#define GT911_STATUS_POINTS_MASK 0x0F

static uint8_t s_touch_addr = 0;

/*
 * i2c_read_regs16
 *
 * Read 'len' bytes from the selected touch controller starting at the
 * 16-bit register address 'reg_addr'. Returns 0 on success, -1 on error.
 */
static int i2c_read_regs16(uint8_t addr, uint16_t reg_addr,
                           uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, (reg_addr >> 8) & 0xFF, true);
    i2c_master_write_byte(cmd, reg_addr & 0xFF, true);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, &buf[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(TOUCH_I2C_PORT, cmd,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

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
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, (reg_addr >> 8) & 0xFF, true);
    i2c_master_write_byte(cmd, reg_addr & 0xFF, true);
    i2c_master_write(cmd, (uint8_t *)buf, len, true);
    i2c_master_stop(cmd);

    esp_err_t err = i2c_master_cmd_begin(TOUCH_I2C_PORT, cmd,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);

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

    if (i2c_read_regs16(addr, GT911_REG_PRODUCT_ID, product_id,
                        sizeof(product_id)) != 0) {
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

    i2c_config_t i2c_cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = TOUCH_I2C_SDA_GPIO,
        .scl_io_num       = TOUCH_I2C_SCL_GPIO,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = TOUCH_I2C_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(TOUCH_I2C_PORT, &i2c_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return -1;
    }

    err = i2c_driver_install(TOUCH_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return -1;
    }

    if (gt911_probe_address(TOUCH_I2C_ADDR_1) != 0 &&
        gt911_probe_address(TOUCH_I2C_ADDR_2) != 0) {
        ESP_LOGE(TAG, "Could not detect GT911 at 0x%02X or 0x%02X",
                 TOUCH_I2C_ADDR_1, TOUCH_I2C_ADDR_2);
        return -1;
    }

    if (gt911_clear_status() != 0) {
        ESP_LOGW(TAG, "Could not clear initial GT911 status");
    }

    ESP_LOGI(TAG, "Touch controller ready");
    return 0;
}

int touch_read(touch_data_t *out)
{
    uint8_t status;

    if (out == NULL) {
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
