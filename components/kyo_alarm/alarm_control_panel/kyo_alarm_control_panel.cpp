#include "kyo_alarm_control_panel.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm.alarm_control_panel";

using namespace alarm_control_panel;

/// Maximum time the transitional state is held while waiting for the panel to report the target state
static const uint32_t TRANSITION_TIMEOUT_MS = 10000;

KyoAlarmControlPanel::KyoAlarmControlPanel() {
  // Until the panel status is read, report pending: this also prevents arming before it is known
  this->current_state_ = ACP_STATE_PENDING;
  this->desired_state_ = ACP_STATE_PENDING;
  this->last_update_ = 0;
}

uint32_t KyoAlarmControlPanel::get_supported_features() const {
  uint32_t features = ACP_FEAT_ARM_AWAY;
  if (this->home_partitions_ != 0)
    features |= ACP_FEAT_ARM_HOME;
  if (this->night_partitions_ != 0)
    features |= ACP_FEAT_ARM_NIGHT;
  if (this->vacation_partitions_ != 0)
    features |= ACP_FEAT_ARM_VACATION;
  return features;
}

void KyoAlarmControlPanel::control(const AlarmControlPanelCall &call) {
  const auto &state = call.get_state();
  if (!state.has_value())
    return;
  if (!this->parent_->get_status().status_valid) {
    ESP_LOGW(TAG, "Panel status unknown, command ignored");
    return;
  }

  KyoPartitionControl control;
  switch (*state) {
    case ACP_STATE_ARMED_AWAY:
      control.arm_total = this->away_partitions_;
      break;
    case ACP_STATE_ARMED_HOME:
      control.arm_partial = this->home_partitions_;
      break;
    case ACP_STATE_ARMED_NIGHT:
      control.arm_partial_no_delay = this->night_partitions_;
      break;
    case ACP_STATE_ARMED_VACATION:
      control.arm_total = this->vacation_partitions_;
      break;
    case ACP_STATE_DISARMED:
      control.disarm = 0xff;
      break;
    default:
      ESP_LOGW(TAG, "Unsupported state %s", LOG_STR_ARG(alarm_control_panel_state_to_string(*state)));
      return;
  }
  // Arming none of the panel partitions would only disarm the others
  if (*state != ACP_STATE_DISARMED && (control.arm() & this->parent_->get_status().partitions) == 0) {
    ESP_LOGW(TAG, "No partitions of the panel configured for %s",
             LOG_STR_ARG(alarm_control_panel_state_to_string(*state)));
    return;
  }

  bool code_needed = *state == ACP_STATE_DISARMED ? this->get_requires_code() : this->get_requires_code_to_arm();
  if (!code_needed) {
    this->execute_(*state, control);
    return;
  }

  const auto &code = call.get_code();
  if (!code.has_value()) {
    ESP_LOGW(TAG, "A code is required");
    return;
  }
  AlarmControlPanelState target = *state;
  this->parent_->verify_code(code->c_str(), [this, target, control](bool valid) {
    if (valid)
      this->execute_(target, control);
  });
}

void KyoAlarmControlPanel::execute_(AlarmControlPanelState target, KyoPartitionControl control) {
  // A mode sets the whole panel: partitions armed now and not part of the mode are disarmed in the same write.
  // Partitions of the mode armed with another type are switched to the mode type without disarming them first.
  if (target != ACP_STATE_DISARMED)
    control.disarm = this->parent_->get_status().armed() & ~control.arm();

  AlarmControlPanelState previous = this->get_state();
  this->in_transition_ = true;
  this->target_state_ = target;
  this->desired_state_ = target;
  this->transition_started_ = millis();
  this->publish_state(target == ACP_STATE_DISARMED ? ACP_STATE_DISARMING : ACP_STATE_ARMING);

  this->parent_->control_partitions(control, [this, previous](bool success) {
    if (!success) {
      // Go back to the actual panel state, or to the state before the command if it is unknown (communication lost)
      this->in_transition_ = false;
      if (this->parent_->get_status().status_valid) {
        this->on_status_update(this->parent_->get_status());
      } else {
        this->publish_state(previous);
      }
    }
  });
}

void KyoAlarmControlPanel::on_status_update(const KyoAlarmStatus &status) {
  if (!status.status_valid)
    return;

  AlarmControlPanelState state = this->compute_state_(status);
  if (this->in_transition_) {
    if (state != this->target_state_ && millis() - this->transition_started_ < TRANSITION_TIMEOUT_MS)
      return;
    this->in_transition_ = false;
  }
  this->publish_state(state);
}

AlarmControlPanelState KyoAlarmControlPanel::compute_state_(const KyoAlarmStatus &status) const {
  uint8_t partitions = status.partitions;
  if (status.realtime_valid && (status.partition_alarm & partitions) != 0)
    return ACP_STATE_TRIGGERED;

  if (status.armed() == 0)
    return ACP_STATE_DISARMED;

  // A mode matches when its partitions, and no others, are armed with its type
  uint8_t total = status.armed_total & partitions;
  uint8_t partial = status.armed_partial & partitions;
  uint8_t partial_no_delay = status.armed_partial_no_delay & partitions;
  if (partial == 0 && partial_no_delay == 0) {
    if (total == (this->away_partitions_ & partitions))
      return ACP_STATE_ARMED_AWAY;
    if (this->vacation_partitions_ != 0 && total == (this->vacation_partitions_ & partitions))
      return ACP_STATE_ARMED_VACATION;
  }
  if (this->home_partitions_ != 0 && total == 0 && partial_no_delay == 0 &&
      partial == (this->home_partitions_ & partitions))
    return ACP_STATE_ARMED_HOME;
  if (this->night_partitions_ != 0 && total == 0 && partial == 0 &&
      partial_no_delay == (this->night_partitions_ & partitions))
    return ACP_STATE_ARMED_NIGHT;
  // Armed from a keypad with a combination not matching any mode
  return ACP_STATE_ARMED_CUSTOM_BYPASS;
}

void KyoAlarmControlPanel::dump_config() {
  ESP_LOGCONFIG(TAG,
                "  Alarm control panel '%s'\n"
                "    Away partitions (total): 0x%02X\n"
                "    Home partitions (partial): 0x%02X\n"
                "    Night partitions (partial, no delay): 0x%02X\n"
                "    Vacation partitions (total): 0x%02X\n"
                "    Requires code: %s\n"
                "    Requires code to arm: %s",
                this->get_name().c_str(), this->away_partitions_, this->home_partitions_, this->night_partitions_,
                this->vacation_partitions_, YESNO(this->get_requires_code()), YESNO(this->get_requires_code_to_arm()));
}

}  // namespace esphome::kyo_alarm
