#include "kyo_alarm.h"

#include <cinttypes>
#include <cstdlib>
#include <cstring>

#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm";

static const uint16_t READ_TIMEOUT_MS = 500;
static const uint16_t CLOSE_TIMEOUT_MS = 100;
static const uint16_t CTRL_PARTITIONS_TIMEOUT_MS = 1000;
static const uint16_t ZONE_BYPASS_TIMEOUT_MS = 200;
static const uint16_t DATETIME_TIMEOUT_MS = 200;
static const uint16_t RESET_TIMEOUT_MS = 500;
/// Consecutive failed requests before the panel is reported offline
static const uint8_t MAX_FAILURES = 3;

optional<uint32_t> KyoAlarmStatus::get(StatusField field) const {
  switch (field) {
    case StatusField::ONLINE:
      return this->online ? 1 : 0;
    case StatusField::ZONE_ACTIVE:
      return this->realtime_valid ? make_optional(this->zone_active) : nullopt;
    case StatusField::ZONE_TAMPER:
      return this->realtime_valid ? make_optional(this->zone_tamper) : nullopt;
    case StatusField::PARTITION_ALARM:
      return this->realtime_valid ? make_optional<uint32_t>(this->partition_alarm) : nullopt;
    case StatusField::WARNINGS:
      return this->realtime_valid ? make_optional<uint32_t>(this->warnings) : nullopt;
    case StatusField::TAMPERS:
      return this->realtime_valid ? make_optional<uint32_t>(this->tampers) : nullopt;
    case StatusField::ZONE_BYPASS:
      return this->status_valid ? make_optional(this->zone_bypass) : nullopt;
    case StatusField::ZONE_ALARM_MEMORY:
      return this->status_valid ? make_optional(this->zone_alarm_memory) : nullopt;
    case StatusField::ZONE_TAMPER_MEMORY:
      return this->status_valid ? make_optional(this->zone_tamper_memory) : nullopt;
  }
  return nullopt;
}

void KyoAlarmComponent::setup() {
#ifdef USE_TIME
  if (this->time_ != nullptr) {
    this->time_->add_on_time_sync_callback([this]() { this->on_time_sync_(); });
  }
#endif
  this->disable_loop();
}

void KyoAlarmComponent::dump_config() {
  ESP_LOGCONFIG(TAG,
                "KYO Alarm:\n"
                "  Model: %s\n"
                "  Zones: %u",
                this->model_, this->max_zones_);
  LOG_UPDATE_INTERVAL(this);
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Panel model", this->panel_model_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Firmware version", this->firmware_version_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Partition status", this->partition_status_text_sensor_);
#endif
#ifdef USE_TIME
  if (this->time_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Panel clock synchronized with time source when off by more than %" PRIu32 "s",
                  this->clock_max_drift_);
#endif
  for (auto *listener : this->listeners_)
    listener->dump_config();
}

void KyoAlarmComponent::update() {
  // Status polls are low priority: skip this cycle if there are commands in flight
  if (this->busy_ || this->queue_count_ > 0)
    return;

  switch (this->phase_) {
    case Phase::READ_PANEL_INFO:
      this->push_back_(
          this->make_read_(KYO_REG_PANEL_INFO, KYO_REG_PANEL_INFO_SIZE, [this](bool success, const uint8_t *data) {
            if (success)
              this->handle_panel_info_(data);
          }));
      break;
    case Phase::READ_PARTITIONS:
      this->push_back_(
          this->make_read_(KYO_REG_PARTITIONS, KYO_REG_PARTITIONS_SIZE, [this](bool success, const uint8_t *data) {
            if (success)
              this->handle_partitions_(data);
          }));
      break;
    case Phase::POLLING:
      this->push_back_(
          this->make_read_(KYO_REG_REALTIME, KYO_REG_REALTIME_SIZE, [this](bool success, const uint8_t *data) {
            if (success)
              this->handle_realtime_(data);
          }));
      this->push_back_(this->make_read_(KYO_REG_STATUS, KYO_REG_STATUS_SIZE, [this](bool success, const uint8_t *data) {
        if (success)
          this->handle_status_(data);
      }));
      break;
  }
}

void KyoAlarmComponent::loop() {
  if (!this->busy_) {
    if (this->queue_count_ > 0) {
      this->start_request_();
    } else {
      this->disable_loop();
    }
    return;
  }

  uint8_t byte;
  while (this->available() > 0 && this->read_byte(&byte)) {
    if (this->rx_size_ < this->rx_.size())
      this->rx_[this->rx_size_++] = byte;
  }

  bool is_read = this->active_.reply_size > 0;
  if (is_read && this->rx_size_ >= KYO_HEADER_SIZE + this->active_.reply_size) {
    this->complete_request_(true);
  } else if (millis() - this->sent_at_ >= this->active_.timeout_ms) {
    // Writes are acknowledged by the header echo only
    this->complete_request_(!is_read && this->rx_size_ >= KYO_HEADER_SIZE);
  }
}

void KyoAlarmComponent::verify_code(const char *code, std::function<void(bool valid)> &&callback) {
  uint32_t pin;
  // An all-zero PIN would match empty slots of the PIN table
  if (code == nullptr || !kyo_encode_pin(code, &pin) || pin == 0) {
    ESP_LOGW(TAG, "Invalid code format, 4 to 6 digits expected");
    callback(false);
    return;
  }

  auto read_first =
      this->make_read_(KYO_REG_PINS_1, KYO_REG_PINS_1_SIZE,
                       [this, pin, callback = std::move(callback)](bool success, const uint8_t *data) mutable {
                         if (!success) {
                           callback(false);
                           return;
                         }
                         std::memcpy(this->pins_.data(), data, KYO_REG_PINS_1_SIZE);

                         auto read_second = this->make_read_(
                             KYO_REG_PINS_2, KYO_REG_PINS_2_SIZE,
                             [this, pin, callback = std::move(callback)](bool success, const uint8_t *data) {
                               bool valid = false;
                               if (success) {
                                 std::memcpy(this->pins_.data() + KYO_REG_PINS_1_SIZE, data, KYO_REG_PINS_2_SIZE);
                                 for (size_t i = 0; i < KYO_MAX_PINS * 3; i += 3) {
                                   uint32_t stored = (uint32_t(this->pins_[i]) << 16) |
                                                     (uint32_t(this->pins_[i + 1]) << 8) | uint32_t(this->pins_[i + 2]);
                                   valid |= stored == pin;
                                 }
                                 if (!valid)
                                   ESP_LOGW(TAG, "Code not recognized by the panel");
                               }
                               this->pins_.fill(0);
                               callback(valid);
                             });
                         read_second.sensitive = true;
                         // Run right after this read, before any other queued request
                         this->push_front_(std::move(read_second));
                       });
  read_first.sensitive = true;
  this->push_back_(std::move(read_first));
}

void KyoAlarmComponent::control_partitions(const KyoPartitionControl &control, ResultCallback &&callback) {
  uint8_t partitions = this->status_.partitions;
  uint8_t data[KYO_REG_CTRL_PARTITIONS_SIZE] = {};
  data[KYO_CTRL_ARM_TOTAL] = control.arm_total & partitions;
  data[KYO_CTRL_ARM_PARTIAL] = control.arm_partial & partitions;
  data[KYO_CTRL_ARM_PARTIAL_NO_DELAY] = control.arm_partial_no_delay & partitions;
  // A partition both armed and disarmed is only armed: the panel was never tested with both bits set in one frame.
  // If a partition ever has to be disarmed and armed again (e.g. to change its arm type), use two writes.
  data[KYO_CTRL_DISARM] = control.disarm & ~control.arm() & partitions;
  ESP_LOGD(TAG, "Arming partitions total 0x%02X, partial 0x%02X, partial no delay 0x%02X, disarming 0x%02X",
           data[KYO_CTRL_ARM_TOTAL], data[KYO_CTRL_ARM_PARTIAL], data[KYO_CTRL_ARM_PARTIAL_NO_DELAY],
           data[KYO_CTRL_DISARM]);
  this->enqueue_write_(KYO_REG_CTRL_PARTITIONS, data, sizeof(data), CTRL_PARTITIONS_TIMEOUT_MS, std::move(callback));
}

void KyoAlarmComponent::set_zone_bypass(uint8_t zone, bool bypass, ResultCallback &&callback) {
  if (zone >= this->max_zones_) {
    ESP_LOGE(TAG, "Invalid zone %u", zone + 1);
  } else if (!this->status_.status_valid) {
    ESP_LOGW(TAG, "Panel status unknown, zone %u not changed", zone + 1);
  } else if (this->status_.armed() != 0) {
    ESP_LOGW(TAG, "Zones can only be bypassed while the panel is disarmed");
  } else {
    ESP_LOGD(TAG, "%s zone %u", bypass ? "Bypassing" : "Restoring", zone + 1);
    uint8_t data[KYO_REG_ZONE_BYPASS_SIZE] = {};
    kyo_encode_zones(1UL << zone, bypass ? &data[0] : &data[4]);
    this->enqueue_write_(KYO_REG_ZONE_BYPASS, data, sizeof(data), ZONE_BYPASS_TIMEOUT_MS, std::move(callback));
    return;
  }
  if (callback)
    callback(false);
}

void KyoAlarmComponent::reset_memory(ResultCallback &&callback) {
  ESP_LOGD(TAG, "Resetting alarm memory");
  uint8_t data[KYO_REG_RESET_SIZE] = {this->status_.partitions, 0x00};
  this->enqueue_write_(KYO_REG_RESET, data, sizeof(data), RESET_TIMEOUT_MS, std::move(callback));
}

void KyoAlarmComponent::set_datetime(const ESPTime &time, ResultCallback &&callback) {
  if (!time.is_valid() || time.year < 2000 || time.year > 2099) {
    ESP_LOGW(TAG, "Invalid time, panel clock not set");
    if (callback)
      callback(false);
    return;
  }
  ESP_LOGD(TAG, "Setting panel clock");
  uint8_t data[KYO_REG_DATETIME_SIZE] = {
      time.day_of_month, time.month, uint8_t(time.year - 2000), time.hour, time.minute, time.second,
  };
  this->enqueue_write_(KYO_REG_DATETIME, data, sizeof(data), DATETIME_TIMEOUT_MS, std::move(callback));
}

#ifdef USE_TIME
void KyoAlarmComponent::sync_clock(ResultCallback &&callback) {
  if (this->time_ == nullptr) {
    ESP_LOGW(TAG, "No time source, panel clock not set");
    if (callback)
      callback(false);
    return;
  }
  this->set_datetime(this->time_->now(), std::move(callback));
}

void KyoAlarmComponent::on_time_sync_() {
  // The time source syncs every few minutes: read the panel clock and write it only when it drifts, to limit
  // writes to the panel. A DST change is a drift of one hour
  this->push_back_(this->make_read_(KYO_REG_DATETIME, KYO_REG_DATETIME_SIZE, [this](bool success, const uint8_t *data) {
    if (success)
      this->handle_datetime_(data);
  }));
}

void KyoAlarmComponent::handle_datetime_(const uint8_t *data) {
  ESPTime now = this->time_->now();
  if (!now.is_valid())
    return;
  // Both clocks are local time: compare them as if they were UTC, so that the time zone does not matter
  ESPTime panel = now;
  panel.day_of_month = data[0];
  panel.month = data[1];
  panel.year = 2000 + data[2];
  panel.hour = data[3];
  panel.minute = data[4];
  panel.second = data[5];
  if (!panel.is_valid(false, false)) {
    ESP_LOGW(TAG, "Invalid panel clock %02u/%02u/%02u %02u:%02u:%02u, setting it", data[0], data[1], data[2], data[3],
             data[4], data[5]);
    this->sync_clock();
    return;
  }
  panel.recalc_timestamp_utc(false);
  now.recalc_timestamp_utc(false);
  int64_t drift = int64_t(panel.timestamp) - int64_t(now.timestamp);
  if (std::abs(drift) <= int64_t(this->clock_max_drift_)) {
    ESP_LOGD(TAG, "Panel clock off by %" PRId64 " s", drift);
    return;
  }
  ESP_LOGI(TAG, "Panel clock off by %" PRId64 " s, setting it", drift);
  this->sync_clock();
}
#endif

void KyoAlarmComponent::read_raw(const std::string &request, ResultCallback &&callback) {
  uint8_t header[KYO_HEADER_SIZE];
  size_t size = kyo_parse_hex(request.c_str(), header, sizeof(header));
  const char *error = nullptr;
  if (this->raw_pending_) {
    error = "Another raw read is in progress";
  } else if (size < KYO_HEADER_SIZE - 1) {
    error = "Expected a read request header in hex, e.g. F0 00 00 0B 00";
  } else if (header[0] != KYO_OP_READ) {
    error = "Only read requests (F0) are allowed";
  } else if (header[3] >= KYO_MAX_READ_SIZE || header[4] != 0x00) {
    error = "Invalid size, reads are at most 64 bytes (3F 00)";
  } else if (size == KYO_HEADER_SIZE && !kyo_verify_checksum(header, size)) {
    error = "Header checksum mismatch";
  } else {
    uint32_t start = header[1] | uint32_t(header[2]) << 8;
    uint32_t end = start + header[3] + 1;
    if (start < KYO_REG_INSTALLER_PIN + KYO_REG_INSTALLER_PIN_SIZE && end > KYO_REG_PINS_1)
      error = "The PIN table cannot be read";
  }
  if (error != nullptr) {
    ESP_LOGW(TAG, "Raw read refused: %s", error);
    this->raw_reply_.clear();
    this->raw_error_ = error;
    if (callback)
      callback(false);
    return;
  }

  uint16_t address = header[1] | header[2] << 8;
  uint8_t data_size = header[3] + 1;
  ESP_LOGD(TAG, "Raw read of %u bytes at 0x%04X", data_size, address);
  this->raw_pending_ = true;
  this->push_back_(this->make_read_(
      address, data_size, [this, data_size, callback = std::move(callback)](bool success, const uint8_t *data) {
        this->raw_pending_ = false;
        this->raw_reply_.clear();
        this->raw_error_.clear();
        // Whatever arrived after the header echo, even if incomplete
        size_t received = data != nullptr && this->rx_size_ > KYO_HEADER_SIZE ? this->rx_size_ - KYO_HEADER_SIZE : 0;
        if (received > size_t(data_size) + 1)
          received = data_size + 1;
        if (received > 0) {
          char hex[format_hex_pretty_size(KYO_MAX_READ_SIZE + 1)];
          this->raw_reply_ = format_hex_pretty_to(hex, data, received, ' ');
        }
        if (data == nullptr) {
          this->raw_error_ = "Request queue full";
        } else if (received == 0) {
          this->raw_error_ = "No reply from the panel";
        } else if (received < size_t(data_size) + 1) {
          this->raw_error_ = "Incomplete reply from the panel";
        } else if (!success) {
          this->raw_error_ = "Reply checksum mismatch";
        }
        if (this->raw_error_.empty()) {
          ESP_LOGD(TAG, "Raw read reply: %s", this->raw_reply_.c_str());
        } else {
          ESP_LOGW(TAG, "Raw read failed: %s %s", this->raw_error_.c_str(), this->raw_reply_.c_str());
        }
        if (callback)
          callback(this->raw_error_.empty());
      }));
}

KyoAlarmComponent::Request KyoAlarmComponent::make_read_(uint16_t address, uint8_t size,
                                                         std::function<void(bool, const uint8_t *)> &&callback) {
  Request request;
  kyo_encode_header(KYO_OP_READ, address, size, request.frame.data());
  request.frame_size = KYO_HEADER_SIZE;
  request.reply_size = size + 1;
  request.timeout_ms = READ_TIMEOUT_MS;
  request.callback = std::move(callback);
  return request;
}

void KyoAlarmComponent::enqueue_write_(uint16_t address, const uint8_t *data, uint8_t size, uint16_t timeout_ms,
                                       ResultCallback &&callback) {
  Request request;
  kyo_encode_header(KYO_OP_WRITE, address, size, request.frame.data());
  std::memcpy(request.frame.data() + KYO_HEADER_SIZE, data, size);
  request.frame[KYO_HEADER_SIZE + size] = kyo_checksum(data, size);
  request.frame_size = KYO_HEADER_SIZE + size + 1;
  request.timeout_ms = timeout_ms;
  // A successful write must be followed by a close request
  request.callback = [this, callback = std::move(callback)](bool success, const uint8_t *) {
    if (!success) {
      ESP_LOGW(TAG, "Write request failed");
      if (callback)
        callback(false);
      return;
    }
    Request close;
    kyo_encode_close(close.frame.data());
    close.frame_size = KYO_HEADER_SIZE;
    close.timeout_ms = CLOSE_TIMEOUT_MS;
    close.callback = [callback](bool success, const uint8_t *) {
      if (!success)
        ESP_LOGW(TAG, "Close request failed");
      if (callback)
        callback(true);
    };
    this->push_front_(std::move(close));
  };
  this->push_back_(std::move(request));
}

bool KyoAlarmComponent::push_back_(Request &&request) {
  if (this->queue_count_ == QUEUE_SIZE) {
    ESP_LOGW(TAG, "Request queue full, request dropped");
    if (request.callback)
      request.callback(false, nullptr);
    return false;
  }
  this->queue_[(this->queue_head_ + this->queue_count_) % QUEUE_SIZE] = std::move(request);
  this->queue_count_++;
  this->enable_loop();
  return true;
}

bool KyoAlarmComponent::push_front_(Request &&request) {
  if (this->queue_count_ == QUEUE_SIZE) {
    ESP_LOGW(TAG, "Request queue full, request dropped");
    if (request.callback)
      request.callback(false, nullptr);
    return false;
  }
  this->queue_head_ = (this->queue_head_ + QUEUE_SIZE - 1) % QUEUE_SIZE;
  this->queue_[this->queue_head_] = std::move(request);
  this->queue_count_++;
  this->enable_loop();
  return true;
}

void KyoAlarmComponent::start_request_() {
  this->active_ = std::move(this->queue_[this->queue_head_]);
  this->queue_[this->queue_head_].callback = nullptr;
  this->queue_head_ = (this->queue_head_ + 1) % QUEUE_SIZE;
  this->queue_count_--;

  // Drop any stale data before sending
  uint8_t byte;
  while (this->available() > 0 && this->read_byte(&byte)) {
  }

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char hex[format_hex_pretty_size(Request::MAX_SIZE)];
  ESP_LOGV(TAG, "TX: %s", format_hex_pretty_to(hex, this->active_.frame.data(), this->active_.frame_size));
#endif
  this->write_array(this->active_.frame.data(), this->active_.frame_size);
  this->rx_size_ = 0;
  this->sent_at_ = millis();
  this->busy_ = true;
}

void KyoAlarmComponent::complete_request_(bool success) {
  this->busy_ = false;
  const uint8_t *reply = this->rx_.data() + KYO_HEADER_SIZE;
  uint8_t reply_size = this->active_.reply_size;

  if (success && reply_size > 0) {
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
    if (!this->active_.sensitive) {
      char hex[format_hex_pretty_size(KYO_MAX_FRAME_SIZE)];
      ESP_LOGV(TAG, "RX: %s", format_hex_pretty_to(hex, reply, reply_size));
    }
#endif
    if (!kyo_verify_checksum(reply, reply_size)) {
      ESP_LOGW(TAG, "Reply checksum mismatch");
      success = false;
    }
  } else if (!success) {
    ESP_LOGD(TAG, "No valid reply from the panel (%u bytes received)", unsigned(this->rx_size_));
  }

  if (success) {
    this->failures_ = 0;
    this->set_online_(true);
  } else if (this->failures_ < MAX_FAILURES && ++this->failures_ == MAX_FAILURES) {
    this->set_online_(false);
  }

  auto callback = std::move(this->active_.callback);
  this->active_.callback = nullptr;
  if (callback)
    callback(success, reply);

  if (this->active_.sensitive)
    this->rx_.fill(0);
}

void KyoAlarmComponent::handle_panel_info_(const uint8_t *data) {
  // 7 characters of model, a space, 4 characters of firmware version
  char model[8];
  char firmware[5];
  std::memcpy(model, data, 7);
  std::memcpy(firmware, data + 8, 4);
  model[7] = '\0';
  firmware[4] = '\0';
  for (int i = 6; i >= 0 && model[i] == ' '; i--)
    model[i] = '\0';
  for (int i = 3; i >= 0 && firmware[i] == ' '; i--)
    firmware[i] = '\0';

  ESP_LOGI(TAG, "Panel model %s, firmware %s", model, firmware);
  if (std::strcmp(model, this->model_) != 0)
    ESP_LOGW(TAG, "Panel model %s differs from the configured model %s", model, this->model_);
#ifdef USE_TEXT_SENSOR
  if (this->panel_model_text_sensor_ != nullptr)
    this->panel_model_text_sensor_->publish_state(model);
  if (this->firmware_version_text_sensor_ != nullptr)
    this->firmware_version_text_sensor_->publish_state(firmware);
#endif

  this->phase_ = Phase::READ_PARTITIONS;
  this->update();
}

void KyoAlarmComponent::handle_partitions_(const uint8_t *data) {
  this->status_.partitions = data[1];
  ESP_LOGI(TAG, "Panel partitions 0x%02X", this->status_.partitions);
  this->phase_ = Phase::POLLING;
  this->update();
}

void KyoAlarmComponent::handle_realtime_(const uint8_t *data) {
  this->status_.zone_active = kyo_decode_zones(data + KYO_RT_ZONE_ACTIVE);
  this->status_.zone_tamper = kyo_decode_zones(data + KYO_RT_ZONE_TAMPER);
  this->status_.warnings = data[KYO_RT_WARNINGS];
  this->status_.partition_alarm = data[KYO_RT_PARTITION_ALARM];
  this->status_.tampers = data[KYO_RT_TAMPERS];
  this->status_.realtime_valid = true;
  this->notify_listeners_();
}

void KyoAlarmComponent::handle_status_(const uint8_t *data) {
  this->status_.armed_total = data[KYO_ST_ARMED_TOTAL];
  this->status_.armed_partial = data[KYO_ST_ARMED_PARTIAL];
  this->status_.armed_partial_no_delay = data[KYO_ST_ARMED_PARTIAL_NO_DELAY];
  this->status_.disarmed = data[KYO_ST_DISARMED];
  this->status_.zone_bypass = kyo_decode_zones(data + KYO_ST_ZONE_BYPASS);
  this->status_.zone_alarm_memory = kyo_decode_zones(data + KYO_ST_ZONE_ALARM_MEMORY);
  this->status_.zone_tamper_memory = kyo_decode_zones(data + KYO_ST_ZONE_TAMPER_MEMORY);
  this->status_.status_valid = true;
#ifdef USE_TEXT_SENSOR
  if (this->partition_status_text_sensor_ != nullptr)
    this->publish_partition_status_(data);
#endif
  this->notify_listeners_();
}

#ifdef USE_TEXT_SENSOR
void KyoAlarmComponent::publish_partition_status_(const uint8_t *data) {
  // The first four bytes of the status register, as returned by a raw read, then the partitions of the panel armed
  // with each arming type, in the format of the arming modes of the alarm control panel, e.g.
  // "09 00 00 F6 (total: [1, 4])"
  static const char *const TYPES[] = {"total", "partial", "partial_no_delay"};
  char hex[format_hex_pretty_size(KYO_ST_DISARMED + 1)];
  std::string text = format_hex_pretty_to(hex, data, KYO_ST_DISARMED + 1, ' ');
  text += " (";
  bool armed = false;
  for (size_t type = KYO_ST_ARMED_TOTAL; type <= KYO_ST_ARMED_PARTIAL_NO_DELAY; type++) {
    uint8_t partitions = data[type] & this->status_.partitions;
    if (partitions == 0)
      continue;
    if (armed)
      text += ", ";
    text += TYPES[type];
    text += ": [";
    const char *separator = "";
    for (uint8_t partition = 0; partition < KYO_MAX_PARTITIONS; partition++) {
      if (partitions & (1 << partition)) {
        text += separator;
        text += char('1' + partition);
        separator = ", ";
      }
    }
    text += ']';
    armed = true;
  }
  text += armed ? ")" : "disarmed)";
  // Polled every update_interval: publish changes only
  if (!this->partition_status_text_sensor_->has_state() || this->partition_status_text_sensor_->state != text)
    this->partition_status_text_sensor_->publish_state(text);
}
#endif

void KyoAlarmComponent::set_online_(bool online) {
  if (this->status_.online == online)
    return;
  if (online) {
    ESP_LOGI(TAG, "Panel online");
  } else {
    ESP_LOGW(TAG, "Panel not responding");
  }
  this->status_.online = online;
  if (!online) {
    // The last status read is stale: it is reported as unknown until the next poll. The panel may also have been
    // restarted or reprogrammed, so model and partitions are read again once it answers
    this->status_.realtime_valid = false;
    this->status_.status_valid = false;
    this->phase_ = Phase::READ_PANEL_INFO;
  }
  this->notify_listeners_();
}

void KyoAlarmComponent::notify_listeners_() {
  for (auto *listener : this->listeners_)
    listener->on_status_update(this->status_);
}

}  // namespace esphome::kyo_alarm
