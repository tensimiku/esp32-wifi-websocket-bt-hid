/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */
#include <inttypes.h>
#include <sdkconfig.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bt.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"


#if CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "store/ram/ble_store_ram.h"

void ble_store_config_init(void);

#else
#include "esp_bt_defs.h"
#if CONFIG_BT_BLE_ENABLED
#include "esp_gap_ble_api.h"
#include "esp_gatt_defs.h"
#include "esp_gatts_api.h"
#endif
#include "esp_bt_device.h"
#include "esp_bt_main.h"
#if CONFIG_BT_SDP_COMMON_ENABLED
#include "esp_sdp_api.h"
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */
#endif

#include "esp_hid_gap.h"
#include "esp_hidd.h"
#include "esp_https_server.h"

static const char *TAG = "HID_DEV_DEMO";

extern const uint8_t cacert_pem_start[] asm("_binary_cacert_pem_start");
extern const uint8_t cacert_pem_end[] asm("_binary_cacert_pem_end");
extern const uint8_t prvtkey_pem_start[] asm("_binary_prvtkey_pem_start");
extern const uint8_t prvtkey_pem_end[] asm("_binary_prvtkey_pem_end");

#include "secrets.h"

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

typedef struct {
  TaskHandle_t task_hdl;
  esp_hidd_dev_t *hid_dev;
  uint8_t protocol_mode;
  uint8_t *buffer;
} local_param_t;

#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
static local_param_t s_ble_hid_param = {0};

const unsigned char mediaReportMap[] = {
    0x05,
    0x0C, // Usage Page (Consumer)
    0x09,
    0x01, // Usage (Consumer Control)
    0xA1,
    0x01, // Collection (Application)
    0x85,
    0x03, //   Report ID (3)
    0x09,
    0x02, //   Usage (Numeric Key Pad)
    0xA1,
    0x02, //   Collection (Logical)
    0x05,
    0x09, //     Usage Page (Button)
    0x19,
    0x01, //     Usage Minimum (0x01)
    0x29,
    0x0A, //     Usage Maximum (0x0A)
    0x15,
    0x01, //     Logical Minimum (1)
    0x25,
    0x0A, //     Logical Maximum (10)
    0x75,
    0x04, //     Report Size (4)
    0x95,
    0x01, //     Report Count (1)
    0x81,
    0x00, //     Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null
          //     Position)
    0xC0, //   End Collection
    0x05,
    0x0C, //   Usage Page (Consumer)
    0x09,
    0x86, //   Usage (Channel)
    0x15,
    0xFF, //   Logical Minimum (-1)
    0x25,
    0x01, //   Logical Maximum (1)
    0x75,
    0x02, //   Report Size (2)
    0x95,
    0x01, //   Report Count (1)
    0x81,
    0x46, //   Input (Data,Var,Rel,No Wrap,Linear,Preferred State,Null State)
    0x09,
    0xE9, //   Usage (Volume Increment)
    0x09,
    0xEA, //   Usage (Volume Decrement)
    0x15,
    0x00, //   Logical Minimum (0)
    0x75,
    0x01, //   Report Size (1)
    0x95,
    0x02, //   Report Count (2)
    0x81,
    0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position)
    0x09,
    0xE2, //   Usage (Mute)
    0x09,
    0x30, //   Usage (Power)
    0x09,
    0x83, //   Usage (Recall Last)
    0x09,
    0x81, //   Usage (Assign Selection)
    0x09,
    0xB0, //   Usage (Play)
    0x09,
    0xB1, //   Usage (Pause)
    0x09,
    0xB2, //   Usage (Record)
    0x09,
    0xB3, //   Usage (Fast Forward)
    0x09,
    0xB4, //   Usage (Rewind)
    0x09,
    0xB5, //   Usage (Scan Next Track)
    0x09,
    0xB6, //   Usage (Scan Previous Track)
    0x09,
    0xB7, //   Usage (Stop)
    0x15,
    0x01, //   Logical Minimum (1)
    0x25,
    0x0C, //   Logical Maximum (12)
    0x75,
    0x04, //   Report Size (4)
    0x95,
    0x01, //   Report Count (1)
    0x81,
    0x00, //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position)
    0x09,
    0x80, //   Usage (Selection)
    0xA1,
    0x02, //   Collection (Logical)
    0x05,
    0x09, //     Usage Page (Button)
    0x19,
    0x01, //     Usage Minimum (0x01)
    0x29,
    0x03, //     Usage Maximum (0x03)
    0x15,
    0x01, //     Logical Minimum (1)
    0x25,
    0x03, //     Logical Maximum (3)
    0x75,
    0x02, //     Report Size (2)
    0x81,
    0x00, //     Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null
          //     Position)
    0xC0, //   End Collection
    0x81,
    0x03, //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position)
    0xC0, // End Collection
};

// send the buttons, change in x, and change in y
void send_mouse(uint8_t buttons, char dx, char dy, char wheel) {
  static uint8_t buffer[4] = {0};
  buffer[0] = buttons;
  buffer[1] = dx;
  buffer[2] = dy;
  buffer[3] = wheel;
  esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 2, buffer, 4);
}

#define CASE(a, b, c)                                                          \
  case a:                                                                      \
    buffer[0] = b;                                                             \
    buffer[2] = c;                                                             \
    break;

// USB keyboard codes
#define USB_HID_MODIFIER_LEFT_CTRL 0x01
#define USB_HID_MODIFIER_LEFT_SHIFT 0x02
#define USB_HID_MODIFIER_LEFT_ALT 0x04
#define USB_HID_MODIFIER_RIGHT_CTRL 0x10
#define USB_HID_MODIFIER_RIGHT_SHIFT 0x20
#define USB_HID_MODIFIER_RIGHT_ALT 0x40

#define USB_HID_SPACE 0x2C
#define USB_HID_DOT 0x37
#define USB_HID_NEWLINE 0x28
#define USB_HID_FSLASH 0x38
#define USB_HID_BSLASH 0x31
#define USB_HID_COMMA 0x36
#define USB_HID_DOT 0x37

const unsigned char compositeReportMap[] = {
    // 7 bytes input (modifiers, resrvd, keys*5), 1 byte output
    0x05,
    0x01, // Usage Page (Generic Desktop Ctrls)
    0x09,
    0x06, // Usage (Keyboard)
    0xA1,
    0x01, // Collection (Application)
    0x85,
    0x01, //   Report ID (1)
    0x05,
    0x07, //   Usage Page (Kbrd/Keypad)
    0x19,
    0xE0, //   Usage Minimum (0xE0)
    0x29,
    0xE7, //   Usage Maximum (0xE7)
    0x15,
    0x00, //   Logical Minimum (0)
    0x25,
    0x01, //   Logical Maximum (1)
    0x75,
    0x01, //   Report Size (1)
    0x95,
    0x08, //   Report Count (8)
    0x81,
    0x02, //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position)
    0x95,
    0x01, //   Report Count (1)
    0x75,
    0x08, //   Report Size (8)
    0x81,
    0x03, //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position)
    0x95,
    0x05, //   Report Count (5)
    0x75,
    0x01, //   Report Size (1)
    0x05,
    0x08, //   Usage Page (LEDs)
    0x19,
    0x01, //   Usage Minimum (Num Lock)
    0x29,
    0x05, //   Usage Maximum (Kana)
    0x91,
    0x02, //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position,Non-volatile)
    0x95,
    0x01, //   Report Count (1)
    0x75,
    0x03, //   Report Size (3)
    0x91,
    0x03, //   Output (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position,Non-volatile)
    0x95,
    0x05, //   Report Count (5)
    0x75,
    0x08, //   Report Size (8)
    0x15,
    0x00, //   Logical Minimum (0)
    0x25,
    0x65, //   Logical Maximum (101)
    0x05,
    0x07, //   Usage Page (Kbrd/Keypad)
    0x19,
    0x00, //   Usage Minimum (0x00)
    0x29,
    0x65, //   Usage Maximum (0x65)
    0x81,
    0x00, //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null
          //   Position)
    0xC0, // End Collection

    // 65 bytes,
    // Mouse Report Map

    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x02, // USAGE (Mouse)
    0xa1, 0x01, // COLLECTION (Application)
    0x85, 0x02, //   REPORT_ID (2)

    0x09, 0x01, //   USAGE (Pointer)
    0xa1, 0x00, //   COLLECTION (Physical)

    0x05, 0x09, //     USAGE_PAGE (Button)
    0x19, 0x01, //     USAGE_MINIMUM (Button 1)
    0x29, 0x03, //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x25, 0x01, //     LOGICAL_MAXIMUM (1)
    0x95, 0x03, //     REPORT_COUNT (3)
    0x75, 0x01, //     REPORT_SIZE (1)
    0x81, 0x02, //     INPUT (Data,Var,Abs)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x75, 0x05, //     REPORT_SIZE (5)
    0x81, 0x03, //     INPUT (Cnst,Var,Abs)

    0x05, 0x01, //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30, //     USAGE (X)
    0x09, 0x31, //     USAGE (Y)
    0x09, 0x38, //     USAGE (Wheel)
    0x15, 0x81, //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f, //     LOGICAL_MAXIMUM (127)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x03, //     REPORT_COUNT (3)
    0x81, 0x06, //     INPUT (Data,Var,Rel)

    0xc0, //   END_COLLECTION
    0xc0  // END_COLLECTION
};

static void char_to_code(uint8_t *buffer, char ch) {
  // Check if lower or upper case
  if (ch >= 'a' && ch <= 'z') {
    buffer[0] = 0;
    // convert ch to HID letter, starting at a = 4
    buffer[2] = (uint8_t)(4 + (ch - 'a'));
  } else if (ch >= 'A' && ch <= 'Z') {
    // Add left shift
    buffer[0] = USB_HID_MODIFIER_LEFT_SHIFT;
    // convert ch to lower case
    ch = ch - ('A' - 'a');
    // convert ch to HID letter, starting at a = 4
    buffer[2] = (uint8_t)(4 + (ch - 'a'));
  } else if (ch >= '0' && ch <= '9') // Check if number
  {
    buffer[0] = 0;
    // convert ch to HID number, starting at 1 = 30, 0 = 39
    if (ch == '0') {
      buffer[2] = 39;
    } else {
      buffer[2] = (uint8_t)(30 + (ch - '1'));
    }
  } else // not a letter nor a number
  {
    switch (ch) {
      CASE(' ', 0, USB_HID_SPACE);
      CASE('.', 0, USB_HID_DOT);
      CASE('\n', 0, USB_HID_NEWLINE);
      CASE('?', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_FSLASH);
      CASE('/', 0, USB_HID_FSLASH);
      CASE('\\', 0, USB_HID_BSLASH);
      CASE('|', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_BSLASH);
      CASE(',', 0, USB_HID_COMMA);
      CASE('<', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_COMMA);
      CASE('>', USB_HID_MODIFIER_LEFT_SHIFT, USB_HID_COMMA);
      CASE('@', USB_HID_MODIFIER_LEFT_SHIFT, 31);
      CASE('!', USB_HID_MODIFIER_LEFT_SHIFT, 30);
      CASE('#', USB_HID_MODIFIER_LEFT_SHIFT, 32);
      CASE('$', USB_HID_MODIFIER_LEFT_SHIFT, 33);
      CASE('%', USB_HID_MODIFIER_LEFT_SHIFT, 34);
      CASE('^', USB_HID_MODIFIER_LEFT_SHIFT, 35);
      CASE('&', USB_HID_MODIFIER_LEFT_SHIFT, 36);
      CASE('*', USB_HID_MODIFIER_LEFT_SHIFT, 37);
      CASE('(', USB_HID_MODIFIER_LEFT_SHIFT, 38);
      CASE(')', USB_HID_MODIFIER_LEFT_SHIFT, 39);
      CASE('-', 0, 0x2D);
      CASE('_', USB_HID_MODIFIER_LEFT_SHIFT, 0x2D);
      CASE('=', 0, 0x2E);
      CASE('+', USB_HID_MODIFIER_LEFT_SHIFT, 39);
      CASE(8, 0, 0x2A); // backspace
      CASE('\t', 0, 0x2B);
    default:
      buffer[0] = 0;
      buffer[2] = 0;
    }
  }
}

void send_keyboard(uint8_t mods, char c) {
  static uint8_t buffer[8] = {0};
  memset(buffer, 0, 8);
  if (c != 0) {
    char_to_code(buffer, c);
  } else {
    buffer[2] = 0;
  }
  buffer[0] |= mods;
  esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
  if (c != 0 || mods == 0) {
    vTaskDelay(50 / portTICK_PERIOD_MS);
    memset(buffer, 0, 8);
    esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
  }
}

static esp_hid_raw_report_map_t ble_report_maps[] = {
    {.data = compositeReportMap, .len = sizeof(compositeReportMap)}};

static esp_hid_device_config_t ble_hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = "ESP BLE HID Combo",
    .manufacturer_name = "Espressif",
    .serial_number = "1234567890",
    .report_maps = ble_report_maps,
    .report_maps_len = 1};

#define HID_CC_RPT_MUTE 1
#define HID_CC_RPT_POWER 2
#define HID_CC_RPT_LAST 3
#define HID_CC_RPT_ASSIGN_SEL 4
#define HID_CC_RPT_PLAY 5
#define HID_CC_RPT_PAUSE 6
#define HID_CC_RPT_RECORD 7
#define HID_CC_RPT_FAST_FWD 8
#define HID_CC_RPT_REWIND 9
#define HID_CC_RPT_SCAN_NEXT_TRK 10
#define HID_CC_RPT_SCAN_PREV_TRK 11
#define HID_CC_RPT_STOP 12

#define HID_CC_RPT_CHANNEL_UP 0x10
#define HID_CC_RPT_CHANNEL_DOWN 0x30
#define HID_CC_RPT_VOLUME_UP 0x40
#define HID_CC_RPT_VOLUME_DOWN 0x80

// HID Consumer Control report bitmasks
#define HID_CC_RPT_NUMERIC_BITS 0xF0
#define HID_CC_RPT_CHANNEL_BITS 0xCF
#define HID_CC_RPT_VOLUME_BITS 0x3F
#define HID_CC_RPT_BUTTON_BITS 0xF0
#define HID_CC_RPT_SELECTION_BITS 0xCF

// Macros for the HID Consumer Control 2-byte report
#define HID_CC_RPT_SET_NUMERIC(s, x)                                           \
  (s)[0] &= HID_CC_RPT_NUMERIC_BITS;                                           \
  (s)[0] = (x)
#define HID_CC_RPT_SET_CHANNEL(s, x)                                           \
  (s)[0] &= HID_CC_RPT_CHANNEL_BITS;                                           \
  (s)[0] |= ((x) & 0x03) << 4
#define HID_CC_RPT_SET_VOLUME_UP(s)                                            \
  (s)[0] &= HID_CC_RPT_VOLUME_BITS;                                            \
  (s)[0] |= 0x40
#define HID_CC_RPT_SET_VOLUME_DOWN(s)                                          \
  (s)[0] &= HID_CC_RPT_VOLUME_BITS;                                            \
  (s)[0] |= 0x80
#define HID_CC_RPT_SET_BUTTON(s, x)                                            \
  (s)[1] &= HID_CC_RPT_BUTTON_BITS;                                            \
  (s)[1] |= (x)
#define HID_CC_RPT_SET_SELECTION(s, x)                                         \
  (s)[1] &= HID_CC_RPT_SELECTION_BITS;                                         \
  (s)[1] |= ((x) & 0x03) << 4

// HID Consumer Usage IDs (subset of the codes available in the USB HID Usage
// Tables spec)
#define HID_CONSUMER_POWER 48 // Power
#define HID_CONSUMER_RESET 49 // Reset
#define HID_CONSUMER_SLEEP 50 // Sleep

#define HID_CONSUMER_MENU 64          // Menu
#define HID_CONSUMER_SELECTION 128    // Selection
#define HID_CONSUMER_ASSIGN_SEL 129   // Assign Selection
#define HID_CONSUMER_MODE_STEP 130    // Mode Step
#define HID_CONSUMER_RECALL_LAST 131  // Recall Last
#define HID_CONSUMER_QUIT 148         // Quit
#define HID_CONSUMER_HELP 149         // Help
#define HID_CONSUMER_CHANNEL_UP 156   // Channel Increment
#define HID_CONSUMER_CHANNEL_DOWN 157 // Channel Decrement

#define HID_CONSUMER_PLAY 176          // Play
#define HID_CONSUMER_PAUSE 177         // Pause
#define HID_CONSUMER_RECORD 178        // Record
#define HID_CONSUMER_FAST_FORWARD 179  // Fast Forward
#define HID_CONSUMER_REWIND 180        // Rewind
#define HID_CONSUMER_SCAN_NEXT_TRK 181 // Scan Next Track
#define HID_CONSUMER_SCAN_PREV_TRK 182 // Scan Previous Track
#define HID_CONSUMER_STOP 183          // Stop
#define HID_CONSUMER_EJECT 184         // Eject
#define HID_CONSUMER_RANDOM_PLAY 185   // Random Play
#define HID_CONSUMER_SELECT_DISC 186   // Select Disk
#define HID_CONSUMER_ENTER_DISC 187    // Enter Disc
#define HID_CONSUMER_REPEAT 188        // Repeat
#define HID_CONSUMER_STOP_EJECT 204    // Stop/Eject
#define HID_CONSUMER_PLAY_PAUSE 205    // Play/Pause
#define HID_CONSUMER_PLAY_SKIP 206     // Play/Skip

#define HID_CONSUMER_VOLUME 224      // Volume
#define HID_CONSUMER_BALANCE 225     // Balance
#define HID_CONSUMER_MUTE 226        // Mute
#define HID_CONSUMER_BASS 227        // Bass
#define HID_CONSUMER_VOLUME_UP 233   // Volume Increment
#define HID_CONSUMER_VOLUME_DOWN 234 // Volume Decrement

#define HID_RPT_ID_CC_IN 3  // Consumer Control input report ID
#define HID_CC_IN_RPT_LEN 2 // Consumer Control input report Len
void esp_hidd_send_consumer_value(uint8_t key_cmd, bool key_pressed) {
  uint8_t buffer[HID_CC_IN_RPT_LEN] = {0, 0};
  if (key_pressed) {
    switch (key_cmd) {
    case HID_CONSUMER_CHANNEL_UP:
      HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_UP);
      break;

    case HID_CONSUMER_CHANNEL_DOWN:
      HID_CC_RPT_SET_CHANNEL(buffer, HID_CC_RPT_CHANNEL_DOWN);
      break;

    case HID_CONSUMER_VOLUME_UP:
      HID_CC_RPT_SET_VOLUME_UP(buffer);
      break;

    case HID_CONSUMER_VOLUME_DOWN:
      HID_CC_RPT_SET_VOLUME_DOWN(buffer);
      break;

    case HID_CONSUMER_MUTE:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_MUTE);
      break;

    case HID_CONSUMER_POWER:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_POWER);
      break;

    case HID_CONSUMER_RECALL_LAST:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_LAST);
      break;

    case HID_CONSUMER_ASSIGN_SEL:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_ASSIGN_SEL);
      break;

    case HID_CONSUMER_PLAY:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PLAY);
      break;

    case HID_CONSUMER_PAUSE:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_PAUSE);
      break;

    case HID_CONSUMER_RECORD:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_RECORD);
      break;

    case HID_CONSUMER_FAST_FORWARD:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_FAST_FWD);
      break;

    case HID_CONSUMER_REWIND:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_REWIND);
      break;

    case HID_CONSUMER_SCAN_NEXT_TRK:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_NEXT_TRK);
      break;

    case HID_CONSUMER_SCAN_PREV_TRK:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_SCAN_PREV_TRK);
      break;

    case HID_CONSUMER_STOP:
      HID_CC_RPT_SET_BUTTON(buffer, HID_CC_RPT_STOP);
      break;

    default:
      break;
    }
  }
  esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, HID_RPT_ID_CC_IN, buffer,
                         HID_CC_IN_RPT_LEN);
  return;
}

void ble_hid_task_start_up(void) {
  // No-op: Background stdin loop replaced by HTTPS Web Server payload injection
}

void ble_hid_task_shut_down(void) {
  // No-op
}

static void ble_hidd_event_callback(void *handler_args, esp_event_base_t base,
                                    int32_t id, void *event_data) {
  esp_hidd_event_t event = (esp_hidd_event_t)id;
  esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
  static const char *TAG = "HID_DEV_BLE";

  switch (event) {
  case ESP_HIDD_START_EVENT: {
    ESP_LOGI(TAG, "START");
    esp_hid_ble_gap_adv_start();
    break;
  }
  case ESP_HIDD_CONNECT_EVENT: {
    ESP_LOGI(TAG, "CONNECT");
    break;
  }
  case ESP_HIDD_PROTOCOL_MODE_EVENT: {
    ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index,
             param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
    break;
  }
  case ESP_HIDD_CONTROL_EVENT: {
    ESP_LOGI(TAG, "CONTROL[%u]: %sSUSPEND", param->control.map_index,
             param->control.control ? "EXIT_" : "");
    if (param->control.control) {
      // exit suspend
      ble_hid_task_start_up();
    } else {
      // suspend
      ble_hid_task_shut_down();
    }
    break;
  }
  case ESP_HIDD_OUTPUT_EVENT: {
    ESP_LOGI(TAG,
             "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index,
             esp_hid_usage_str(param->output.usage), param->output.report_id,
             param->output.length);
    ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
    break;
  }
  case ESP_HIDD_FEATURE_EVENT: {
    ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:",
             param->feature.map_index, esp_hid_usage_str(param->feature.usage),
             param->feature.report_id, param->feature.length);
    ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
    break;
  }
  case ESP_HIDD_DISCONNECT_EVENT: {
    ESP_LOGI(TAG, "DISCONNECT: %s",
             esp_hid_disconnect_reason_str(
                 esp_hidd_dev_transport_get(param->disconnect.dev),
                 param->disconnect.reason));
    ble_hid_task_shut_down();
    esp_hid_ble_gap_adv_start();
    break;
  }
  case ESP_HIDD_STOP_EVENT: {
    ESP_LOGI(TAG, "STOP");
    break;
  }
  default:
    break;
  }
  return;
}
#endif

#if CONFIG_BT_HID_DEVICE_ENABLED
static local_param_t s_bt_hid_param = {0};
const unsigned char mouseReportMap[] = {
    0x05, 0x01, // USAGE_PAGE (Generic Desktop)
    0x09, 0x02, // USAGE (Mouse)
    0xa1, 0x01, // COLLECTION (Application)

    0x09, 0x01, //   USAGE (Pointer)
    0xa1, 0x00, //   COLLECTION (Physical)

    0x05, 0x09, //     USAGE_PAGE (Button)
    0x19, 0x01, //     USAGE_MINIMUM (Button 1)
    0x29, 0x03, //     USAGE_MAXIMUM (Button 3)
    0x15, 0x00, //     LOGICAL_MINIMUM (0)
    0x25, 0x01, //     LOGICAL_MAXIMUM (1)
    0x95, 0x03, //     REPORT_COUNT (3)
    0x75, 0x01, //     REPORT_SIZE (1)
    0x81, 0x02, //     INPUT (Data,Var,Abs)
    0x95, 0x01, //     REPORT_COUNT (1)
    0x75, 0x05, //     REPORT_SIZE (5)
    0x81, 0x03, //     INPUT (Cnst,Var,Abs)

    0x05, 0x01, //     USAGE_PAGE (Generic Desktop)
    0x09, 0x30, //     USAGE (X)
    0x09, 0x31, //     USAGE (Y)
    0x09, 0x38, //     USAGE (Wheel)
    0x15, 0x81, //     LOGICAL_MINIMUM (-127)
    0x25, 0x7f, //     LOGICAL_MAXIMUM (127)
    0x75, 0x08, //     REPORT_SIZE (8)
    0x95, 0x03, //     REPORT_COUNT (3)
    0x81, 0x06, //     INPUT (Data,Var,Rel)

    0xc0, //   END_COLLECTION
    0xc0  // END_COLLECTION
};

static esp_hid_raw_report_map_t bt_report_maps[] = {
    {.data = compositeReportMap, .len = sizeof(compositeReportMap)},
};

static esp_hid_device_config_t bt_hid_config = {.vendor_id = 0x16C0,
                                                .product_id = 0x05DF,
                                                .version = 0x0100,
                                                .device_name = "ESP BT HID1",
                                                .manufacturer_name =
                                                    "Espressif",
                                                .serial_number = "1234567890",
                                                .report_maps = bt_report_maps,
                                                .report_maps_len = 1};

// send the buttons, change in x, and change in y
void send_mouse(uint8_t buttons, char dx, char dy, char wheel) {
  static uint8_t buffer[4] = {0};
  buffer[0] = buttons;
  buffer[1] = dx;
  buffer[2] = dy;
  buffer[3] = wheel;
  esp_hidd_dev_input_set(s_bt_hid_param.hid_dev, 0, 0, buffer, 4);
}

void bt_hid_demo_task(void *pvParameters) {
  static const char *help_string = "###########################################"
                                   "#############################\n"
                                   "BT hid mouse demo usage:\n"
                                   "You can input these value to simulate "
                                   "mouse: 'q', 'w', 'e', 'a', 's', 'd', 'h'\n"
                                   "q -- click the left key\n"
                                   "w -- move up\n"
                                   "e -- click the right key\n"
                                   "a -- move left\n"
                                   "s -- move down\n"
                                   "d -- move right\n"
                                   "h -- show the help\n"
                                   "###########################################"
                                   "#############################\n";
  printf("%s\n", help_string);
  char c;
  while (1) {
    c = fgetc(stdin);
    switch (c) {
    case 'q':
      send_mouse(1, 0, 0, 0);
      break;
    case 'w':
      send_mouse(0, 0, -10, 0);
      break;
    case 'e':
      send_mouse(2, 0, 0, 0);
      break;
    case 'a':
      send_mouse(0, -10, 0, 0);
      break;
    case 's':
      send_mouse(0, 0, 10, 0);
      break;
    case 'd':
      send_mouse(0, 10, 0, 0);
      break;
    case 'h':
      printf("%s\n", help_string);
      break;
    default:
      break;
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void bt_hid_task_start_up(void) {
  xTaskCreate(bt_hid_demo_task, "bt_hid_demo_task", 2 * 1024, NULL,
              configMAX_PRIORITIES - 3, &s_bt_hid_param.task_hdl);
  return;
}

void bt_hid_task_shut_down(void) {
  if (s_bt_hid_param.task_hdl) {
    vTaskDelete(s_bt_hid_param.task_hdl);
    s_bt_hid_param.task_hdl = NULL;
  }
}

static void bt_hidd_event_callback(void *handler_args, esp_event_base_t base,
                                   int32_t id, void *event_data) {
  esp_hidd_event_t event = (esp_hidd_event_t)id;
  esp_hidd_event_data_t *param = (esp_hidd_event_data_t *)event_data;
  static const char *TAG = "HID_DEV_BT";

  switch (event) {
  case ESP_HIDD_START_EVENT: {
    if (param->start.status == ESP_OK) {
      ESP_LOGI(TAG, "START OK");
      ESP_LOGI(TAG, "Setting to connectable, discoverable");
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    } else {
      ESP_LOGE(TAG, "START failed!");
    }
    break;
  }
  case ESP_HIDD_CONNECT_EVENT: {
    if (param->connect.status == ESP_OK) {
      ESP_LOGI(TAG, "CONNECT OK");
      ESP_LOGI(TAG, "Setting to non-connectable, non-discoverable");
      esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);
      bt_hid_task_start_up();
    } else {
      ESP_LOGE(TAG, "CONNECT failed!");
    }
    break;
  }
  case ESP_HIDD_PROTOCOL_MODE_EVENT: {
    ESP_LOGI(TAG, "PROTOCOL MODE[%u]: %s", param->protocol_mode.map_index,
             param->protocol_mode.protocol_mode ? "REPORT" : "BOOT");
    break;
  }
  case ESP_HIDD_OUTPUT_EVENT: {
    ESP_LOGI(TAG,
             "OUTPUT[%u]: %8s ID: %2u, Len: %d, Data:", param->output.map_index,
             esp_hid_usage_str(param->output.usage), param->output.report_id,
             param->output.length);
    ESP_LOG_BUFFER_HEX(TAG, param->output.data, param->output.length);
    break;
  }
  case ESP_HIDD_FEATURE_EVENT: {
    ESP_LOGI(TAG, "FEATURE[%u]: %8s ID: %2u, Len: %d, Data:",
             param->feature.map_index, esp_hid_usage_str(param->feature.usage),
             param->feature.report_id, param->feature.length);
    ESP_LOG_BUFFER_HEX(TAG, param->feature.data, param->feature.length);
    break;
  }
  case ESP_HIDD_DISCONNECT_EVENT: {
    if (param->disconnect.status == ESP_OK) {
      ESP_LOGI(TAG, "DISCONNECT OK");
      bt_hid_task_shut_down();
      ESP_LOGI(TAG, "Setting to connectable, discoverable again");
      esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    } else {
      ESP_LOGE(TAG, "DISCONNECT failed!");
    }
    break;
  }
  case ESP_HIDD_STOP_EVENT: {
    ESP_LOGI(TAG, "STOP");
    break;
  }
  default:
    break;
  }
  return;
}

#if CONFIG_BT_SDP_COMMON_ENABLED
static void esp_sdp_cb(esp_sdp_cb_event_t event, esp_sdp_cb_param_t *param) {
  switch (event) {
  case ESP_SDP_INIT_EVT:
    ESP_LOGI(TAG, "ESP_SDP_INIT_EVT: status:%d", param->init.status);
    if (param->init.status == ESP_SDP_SUCCESS) {
      esp_bluetooth_sdp_dip_record_t dip_record = {
          .hdr =
              {
                  .type = ESP_SDP_TYPE_DIP_SERVER,
              },
          .vendor = bt_hid_config.vendor_id,
          .vendor_id_source = ESP_SDP_VENDOR_ID_SRC_BT,
          .product = bt_hid_config.product_id,
          .version = bt_hid_config.version,
          .primary_record = true,
      };
      esp_sdp_create_record((esp_bluetooth_sdp_record_t *)&dip_record);
    }
    break;
  case ESP_SDP_DEINIT_EVT:
    ESP_LOGI(TAG, "ESP_SDP_DEINIT_EVT: status:%d", param->deinit.status);
    break;
  case ESP_SDP_SEARCH_COMP_EVT:
    ESP_LOGI(TAG, "ESP_SDP_SEARCH_COMP_EVT: status:%d", param->search.status);
    break;
  case ESP_SDP_CREATE_RECORD_COMP_EVT:
    ESP_LOGI(TAG, "ESP_SDP_CREATE_RECORD_COMP_EVT: status:%d, handle:0x%x",
             param->create_record.status, param->create_record.record_handle);
    break;
  case ESP_SDP_REMOVE_RECORD_COMP_EVT:
    ESP_LOGI(TAG, "ESP_SDP_REMOVE_RECORD_COMP_EVT: status:%d",
             param->remove_record.status);
    break;
  default:
    break;
  }
}
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */

#endif

#if CONFIG_BT_NIMBLE_ENABLED
void ble_hid_device_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");
  /* This function will return only when nimble_port_stop() is executed */
  nimble_port_run();
  nimble_port_freertos_deinit();
}
#endif

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    esp_wifi_connect();
    ESP_LOGI(TAG, "retry to connect to the AP");
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
    if (s_wifi_event_group) {
      xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
  }
}

static void wifi_init_sta(void) {
  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
      &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
      &instance_got_ip));

  wifi_config_t wifi_config = {
      .sta =
          {
              .ssid = WIFI_SSID,
              .password = WIFI_PASS,
              .threshold.authmode = WIFI_AUTH_WPA2_PSK,
              .pmf_cfg = {.capable = true, .required = false},
          },
  };
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "wifi_init_sta finished.");
  xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE,
                      portMAX_DELAY);
}

static const char *index_html =
    "<!DOCTYPE html>\n"
    "<html>\n"
    "<head><title>ESP32 BLE Combo</title>\n"
    "<meta charset=\"UTF-8\">\n"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
    "<style>\n"
    "body{font-family:sans-serif;padding:20px;background:#f0f0f0;text-align:"
    "center;}\n"
    "input, button{font-size:16px;padding:5px;border:1px solid "
    "#ccc;border-radius:4px;margin:5px;}\n"
    "#pad{width:500px;height:500px;background:#ddd;border:2px dashed "
    "#999;border-radius:10px;margin:10px "
    "auto;touch-action:none;cursor:crosshair;user-select:none;}\n"
    ".active{background:#aaa !important;}\n"
    "</style>\n"
    "</head>\n"
    "<body>\n"
    "    <h2>ESP32 BLE Remote</h2>\n"
    "    <label>Password: <input type=\"password\" id=\"pw\" "
    "value=\"yourpwhere\"></label><br>\n"
    "    <button id=\"pairBtn\">Force BLE Pairing Mode</button><hr>\n"
    "    <h3>Keyboard</h3>\n"
    "    <div>\n"
    "        <button id=\"bCtrl\" onclick=\"tM(this,1)\">L-Ctrl</button>\n"
    "        <button id=\"bShift\" onclick=\"tM(this,2)\">L-Shift</button>\n"
    "        <button id=\"bAlt\" onclick=\"tM(this,4)\">L-Alt</button>\n"
    "        <button id=\"bWin\" onclick=\"tM(this,8)\">L-GUI</button>\n"
    "        <button onclick=\"sd('T', 64)\" "
    "style=\"background:#ffebcd;\">R-Alt (한/영)</button>\n"
    "        <button onclick=\"sd('T', 16)\" "
    "style=\"background:#ffebcd;\">R-Ctrl (한자)</button>\n"
    "    </div><br>\n"
    "    <input type=\"text\" id=\"kb\" placeholder=\"Type here...\">\n"
    "    <h3>Mouse Trackpad</h3>\n"
    "    <div id=\"pad\">Drag here</div>\n"
    "    <script>\n"
    "        const pwElem = document.getElementById(\"pw\");\n"
    "        let ws = new WebSocket('wss://' + location.host + '/ws');\n"
    "        ws.onclose = () => setTimeout(() => location.reload(), 2000);\n"
    "        const sd = (t, d) => {\n"
    "            if (ws.readyState === WebSocket.OPEN) ws.send(pwElem.value + "
    "'|' + t + '|' + d);\n"
    "        };\n"
    "        document.getElementById(\"pairBtn\").addEventListener(\"click\", "
    "async () => {\n"
    "            await fetch(\"/pair\", {method: "
    "\"POST\"}).catch(e=>console.error(e));\n"
    "            alert(\"Pairing mode activated!\");\n"
    "        });\n"
    "        let cM = 0;\n"
    "        function tM(b, m) { if(cM & m){ cM &= ~m; "
    "b.classList.remove('active');sd('K', cM + '|'); }else{ cM |= m; "
    "b.classList.add('active');sd('K', cM + '|'); } }\n"
    "        const kb = document.getElementById(\"kb\");\n"
    "        kb.addEventListener(\"keydown\", (e) => {\n"
    "            if (e.code === 'AltRight' || (e.shiftKey && e.code === "
    "'Space')) { e.preventDefault(); sd('T', 64); return; }\n"
    "            if (e.code === 'ControlRight') { e.preventDefault(); sd('T', "
    "16); return; }\n"
    "            const map = { 'ArrowUp':82, 'ArrowDown':81, 'ArrowLeft':80, "
    "'ArrowRight':79, \n"
    "                          'Backspace':42, 'Enter':40, 'Tab':43, "
    "'Delete':76, 'Escape':41 };\n"
    "            if (map[e.key]) {\n"
    "                e.preventDefault();\n"
    "                let m = "
    "(e.ctrlKey?1:0)|(e.shiftKey?2:0)|(e.altKey?4:0)|(e.metaKey?8:0);\n"
    "                sd('R', m + '|' + map[e.key]);\n"
    "                return;\n"
    "            }\n"
    "            let m = cM; if(e.ctrlKey)m|=1; if(e.shiftKey)m|=2; "
    "if(e.altKey)m|=4; if(e.metaKey)m|=8;\n"
    "            if([\"Control\",\"Shift\",\"Alt\",\"Meta\"].includes(e.key)) "
    "return;\n"
    "            if(e.key.length === 1) {\n"
    "                sd('K', m + '|' + e.key);\n"
    "                setTimeout(() => { kb.value = ''; }, 10);\n"
    "            }\n"
    "        });\n"
    "        const pad = document.getElementById(\"pad\");\n"
    "        let isD=false, lx=0, ly=0, lS=0;\n"
    "        const sm = (b,x,y,w) => sd('M', b+','+x+','+y+','+w);\n"
    "        pad.addEventListener('mousedown', e => { isD=true; lx=e.clientX; "
    "ly=e.clientY; sm(e.button===2?2:1,0,0,0); });\n"
    "        pad.addEventListener('mouseup', e => { isD=false; sm(0,0,0,0); "
    "});\n"
    "        pad.addEventListener('mouseleave', e => { if(isD){ isD=false; "
    "sm(0,0,0,0); } });\n"
    "        pad.addEventListener('contextmenu', e => e.preventDefault());\n"
    "        pad.addEventListener('mouseenter', e => { lx=e.clientX; "
    "ly=e.clientY; });\n"
    "        pad.addEventListener('mousemove', e => {\n"
    "            const now = Date.now(); if(now - lS < 100) return; lS = now;\n"
    "            let dx = (e.clientX - lx) | 0; let dy = (e.clientY - ly) | "
    "0;\n"
    "            if(dx>127)dx=127; if(dx<-127)dx=-127;\n"
    "            if(dy>127)dy=127; if(dy<-127)dy=-127;\n"
    "            lx += dx; ly += dy;\n"
    "            sm(e.buttons&2?2:(e.buttons&1?1:0), dx, dy, 0);\n"
    "        });\n"
    "        pad.addEventListener('touchstart', e => { isD=true; let "
    "t=e.touches[0]; lx=t.clientX; ly=t.clientY; sm(1,0,0,0); "
    "e.preventDefault(); }, {passive:false});\n"
    "        pad.addEventListener('touchend', e => { isD=false; sm(0,0,0,0); "
    "e.preventDefault(); }, {passive:false});\n"
    "        pad.addEventListener('touchmove', e => {\n"
    "            if(!isD) return; e.preventDefault();\n"
    "            const now = Date.now(); if(now - lS < 100) return; lS = now;\n"
    "            let t=e.touches[0]; let dx = (t.clientX - lx) | 0; let dy = "
    "(t.clientY - ly) | 0;\n"
    "            if(dx>127)dx=127; if(dx<-127)dx=-127;\n"
    "            if(dy>127)dy=127; if(dy<-127)dy=-127;\n"
    "            lx += dx; ly += dy;\n"
    "            sm(1, dx, dy, 0);\n"
    "        }, {passive:false});\n"
    "        pad.addEventListener('wheel', e => { e.preventDefault(); "
    "sm(0,0,0,e.deltaY>0?-1:1); }, {passive:false});\n"
    "    </script>\n"
    "</body>\n"
    "</html>\n";

static esp_err_t index_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
}

extern void send_keyboard(uint8_t mods, char c);
extern void send_mouse(uint8_t buttons, char dx, char dy, char wheel);

static esp_err_t pair_post_handler(httpd_req_t *req) {
  ESP_LOGI(TAG, "HTTPS payload verified. Restarting Advertisement to allow new "
                "pairing...");
  esp_hid_ble_gap_adv_start();
  httpd_resp_send_chunk(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t ws_handler(httpd_req_t *req) {
  if (req->method == HTTP_GET) {
    ESP_LOGI(TAG, "WebSocket connection established");
    return ESP_OK;
  }

  httpd_ws_frame_t ws_pkt;
  memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK)
    return ret;

  if (ws_pkt.len) {
    char *buf = calloc(1, ws_pkt.len + 1);
    if (!buf)
      return ESP_ERR_NO_MEM;
    ws_pkt.payload = (uint8_t *)buf;
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret == ESP_OK) {
      char *delim = strchr(buf, '|');
      if (delim) {
        *delim = '\0';
        char *pw = buf;
        char *payload = delim + 1;
        if (strcmp(pw, HTTP_PASSWORD) == 0 && strlen(payload) >= 3 &&
            payload[1] == '|') {
          char type = payload[0];
          char *data = payload + 2;
          if (type == 'K') {
            int mods = 0;
            char key_char = 0;
            char *split = strchr(data, '|');
            if (split) {
              *split = '\0';
              mods = atoi(data);
              key_char = *(split + 1);
              ESP_LOGI(TAG, "Keyboard Mod: %d, Char: %c", mods, key_char);
              send_keyboard((uint8_t)mods, key_char);
            } else {
              send_keyboard(0, data[0]);
            }
          } else if (type == 'R') {
            int mods = 0, hid = 0;
            if (sscanf(data, "%d|%d", &mods, &hid) == 2) {
              uint8_t buffer[8] = {0};
              buffer[0] = (uint8_t)mods;
              buffer[2] = (uint8_t)hid;
              ESP_LOGI(TAG, "Raw Key Mod: %d, HID: %d", mods, hid);
              esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
              vTaskDelay(50 / portTICK_PERIOD_MS);
              memset(buffer, 0, 8);
              esp_hidd_dev_input_set(s_ble_hid_param.hid_dev, 0, 1, buffer, 8);
            }
          } else if (type == 'T') {
            int mods = atoi(data);
            ESP_LOGI(TAG, "Tap modifier: %d", mods);
            send_keyboard((uint8_t)mods, 0);
            vTaskDelay(50 / portTICK_PERIOD_MS);
            send_keyboard(0, 0);
          } else if (type == 'M') {
            int b = 0, x = 0, y = 0, w = 0;
            if (sscanf(data, "%d,%d,%d,%d", &b, &x, &y, &w) == 4) {
              ESP_LOGI(TAG, "Mouse B:%d X:%d Y:%d W:%d", b, x, y, w);
              send_mouse((uint8_t)b, (char)x, (char)y, (char)w);
            } else {
              ESP_LOGW(TAG, "Mouse parsing failed for payload: %s", data);
            }
          }
        }
      }
    }
    free(buf);
  }
  return ret;
}

static httpd_handle_t start_webserver(void) {
  httpd_handle_t server = NULL;
  httpd_ssl_config_t conf = HTTPD_SSL_CONFIG_DEFAULT();
  conf.servercert = cacert_pem_start;
  conf.servercert_len = cacert_pem_end - cacert_pem_start;
  conf.prvtkey_pem = prvtkey_pem_start;
  conf.prvtkey_len = prvtkey_pem_end - prvtkey_pem_start;
  conf.httpd.keep_alive_enable = true;

  ESP_LOGI(TAG, "Starting HTTPS server...");
  esp_err_t ret = httpd_ssl_start(&server, &conf);
  if (ESP_OK != ret) {
    ESP_LOGI(TAG, "Error starting server! status %d", ret);
    return NULL;
  }

  httpd_uri_t index_uri = {.uri = "/",
                           .method = HTTP_GET,
                           .handler = index_get_handler,
                           .user_ctx = NULL};
  httpd_register_uri_handler(server, &index_uri);

  httpd_uri_t ws_uri = {.uri = "/ws",
                        .method = HTTP_GET,
                        .handler = ws_handler,
                        .user_ctx = NULL,
                        .is_websocket = true};
  httpd_register_uri_handler(server, &ws_uri);

  httpd_uri_t pair_uri = {.uri = "/pair",
                          .method = HTTP_POST,
                          .handler = pair_post_handler,
                          .user_ctx = NULL};
  httpd_register_uri_handler(server, &pair_uri);

  return server;
}

void app_main(void) {
  esp_err_t ret;
#if HID_DEV_MODE == HIDD_IDLE_MODE
  ESP_LOGE(TAG, "Please turn on BT HID device or BLE!");
  return;
#endif
  ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  esp_err_t evt_ret = esp_event_loop_create_default();
  if (evt_ret != ESP_OK && evt_ret != ESP_ERR_INVALID_STATE) {
    ESP_ERROR_CHECK(evt_ret);
  }

  ESP_LOGI(TAG, "Initializing Wi-Fi Station...");
  wifi_init_sta();

  ESP_LOGI(TAG, "Starting HTTPS Web Server...");
  start_webserver();

  ESP_LOGI(TAG, "setting hid gap, mode:%d", HID_DEV_MODE);
  ret = esp_hid_gap_init(HID_DEV_MODE);
  ESP_ERROR_CHECK(ret);

#if CONFIG_BT_BLE_ENABLED || CONFIG_BT_NIMBLE_ENABLED
#if CONFIG_EXAMPLE_HID_DEVICE_ROLE == 2
  ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD,
                                 ble_hid_config.device_name);
#elif CONFIG_EXAMPLE_HID_DEVICE_ROLE == 3
  ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_MOUSE,
                                 ble_hid_config.device_name);
#else
  ret = esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_GENERIC,
                                 ble_hid_config.device_name);
#endif
  ESP_ERROR_CHECK(ret);
#if CONFIG_BT_BLE_ENABLED
  if ((ret = esp_ble_gatts_register_callback(esp_hidd_gatts_event_handler)) !=
      ESP_OK) {
    ESP_LOGE(TAG, "GATTS register callback failed: %d", ret);
    return;
  }
#endif
  ESP_LOGI(TAG, "setting ble device");
  ESP_ERROR_CHECK(esp_hidd_dev_init(&ble_hid_config, ESP_HID_TRANSPORT_BLE,
                                    ble_hidd_event_callback,
                                    &s_ble_hid_param.hid_dev));
#endif

#if CONFIG_BT_HID_DEVICE_ENABLED
  ESP_LOGI(TAG, "setting device name");
  esp_bt_gap_set_device_name(bt_hid_config.device_name);
  ESP_LOGI(TAG, "setting cod major, peripheral");
  esp_bt_cod_t cod = {0};
  cod.major = ESP_BT_COD_MAJOR_DEV_PERIPHERAL;
  cod.minor = ESP_BT_COD_MINOR_PERIPHERAL_POINTING;
  esp_bt_gap_set_cod(cod, ESP_BT_SET_COD_MAJOR_MINOR);
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  ESP_LOGI(TAG, "setting bt device");
  ESP_ERROR_CHECK(esp_hidd_dev_init(&bt_hid_config, ESP_HID_TRANSPORT_BT,
                                    bt_hidd_event_callback,
                                    &s_bt_hid_param.hid_dev));
#if CONFIG_BT_SDP_COMMON_ENABLED
  ESP_ERROR_CHECK(esp_sdp_register_callback(esp_sdp_cb));
  ESP_ERROR_CHECK(esp_sdp_init());
#endif /* CONFIG_BT_SDP_COMMON_ENABLED */
#endif /* CONFIG_BT_HID_DEVICE_ENABLED */
#if CONFIG_BT_NIMBLE_ENABLED
  /* XXX Need to have template for store */
  ble_store_config_init();

  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
  /* Starting nimble task after gatts is initialized*/
  ret = esp_nimble_enable(ble_hid_device_host_task);
  if (ret) {
    ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
  }
#endif
}
