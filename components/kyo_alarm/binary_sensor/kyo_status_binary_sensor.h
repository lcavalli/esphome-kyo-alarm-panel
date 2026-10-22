#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"

#include "../kyo_alarm.h"

namespace esphome::kyo_alarm {

/// Binary sensor bound to one bit of a status field (zone status, alarm memory, warnings, ...).
class KyoStatusBinarySensor : public binary_sensor::BinarySensor, public KyoAlarmListener {
 public:
  KyoStatusBinarySensor(StatusField field, uint8_t bit) : field_(field), bit_(bit) {}

  void on_status_update(const KyoAlarmStatus &status) override;
  void dump_config() override;

 protected:
  StatusField field_;
  uint8_t bit_;
};

}  // namespace esphome::kyo_alarm
