#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/helpers.h"

#include "../kyo_alarm.h"

namespace esphome::kyo_alarm {

/// Zone bypass switch: on when the zone is bypassed (excluded).
class KyoZoneBypassSwitch : public switch_::Switch, public KyoAlarmListener, public Parented<KyoAlarmComponent> {
 public:
  explicit KyoZoneBypassSwitch(uint8_t zone) : zone_(zone) {}

  void on_status_update(const KyoAlarmStatus &status) override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;

  uint8_t zone_;
};

}  // namespace esphome::kyo_alarm
