#pragma once

#include "esphome/components/button/button.h"
#include "esphome/core/helpers.h"

#include "../kyo_alarm.h"

namespace esphome::kyo_alarm {

/// Resets the alarm and tamper memories.
class KyoResetMemoryButton : public button::Button, public KyoAlarmListener, public Parented<KyoAlarmComponent> {
 public:
  void on_status_update(const KyoAlarmStatus &status) override {}
  void dump_config() override;

 protected:
  void press_action() override;
};

}  // namespace esphome::kyo_alarm
