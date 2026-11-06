#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <string>
#include <vector>

#include "esphome/components/time/real_time_clock.h"
#include "esphome/components/uart/uart_component.h"
#include "esphome/components/kyo_alarm/kyo_alarm.h"
#include "esphome/components/kyo_alarm/kyo_protocol.h"

namespace esphome::kyo_alarm::testing {

/// Time returned by millis(), see fake_millis.cpp.
extern uint32_t now_ms;

/// A write received by the panel.
struct PanelWrite {
  uint16_t address;
  std::vector<uint8_t> data;
};

/// A KYO panel on the other side of the UART: answers each request as soon as it is written, from its memory.
class FakePanel : public uart::UARTComponent {
 public:
  FakePanel() {
    // Model and firmware, partitions 1 to 4, PIN 1 is 1234, the event log is empty, the clock is 1 Jan 2026 00:00:00,
    // no names
    this->set_memory(KYO_REG_PANEL_INFO, "KYO32G  2.13");
    this->memory[KYO_REG_PARTITIONS + 1] = 0x0f;
    this->set_memory(KYO_REG_PINS_1, {0x12, 0x34, 0xff});
    this->set_memory(KYO_REG_DATETIME, {1, 1, 26, 0, 0, 0});
    // Names of partitions, zones, codes and keys, all blank
    std::fill(this->memory.begin() + KYO_REG_PARTITION_NAMES,
              this->memory.begin() + KYO_REG_KEY_NAMES + 128 * KYO_NAME_SIZE, ' ');
  }

  void set_memory(uint16_t address, const std::vector<uint8_t> &data) {
    std::memcpy(this->memory.data() + address, data.data(), data.size());
  }
  void set_memory(uint16_t address, const char *text) {
    std::memcpy(this->memory.data() + address, text, std::strlen(text));
  }
  /// The status register (0x1500), from byte 2 on: armed total, partial, partial no delay, disarmed.
  void set_armed(uint8_t total, uint8_t partial = 0, uint8_t partial_no_delay = 0) {
    this->set_memory(KYO_REG_STATUS + KYO_ST_ARMED_TOTAL,
                     {total, partial, partial_no_delay, uint8_t(~(total | partial | partial_no_delay))});
  }
  void set_event_next(uint8_t next) { this->memory[KYO_REG_STATUS + KYO_ST_EVENT_NEXT] = next; }
  /// Write an event record in the event log.
  void set_event(uint8_t slot, uint16_t code, uint8_t day = 1, uint8_t month = 11, uint8_t year = 26, uint8_t hour = 11,
                 uint8_t minute = 59) {
    this->set_memory(KYO_REG_EVENT_LOG + slot * KYO_EVENT_SIZE,
                     {uint8_t(code >> 8), uint8_t(code), day, month, year, hour, minute});
  }

  // UARTComponent

  void write_array(const uint8_t *data, size_t len) override {
    std::vector<uint8_t> frame(data, data + len);
    this->frames.push_back(frame);
    if (this->silent > 0) {
      this->silent--;
      return;
    }
    if (this->silent < 0)
      return;
    uint16_t address = data[1] | data[2] << 8;
    if (std::find(this->silent_addresses.begin(), this->silent_addresses.end(), address) !=
        this->silent_addresses.end())
      return;
    // The header echo
    this->rx.insert(this->rx.end(), data, data + std::min<size_t>(len, KYO_HEADER_SIZE));
    size_t size = data[3] + 1;
    if (data[0] == KYO_OP_READ) {
      std::vector<uint8_t> reply(this->memory.begin() + address, this->memory.begin() + address + size);
      reply.push_back(kyo_checksum(reply.data(), reply.size()));
      if (this->corrupt > 0) {
        this->corrupt--;
        reply.back() ^= 0xff;
      }
      if (this->truncate >= 0) {
        reply.resize(this->truncate);
        this->truncate = -1;
      }
      reply.insert(reply.end(), this->extra, 0x55);
      this->extra = 0;
      this->rx.insert(this->rx.end(), reply.begin(), reply.end());
    } else if (data[0] == KYO_OP_WRITE) {
      this->writes.push_back({address, std::vector<uint8_t>(data + KYO_HEADER_SIZE, data + KYO_HEADER_SIZE + size)});
    } else if (data[0] == KYO_OP_CLOSE) {
      this->closes++;
    }
  }
  bool read_array(uint8_t *data, size_t len) override {
    if (this->rx.size() < len)
      return false;
    for (size_t i = 0; i < len; i++) {
      data[i] = this->rx.front();
      this->rx.pop_front();
    }
    return true;
  }
  bool peek_byte(uint8_t *data) override {
    if (this->rx.empty())
      return false;
    *data = this->rx.front();
    return true;
  }
  size_t available() override { return this->rx.size(); }
  uart::UARTFlushResult flush() override { return uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS; }
  void check_logger_conflict() override {}
#if defined(USE_ESP8266) || defined(USE_ESP32)
  void load_settings(bool dump_config) override {}
#endif

  std::array<uint8_t, 0x10000> memory{};
  /// Bytes to be read by the hub
  std::deque<uint8_t> rx;
  /// Every frame written by the hub
  std::vector<std::vector<uint8_t>> frames;
  std::vector<PanelWrite> writes;
  int closes{0};

  /// Requests not answered at all: a count, or -1 for all of them (panel disconnected)
  int silent{0};
  /// Addresses whose requests are not answered
  std::vector<uint16_t> silent_addresses;
  /// Read replies sent with a wrong checksum
  int corrupt{0};
  /// Bytes sent after the header echo of the next read reply, -1 for all of them
  int truncate{-1};
  /// Bytes sent after the next read reply
  size_t extra{0};
};

/// A time source whose time is set by the test.
class FakeClock : public time::RealTimeClock {
 public:
  time_t timestamp_now() override { return this->now; }
  void update() override {}
  /// Call the time sync callbacks, as a time source does when it gets the time.
  void sync() { this->time_sync_callback_.call(); }

  time_t now{0};
};

/// The hub, with access to its queue for the tests.
class TestHub : public KyoAlarmComponent {
 public:
  bool idle() const { return !this->busy_ && this->queue_count_ == 0; }
  size_t queued() const { return this->queue_count_; }
  void notify_status() { this->notify_listeners_(); }
  /// A status read while the events of a previous one are still being read
  void status_read_while_reading_events(uint8_t next) {
    this->events_busy_ = true;
    this->handle_event_next_(next);
    this->events_busy_ = false;
  }
};

/// A listener that records every status update.
class RecordingListener : public KyoAlarmListener {
 public:
  void on_status_update(const KyoAlarmStatus &status) override { this->updates.push_back(status); }

  std::vector<KyoAlarmStatus> updates;
};

/// UTC epoch of a date and time.
inline time_t epoch(int year, int month, int day, int hour, int minute, int second) {
  struct tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  return timegm(&tm);
}

/// A hub connected to a fake panel, with a fake time.
class HubTest : public ::testing::Test {
 protected:
  void SetUp() override {
    now_ms = 100000;
    this->hub_.set_uart_parent(&this->panel_);
    this->hub_.set_model("KYO32G", 32);
    this->hub_.register_listener(&this->listener_);
  }

  /// Advance the time by `ms`, running the hub loop every 10 ms.
  void run(uint32_t ms) {
    for (uint32_t elapsed = 0; elapsed < ms; elapsed += 10) {
      now_ms += 10;
      this->hub_.loop();
    }
  }
  /// Run the hub until it has no request in progress or queued (at most for 30 s), then past the gap between requests.
  void settle() {
    for (int i = 0; i < 3000 && !this->hub_.idle(); i++)
      this->run(10);
    ASSERT_TRUE(this->hub_.idle()) << "The hub is still busy";
    this->run(60);
  }
  /// Run a status poll and wait for it.
  void poll() {
    this->hub_.update();
    this->settle();
  }
  /// Read panel info and partitions, then the status, as after boot.
  void connect() {
    this->poll();
    ASSERT_TRUE(this->hub_.get_status().status_valid);
  }
  const KyoAlarmStatus &status() { return this->hub_.get_status(); }

  FakePanel panel_;
  TestHub hub_;
  RecordingListener listener_;
};

}  // namespace esphome::kyo_alarm::testing
