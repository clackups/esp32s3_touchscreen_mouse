/*
 * touch.c
 *
 * CST820 capacitive touch controller driver.
 *
 * Register map used (starting at address 0x01):
 *   0x01  gesture ID  (0x00 = none, 0x01 = swipe-up, ...)
 *   0x02  number of touch points (0 or 1)
 *   0x03  XH[3:0]  and event flag[7:6]  (event: 0x00=down, 0x04=up, 0x80=move)
 *   0x04  XL[7:0]
 *   0x05  YH[3:0]
 *   0x06  YL[7:0]
 *
 * The driver performs a burst read of registers 0x01-0x06 in one
 * I2C transaction to minimise bus traffic.
 */

#include "touch.h"
#include "device_config.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch";

/* I2C read timeout */
#define I2C_TIMEOUT_MS   50

/* CST820 register addresses */
#define CST820_REG_STATUS   0x01
#define CST820_REG_CHIP_ID  0xA7

/* ======================================================================
 * Low-level I2C helpers
 * ====================================================================== */

/*
 * i2c_read_regs
 *
 * Read 'len' bytes from the CST820 starting at register 'reg_addr'.
 * Returns 0 on success, -1 on error.
 */
static int i2c_read_regs(uint8_t reg_addr, uint8_t *buf, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();

    /* Write register address */
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TOUCH_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);

    /* Repeated start then read */
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (TOUCH_I2C_ADDR << 1) | I2C_MASTER_READ, true);
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

/* ======================================================================
 * Public API
 * ====================================================================== */

int touch_init(void)
{
    ESP_LOGI(TAG, "Initialising CST820 touch controller");

    /* Hardware reset */
    gpio_config_t rst_cfg = {
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = 1ULL << TOUCH_RST_GPIO,
    };
    gpio_config(&rst_cfg);

    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Configure INT pin as input (the CST820 asserts it when touched) */
    gpio_config_t int_cfg = {
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
        .pin_bit_mask = 1ULL << TOUCH_INT_GPIO,
    };
    gpio_config(&int_cfg);

    /* Initialise I2C master */
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

    /* Verify chip ID */
    uint8_t chip_id = 0;
    if (i2c_read_regs(CST820_REG_CHIP_ID, &chip_id, 1) == 0) {
        ESP_LOGI(TAG, "CST820 chip ID: 0x%02X", chip_id);
    } else {
        ESP_LOGW(TAG, "Could not read chip ID (device may still work)");
    }

    ESP_LOGI(TAG, "Touch controller ready");
    return 0;
}

int touch_read(touch_data_t *out)
{
    uint8_t buf[6];

    if (i2c_read_regs(CST820_REG_STATUS, buf, 6) != 0) {
        return -1;
    }

    /*
     * buf[0] = gesture ID
     * buf[1] = number of touch points
     * buf[2] = XH[3:0] + event[7:6]  (event nibble in bits 7:6)
     * buf[3] = XL
     * buf[4] = YH[3:0]
     * buf[5] = YL
     */
    uint8_t num_points = buf[1] & 0x0F;

    if (num_points == 0) {
        if (out->event == TOUCH_EVENT_DOWN || out->event == TOUCH_EVENT_MOVE) {
            /* Finger just lifted */
            out->event = TOUCH_EVENT_UP;
        } else {
            out->event = TOUCH_EVENT_NONE;
        }
        return 0;
    }

    int x = (int)(((buf[2] & 0x0F) << 8) | buf[3]);
    int y = (int)(((buf[4] & 0x0F) << 8) | buf[5]);

    /* Clamp to display bounds */
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    uint8_t event_flag = (buf[2] >> 6) & 0x03;
    /* event_flag: 0=press down, 1=lift up, 2=contact */

    touch_event_t ev;
    switch (event_flag) {
        case 0:
            ev = (out->event == TOUCH_EVENT_NONE || out->event == TOUCH_EVENT_UP)
                 ? TOUCH_EVENT_DOWN : TOUCH_EVENT_MOVE;
            break;
        case 1:
            ev = TOUCH_EVENT_UP;
            break;
        case 2:
            ev = (out->event == TOUCH_EVENT_DOWN) ? TOUCH_EVENT_MOVE : TOUCH_EVENT_DOWN;
            break;
        default:
            ev = TOUCH_EVENT_MOVE;
            break;
    }

    out->event = ev;
    out->x     = x;
    out->y     = y;
    return 0;
}
