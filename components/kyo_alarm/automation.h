#pragma once

#include <string>

#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"

#include "kyo_alarm.h"

namespace esphome::kyo_alarm {

/// Sends a raw read request and continues once it completes, with the outcome in get_raw_reply() and
/// get_raw_error() of the hub.
template<typename... Ts> class ReadRawAction : public Action<Ts...>, public Parented<KyoAlarmComponent> {
 public:
  TEMPLATABLE_VALUE(std::string, request)

  void play_complex(const Ts &...x) override {
    this->num_running_++;
    this->parent_->read_raw(this->request_.value(x...), [this, x...](bool /*success*/) { this->play_next_(x...); });
  }

 protected:
  void play(const Ts &...x) override { /* see play_complex */ }
};

}  // namespace esphome::kyo_alarm
