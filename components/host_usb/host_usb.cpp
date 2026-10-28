// SPDX-License-Identifier: GPL-3.0-only
#include "host_usb.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cinttypes>

#include "esp_rom_sys.h"
#include "soc/usb_dwc_struct.h"

#include "class/hid/hid_device.h"
#include "tinyusb.h"
#include "tinyusb_cdc_acm.h"
#include "tinyusb_default_config.h"

namespace esphome::host_usb {

static const char *const TAG = "host_usb";
static constexpr size_t RX_BUFFER_SIZE = 512;
static constexpr uint32_t SUSPEND_WAIT_US = 10000;
static constexpr uint32_t RECONNECT_MS = 10;
static portMUX_TYPE dwc_lock = portMUX_INITIALIZER_UNLOCKED;

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

static const uint8_t CONFIG_DESC[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_COUNT, 0, CONFIG_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_CDC_DESCRIPTOR(ITF_CDC, 4, EP_CDC_NOTIF, 8, EP_CDC_OUT, EP_CDC_IN, 64),
    TUD_HID_DESCRIPTOR(ITF_HID, 5, HID_ITF_PROTOCOL_KEYBOARD, sizeof(HID_REPORT_DESC), EP_HID_IN, 8, 10),
};

// A composite device with an IAD needs the MISC class (see tinyusb.h). PID 0x4002 is an Espressif
// test PID. The PCB needs its own PID before release.
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
    .bcdDevice = 0x0200,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

// The serial number gives the Host a fixed /dev/serial/by-id/ path
static const char *STRING_DESC[] = {
    "\x09\x04",           // 0: English
    "Plum Solutions",     // 1: manufacturer
    "Plum-CEC",           // 2: product
    "PLUMCEC0001",        // 3: serial number
    "Plum-CEC serial",    // 4: CDC interface
    "Plum-CEC keyboard",  // 5: HID interface
};

static void on_event(tinyusb_event_t *event, void *arg) {
  auto *self = static_cast<HostUsb *>(arg);
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

static HostUsb *rx_owner = nullptr;

static void cdc_rx_callback(int itf, cdcacm_event_t *event) {
  if (rx_owner != nullptr)
    rx_owner->on_cdc_rx();
}

void HostUsb::setup() {
  this->rx_buffer_ = xStreamBufferCreate(RX_BUFFER_SIZE, 1);
  if (this->rx_buffer_ == nullptr) {
    ESP_LOGE(TAG, "RX buffer create failed");
    this->mark_failed();
    return;
  }
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
  rx_owner = this;
  tinyusb_config_cdcacm_t acm = {};
  acm.cdc_port = TINYUSB_CDC_ACM_0;
  acm.callback_rx = cdc_rx_callback;
  err = tinyusb_cdcacm_init(&acm);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_cdcacm_init failed: %s", esp_err_to_name(err));
    this->mark_failed();
    return;
  }
  this->installed_ = true;
}

void HostUsb::on_cdc_rx() {
  uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE];
  size_t len = 0;
  while (tinyusb_cdcacm_read(TINYUSB_CDC_ACM_0, buf, sizeof(buf), &len) == ESP_OK && len > 0) {
    // Never wait in the TinyUSB task: drop on overflow
    size_t sent = xStreamBufferSend(this->rx_buffer_, buf, len, 0);
    if (sent < len)
      this->rx_dropped_.fetch_add(len - sent, std::memory_order_relaxed);
  }
}

size_t HostUsb::read(uint8_t *buf, size_t len, TickType_t timeout) {
  if (this->rx_buffer_ == nullptr) {
    vTaskDelay(timeout);
    return 0;
  }
  return xStreamBufferReceive(this->rx_buffer_, buf, len, timeout);
}

size_t HostUsb::write(const uint8_t *data, size_t len) {
  if (!this->installed_ || !tud_ready())
    return 0;
  size_t queued = tinyusb_cdcacm_write_queue(TINYUSB_CDC_ACM_0, data, len);
  tinyusb_cdcacm_write_flush(TINYUSB_CDC_ACM_0, 0);
  return queued;
}

void HostUsb::on_suspend(bool remote_wakeup_enabled) {
  this->remote_wakeup_enabled_ = remote_wakeup_enabled;
  this->suspended_ = true;
  this->events_++;
}

void HostUsb::on_resume() {
  this->suspended_ = false;
  this->events_++;
}

void HostUsb::on_attach(bool attached) {
  this->attached_ = attached;
  this->events_++;
}

void HostUsb::loop() {
  if (this->release_pending_ && tud_hid_ready()) {
    uint8_t value = 0;
    tud_hid_report(REPORT_ID_SYSTEM, &value, sizeof(value));
    this->release_pending_ = false;
  }
  int events = this->events_.load();
  if (events == this->logged_events_)
    return;
  this->logged_events_ = events;
  if (!this->suspended_.load())
    this->wakes_in_suspend_ = 0;
  ESP_LOGI(TAG, "USB state: attached=%s mounted=%s suspended=%s remote_wakeup_enabled=%s",
           YESNO(this->attached_.load()), YESNO(tud_mounted()), YESNO(this->suspended_.load()),
           YESNO(this->remote_wakeup_enabled_.load()));
}

// DSTS bit 0 is SuspSts. Bits 23:22 are the line state: 01 is J, 10 is K.
static inline bool core_suspended(uint32_t dsts) { return (dsts & 1) != 0; }
static inline unsigned line_state(uint32_t dsts) { return (dsts >> 22) & 3; }
static const char *line_name(unsigned state) {
  switch (state) {
    case 1:
      return "J";
    case 2:
      return "K";
    case 0:
      return "SE0";
    default:
      return "SE1";
  }
}

void HostUsb::wake(uint32_t duration_ms) {
  if (!tud_suspended()) {
    ESP_LOGW(TAG, "Wake: the bus is not suspended");
    return;
  }
  if (!this->remote_wakeup_enabled_.load()) {
    ESP_LOGW(TAG, "Wake: the Host did not enable remote wakeup");
    return;
  }
  this->wakes_in_suspend_++;
  // The DWC2 core drives RmtWkUpSig only while DSTS.SuspSts is set. After a wake that the Host
  // did not answer, the core can leave suspend. The bus is then idle, so the core sets SuspSts
  // again after 3 ms. Wait for it, so that a second wake in one suspend drives the line again.
  uint32_t before = USB_DWC.dsts_reg.val;
  uint32_t waited_us = 0;
  while (!core_suspended(before) && waited_us < SUSPEND_WAIT_US) {
    esp_rom_delay_us(100);
    waited_us += 100;
    before = USB_DWC.dsts_reg.val;
  }
  if (!core_suspended(before))
    ESP_LOGW(TAG,
             "Wake %" PRIu32 ": the USB core is not in suspend (DSTS 0x%08" PRIX32 "). Use Wake Host by reconnect.",
             this->wakes_in_suspend_, before);
  // tud_remote_wakeup() holds the signal for vTaskDelay(1), 0 to 1 ms at 1000 Hz, so drive the
  // register here. USB 2.0 7.1.7.7 allows 1 to 15 ms.
  // The TinyUSB interrupt also writes GINTMSK and DCTL. The TinyUSB task installs it on CPU1
  // (TINYUSB_DEFAULT_TASK_AFFINITY), the core of loop(), so a short critical section makes each
  // read-modify-write safe.
  portENTER_CRITICAL(&dwc_lock);
  USB_DWC.gintsts_reg.val = 1u << 3;  // clear a stale SOF (bit 3, write 1 to clear)
  USB_DWC.gintmsk_reg.sofmsk = 1;     // TinyUSB sees the resume from the SOF
  USB_DWC.dctl_reg.rmtwkupsig = 1;
  portEXIT_CRITICAL(&dwc_lock);
  esp_rom_delay_us(duration_ms * 500);
  uint32_t during = USB_DWC.dsts_reg.val;
  esp_rom_delay_us(duration_ms * 500);
  portENTER_CRITICAL(&dwc_lock);
  USB_DWC.dctl_reg.rmtwkupsig = 0;
  portEXIT_CRITICAL(&dwc_lock);
  esp_rom_delay_us(100);
  uint32_t after = USB_DWC.dsts_reg.val;
  ESP_LOGI(TAG,
           "Wake %" PRIu32 " in this suspend (%" PRIu32 " ms, waited %" PRIu32 " us): line %s before, %s during, %s "
           "after. DSTS 0x%08" PRIX32 " 0x%08" PRIX32 " 0x%08" PRIX32,
           this->wakes_in_suspend_, duration_ms, waited_us, line_name(line_state(before)),
           line_name(line_state(during)), line_name(line_state(after)), before, during, after);
}

void HostUsb::reconnect() {
  if (!this->installed_) {
    ESP_LOGW(TAG, "Reconnect: USB is not installed");
    return;
  }
  ESP_LOGI(TAG, "Reconnect: disconnect for %" PRIu32 " ms", RECONNECT_MS);
  tud_disconnect();
  delay(RECONNECT_MS);
  tud_connect();
}

void HostUsb::sleep() {
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

void HostUsb::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Host USB:\n"
                "  TinyUSB: %s\n"
                "  CDC + HID keyboard, VID 0x303A PID 0x4002, remote wakeup in descriptor\n"
                "  RX dropped: %" PRIu32 " B",
                this->installed_ ? "installed" : "not installed", this->rx_dropped_.load());
}

}  // namespace esphome::host_usb

// TinyUSB HID callbacks. The keyboard interface makes Linux enable wakeup. Only the system control
// report is sent.
extern "C" {
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return esphome::host_usb::HID_REPORT_DESC; }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {}
}
