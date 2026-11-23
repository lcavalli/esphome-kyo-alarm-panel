#include "kyo_passthrough_switch.h"

#ifdef USE_KYO_ALARM_PASSTHROUGH

#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm.switch";

void KyoPassthroughSwitch::on_status_update(const KyoAlarmStatus &status) { this->publish_state(status.passthrough); }

void KyoPassthroughSwitch::write_state(bool state) {
  // The state is published by the hub
  this->parent_->set_passthrough(state);
}

void KyoPassthroughSwitch::dump_config() { LOG_SWITCH("  ", "Passthrough switch", this); }

}  // namespace esphome::kyo_alarm

#endif
