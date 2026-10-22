#pragma once

#include <array>
#include <atomic>
#include <initializer_list>
#include <span>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"

#include "ack_rules.h"
#include "cec_tx.h"

#ifdef USE_ESP32
#include "driver/gptimer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#endif
#ifdef USE_RP2040
#include <hardware/timer.h>
#endif

namespace esphome::hdmi_cec {

// Release/acquire store and load. The read-modify-write forms of std::atomic are not available on
// every target (the ESP8266 has none), so a writer computes the new value itself; that is safe here
// because every atomic in this component has exactly one writer.
template<typename T> ESPHOME_ALWAYS_INLINE inline void atomic_set(std::atomic<T> &atom, T value) {
  atom.store(value, std::memory_order_release);
}
template<typename T> ESPHOME_ALWAYS_INLINE inline T atomic_get(const std::atomic<T> &atom) {
  return atom.load(std::memory_order_acquire);
}

/// One CEC bus message: a header byte with the initiator and destination addresses, then up to
/// 15 payload bytes of which the first is the opcode. Fixed storage, so the receive interrupt
/// never allocates.
class Frame {
 public:
  static constexpr size_t MAX_LENGTH = 16;  // HDMI-CEC 1.4
  static constexpr size_t MAX_PAYLOAD_LENGTH = MAX_LENGTH - 1;

  Frame() = default;
  /// The payload is at most MAX_PAYLOAD_LENGTH bytes; send() checks that
  Frame(uint8_t initiator_addr, uint8_t destination_addr, std::span<const uint8_t> payload);

  ESPHOME_ALWAYS_INLINE uint8_t initiator_addr() const { return this->data_[0] >> 4; }
  ESPHOME_ALWAYS_INLINE uint8_t destination_addr() const { return this->data_[0] & 0xf; }
  ESPHOME_ALWAYS_INLINE bool is_broadcast() const { return this->destination_addr() == 0xf; }
  ESPHOME_ALWAYS_INLINE size_t size() const { return this->size_; }
  std::span<const uint8_t> bytes() const { return {this->data_.data(), this->size_}; }
  /// The bytes after the header, starting with the opcode
  std::span<const uint8_t> payload() const { return this->bytes().subspan(1); }
  ESPHOME_ALWAYS_INLINE void clear() {
    this->size_ = 0;
    this->flags_ = 0;
  }
  /// This device sent the frame (local change, plan step 9a)
  ESPHOME_ALWAYS_INLINE bool is_own() const { return this->flags_ & FLAG_OWN; }
  /// The on_message automations get the frame (local change, plan step 9a)
  ESPHOME_ALWAYS_INLINE bool for_automations() const { return this->flags_ & FLAG_FOR_AUTOMATIONS; }
  ESPHOME_ALWAYS_INLINE void set_flags(bool own, bool for_automations) {
    this->flags_ = (own ? FLAG_OWN : 0) | (for_automations ? FLAG_FOR_AUTOMATIONS : 0);
  }
  /// False when the frame is full already; the bus allows no more
  ESPHOME_ALWAYS_INLINE bool push_back(uint8_t data) {
    if (this->size_ == MAX_LENGTH) {
      return false;
    }
    this->data_[this->size_++] = data;
    return true;
  }

 protected:
  static constexpr uint8_t FLAG_OWN = 1 << 0;
  static constexpr uint8_t FLAG_FOR_AUTOMATIONS = 1 << 1;

  std::array<uint8_t, MAX_LENGTH> data_{};
  uint8_t size_{0};
  uint8_t flags_{0};
};

#ifdef USE_ESP32
/// One frame for the TX task (local change, plan step 9b)
struct TxRequest {
  Frame frame;
  /// Bus attempts before the result goes back. Pulse-Eight TX uses 1, because the kernel retries
  uint8_t max_attempts{1};
  /// Free bit periods on the line before the first attempt; 0 for the CEC standard time
  uint8_t signal_free_bits{0};
  TxDoneCallback done{nullptr};
  void *context{nullptr};
};
#endif

/// A queue of frames between one producer and one consumer, for instance the receive interrupt and
/// loop(). The indices run freely and each is written by one side only, so no lock is needed.
/// back() hands out a slot to fill and push_back() commits it; front() and pop_front() do the same
/// on the reading side.
template<size_t SIZE> class FrameRingBuffer {
  static_assert((SIZE & (SIZE - 1)) == 0, "SIZE must be a power of two");

 public:
  ESPHOME_ALWAYS_INLINE Frame *front() { return this->is_empty() ? nullptr : this->slot_(this->front_inx_); }
  ESPHOME_ALWAYS_INLINE void pop_front() { this->advance_(this->front_inx_); }
  ESPHOME_ALWAYS_INLINE Frame *back() { return this->count_() == SIZE ? nullptr : this->slot_(this->back_inx_); }
  ESPHOME_ALWAYS_INLINE void push_back() { this->advance_(this->back_inx_); }
  ESPHOME_ALWAYS_INLINE bool is_empty() const { return this->count_() == 0; }

 protected:
  ESPHOME_ALWAYS_INLINE uint8_t count_() const {
    return static_cast<uint8_t>(atomic_get(this->back_inx_) - atomic_get(this->front_inx_));
  }
  ESPHOME_ALWAYS_INLINE Frame *slot_(const std::atomic<uint8_t> &inx) { return &this->store_[atomic_get(inx) % SIZE]; }
  ESPHOME_ALWAYS_INLINE void advance_(std::atomic<uint8_t> &inx) {
    atomic_set<uint8_t>(inx, static_cast<uint8_t>(atomic_get(inx) + 1));
  }

  std::array<Frame, SIZE> store_{};
  std::atomic<uint8_t> front_inx_{0};
  std::atomic<uint8_t> back_inx_{0};
};

/// The HDMI-CEC bus on one GPIO pin. The receive interrupt decodes the edges of every frame,
/// including those this device sends, and queues complete frames for loop(); loop() runs the
/// on_message automations and transmits queued frames, one blocking bit-banged attempt per pass.
class HDMICEC : public Component {
 public:
  HDMICEC(InternalGPIOPin *pin, uint8_t address) : pin_(pin), address_(address) {}
  void setup() override;
  void dump_config() override;
  void loop() override;

  void set_promiscuous_mode(bool promiscuous) { this->promiscuous_mode_ = promiscuous; }
  void set_monitor_mode(bool monitor_mode) { this->monitor_mode_ = monitor_mode; }
  /// ACK every destination whose bit is set; bit 15 (broadcast) is ignored. Overrides the single
  /// address until clear_ack_mask() (local change, plan step 9a)
  void set_ack_mask(uint16_t mask) { atomic_set<uint32_t>(this->ack_mask_, mask); }
  void clear_ack_mask() { atomic_set<uint32_t>(this->ack_mask_, NO_ACK_MASK); }
  /// Called in loop() with every valid frame on the bus, header-only polls and our own frames too,
  /// and whether this device sent it (local change, plan step 9a). Frames queue only once a listener exists
  template<typename F> void add_frame_listener(F &&listener) {
    this->frame_listeners_.add(std::forward<F>(listener));
    atomic_set(this->deliver_all_, true);
  }
#ifdef HDMI_CEC_MESSAGE_CALLBACK_COUNT
  /// Called with the initiator, the destination and the payload (opcode first) of every received
  /// frame; the payload is only valid during the call
  template<typename F> void add_on_message_callback(F &&callback) {
    this->message_callbacks_.add(std::forward<F>(callback));
  }
#endif

  /// Queue a frame for transmission; false when the queue is full or in monitor mode
  bool send(uint8_t source, uint8_t destination, std::span<const uint8_t> payload);
#ifdef USE_ESP32
  /// Queue a TX request from any task; false when the queue is full or in monitor mode. The result
  /// goes to request.done in the TX task (local change, plan step 9b)
  bool submit(const TxRequest &request);
#endif
  bool send(uint8_t destination, std::span<const uint8_t> payload) {
    return this->send(this->address_, destination, payload);
  }
  /// For lambdas: id(cec).send(0x0, {0x44, 0x41});
  bool send(uint8_t destination, std::initializer_list<uint8_t> payload) {
    return this->send(destination, std::span<const uint8_t>(payload.begin(), payload.size()));
  }

 protected:
  enum class ReceiverState : uint8_t {
    RECEIVER_STATE_IDLE,
    RECEIVER_STATE_RECEIVING_BYTE,
    RECEIVER_STATE_WAITING_FOR_EOM,
    RECEIVER_STATE_WAITING_FOR_ACK,
  };

  // Receive side, in the interrupt
  static void gpio_isr_s(HDMICEC *self);
  void gpio_isr_();
  void reset_receiver_();
  /// Pull the line low for the acknowledge bit of a frame addressed to us
  void send_ack_();
  // A hardware one-shot timer releases the line after the acknowledge bit (gptimer on the ESP32,
  // timer 1 on the ESP8266, a hardware alarm on the RP2040), so the interrupt returns at once;
  // without one the interrupt holds the line for the 1.5 ms itself
  bool setup_ack_timer_();
  void start_ack_timer_();
#ifdef USE_ESP32
  static bool ack_timer_isr_s(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *arg);
#endif
#ifdef USE_ESP8266
  static void ack_timer_isr_s();
#endif
#ifdef USE_RP2040
  static void ack_timer_isr_s(uint alarm_num);
#endif

#ifdef USE_ESP32
  // Transmit side: a TX task starts each frame, and a gptimer alarm drives every edge and samples the
  // line at the sample point of each bit, so the timing does not depend on loop() (cec_tx.cpp)
  enum class TxPhase : uint8_t { START_RELEASE, START_SAMPLE, BIT_START, BIT_RELEASE, BIT_SAMPLE };
  bool setup_tx_();
  static void tx_task_s(void *arg);
  void tx_task_();
  /// Wait until the line was free for this many bit periods; false on a timeout
  bool wait_bus_free_(uint8_t signal_free_bits);
  uint8_t standard_signal_free_bits_() const;
  TxResult tx_attempt_(const Frame &frame);
  static bool tx_timer_isr_s(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *arg);
  bool tx_timer_isr_(gptimer_handle_t timer);
  bool tx_current_bit_() const;
  /// Go to the next bit; true when the frame is complete
  bool tx_advance_();
  bool tx_finish_(gptimer_handle_t timer, TxResult result);
#endif

  // Transmit side, in loop() (not on the ESP32)
  void transmit_message_();
  /// Keep the frame for another attempt, or drop it once the attempts are used up
  void finish_attempt_(bool sent_ok);
  /// Drive the frame onto the line; false when another initiator sent at the same time
  bool transmit_frame_(const Frame *frame);
  bool send_start_bit_();
  /// Send one bit; false when a logical 1 found the line held low by another initiator
  bool send_bit_(bool bit_value);
  ESPHOME_ALWAYS_INLINE void set_pin_input_high_() { this->isr_pin_.pin_mode(gpio::FLAG_INPUT | gpio::FLAG_PULLUP); }
  ESPHOME_ALWAYS_INLINE void set_pin_output_low_() {
    this->isr_pin_.pin_mode(gpio::FLAG_OUTPUT | gpio::FLAG_OPEN_DRAIN);
    this->isr_pin_.digital_write(false);
  }

  static constexpr size_t QUEUE_SIZE = 4;
  // Each queue is 74 bytes, so two single-byte fields follow each to fill the alignment gap
  FrameRingBuffer<QUEUE_SIZE> frames_queue_;  // received, interrupt to loop()
  uint8_t recv_bit_counter_{0};
  uint8_t recv_byte_{0};
  FrameRingBuffer<QUEUE_SIZE> send_queue_;  // to send, both ends on loop()
  uint8_t transmit_attempts_{0};
  uint8_t reported_dropped_frames_{0};
#ifdef HDMI_CEC_MESSAGE_CALLBACK_COUNT
  StaticCallbackManager<HDMI_CEC_MESSAGE_CALLBACK_COUNT, void(uint8_t, uint8_t, std::span<const uint8_t>)>
      message_callbacks_;
#endif
  CallbackManager<void(const Frame &, bool)> frame_listeners_;
  InternalGPIOPin *pin_;
  ISRInternalGPIOPin isr_pin_;
#ifdef USE_ESP32
  gptimer_handle_t ack_timer_{nullptr};
  gptimer_handle_t tx_timer_{nullptr};
  QueueHandle_t tx_queue_{nullptr};
  TaskHandle_t tx_task_handle_{nullptr};
  // TX state machine, written by the TX task before the timer starts, then by the timer interrupt
  const uint8_t *tx_bytes_{nullptr};
  uint32_t tx_bit_start_us_{0};  // timer count at the start of the current bit
  uint32_t last_tx_end_us_{0};   // TX task only
  Frame readback_;               // our own bytes as the receive interrupt read them
  uint8_t tx_size_{0};
  uint8_t tx_byte_inx_{0};
  uint8_t tx_bit_inx_{0};  // 0 to 7 data, 8 end of message, 9 acknowledge
  TxPhase tx_phase_{TxPhase::START_RELEASE};
  bool tx_broadcast_{false};
  bool last_tx_ok_{false};  // TX task only
  std::atomic<TxResult> tx_result_{TxResult::SUCCESS};
#endif
#ifdef USE_RP2040
  int ack_alarm_{-1};
#endif

  // The word-sized fields come first and the single-byte ones form one run, so nothing is padded
  std::atomic<uint32_t> last_edge_us_{0};        // the bus is busy until this is some time ago; interrupt writes
  std::atomic<uint32_t> ack_mask_{NO_ACK_MASK};  // the interrupt reads it; one word, so one atomic load
  Frame *recv_frame_{nullptr};                   // interrupt only; null when the queue was full or the frame too long
  uint32_t last_falling_edge_us_{0};             // interrupt only
  uint32_t last_attempt_us_{0};                  // loop() only; a refused start bit drives no edge, so it is timed here
  uint16_t required_idle_period_{0};             // loop() only

  // Shared with the interrupt. The tally of our own frame is written by the interrupt while
  // transmitting_ is set and no end of message has passed yet; loop() reads it once eom_received_
  // says the frame is over
  std::atomic<bool> eom_received_{false};  // set for every frame, ours or not
  std::atomic<uint8_t> dropped_frames_{0};
  std::atomic<bool> deliver_all_{false};  // a frame listener exists, so every valid frame is queued
  uint8_t n_bytes_received_{0};
  uint8_t n_acks_received_{0};
  std::atomic<bool> transmitting_{false};  // our frame is on the line; the receive interrupt reads it

  // Receive state, interrupt only
  uint8_t recv_header_{0};  // kept outside the frame so a dropped frame can still be judged
  uint8_t recv_length_{0};
  ReceiverState receiver_state_{ReceiverState::RECEIVER_STATE_IDLE};
  bool recv_eom_{false};
  bool recv_ack_queued_{false};
  bool prev_pin_level_{true};

  uint8_t address_;  // logical address, 0xf when only listening
  bool promiscuous_mode_{false};
  bool monitor_mode_{false};
};

/// hdmi_cec.send. Local change for ESPHome 2026.9.1, which has no register_apply_action (see UPSTREAM.md)
template<typename... Ts> class SendAction : public Action<Ts...>, public Parented<HDMICEC> {
 public:
  TEMPLATABLE_VALUE(uint8_t, destination)
  TEMPLATABLE_VALUE(std::vector<uint8_t>, data)

  void play(const Ts &...x) override {
    const std::vector<uint8_t> data = this->data_.value(x...);
    this->parent_->send(this->destination_.value(x...), std::span<const uint8_t>(data.data(), data.size()));
  }
};

}  // namespace esphome::hdmi_cec
