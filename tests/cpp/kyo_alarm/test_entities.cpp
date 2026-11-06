#include <gtest/gtest.h>

#include <vector>

#include "common.h"
#include "esphome/components/kyo_alarm/binary_sensor/kyo_status_binary_sensor.h"
#include "esphome/components/kyo_alarm/button/kyo_reset_memory_button.h"
#include "esphome/components/kyo_alarm/button/kyo_sync_clock_button.h"
#include "esphome/components/kyo_alarm/switch/kyo_zone_bypass_switch.h"

namespace esphome::kyo_alarm::testing {

TEST_F(HubTest, BinarySensorShowsItsBit) {
  KyoStatusBinarySensor zone(StatusField::ZONE_ACTIVE, 2);
  KyoStatusBinarySensor communication(StatusField::ONLINE, 0);
  this->hub_.register_listener(&zone);
  this->hub_.register_listener(&communication);
  this->panel_.set_memory(KYO_REG_REALTIME, {0, 0, 0, 0x04});
  this->connect();
  EXPECT_TRUE(zone.state);
  EXPECT_TRUE(communication.state);
  this->panel_.set_memory(KYO_REG_REALTIME, {0, 0, 0, 0x03});
  this->poll();
  EXPECT_FALSE(zone.state);

  // Unknown once the communication is lost
  this->panel_.silent = -1;
  this->poll();
  this->poll();
  EXPECT_FALSE(zone.has_state());
  EXPECT_FALSE(communication.state);
  zone.dump_config();
}

TEST_F(HubTest, SwitchBypassesItsZone) {
  KyoZoneBypassSwitch bypass(2);
  bypass.set_parent(&this->hub_);
  this->hub_.register_listener(&bypass);
  // Not published while the status is unknown
  this->hub_.notify_status();
  EXPECT_FALSE(bypass.has_state());
  this->panel_.set_memory(KYO_REG_STATUS + KYO_ST_ZONE_BYPASS, {0, 0, 0, 0x04});
  this->connect();
  EXPECT_TRUE(bypass.state);

  bypass.turn_off();
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0, 0x04}));
  // Published when the panel reports it
  EXPECT_TRUE(bypass.state);
  this->panel_.set_memory(KYO_REG_STATUS + KYO_ST_ZONE_BYPASS, {0, 0, 0, 0});
  this->poll();
  EXPECT_FALSE(bypass.state);
  bypass.dump_config();
}

TEST_F(HubTest, ButtonResetsTheMemories) {
  KyoResetMemoryButton reset;
  reset.set_parent(&this->hub_);
  this->hub_.register_listener(&reset);
  this->connect();
  reset.press();
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].address, KYO_REG_RESET);
  reset.dump_config();
}

TEST_F(HubTest, ButtonSyncsTheClock) {
  FakeClock clock;
  clock.now = epoch(2026, 11, 6, 22, 0, 0);
  this->hub_.set_time(&clock);
  KyoSyncClockButton sync;
  sync.set_parent(&this->hub_);
  this->hub_.register_listener(&sync);
  this->connect();
  sync.press();
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{6, 11, 26, 22, 0, 0}));
  sync.dump_config();
}

}  // namespace esphome::kyo_alarm::testing
