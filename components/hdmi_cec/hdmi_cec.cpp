#include "hdmi_cec.h"

#include <algorithm>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_ESP8266
#include <Arduino.h>
#endif
#ifdef USE_RP2040
#include <pico/time.h>
#endif

namespace esphome::hdmi_cec {

static const char *const TAG = "hdmi_cec";

#if defined(USE_ESP8266) || defined(USE_RP2040)
// The timer interrupt of these platforms carries no argument
static HDMICEC *ack_timer_owner = nullptr;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
#endif

// Bit timing from the HDMI 1.4 CEC specification, in microseconds
static constexpr uint32_t START_BIT_MIN_US = 3500;  // a low period at least this long is a start bit
static constexpr uint32_t START_BIT_NOM_US = 3700;
static constexpr uint32_t START_BIT_HIGH_US = 800;
static constexpr uint32_t HIGH_BIT_MIN_US = 400;  // shortest low period of a logical 1
static constexpr uint32_t HIGH_BIT_US = 600;      // nominal low period of a logical 1
static constexpr uint32_t LOW_BIT_US = 1500;      // nominal low period of a logical 0
static constexpr uint32_t SAFE_SAMPLE_US = 1050;  // the line is sampled here; a longer low period is a logical 0
static constexpr uint32_t TOTAL_BIT_US = 2400;
static constexpr uint32_t SIGNAL_FREE_TIME_AFTER_RECEIVE = TOTAL_BIT_US * 5;  // also for a new initiator
static constexpr uint32_t SIGNAL_FREE_TIME_AFTER_XMIT_FAIL = TOTAL_BIT_US * 3;
static constexpr uint32_t SIGNAL_FREE_TIME_AFTER_XMIT_SUCCESS = TOTAL_BIT_US * 7;
static constexpr uint32_t ABORTED_FRAME_US = TOTAL_BIT_US * 15;  // no edge for this long: our frame was cut short
static constexpr uint8_t MAX_ATTEMPTS = 8;  // CEC allows 5, but a busy bus counts as an attempt here too

Frame::Frame(uint8_t initiator_addr, uint8_t destination_addr, std::span<const uint8_t> payload) {
  this->data_[0] = ((initiator_addr & 0xf) << 4) | (destination_addr & 0xf);
  this->size_ = 1 + payload.size();
  std::copy(payload.begin(), payload.end(), this->data_.begin() + 1);
}

void HDMICEC::setup() {
  // The line is open drain with a weak pull-up: driven low to send, released to read
  this->pin_->setup();
  this->isr_pin_ = this->pin_->to_isr();
  this->set_pin_input_high_();
  this->required_idle_period_ = SIGNAL_FREE_TIME_AFTER_RECEIVE;
  if (!this->setup_ack_timer_()) {
    ESP_LOGE(TAG, "Failed to set up the acknowledge timer");
    this->mark_failed();
    return;
  }
#ifdef USE_ESP32
  if (!this->setup_tx_()) {
    ESP_LOGE(TAG, "Failed to set up the TX timer and task");
    this->mark_failed();
    return;
  }
#endif
  this->pin_->attach_interrupt(HDMICEC::gpio_isr_s, this, gpio::INTERRUPT_ANY_EDGE);
  // loop() only has work once the interrupt queues a frame or send() queues one
  this->disable_loop();
}

void HDMICEC::dump_config() {
  ESP_LOGCONFIG(TAG,
                "HDMI-CEC:\n"
                "  Address: 0x%X\n"
                "  Monitor mode: %s\n"
                "  Promiscuous mode: %s",
                this->address_, YESNO(this->monitor_mode_), YESNO(this->promiscuous_mode_));
  LOG_PIN("  Pin: ", this->pin_);
}

void HDMICEC::loop() {
  const uint8_t dropped = atomic_get(this->dropped_frames_);
  if (dropped != this->reported_dropped_frames_) {
    const uint8_t count = dropped - this->reported_dropped_frames_;
    ESP_LOGW(TAG, "%u received frame%s dropped (queue full or oversized)", count,
             count == 1 ? LOG_STR_LITERAL("") : LOG_STR_LITERAL("s"));
    this->reported_dropped_frames_ = dropped;
  }
  // One inbound frame per pass keeps the time spent in the automations bounded
  if (const Frame *frame = this->frames_queue_.front()) {
#ifdef ESPHOME_LOG_HAS_DEBUG
    char line[format_hex_pretty_size(Frame::MAX_LENGTH)];
    ESP_LOGD(TAG, "Received: %s%s", format_hex_pretty_to(line, frame->bytes().data(), frame->size()),
             frame->is_own() ? LOG_STR_LITERAL(" (own)") : LOG_STR_LITERAL(""));
#endif
#ifdef HDMI_CEC_MESSAGE_CALLBACK_COUNT
    if (frame->for_automations()) {
      this->message_callbacks_.call(frame->initiator_addr(), frame->destination_addr(), frame->payload());
    }
#endif
    this->frame_listeners_.call(*frame, frame->is_own());
    this->frames_queue_.pop_front();
  }
#ifndef USE_ESP32
  // A frame being sent stays at the front of the queue until its attempts are over
  if (!this->send_queue_.is_empty()) {
    this->transmit_message_();
  }
#endif
  if (this->frames_queue_.is_empty() && this->send_queue_.is_empty()) {
    this->disable_loop();
  }
}

bool HDMICEC::send(uint8_t source, uint8_t destination, std::span<const uint8_t> payload) {
  if (this->monitor_mode_) {
    ESP_LOGW(TAG, "Monitor mode, not sending 0x%X->0x%X", source, destination);
    return false;
  }
  if (source > 0xf || destination > 0xf) {
    ESP_LOGE(TAG, "Send abort: logical address 0x%X->0x%X out of range", source, destination);
    return false;
  }
  if (payload.size() > Frame::MAX_PAYLOAD_LENGTH) {
    ESP_LOGE(TAG, "Send abort 0x%X->0x%X: payload of %zu bytes exceeds %zu", source, destination, payload.size(),
             Frame::MAX_PAYLOAD_LENGTH);
    return false;
  }
#ifdef USE_ESP32
  TxRequest request;
  request.frame = Frame(source, destination, payload);
  request.max_attempts = MAX_ATTEMPTS;
  return this->submit(request);
#else
  Frame *frame = this->send_queue_.back();
  if (frame == nullptr) {
    ESP_LOGW(TAG, "Send queue full, frame 0x%X->0x%X dropped", source, destination);
    return false;
  }
  *frame = Frame(source, destination, payload);
  this->send_queue_.push_back();
  this->enable_loop();
  return true;
#endif
}

void HDMICEC::transmit_message_() {
  const uint32_t now = micros();
  const uint32_t since_last_edge = now - atomic_get(this->last_edge_us_);
  if (atomic_get(this->transmitting_)) {
    // The interrupt sees our own frame; wait for its end of message bit, or give up when the
    // bus went quiet without one
    if (!atomic_get(this->eom_received_) && since_last_edge < ABORTED_FRAME_US) {
      return;
    }
    atomic_set(this->transmitting_, false);
    atomic_set(this->eom_received_, false);  // the end of our own frame is not a foreign one
    const Frame *frame = this->send_queue_.front();
    // A broadcast is acknowledged by nobody; a directed frame on every byte
    const bool bytes_ok = this->n_bytes_received_ == frame->size();
    const bool acks_ok = this->n_acks_received_ == (frame->is_broadcast() ? 0 : frame->size());
    if (bytes_ok && acks_ok) {
      ESP_LOGD(TAG, "Send frame success in %u attempt%s", this->transmit_attempts_,
               this->transmit_attempts_ > 1 ? LOG_STR_LITERAL("s") : LOG_STR_LITERAL(""));
    } else if (!bytes_ok) {
      ESP_LOGD(TAG, "Send frame incorrect on attempt %u, saw %u bytes but expected %zu", this->transmit_attempts_,
               this->n_bytes_received_, frame->size());
    } else {
      ESP_LOGD(TAG, "Send frame not acknowledged on attempt %u", this->transmit_attempts_);
    }
    this->finish_attempt_(bytes_ok && acks_ok);
  } else if (atomic_get(this->eom_received_)) {
    // Another device finished a frame; the standard asks a new initiator to wait after it
    this->required_idle_period_ = SIGNAL_FREE_TIME_AFTER_RECEIVE;
    atomic_set(this->eom_received_, false);
  }

  const Frame *frame = this->send_queue_.front();
  // An edge within the signal free time means the bus is in use or not yet free for us; a start
  // bit refused by a held line drives no edge, so that attempt is timed on its own
  if (frame == nullptr || since_last_edge < this->required_idle_period_ ||
      now - this->last_attempt_us_ < this->required_idle_period_) {
    return;
  }

  this->last_attempt_us_ = now;
  this->transmit_attempts_++;
#ifdef ESPHOME_LOG_HAS_DEBUG
  if (this->transmit_attempts_ == 1) {
    char line[format_hex_pretty_size(Frame::MAX_LENGTH)];
    ESP_LOGD(TAG, "Sending: %s", format_hex_pretty_to(line, frame->bytes().data(), frame->size()));
  }
#endif
  // The interrupt counts the bytes and acknowledge bits of our own frame
  this->n_bytes_received_ = 0;
  this->n_acks_received_ = 0;
  atomic_set(this->eom_received_, false);
  atomic_set(this->transmitting_, true);
  if (!this->transmit_frame_(frame)) {
    // The line was held, or another initiator sent at the same time
    atomic_set(this->transmitting_, false);
    this->finish_attempt_(false);
  }
  // Otherwise the acknowledge result arrives through the interrupt
}

void HDMICEC::finish_attempt_(bool sent_ok) {
  this->required_idle_period_ = sent_ok ? SIGNAL_FREE_TIME_AFTER_XMIT_SUCCESS : SIGNAL_FREE_TIME_AFTER_XMIT_FAIL;
  if (!sent_ok) {
    if (this->transmit_attempts_ < MAX_ATTEMPTS) {
      return;  // the signal free time has to pass before the retry
    }
    ESP_LOGW(TAG, "Frame not sent after %u attempts, dropped", this->transmit_attempts_);
  }
  this->transmit_attempts_ = 0;
  this->send_queue_.pop_front();
}

bool HDMICEC::transmit_frame_(const Frame *frame) {
  if (!this->send_start_bit_()) {
    return false;
  }
  const auto bytes = frame->bytes();
  for (size_t byte_inx = 0; byte_inx < bytes.size(); byte_inx++) {
    const uint8_t current_byte = bytes[byte_inx];
    // Most significant bit first. The four initiator address bits watch for a collision with
    // another initiator that started at the same moment; a lower address wins by holding the line
    for (int8_t i = 7; i >= 0; i--) {
      const bool bit_value = (current_byte >> i) & 0x1;
      if (!this->send_bit_(bit_value) && byte_inx == 0 && i >= 4) {
        ESP_LOGD(TAG, "Bus collision on sending initiator address 0x%X on attempt %u", frame->initiator_addr(),
                 this->transmit_attempts_);
        return false;
      }
    }
    // End of message bit, then the acknowledge bit which the receiver may pull low
    this->send_bit_(byte_inx == bytes.size() - 1);
    this->send_bit_(true);
  }
  return true;
}

bool HDMICEC::send_start_bit_() {
  if (!this->isr_pin_.digital_read()) {
    ESP_LOGD(TAG, "Bus occupied before start bit on attempt %u", this->transmit_attempts_);
    return false;
  }
  this->set_pin_output_low_();
  delay_microseconds_safe(START_BIT_NOM_US);
  // Release the line and check that nobody else keeps it low
  this->set_pin_input_high_();
  delay_microseconds_safe(START_BIT_HIGH_US / 2);
  bool value = this->isr_pin_.digital_read();
  delay_microseconds_safe(START_BIT_HIGH_US / 2);
  value &= this->isr_pin_.digital_read();
  if (!value) {
    ESP_LOGD(TAG, "Bus collision on sending start bit on attempt %u", this->transmit_attempts_);
  }
  return value;
}

bool HDMICEC::send_bit_(bool bit_value) {
  const uint32_t start_us = micros();
  this->set_pin_output_low_();
  delay_microseconds_safe(bit_value ? HIGH_BIT_US : LOW_BIT_US);
  this->set_pin_input_high_();
  bool clear = true;
  if (bit_value) {
    // Sample in the middle of the safe sample period: a line still low means another initiator
    // is sending a logical 0 at the same time
    uint32_t elapsed = micros() - start_us;
    if (elapsed < SAFE_SAMPLE_US) {
      delay_microseconds_safe(SAFE_SAMPLE_US - elapsed);
    }
    clear = this->isr_pin_.digital_read();
  }
  const uint32_t elapsed = micros() - start_us;
  if (elapsed < TOTAL_BIT_US) {
    delay_microseconds_safe(TOTAL_BIT_US - elapsed);
  }
  return clear;
}

void IRAM_ATTR HDMICEC::send_ack_() {
  // The acknowledge has to start right at this falling edge; the timer releases the line
  this->set_pin_output_low_();
  this->start_ack_timer_();
}

#if defined(USE_ESP32)
bool HDMICEC::setup_ack_timer_() {
  gptimer_config_t config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000,  // one tick per microsecond
  };
  if (gptimer_new_timer(&config, &this->ack_timer_) != ESP_OK) {
    return false;
  }
  gptimer_event_callbacks_t callbacks = {.on_alarm = HDMICEC::ack_timer_isr_s};
  // The alarm reloads the count to zero itself, so the interrupt only has to start the timer
  gptimer_alarm_config_t alarm = {
      .alarm_count = LOW_BIT_US, .reload_count = 0, .flags = {.auto_reload_on_alarm = true}};
  return gptimer_register_event_callbacks(this->ack_timer_, &callbacks, this) == ESP_OK &&
         gptimer_set_alarm_action(this->ack_timer_, &alarm) == ESP_OK && gptimer_enable(this->ack_timer_) == ESP_OK;
}

void IRAM_ATTR HDMICEC::start_ack_timer_() { gptimer_start(this->ack_timer_); }

bool IRAM_ATTR HDMICEC::ack_timer_isr_s(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *arg) {
  auto *self = static_cast<HDMICEC *>(arg);
  self->set_pin_input_high_();
  gptimer_stop(timer);
  return false;  // no task to wake
}

#elif defined(USE_ESP8266)
static constexpr uint32_t TIMER1_TICKS_PER_US = 5;  // TIM_DIV16 of the 80 MHz clock

bool HDMICEC::setup_ack_timer_() {
  ack_timer_owner = this;
  timer1_isr_init();
  timer1_attachInterrupt(HDMICEC::ack_timer_isr_s);
  timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);  // a write arms one shot
  return true;
}

void IRAM_ATTR HDMICEC::start_ack_timer_() { timer1_write(LOW_BIT_US * TIMER1_TICKS_PER_US); }

void IRAM_ATTR HDMICEC::ack_timer_isr_s() { ack_timer_owner->set_pin_input_high_(); }

#elif defined(USE_RP2040)
bool HDMICEC::setup_ack_timer_() {
  this->ack_alarm_ = hardware_alarm_claim_unused(false);
  if (this->ack_alarm_ < 0) {
    return false;  // every hardware alarm is taken
  }
  ack_timer_owner = this;
  hardware_alarm_set_callback(this->ack_alarm_, HDMICEC::ack_timer_isr_s);
  return true;
}

void IRAM_ATTR HDMICEC::start_ack_timer_() {
  hardware_alarm_set_target(this->ack_alarm_, make_timeout_time_us(LOW_BIT_US));
}

void IRAM_ATTR HDMICEC::ack_timer_isr_s(uint alarm_num) { ack_timer_owner->set_pin_input_high_(); }

#else
bool HDMICEC::setup_ack_timer_() { return true; }

// Without a one-shot timer the interrupt holds the line itself
void IRAM_ATTR HDMICEC::start_ack_timer_() {
  delay_microseconds_safe(LOW_BIT_US);
  this->set_pin_input_high_();
}
#endif

void IRAM_ATTR HDMICEC::gpio_isr_s(HDMICEC *self) { self->gpio_isr_(); }

void IRAM_ATTR HDMICEC::gpio_isr_() {
  const uint32_t now = micros();
  const bool level = this->isr_pin_.digital_read();
  if (level == this->prev_pin_level_) {
    return;  // a mode change of the pin, not a bus edge
  }
  this->prev_pin_level_ = level;
  atomic_set(this->last_edge_us_, now);

  if (!level) {
    // Falling edge: the start of a low period
    if (now - this->last_falling_edge_us_ > START_BIT_NOM_US + TOTAL_BIT_US) {
      // The bus was quiet; whatever was gathered belongs to an aborted frame
      this->reset_receiver_();
    }
    this->last_falling_edge_us_ = now;
    if (this->recv_ack_queued_) {
      // Acknowledge by stretching this low period
      this->send_ack_();
      this->recv_ack_queued_ = false;
    }
    return;
  }

  // Rising edge: the low period tells the bit value
  const uint32_t pulse_duration = now - this->last_falling_edge_us_;
  if (pulse_duration > START_BIT_MIN_US) {
    this->reset_receiver_();
    this->receiver_state_ = ReceiverState::RECEIVER_STATE_RECEIVING_BYTE;
    // Without a free slot the frame is neither acknowledged nor delivered, so its initiator retries
    this->recv_frame_ = this->frames_queue_.back();
    if (this->recv_frame_ != nullptr) {
      this->recv_frame_->clear();
    }
#ifdef USE_ESP32
    this->readback_.clear();
#endif
    return;
  }
  if (pulse_duration < HIGH_BIT_MIN_US / 4) {
    return;  // glitch
  }
  const bool value = pulse_duration <= SAFE_SAMPLE_US;
  // Our own frame is on the bus until its end of message; send() may give it any initiator address
  const bool own_frame = atomic_get(this->transmitting_) && !atomic_get(this->eom_received_);

  switch (this->receiver_state_) {
    case ReceiverState::RECEIVER_STATE_IDLE:
      break;

    case ReceiverState::RECEIVER_STATE_RECEIVING_BYTE:
      this->recv_byte_ = (this->recv_byte_ << 1) | value;
      if (++this->recv_bit_counter_ == 8) {
        if (this->recv_length_++ == 0) {
          this->recv_header_ = this->recv_byte_;
        }
#ifdef USE_ESP32
        if (own_frame) {
          this->readback_.push_back(this->recv_byte_);
        }
#endif
        if (this->recv_frame_ != nullptr && !this->recv_frame_->push_back(this->recv_byte_)) {
          this->recv_frame_ = nullptr;  // longer than the bus allows: corrupt, do not deliver it
        }
        this->recv_bit_counter_ = 0;
        this->receiver_state_ = ReceiverState::RECEIVER_STATE_WAITING_FOR_EOM;
      }
      break;

    case ReceiverState::RECEIVER_STATE_WAITING_FOR_EOM:
      this->recv_eom_ = value;
      // A frame for us is acknowledged on the next bit, unless there is no room for it (so its
      // initiator retries) or it is our own. wants_ack() decides "for us": the ACK mask, or the
      // single address. A polling message for us (header only) needs no room, and goes
      // unacknowledged only when nobody holds the address
      if (!this->monitor_mode_ && !own_frame && (this->recv_frame_ != nullptr || (value && this->recv_length_ == 1)) &&
          wants_ack(this->recv_header_, atomic_get(this->ack_mask_), this->address_)) {
        this->recv_ack_queued_ = true;
      }
      this->receiver_state_ = ReceiverState::RECEIVER_STATE_WAITING_FOR_ACK;
      break;

    case ReceiverState::RECEIVER_STATE_WAITING_FOR_ACK:
      if (own_frame) {
        // A low acknowledge bit means the addressed device took the byte
        // (for a broadcast it means some device refused it)
        this->n_bytes_received_++;
        if (!value) {
          this->n_acks_received_++;
        }
      }
      if (!this->recv_eom_) {
        this->receiver_state_ = ReceiverState::RECEIVER_STATE_RECEIVING_BYTE;
        break;
      }
      atomic_set(this->eom_received_, true);  // publishes the tally above to loop()
      // The automations get the frames for_automations() allows. A frame listener gets every
      // valid frame, polls and our own frames too
      const bool automations =
          for_automations(this->recv_header_, this->recv_length_, own_frame, this->address_, this->promiscuous_mode_);
      if (!automations && !atomic_get(this->deliver_all_)) {
        // nothing to deliver
      } else if (this->recv_frame_ == nullptr) {
        atomic_set<uint8_t>(this->dropped_frames_, atomic_get(this->dropped_frames_) + 1);
      } else {
        this->recv_frame_->set_flags(own_frame, automations);
        this->frames_queue_.push_back();
        this->enable_loop_soon_any_context();
      }
      this->reset_receiver_();
      break;
  }
}

#ifdef USE_ESP32
// The TX task: high priority, so that a frame starts soon after the line is free. The gptimer
// interrupt does the bit timing, so the task priority does not change the timing
static constexpr UBaseType_t TX_TASK_PRIORITY = 15;
static constexpr uint32_t TX_TASK_STACK = 4096;
static constexpr UBaseType_t TX_QUEUE_LENGTH = 4;
static constexpr uint32_t BUS_FREE_TIMEOUT_MS = 2000;
// The longest frame: a start bit and 16 bytes of 10 bits is 389 ms
static constexpr uint32_t FRAME_TIMEOUT_MS = 600;
static constexpr uint8_t SIGNAL_FREE_BITS_NEW_INITIATOR = 5;
static constexpr uint8_t SIGNAL_FREE_BITS_NEXT_FRAME = 7;
static constexpr uint8_t SIGNAL_FREE_BITS_RETRY = 3;
static constexpr uint8_t ACK_BIT_INX = 9;
static constexpr uint8_t INITIATOR_BITS = 4;

bool HDMICEC::setup_tx_() {
  gptimer_config_t config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000,  // one tick per microsecond
  };
  if (gptimer_new_timer(&config, &this->tx_timer_) != ESP_OK) {
    return false;
  }
  gptimer_event_callbacks_t callbacks = {.on_alarm = HDMICEC::tx_timer_isr_s};
  if (gptimer_register_event_callbacks(this->tx_timer_, &callbacks, this) != ESP_OK ||
      gptimer_enable(this->tx_timer_) != ESP_OK) {
    return false;
  }
  this->tx_queue_ = xQueueCreate(TX_QUEUE_LENGTH, sizeof(TxRequest));
  return this->tx_queue_ != nullptr && xTaskCreate(HDMICEC::tx_task_s, "cec_tx", TX_TASK_STACK, this, TX_TASK_PRIORITY,
                                                   &this->tx_task_handle_) == pdPASS;
}

bool HDMICEC::submit(const TxRequest &request) {
  if (this->monitor_mode_) {
    ESP_LOGW(TAG, "Monitor mode, not sending 0x%X->0x%X", request.frame.initiator_addr(),
             request.frame.destination_addr());
    return false;
  }
  if (this->tx_queue_ == nullptr || xQueueSend(this->tx_queue_, &request, 0) != pdTRUE) {
    ESP_LOGW(TAG, "Send queue full, frame 0x%X->0x%X dropped", request.frame.initiator_addr(),
             request.frame.destination_addr());
    return false;
  }
  return true;
}

void HDMICEC::tx_task_s(void *arg) { static_cast<HDMICEC *>(arg)->tx_task_(); }

void HDMICEC::tx_task_() {
  TxRequest request;
  while (true) {
    if (xQueueReceive(this->tx_queue_, &request, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    uint8_t free_bits = request.signal_free_bits != 0 ? request.signal_free_bits : this->standard_signal_free_bits_();
    TxResult result = TxResult::TIMEOUT;
    uint8_t attempts = 0;
    while (attempts < request.max_attempts) {
      if (!this->wait_bus_free_(free_bits)) {
        result = TxResult::TIMEOUT;
        break;
      }
      attempts++;
      result = this->tx_attempt_(request.frame);
      if (result == TxResult::SUCCESS) {
        break;
      }
      // After a lost arbitration the line carried another initiator's frame
      free_bits = result == TxResult::ARBITRATION_LOST ? SIGNAL_FREE_BITS_NEW_INITIATOR : SIGNAL_FREE_BITS_RETRY;
    }
    this->last_tx_end_us_ = micros();
    this->last_tx_ok_ = result == TxResult::SUCCESS;
    char line[format_hex_pretty_size(Frame::MAX_LENGTH)];
    if (result == TxResult::SUCCESS) {
      ESP_LOGD(TAG, "Sent %s after %u attempt%s",
               format_hex_pretty_to(line, request.frame.bytes().data(), request.frame.size()), attempts,
               attempts == 1 ? LOG_STR_LITERAL("") : LOG_STR_LITERAL("s"));
    } else {
      ESP_LOGW(TAG, "Not sent %s: %s after %u attempt%s",
               format_hex_pretty_to(line, request.frame.bytes().data(), request.frame.size()), tx_result_str(result),
               attempts, attempts == 1 ? LOG_STR_LITERAL("") : LOG_STR_LITERAL("s"));
    }
    if (request.done != nullptr) {
      request.done(result, request.context);
    }
  }
}

uint8_t HDMICEC::standard_signal_free_bits_() const {
  // An initiator that sends again right after its own successful frame waits longer, so that
  // another initiator gets the line; a new initiator waits 5 bit periods
  const bool no_frame_since_ours = static_cast<int32_t>(atomic_get(this->last_edge_us_) - this->last_tx_end_us_) <= 0;
  return this->last_tx_ok_ && no_frame_since_ours ? SIGNAL_FREE_BITS_NEXT_FRAME : SIGNAL_FREE_BITS_NEW_INITIATOR;
}

bool HDMICEC::wait_bus_free_(uint8_t signal_free_bits) {
  const uint32_t needed_us = signal_free_bits * TOTAL_BIT_US;
  const uint32_t start_ms = millis();
  while (true) {
    const uint32_t since_edge_us = micros() - atomic_get(this->last_edge_us_);
    // A line held low has no edges, so the level counts too
    if (since_edge_us >= needed_us && this->isr_pin_.digital_read()) {
      return true;
    }
    if (millis() - start_ms > BUS_FREE_TIMEOUT_MS) {
      return false;
    }
    const uint32_t wait_ms = since_edge_us < needed_us ? (needed_us - since_edge_us) / 1000 : 0;
    vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(wait_ms)));
  }
}

TxResult HDMICEC::tx_attempt_(const Frame &frame) {
  this->tx_bytes_ = frame.bytes().data();
  this->tx_size_ = frame.size();
  this->tx_byte_inx_ = 0;
  this->tx_bit_inx_ = 0;
  this->tx_broadcast_ = frame.is_broadcast();
  this->tx_phase_ = TxPhase::START_RELEASE;
  this->readback_.clear();
  atomic_set(this->eom_received_, false);
  ulTaskNotifyTake(pdTRUE, 0);  // drop a stale notification

  gptimer_set_raw_count(this->tx_timer_, 0);
  gptimer_alarm_config_t alarm = {.alarm_count = START_BIT_NOM_US};
  gptimer_set_alarm_action(this->tx_timer_, &alarm);
  // The start bit begins with the timer; nothing may run between the two
  static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&lock);
  if (!this->isr_pin_.digital_read()) {
    portEXIT_CRITICAL(&lock);
    return TxResult::ARBITRATION_LOST;  // another initiator started first
  }
  atomic_set(this->transmitting_, true);
  this->set_pin_output_low_();
  gptimer_start(this->tx_timer_);
  portEXIT_CRITICAL(&lock);

  TxResult result;
  if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(FRAME_TIMEOUT_MS)) == 0) {
    gptimer_stop(this->tx_timer_);
    this->set_pin_input_high_();
    atomic_set(this->transmitting_, false);
    ESP_LOGW(TAG, "TX timer did not finish the frame");
    return TxResult::LINE_ERROR;
  }
  result = atomic_get(this->tx_result_);
  if (atomic_get(this->transmitting_)) {
    // The frame ended at the sample point of an acknowledge bit. A follower that ACKs holds the bit
    // low up to 1.7 ms, so the receive interrupt sees the last edge of our frame after this. Clear
    // the flag only after that edge, or the receiver takes our frame for a foreign one
    const uint32_t wait_start_us = micros();
    while (!atomic_get(this->eom_received_) && micros() - wait_start_us < TOTAL_BIT_US) {
      vTaskDelay(1);
    }
    atomic_set(this->transmitting_, false);
  }
  if (result == TxResult::SUCCESS) {
    const auto sent = frame.bytes();
    const auto read = this->readback_.bytes();
    if (!std::equal(sent.begin(), sent.end(), read.begin(), read.end())) {
      char sent_line[format_hex_pretty_size(Frame::MAX_LENGTH)];
      char read_line[format_hex_pretty_size(Frame::MAX_LENGTH)];
      ESP_LOGW(TAG, "Read back %s, sent %s", format_hex_pretty_to(read_line, read.data(), read.size()),
               format_hex_pretty_to(sent_line, sent.data(), sent.size()));
      result = TxResult::LINE_ERROR;
    }
  }
  return result;
}

bool IRAM_ATTR HDMICEC::tx_timer_isr_s(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *arg) {
  return static_cast<HDMICEC *>(arg)->tx_timer_isr_(timer);
}

bool IRAM_ATTR HDMICEC::tx_current_bit_() const {
  if (this->tx_bit_inx_ < 8) {
    return (this->tx_bytes_[this->tx_byte_inx_] >> (7 - this->tx_bit_inx_)) & 1;
  }
  if (this->tx_bit_inx_ == 8) {
    return this->tx_byte_inx_ == this->tx_size_ - 1;  // end of message
  }
  return true;  // the initiator sends the acknowledge bit as a 1
}

bool IRAM_ATTR HDMICEC::tx_advance_() {
  if (++this->tx_bit_inx_ > ACK_BIT_INX) {
    this->tx_bit_inx_ = 0;
    if (++this->tx_byte_inx_ == this->tx_size_) {
      return true;
    }
  }
  this->tx_bit_start_us_ += TOTAL_BIT_US;
  this->tx_phase_ = TxPhase::BIT_START;
  return false;
}

bool IRAM_ATTR HDMICEC::tx_finish_(gptimer_handle_t timer, TxResult result) {
  gptimer_stop(timer);
  this->set_pin_input_high_();
  if (result == TxResult::ARBITRATION_LOST || result == TxResult::LINE_ERROR) {
    // From here the receive interrupt treats the line as foreign, also the rest of a frame that won
    // the arbitration. After SUCCESS and NACK the TX task clears the flag after the last edge
    atomic_set(this->transmitting_, false);
  }
  atomic_set(this->tx_result_, result);
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(this->tx_task_handle_, &woken);
  return woken == pdTRUE;
}

bool IRAM_ATTR HDMICEC::tx_timer_isr_(gptimer_handle_t timer) {
  // Every time is a count of the timer, which started at the falling edge of the start bit
  uint32_t next_us;
  switch (this->tx_phase_) {
    case TxPhase::START_RELEASE:
      this->set_pin_input_high_();
      next_us = START_BIT_NOM_US + START_BIT_HIGH_US / 2;
      this->tx_phase_ = TxPhase::START_SAMPLE;
      break;
    case TxPhase::START_SAMPLE:
      if (!this->isr_pin_.digital_read()) {
        return this->tx_finish_(timer, TxResult::ARBITRATION_LOST);  // another start bit holds the line
      }
      this->tx_bit_start_us_ = START_BIT_NOM_US + START_BIT_HIGH_US;
      next_us = this->tx_bit_start_us_;
      this->tx_phase_ = TxPhase::BIT_START;
      break;
    case TxPhase::BIT_START:
      this->set_pin_output_low_();
      next_us = this->tx_bit_start_us_ + (this->tx_current_bit_() ? HIGH_BIT_US : LOW_BIT_US);
      this->tx_phase_ = TxPhase::BIT_RELEASE;
      break;
    case TxPhase::BIT_RELEASE:
      this->set_pin_input_high_();
      if (this->tx_current_bit_()) {
        // A 1 is released before the sample point, so the line can be checked there
        next_us = this->tx_bit_start_us_ + SAFE_SAMPLE_US;
        this->tx_phase_ = TxPhase::BIT_SAMPLE;
        break;
      }
      // A 0 is released after the sample point. Nothing can win against it, and the acknowledge
      // bit is always a 1, so the next bit follows
      if (this->tx_advance_()) {
        return this->tx_finish_(timer, TxResult::SUCCESS);  // not reached: a frame ends on a 1
      }
      next_us = this->tx_bit_start_us_;
      break;
    case TxPhase::BIT_SAMPLE:
    default: {
      const bool level = this->isr_pin_.digital_read();
      if (this->tx_bit_inx_ == ACK_BIT_INX) {
        // A directed frame needs the follower to pull the bit low. On a broadcast a low bit means a
        // follower rejected the frame, which is not a failure of the send
        if (!this->tx_broadcast_ && level) {
          return this->tx_finish_(timer, TxResult::NACK);
        }
      } else if (this->tx_current_bit_() && !level) {
        // Another initiator sends a 0 where we send a 1. In the initiator address that is a lost
        // arbitration; the lower address wins. Anywhere else it is an error on the line
        const bool arbitration = this->tx_byte_inx_ == 0 && this->tx_bit_inx_ < INITIATOR_BITS;
        return this->tx_finish_(timer, arbitration ? TxResult::ARBITRATION_LOST : TxResult::LINE_ERROR);
      }
      if (this->tx_advance_()) {
        return this->tx_finish_(timer, TxResult::SUCCESS);
      }
      next_us = this->tx_bit_start_us_;
      break;
    }
  }
  // An interrupt that runs late (a flash write, a WiFi interrupt) puts the next edge in the past.
  // The bit is then out of the timing window, so stop the frame with the line released
  uint64_t now_us = 0;
  gptimer_get_raw_count(timer, &now_us);
  if (now_us >= next_us) {
    return this->tx_finish_(timer, TxResult::LINE_ERROR);
  }
  gptimer_alarm_config_t alarm = {.alarm_count = next_us};
  gptimer_set_alarm_action(timer, &alarm);
  return false;
}
#endif  // USE_ESP32

void IRAM_ATTR HDMICEC::reset_receiver_() {
  this->receiver_state_ = ReceiverState::RECEIVER_STATE_IDLE;
  this->recv_bit_counter_ = 0;
  this->recv_length_ = 0;
  this->recv_ack_queued_ = false;
  this->recv_frame_ = nullptr;
}

}  // namespace esphome::hdmi_cec
