#include "kyo_reset_memory_button.h"

#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm.button";

void KyoResetMemoryButton::press_action() { this->parent_->reset_memory(); }

void KyoResetMemoryButton::dump_config() { LOG_BUTTON("  ", "Reset memory button", this); }

}  // namespace esphome::kyo_alarm
