/*
 * hid_mouse.c
 *
 * USB HID mouse implementation using the ESP-IDF TinyUSB integration.
 *
 * USB descriptors are passed into tinyusb_driver_install() via the v2.x
 * nested tinyusb_desc_config_t sub-struct so they do not conflict with
 * the default descriptor table built by the driver.
 *
 * Only the HID class callbacks are implemented here:
 *   tud_hid_descriptor_report_cb  - supply the report descriptor
 *   tud_hid_get_report_cb         - required stub
 *   tud_hid_set_report_cb         - required stub
 */

#include "hid_mouse.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"

static const char *TAG = "hid_mouse";

/* ---- HID Report Descriptor -------------------------------------------- */

/*
 * Standard boot-compatible mouse:
 *   3 buttons + X + Y + scroll wheel.
 *
 * Report layout (4 bytes total):
 *   byte 0 [7:3] padding (constant 0)
 *   byte 0 [2:0] buttons  (left, right, middle)
 *   byte 1       X movement  (signed 8-bit relative)
 *   byte 2       Y movement  (signed 8-bit relative)
 *   byte 3       wheel       (signed 8-bit relative)
 */
static const uint8_t s_hid_report_descriptor[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)       */
    0x09, 0x02,        /* Usage (Mouse)                      */
    0xA1, 0x01,        /* Collection (Application)           */
    0x09, 0x01,        /*   Usage (Pointer)                  */
    0xA1, 0x00,        /*   Collection (Physical)            */
    /* Buttons */
    0x05, 0x09,        /*     Usage Page (Buttons)           */
    0x19, 0x01,        /*     Usage Minimum (1)              */
    0x29, 0x03,        /*     Usage Maximum (3)              */
    0x15, 0x00,        /*     Logical Minimum (0)            */
    0x25, 0x01,        /*     Logical Maximum (1)            */
    0x95, 0x03,        /*     Report Count (3)               */
    0x75, 0x01,        /*     Report Size (1 bit)            */
    0x81, 0x02,        /*     Input (Data,Var,Abs)           */
    /* Padding to complete byte 0 */
    0x95, 0x01,        /*     Report Count (1)               */
    0x75, 0x05,        /*     Report Size (5 bits)           */
    0x81, 0x03,        /*     Input (Const,Var,Abs)          */
    /* X, Y, Wheel */
    0x05, 0x01,        /*     Usage Page (Generic Desktop)   */
    0x09, 0x30,        /*     Usage (X)                      */
    0x09, 0x31,        /*     Usage (Y)                      */
    0x09, 0x38,        /*     Usage (Wheel)                  */
    0x15, 0x81,        /*     Logical Minimum (-127)         */
    0x25, 0x7F,        /*     Logical Maximum (127)          */
    0x75, 0x08,        /*     Report Size (8 bits)           */
    0x95, 0x03,        /*     Report Count (3)               */
    0x81, 0x06,        /*     Input (Data,Var,Rel)           */
    0xC0,              /*   End Collection (Physical)        */
    0xC0               /* End Collection (Application)       */
};

#define HID_REPORT_DESC_LEN  ((uint16_t)sizeof(s_hid_report_descriptor))

/* ---- USB Descriptors -------------------------------------------------- */

/* String descriptor indices */
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_COUNT
};

static const char *s_string_descriptors[STRID_COUNT] = {
    "\x09\x04",                                /* 0: supported language = English (0x0409) */
    CONFIG_TINYUSB_DESC_MANUFACTURER_STRING,   /* 1: manufacturer */
    CONFIG_TINYUSB_DESC_PRODUCT_STRING,        /* 2: product      */
    CONFIG_TINYUSB_DESC_SERIAL_STRING,         /* 3: serial       */
};

/* Device descriptor */
static const tusb_desc_device_t s_desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,                   /* CFG_TUD_ENDPOINT0_SIZE */
    .idVendor           = CONFIG_USB_VID,
    .idProduct          = CONFIG_USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 0x01
};

/*
 * Configuration descriptor
 *
 * Total = 9 (config) + 9 (interface) + 9 (HID class) + 7 (endpoint) = 34
 */
#define CONFIG_DESC_TOTAL_LEN  34
#define HID_IN_EP_ADDR         0x81   /* EP1 IN */
#define HID_IN_EP_INTERVAL     10     /* poll interval in ms (Full Speed) */

static const uint8_t s_desc_configuration[CONFIG_DESC_TOTAL_LEN] = {
    /* Configuration descriptor (9 bytes) */
    9,                              /* bLength              */
    TUSB_DESC_CONFIGURATION,        /* bDescriptorType      */
    CONFIG_DESC_TOTAL_LEN, 0x00,    /* wTotalLength         */
    1,                              /* bNumInterfaces       */
    1,                              /* bConfigurationValue  */
    0,                              /* iConfiguration       */
    0x80 | 0x40,                    /* bmAttributes: bus-powered, remote wakeup */
    50,                             /* bMaxPower: 100 mA    */

    /* Interface descriptor (9 bytes) */
    9,                              /* bLength              */
    TUSB_DESC_INTERFACE,            /* bDescriptorType      */
    0,                              /* bInterfaceNumber     */
    0,                              /* bAlternateSetting    */
    1,                              /* bNumEndpoints        */
    TUSB_CLASS_HID,                 /* bInterfaceClass      */
    HID_SUBCLASS_BOOT,              /* bInterfaceSubClass   */
    HID_ITF_PROTOCOL_MOUSE,         /* bInterfaceProtocol   */
    0,                              /* iInterface           */

    /* HID Class descriptor (9 bytes) */
    9,                              /* bLength              */
    HID_DESC_TYPE_HID,              /* bDescriptorType      */
    0x11, 0x01,                     /* bcdHID 1.11          */
    0x00,                           /* bCountryCode         */
    1,                              /* bNumDescriptors      */
    HID_DESC_TYPE_REPORT,           /* bDescriptorType[0]   */
    (uint8_t)(HID_REPORT_DESC_LEN & 0xFF),
    (uint8_t)((HID_REPORT_DESC_LEN >> 8) & 0xFF),

    /* Endpoint descriptor (7 bytes) */
    7,                              /* bLength              */
    TUSB_DESC_ENDPOINT,             /* bDescriptorType      */
    HID_IN_EP_ADDR,                 /* bEndpointAddress     */
    TUSB_XFER_INTERRUPT,            /* bmAttributes         */
    8, 0x00,                        /* wMaxPacketSize       */
    HID_IN_EP_INTERVAL              /* bInterval            */
};

/* ---- Required TinyUSB HID callbacks ----------------------------------- */

/*
 * tud_hid_descriptor_report_cb (required by TinyUSB)
 *
 * Return the HID report descriptor for the given interface instance.
 */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_descriptor;
}

/*
 * tud_hid_get_report_cb (required by TinyUSB, may be a stub)
 */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

/*
 * tud_hid_set_report_cb (required by TinyUSB, may be a stub)
 */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

/* ---- Public API ------------------------------------------------------- */

int hid_mouse_init(void)
{
    ESP_LOGI(TAG, "Initialising TinyUSB HID mouse");

    /*
     * esp_tinyusb v2.x: start from the default configuration and override
     * only the descriptor sub-struct.  The driver creates its own internal
     * tud_task() loop, so no external task is required.
     */
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device           = &s_desc_device;
    tusb_cfg.descriptor.string           = s_string_descriptors;
    tusb_cfg.descriptor.string_count     = STRID_COUNT;
    tusb_cfg.descriptor.full_speed_config = s_desc_configuration;

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "TinyUSB HID mouse ready");
    return 0;
}

int hid_mouse_send(uint8_t buttons, int8_t dx, int8_t dy, int8_t scroll)
{
    if (!tud_hid_ready()) {
        return -1;
    }
    /* tud_hid_mouse_report(report_id, buttons, x, y, vertical, horizontal) */
    return tud_hid_mouse_report(0, buttons, dx, dy, scroll, 0) ? 0 : -1;
}
