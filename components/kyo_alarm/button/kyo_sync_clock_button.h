#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

#include "../kyo_alarm.h"

// Requires the time source of the hub, enforced by the final validation of the platform
#ifdef USE_TIME

namespace esphome::kyo_alarm {

/// Sets the panel clock from the time source of the hub.
class KyoSyncClockButton : public button::Button, public KyoAlarmListener, public Parented<KyoAlarmComponent> {
 public:
  void on_status_update(const KyoAlarmStatus &status) override {}
  void dump_config() override;

 protected:
  void press_action() override;
};

}  // namespace esphome::kyo_alarm

#endif
