#include "kyo_alarm_control_panel.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm.alarm_control_panel";

using namespace alarm_control_panel;

/// Maximum time the transitional state is held while waiting for the panel to report the target state
static const uint32_t TRANSITION_TIMEOUT_MS = 10000;

/// True if `armed`, read from the panel, arms the partitions of `mode` with their arming type, and no others.
static bool matches_mode(const KyoPartitionControl &mode, const KyoPartitionControl &armed, uint8_t partitions) {
  return mode.arm() != 0 && (mode.arm_total & partitions) == (armed.arm_total & partitions) &&
         (mode.arm_partial & partitions) == (armed.arm_partial & partitions) &&
         (mode.arm_partial_no_delay & partitions) == (armed.arm_partial_no_delay & partitions);
}

static void dump_mode(const char *name, const KyoPartitionControl &mode) {
  if (mode.arm() != 0) {
    ESP_LOGCONFIG(TAG, "    %s partitions: total 0x%02X, partial 0x%02X, partial no delay 0x%02X", name, mode.arm_total,
                  mode.arm_partial, mode.arm_partial_no_delay);
  }
}

KyoAlarmControlPanel::KyoAlarmControlPanel() {
  // Until the panel status is read, report pending: this also prevents arming before it is known
  this->current_state_ = ACP_STATE_PENDING;
  this->desired_state_ = ACP_STATE_PENDING;
  this->last_update_ = 0;
}

uint32_t KyoAlarmControlPanel::get_supported_features() const {
  uint32_t features = ACP_FEAT_ARM_AWAY;
  if (this->home_.arm() != 0)
    features |= ACP_FEAT_ARM_HOME;
  if (this->night_.arm() != 0)
    features |= ACP_FEAT_ARM_NIGHT;
  if (this->vacation_.arm() != 0)
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
      control = this->away_;
      break;
    case ACP_STATE_ARMED_HOME:
      control = this->home_;
      break;
    case ACP_STATE_ARMED_NIGHT:
      control = this->night_;
      break;
    case ACP_STATE_ARMED_VACATION:
      control = this->vacation_;
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
  // Partitions of the mode armed with another type are switched to their type without disarming them first.
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

  KyoPartitionControl armed{status.armed_total, status.armed_partial, status.armed_partial_no_delay};
  if (matches_mode(this->away_, armed, partitions))
    return ACP_STATE_ARMED_AWAY;
  if (matches_mode(this->home_, armed, partitions))
    return ACP_STATE_ARMED_HOME;
  if (matches_mode(this->night_, armed, partitions))
    return ACP_STATE_ARMED_NIGHT;
  if (matches_mode(this->vacation_, armed, partitions))
    return ACP_STATE_ARMED_VACATION;
  // Armed from a keypad with a combination not matching any mode
  return ACP_STATE_ARMED_CUSTOM_BYPASS;
}

void KyoAlarmControlPanel::dump_config() {
  ESP_LOGCONFIG(TAG,
                "  Alarm control panel '%s'\n"
                "    Requires code: %s\n"
                "    Requires code to arm: %s",
                this->get_name().c_str(), YESNO(this->get_requires_code()), YESNO(this->get_requires_code_to_arm()));
  dump_mode("Away", this->away_);
  dump_mode("Home", this->home_);
  dump_mode("Night", this->night_);
  dump_mode("Vacation", this->vacation_);
}

}  // namespace esphome::kyo_alarm
