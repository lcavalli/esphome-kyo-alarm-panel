#include "kyo_status_binary_sensor.h"

#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm.binary_sensor";

void KyoStatusBinarySensor::on_status_update(const KyoAlarmStatus &status) {
  auto value = status.get(this->field_);
  if (value.has_value())
    this->publish_state((*value >> this->bit_) & 1);
}

void KyoStatusBinarySensor::dump_config() { LOG_BINARY_SENSOR("  ", "Binary sensor", this); }

}  // namespace esphome::kyo_alarm
