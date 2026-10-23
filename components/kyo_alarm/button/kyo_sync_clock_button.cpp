#include "kyo_sync_clock_button.h"

#ifdef USE_TIME

#include "esphome/core/log.h"

namespace esphome::kyo_alarm {

static const char *const TAG = "kyo_alarm.button";

void KyoSyncClockButton::press_action() { this->parent_->sync_clock(); }

void KyoSyncClockButton::dump_config() { LOG_BUTTON("  ", "Sync clock button", this); }

}  // namespace esphome::kyo_alarm

#endif
