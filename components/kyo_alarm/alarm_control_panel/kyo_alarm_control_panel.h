#pragma once

#include "esphome/components/alarm_control_panel/alarm_control_panel.h"
#include "esphome/core/helpers.h"

#include "../kyo_alarm.h"

namespace esphome::kyo_alarm {

/// Alarm control panel for the whole panel, each arming mode arms a set of partitions, each with an arming type.
class KyoAlarmControlPanel : public alarm_control_panel::AlarmControlPanel,
                             public KyoAlarmListener,
                             public Parented<KyoAlarmComponent> {
 public:
  KyoAlarmControlPanel();

  void set_away_partitions(uint8_t total, uint8_t partial, uint8_t partial_no_delay) {
    this->away_ = {total, partial, partial_no_delay};
  }
  void set_home_partitions(uint8_t total, uint8_t partial, uint8_t partial_no_delay) {
    this->home_ = {total, partial, partial_no_delay};
  }
  void set_night_partitions(uint8_t total, uint8_t partial, uint8_t partial_no_delay) {
    this->night_ = {total, partial, partial_no_delay};
  }
  void set_vacation_partitions(uint8_t total, uint8_t partial, uint8_t partial_no_delay) {
    this->vacation_ = {total, partial, partial_no_delay};
  }
  void set_requires_code(bool requires_code) { this->requires_code_ = requires_code; }
  void set_requires_code_to_arm(bool requires_code_to_arm) { this->requires_code_to_arm_ = requires_code_to_arm; }

  uint32_t get_supported_features() const override;
  bool get_requires_code() const override { return this->requires_code_; }
  bool get_requires_code_to_arm() const override { return this->requires_code_ && this->requires_code_to_arm_; }

  void on_status_update(const KyoAlarmStatus &status) override;
  void dump_config() override;

 protected:
  void control(const alarm_control_panel::AlarmControlPanelCall &call) override;
  void execute_(alarm_control_panel::AlarmControlPanelState target, KyoPartitionControl control);
  alarm_control_panel::AlarmControlPanelState compute_state_(const KyoAlarmStatus &status) const;

  // Partitions armed by each mode, by arming type (disarm is not used)
  KyoPartitionControl away_{0xff};
  KyoPartitionControl home_{};
  KyoPartitionControl night_{};
  KyoPartitionControl vacation_{};
  bool requires_code_{true};
  bool requires_code_to_arm_{true};

  // Arming/disarming in progress: hold the transitional state until the panel reports the target state
  bool in_transition_{false};
  alarm_control_panel::AlarmControlPanelState target_state_{alarm_control_panel::ACP_STATE_DISARMED};
  uint32_t transition_started_{0};
};

}  // namespace esphome::kyo_alarm
