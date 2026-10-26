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

  uint8_t arm = 0;
  uint8_t disarm = 0;
  switch (*state) {
    case ACP_STATE_ARMED_AWAY:
      arm = this->away_partitions_;
      break;
    case ACP_STATE_ARMED_HOME:
      arm = this->home_partitions_;
      break;
    case ACP_STATE_ARMED_NIGHT:
      arm = this->night_partitions_;
      break;
    case ACP_STATE_ARMED_VACATION:
      arm = this->vacation_partitions_;
      break;
    case ACP_STATE_DISARMED:
      disarm = 0xff;
      break;
    default:
      ESP_LOGW(TAG, "Unsupported state %s", LOG_STR_ARG(alarm_control_panel_state_to_string(*state)));
      return;
  }
  if (*state != ACP_STATE_DISARMED && arm == 0) {
    ESP_LOGW(TAG, "No partitions configured for %s", LOG_STR_ARG(alarm_control_panel_state_to_string(*state)));
    return;
  }

  bool code_needed = *state == ACP_STATE_DISARMED ? this->get_requires_code() : this->get_requires_code_to_arm();
  if (!code_needed) {
    this->execute_(*state, arm, disarm);
    return;
  }

  const auto &code = call.get_code();
  if (!code.has_value()) {
    ESP_LOGW(TAG, "A code is required");
    return;
  }
  AlarmControlPanelState target = *state;
  this->parent_->verify_code(code->c_str(), [this, target, arm, disarm](bool valid) {
    if (valid)
      this->execute_(target, arm, disarm);
  });
}

void KyoAlarmControlPanel::execute_(AlarmControlPanelState target, uint8_t arm, uint8_t disarm) {
  AlarmControlPanelState previous = this->get_state();
  this->in_transition_ = true;
  this->target_state_ = target;
  this->desired_state_ = target;
  this->transition_started_ = millis();
  this->publish_state(target == ACP_STATE_DISARMED ? ACP_STATE_DISARMING : ACP_STATE_ARMING);

  this->parent_->control_partitions(arm, disarm, [this, previous](bool success) {
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

  uint8_t armed = status.armed();
  if (armed == 0)
    return ACP_STATE_DISARMED;
  if (armed == (this->away_partitions_ & partitions))
    return ACP_STATE_ARMED_AWAY;
  if (this->home_partitions_ != 0 && armed == (this->home_partitions_ & partitions))
    return ACP_STATE_ARMED_HOME;
  if (this->night_partitions_ != 0 && armed == (this->night_partitions_ & partitions))
    return ACP_STATE_ARMED_NIGHT;
  if (this->vacation_partitions_ != 0 && armed == (this->vacation_partitions_ & partitions))
    return ACP_STATE_ARMED_VACATION;
  // Armed from a keypad with a combination not matching any mode
  return ACP_STATE_ARMED_CUSTOM_BYPASS;
}

void KyoAlarmControlPanel::dump_config() {
  ESP_LOGCONFIG(TAG,
                "  Alarm control panel '%s'\n"
                "    Away partitions: 0x%02X\n"
                "    Home partitions: 0x%02X\n"
                "    Night partitions: 0x%02X\n"
                "    Vacation partitions: 0x%02X\n"
                "    Requires code: %s\n"
                "    Requires code to arm: %s",
                this->get_name().c_str(), this->away_partitions_, this->home_partitions_, this->night_partitions_,
                this->vacation_partitions_, YESNO(this->get_requires_code()), YESNO(this->get_requires_code_to_arm()));
}

}  // namespace esphome::kyo_alarm
