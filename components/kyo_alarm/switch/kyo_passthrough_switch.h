#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#include "../kyo_alarm.h"

// Requires the passthrough port of the hub, enforced by the final validation of the platform
#ifdef USE_KYO_ALARM_PASSTHROUGH

namespace esphome::kyo_alarm {

/// Passthrough mode switch: on while the hub forwards the panel UART to its passthrough port, or waits to.
class KyoPassthroughSwitch : public switch_::Switch, public KyoAlarmListener, public Parented<KyoAlarmComponent> {
 public:
  void on_status_update(const KyoAlarmStatus &status) override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
};

}  // namespace esphome::kyo_alarm

#endif
