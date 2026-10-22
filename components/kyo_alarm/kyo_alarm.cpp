#include "kyo_alarm.h"

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
    case StatusField::ZONE_OPEN:
      return this->realtime_valid ? make_optional(this->zone_open) : nullopt;
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
    this->time_->add_on_time_sync_callback([this]() { this->set_datetime(this->time_->now()); });
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
#endif
#ifdef USE_TIME
  if (this->time_ != nullptr)
    ESP_LOGCONFIG(TAG, "  Panel clock synchronized with time source");
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

void KyoAlarmComponent::control_partitions(uint8_t arm, uint8_t disarm, ResultCallback &&callback) {
  uint8_t data[KYO_REG_CTRL_PARTITIONS_SIZE] = {};
  data[0] = arm & this->status_.partitions;
  data[3] = disarm & this->status_.partitions;
  ESP_LOGD(TAG, "Arming partitions 0x%02X, disarming partitions 0x%02X", data[0], data[3]);
  this->enqueue_write_(KYO_REG_CTRL_PARTITIONS, data, sizeof(data), CTRL_PARTITIONS_TIMEOUT_MS, std::move(callback));
}

void KyoAlarmComponent::set_zone_bypass(uint8_t zone, bool bypass, ResultCallback &&callback) {
  if (zone >= this->max_zones_) {
    ESP_LOGE(TAG, "Invalid zone %u", zone + 1);
  } else if (!this->status_.status_valid || this->status_.armed() != 0) {
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
  this->status_.zone_open = kyo_decode_zones(data + KYO_RT_ZONE_OPEN);
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
  this->notify_listeners_();
}

void KyoAlarmComponent::set_online_(bool online) {
  if (this->status_.online == online)
    return;
  if (online) {
    ESP_LOGI(TAG, "Panel online");
  } else {
    ESP_LOGW(TAG, "Panel not responding");
  }
  this->status_.online = online;
  this->notify_listeners_();
}

void KyoAlarmComponent::notify_listeners_() {
  for (auto *listener : this->listeners_)
    listener->on_status_update(this->status_);
}

}  // namespace esphome::kyo_alarm
