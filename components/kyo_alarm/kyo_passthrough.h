#pragma once

#include "esphome/core/defines.h"

#ifdef USE_KYO_ALARM_PASSTHROUGH

#include <cstddef>
#include <cstdint>

#include "esphome/components/uart/uart_component.h"

namespace esphome::kyo_alarm {

class KyoAlarmComponent;

/// The UART of the panel as seen by another program, e.g. the programming software of the panel through a uart_tcp
/// server. While the hub is in passthrough mode it forwards the bytes both ways and logs them; otherwise it reads
/// nothing and drops what it is sent.
class KyoPassthroughPort : public uart::UARTComponent {
 public:
  explicit KyoPassthroughPort(KyoAlarmComponent *hub);

  void write_array(const uint8_t *data, size_t len) override;
  bool peek_byte(uint8_t *data) override;
  bool read_array(uint8_t *data, size_t len) override;
  size_t available() override;
  size_t available_for_write() override;
  uart::UARTFlushResult flush() override;
#if defined(USE_ESP8266) || defined(USE_ESP32)
  // The settings are those of the panel UART
  void load_settings(bool dump_config) override {}
#endif

 protected:
  void check_logger_conflict() override {}

  KyoAlarmComponent *hub_;
};

}  // namespace esphome::kyo_alarm

#endif
