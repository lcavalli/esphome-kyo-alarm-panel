#include "kyo_zone_bypass_switch.h"

#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm.switch";

void KyoZoneBypassSwitch::on_status_update(const KyoAlarmStatus &status) {
  auto value = status.get(StatusField::ZONE_BYPASS);
  if (value.has_value())
    this->publish_state((*value >> this->zone_) & 1);
}

void KyoZoneBypassSwitch::write_state(bool state) {
  // The state is published once the panel reports it
  this->parent_->set_zone_bypass(this->zone_, state);
}

void KyoZoneBypassSwitch::dump_config() {
  LOG_SWITCH("  ", "Bypass switch", this);
  ESP_LOGCONFIG(TAG, "    Zone: %u", this->zone_ + 1);
}

}  // namespace esphome::kyo_alarm
