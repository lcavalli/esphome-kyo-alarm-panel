#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/core/optional.h"
#include "esphome/core/time.h"
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

#include "kyo_protocol.h"

namespace esphome::kyo_alarm {

/// Fields of the panel status an entity can be bound to.
enum class StatusField : uint8_t {
  ONLINE,
  ZONE_ACTIVE,
  ZONE_TAMPER,
  ZONE_BYPASS,
  ZONE_ALARM_MEMORY,
  ZONE_TAMPER_MEMORY,
  PARTITION_ALARM,
  WARNINGS,
  TAMPERS,
};

/// Snapshot of the panel status, built from the real-time and status registers.
struct KyoAlarmStatus {
  bool online{false};
  bool realtime_valid{false};
  bool status_valid{false};

  /// Partitions configured in the panel (bit 0 is partition 1)
  uint8_t partitions{0};

  // Real-time register
  uint32_t zone_active{0};
  uint32_t zone_tamper{0};
  uint8_t partition_alarm{0};
  uint8_t warnings{0};
  uint8_t tampers{0};

  // Status register
  uint8_t armed_total{0};
  uint8_t armed_partial{0};
  uint8_t armed_partial_no_delay{0};
  uint8_t disarmed{0};
  uint32_t zone_bypass{0};
  uint32_t zone_alarm_memory{0};
  uint32_t zone_tamper_memory{0};

  /// Partitions armed in any mode.
  uint8_t armed() const {
    return (this->armed_total | this->armed_partial | this->armed_partial_no_delay) & this->partitions;
  }
  /// Bitmap of `field`, or nothing if it is unknown (not read yet, or communication lost).
  optional<uint32_t> get(StatusField field) const;
};

/// Partitions to arm or disarm with KyoAlarmComponent::control_partitions() (bit 0 is partition 1).
struct KyoPartitionControl {
  uint8_t arm_total{0};
  uint8_t arm_partial{0};
  /// Partial arming with entry delays set to 0
  uint8_t arm_partial_no_delay{0};
  uint8_t disarm{0};

  /// Partitions armed with any type.
  uint8_t arm() const { return this->arm_total | this->arm_partial | this->arm_partial_no_delay; }
};

/// Implemented by entities that register with KyoAlarmComponent to receive status updates.
class KyoAlarmListener {
 public:
  virtual void on_status_update(const KyoAlarmStatus &status) = 0;
  virtual void dump_config() {}
};

class KyoAlarmComponent : public uart::UARTDevice, public PollingComponent {
 public:
  using ResultCallback = std::function<void(bool success)>;

  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_model(const char *model, uint8_t max_zones) {
    this->model_ = model;
    this->max_zones_ = max_zones;
  }
  void register_listener(KyoAlarmListener *listener) { this->listeners_.push_back(listener); }
#ifdef USE_TEXT_SENSOR
  void set_panel_model_text_sensor(text_sensor::TextSensor *sensor) { this->panel_model_text_sensor_ = sensor; }
  void set_firmware_version_text_sensor(text_sensor::TextSensor *sensor) {
    this->firmware_version_text_sensor_ = sensor;
  }
  void set_partition_status_text_sensor(text_sensor::TextSensor *sensor) {
    this->partition_status_text_sensor_ = sensor;
  }
#endif
#ifdef USE_TIME
  void set_time(time::RealTimeClock *time) { this->time_ = time; }
  void set_clock_max_drift(uint32_t max_drift_s) { this->clock_max_drift_ = max_drift_s; }
#endif

  const KyoAlarmStatus &get_status() const { return this->status_; }
  uint8_t get_max_zones() const { return this->max_zones_; }

  // Commands. They are queued and executed asynchronously, `callback` (if any) is called from loop().

  /// Check `code` against the user PINs stored in the panel.
  void verify_code(const char *code, std::function<void(bool valid)> &&callback);
  /// Arm and disarm partitions in a single write, partitions not in `control` are left as they are.
  void control_partitions(const KyoPartitionControl &control, ResultCallback &&callback = nullptr);
  /// Bypass (exclude) or restore a zone, `zone` is 0-based.
  void set_zone_bypass(uint8_t zone, bool bypass, ResultCallback &&callback = nullptr);
  /// Reset the alarm and tamper memories of all partitions.
  void reset_memory(ResultCallback &&callback = nullptr);
  /// Set the panel clock.
  void set_datetime(const ESPTime &time, ResultCallback &&callback = nullptr);
#ifdef USE_TIME
  /// Set the panel clock from the time source now, regardless of `clock_max_drift`.
  void sync_clock(ResultCallback &&callback = nullptr);
#endif

  // Raw reads, for diagnostics

  /// Send `request`, a read request header in hex (checksum optional), e.g. "F0 00 00 0B 00".
  /// When `callback` is called, get_raw_reply() and get_raw_error() hold the outcome. Writes and the PIN table are
  /// refused, and only one raw read can be in progress.
  void read_raw(const std::string &request, ResultCallback &&callback);
  /// Reply to the last raw read in hex (data and checksum), empty if none was received.
  const std::string &get_raw_reply() const { return this->raw_reply_; }
  /// Why the last raw read failed, empty if it succeeded.
  const std::string &get_raw_error() const { return this->raw_error_; }

 protected:
  enum class Phase : uint8_t {
    READ_PANEL_INFO,
    READ_PARTITIONS,
    POLLING,
  };

  struct Request {
    static const size_t MAX_SIZE = KYO_HEADER_SIZE + KYO_REG_ZONE_BYPASS_SIZE + 1;
    std::array<uint8_t, MAX_SIZE> frame{};
    uint8_t frame_size{0};
    /// Bytes expected after the header echo (data and checksum), 0 for writes
    uint8_t reply_size{0};
    /// Writes always wait the full timeout before completing, reads complete as soon as the reply is in
    uint16_t timeout_ms{0};
    /// Do not log the reply (PINs)
    bool sensitive{false};
    std::function<void(bool success, const uint8_t *data)> callback;
  };

  static const size_t QUEUE_SIZE = 8;

  Request make_read_(uint16_t address, uint8_t size, std::function<void(bool, const uint8_t *)> &&callback);
  void enqueue_write_(uint16_t address, const uint8_t *data, uint8_t size, uint16_t timeout_ms,
                      ResultCallback &&callback);
  bool push_back_(Request &&request);
  bool push_front_(Request &&request);
  void start_request_();
  void complete_request_(bool success);

  void handle_panel_info_(const uint8_t *data);
  void handle_partitions_(const uint8_t *data);
  void handle_realtime_(const uint8_t *data);
  void handle_status_(const uint8_t *data);
#ifdef USE_TEXT_SENSOR
  void publish_partition_status_(const uint8_t *data);
#endif
  void set_online_(bool online);
#ifdef USE_TIME
  void on_time_sync_();
  void handle_datetime_(const uint8_t *data);
#endif
  void notify_listeners_();

  const char *model_{""};
  uint8_t max_zones_{KYO_MAX_ZONES};
  std::vector<KyoAlarmListener *> listeners_;
#ifdef USE_TEXT_SENSOR
  text_sensor::TextSensor *panel_model_text_sensor_{nullptr};
  text_sensor::TextSensor *firmware_version_text_sensor_{nullptr};
  text_sensor::TextSensor *partition_status_text_sensor_{nullptr};
#endif
#ifdef USE_TIME
  time::RealTimeClock *time_{nullptr};
  uint32_t clock_max_drift_{0};
#endif

  Phase phase_{Phase::READ_PANEL_INFO};
  KyoAlarmStatus status_{};
  uint8_t failures_{0};

  // Request queue (ring buffer) and the request in progress
  std::array<Request, QUEUE_SIZE> queue_{};
  size_t queue_head_{0};
  size_t queue_count_{0};
  Request active_{};
  bool busy_{false};
  uint32_t sent_at_{0};
  std::array<uint8_t, KYO_MAX_FRAME_SIZE> rx_{};
  size_t rx_size_{0};

  // PIN table, filled while verifying a code and wiped right after
  std::array<uint8_t, KYO_REG_PINS_1_SIZE + KYO_REG_PINS_2_SIZE> pins_{};

  // Outcome of the last raw read
  bool raw_pending_{false};
  std::string raw_reply_;
  std::string raw_error_;
};

}  // namespace esphome::kyo_alarm
