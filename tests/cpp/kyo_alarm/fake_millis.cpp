#include <cstdint>

#include "common.h"

namespace esphome::kyo_alarm::testing {

uint32_t now_ms = 0;

}  // namespace esphome::kyo_alarm::testing

// The linker sends the calls to esphome::millis() here (-Wl,--wrap in cpp-tests.yaml), so that the tests set the time
// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" uint32_t __wrap__ZN7esphome6millisEv() { return esphome::kyo_alarm::testing::now_ms; }
