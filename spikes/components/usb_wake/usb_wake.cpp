// SPDX-License-Identifier: GPL-3.0-only
#include "usb_wake.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>

#include "esp_rom_sys.h"
#include "soc/usb_dwc_struct.h"

#include "class/hid/hid_device.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

namespace esphome::usb_wake {

static const char *const TAG = "usb_wake";

enum : uint8_t { ITF_CDC = 0, ITF_CDC_DATA, ITF_HID, ITF_COUNT };
enum : uint8_t { EP_CDC_NOTIF = 0x81, EP_CDC_OUT = 0x02, EP_CDC_IN = 0x82, EP_HID_IN = 0x83 };

enum : uint8_t { REPORT_ID_KEYBOARD = 1, REPORT_ID_SYSTEM = 2 };
// System control values in TUD_HID_REPORT_DESC_SYSTEM_CONTROL: 1 power down, 2 sleep, 3 wake up
static const uint8_t SYSTEM_SLEEP = 2;

static const uint8_t HID_REPORT_DESC[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_SYSTEM_CONTROL(HID_REPORT_ID(REPORT_ID_SYSTEM)),
};

static const uint16_t CONFIG_LEN = TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_DESC_LEN;

// The remote wakeup attribute is the subject of this spike
static const uint8_t CONFIG_DESC[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_HID, 5, HID_ITF_PROTOCOL_KEYBOARD, sizeof(HID_REPORT_DESC), EP_HID_IN, 8, 10),
};

// A composite device with an IAD needs the MISC class (see tinyusb.h)
static const tusb_desc_device_t DEVICE_DESC = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4002,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

static const char *STRING_DESC[] = {
    "\x09\x04",             // 0: English
    "Plum Solutions",       // 1: manufacturer
    "Plum-CEC wake spike",  // 2: product
    "S3WAKE0001",           // 3: serial number
    "Plum-CEC serial",      // 4: CDC interface
    "Plum-CEC keyboard",    // 5: HID interface
};

static UsbWake *instance = nullptr;

static void on_event(tinyusb_event_t *event, void *arg) {
  auto *self = static_cast<UsbWake *>(arg);
  switch (event->id) {
    case TINYUSB_EVENT_ATTACHED:
      self->on_attach(true);
      break;
    case TINYUSB_EVENT_DETACHED:
      self->on_attach(false);
      break;
    case TINYUSB_EVENT_SUSPENDED:
      self->on_suspend(event->suspended.remote_wakeup);
      break;
    case TINYUSB_EVENT_RESUMED:
      self->on_resume();
      break;
  }
}

void UsbWake::setup() {
  instance = this;
  tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
  cfg.port = TINYUSB_PORT_FULL_SPEED_0;
  cfg.phy.skip_setup = false;
  cfg.descriptor.device = &DEVICE_DESC;
  cfg.descriptor.string = STRING_DESC;
  cfg.descriptor.string_count = sizeof(STRING_DESC) / sizeof(STRING_DESC[0]);
  cfg.descriptor.full_speed_config = CONFIG_DESC;
  cfg.event_cb = on_event;
  cfg.event_arg = this;
  esp_err_t err = tinyusb_driver_install(&cfg);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_driver_install failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  tinyusb_config_cdcacm_t acm = {};
  acm.cdc_port = TINYUSB_CDC_ACM_0;
  err = tinyusb_cdcacm_init(&acm);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_cdcacm_init failed: %s", esp_err_to_name(err));
    this->mark_failed();
  }
}

void UsbWake::on_suspend(bool remote_wakeup_enabled) {
  this->remote_wakeup_enabled_ = remote_wakeup_enabled;
  this->suspended_ = true;
  this->events_++;
}

void UsbWake::on_resume() {
  this->suspended_ = false;
  this->events_++;
}

void UsbWake::on_attach(bool attached) {
  this->attached_ = attached;
  this->events_++;
}

void UsbWake::loop() {
  if (this->release_pending_ && tud_hid_ready()) {
    uint8_t value = 0;
    tud_hid_report(REPORT_ID_SYSTEM, &value, sizeof(value));
    this->release_pending_ = false;
  }
  uint32_t now = millis();
  if (this->suspended_.load() && now - this->last_line_log_ >= 2000) {
    this->last_line_log_ = now;
    uint32_t dsts = USB_DWC.dsts_reg.val;
    ESP_LOGD(TAG, "Suspended: DSTS 0x%08" PRIX32 " suspsts=%u D+=%u D-=%u", dsts, (unsigned) (dsts & 1),
             (unsigned) ((dsts >> 23) & 1), (unsigned) ((dsts >> 22) & 1));
  }
  int events = this->events_.load();
  if (events == this->logged_events_)
    return;
  this->logged_events_ = events;
  ESP_LOGI(TAG, "USB state: attached=%s mounted=%s suspended=%s remote_wakeup_enabled=%s",
           YESNO(this->attached_.load()), YESNO(tud_mounted()), YESNO(this->suspended_.load()),
           YESNO(this->remote_wakeup_enabled_.load()));
}

void UsbWake::wake(uint32_t duration_ms) {
  if (!tud_suspended()) {
    ESP_LOGW(TAG, "Wake: the bus is not suspended");
    return;
  }
  if (!this->remote_wakeup_enabled_.load()) {
    ESP_LOGW(TAG, "Wake: the Host did not enable remote wakeup");
    return;
  }
  // The DWC2 core drives RmtWkUpSig only once for each suspend (seen on the bench), so only the
  // first wake after a suspend counts. USB 2.0 7.1.7.7 allows 1 to 15 ms. Longer values are a
  // test of the AMD chipset port in S3 sleep. tud_remote_wakeup() holds the signal for
  // vTaskDelay(1), 0 to 1 ms at 1000 Hz, so drive the register here.
  uint32_t before = USB_DWC.dsts_reg.val;
  USB_DWC.gintsts_reg.val = 1u << 3;  // clear a stale SOF (bit 3, write 1 to clear)
  USB_DWC.gintmsk_reg.sofmsk = 1;     // TinyUSB sees the resume from the SOF
  USB_DWC.dctl_reg.rmtwkupsig = 1;
  esp_rom_delay_us(duration_ms * 500);
  uint32_t during = USB_DWC.dsts_reg.val;
  esp_rom_delay_us(duration_ms * 500);
  USB_DWC.dctl_reg.rmtwkupsig = 0;
  esp_rom_delay_us(100);
  uint32_t after = USB_DWC.dsts_reg.val;
  // DSTS bits 23:22 are the line state: 01 is J, 10 is K
  ESP_LOGI(TAG,
           "Remote wakeup sent (%" PRIu32 " ms). DSTS before 0x%08" PRIX32 " during 0x%08" PRIX32 " after 0x%08" PRIX32,
           duration_ms, before, during, after);
}

void UsbWake::sleep() {
  if (!tud_mounted() || this->suspended_.load()) {
    ESP_LOGW(TAG, "Sleep: the Host is not awake");
    return;
  }
  if (!tud_hid_ready()) {
    ESP_LOGW(TAG, "Sleep: the HID endpoint is busy");
    return;
  }
  // Press and release, so that the Host sees one key event
  uint8_t value = SYSTEM_SLEEP;
  tud_hid_report(REPORT_ID_SYSTEM, &value, sizeof(value));
  this->release_pending_ = true;
  ESP_LOGI(TAG, "System Sleep key sent");
}

void UsbWake::dump_config() {
  ESP_LOGCONFIG(TAG, "USB wake spike: CDC + HID keyboard, VID 0x303A PID 0x4002, remote wakeup in descriptor");
}

}  // namespace esphome::usb_wake

// TinyUSB HID callbacks. The keyboard interface makes Linux enable wakeup. Only the system control
// report is sent.
extern "C" {
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return esphome::usb_wake::HID_REPORT_DESC; }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {}
}
