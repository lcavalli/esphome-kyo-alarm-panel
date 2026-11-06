#include <gtest/gtest.h>

#include <tuple>
#include <vector>

#include "common.h"
#include "esphome/components/kyo_alarm/alarm_control_panel/kyo_alarm_control_panel.h"

namespace esphome::kyo_alarm::testing {

using namespace alarm_control_panel;

/// The alarm control panel, with control() reachable.
class TestAlarmControlPanel : public KyoAlarmControlPanel {
 public:
  using KyoAlarmControlPanel::control;
};

/// An alarm control panel with every mode: away arms everything, home [1, 4], night [1, 3, 4] and vacation 1 total,
/// 2 partial and 3 partial with no delay.
class AlarmControlPanelTest : public HubTest {
 protected:
  void SetUp() override {
    HubTest::SetUp();
    this->acp_.set_parent(&this->hub_);
    this->acp_.set_home_partitions(0x09, 0, 0);
    this->acp_.set_night_partitions(0x0d, 0, 0);
    this->acp_.set_vacation_partitions(0x01, 0x02, 0x04);
    this->hub_.register_listener(&this->acp_);
  }

  /// Send a command and run it to its end.
  void command(AlarmControlPanelState state, const char *code = nullptr) {
    auto call = this->acp_.make_call();
    switch (state) {
      case ACP_STATE_ARMED_AWAY:
        call.arm_away();
        break;
      case ACP_STATE_ARMED_HOME:
        call.arm_home();
        break;
      case ACP_STATE_ARMED_NIGHT:
        call.arm_night();
        break;
      case ACP_STATE_ARMED_VACATION:
        call.arm_vacation();
        break;
      case ACP_STATE_ARMED_CUSTOM_BYPASS:
        call.arm_custom_bypass();
        break;
      default:
        call.disarm();
        break;
    }
    if (code != nullptr)
      call.set_code(code);
    call.perform();
    this->settle();
  }
  /// The partitions written to the panel by the last command: arm total, partial, partial no delay, disarm.
  std::vector<uint8_t> written() {
    return this->panel_.writes.empty() ? std::vector<uint8_t>{} : this->panel_.writes.back().data;
  }

  TestAlarmControlPanel acp_;
};

TEST_F(AlarmControlPanelTest, PendingUntilTheStatusIsRead) {
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_PENDING);
  this->panel_.silent = 1;
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_PENDING);
  this->connect();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
}

TEST_F(AlarmControlPanelTest, ReportsTheModeArmedOnThePanel) {
  this->connect();
  // Partitions outside the panel do not count
  for (auto [total, partial, no_delay, state] :
       std::vector<std::tuple<uint8_t, uint8_t, uint8_t, AlarmControlPanelState>>{
           {0x0f, 0, 0, ACP_STATE_ARMED_AWAY},
           {0xff, 0, 0, ACP_STATE_ARMED_AWAY},
           {0x09, 0, 0, ACP_STATE_ARMED_HOME},
           {0x19, 0, 0, ACP_STATE_ARMED_HOME},
           {0x0d, 0, 0, ACP_STATE_ARMED_NIGHT},
           {0x01, 0x02, 0x04, ACP_STATE_ARMED_VACATION},
           {0x01, 0x00, 0x06, ACP_STATE_ARMED_CUSTOM_BYPASS},
           {0x01, 0x02, 0x00, ACP_STATE_ARMED_CUSTOM_BYPASS},
           {0x02, 0, 0, ACP_STATE_ARMED_CUSTOM_BYPASS},
           {0x00, 0x09, 0, ACP_STATE_ARMED_CUSTOM_BYPASS},
           {0x10, 0, 0, ACP_STATE_DISARMED},
       }) {
    this->panel_.set_armed(total, partial, no_delay);
    this->poll();
    EXPECT_EQ(this->acp_.get_state(), state) << int(total) << " " << int(partial) << " " << int(no_delay);
  }
}

TEST_F(AlarmControlPanelTest, ReportsAnAlarmInAPartitionOfThePanel) {
  this->connect();
  this->panel_.memory[KYO_REG_REALTIME + KYO_RT_PARTITION_ALARM] = 0x10;
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
  this->panel_.memory[KYO_REG_REALTIME + KYO_RT_PARTITION_ALARM] = 0x02;
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_TRIGGERED);
}

TEST_F(AlarmControlPanelTest, ShowsTheStateBeforeTheRealTimeRegisterIsRead) {
  this->panel_.memory[KYO_REG_REALTIME + KYO_RT_PARTITION_ALARM] = 0x01;
  this->panel_.silent_addresses.push_back(KYO_REG_REALTIME);
  this->connect();
  EXPECT_FALSE(this->status().realtime_valid);
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
}

TEST_F(AlarmControlPanelTest, SupportsTheConfiguredModes) {
  EXPECT_EQ(this->acp_.get_supported_features(),
            ACP_FEAT_ARM_AWAY | ACP_FEAT_ARM_HOME | ACP_FEAT_ARM_NIGHT | ACP_FEAT_ARM_VACATION);
  KyoAlarmControlPanel away_only;
  EXPECT_EQ(away_only.get_supported_features(), uint32_t(ACP_FEAT_ARM_AWAY));
}

TEST_F(AlarmControlPanelTest, ArmsWithAValidCode) {
  this->connect();
  this->command(ACP_STATE_ARMED_HOME, "1234");
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x09, 0x00, 0x00, 0x00}));
  // Arming until the panel reports it armed
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_ARMING);
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_ARMING);
  this->panel_.set_armed(0x09);
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_ARMED_HOME);

  this->command(ACP_STATE_DISARMED, "1234");
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x00, 0x00, 0x00, 0x0f}));
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMING);
  this->panel_.set_armed(0x00);
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
}

TEST_F(AlarmControlPanelTest, ArmsEachMode) {
  this->acp_.set_requires_code(false);
  this->connect();
  this->command(ACP_STATE_ARMED_AWAY);
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x0f, 0x00, 0x00, 0x00}));
  // The panel stays disarmed
  this->run(10000);
  this->poll();
  this->command(ACP_STATE_ARMED_NIGHT);
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x0d, 0x00, 0x00, 0x00}));
  this->run(10000);
  this->poll();
  this->command(ACP_STATE_ARMED_VACATION);
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x01, 0x02, 0x04, 0x00}));
}

TEST_F(AlarmControlPanelTest, ArmsTheAwayPartitions) {
  this->acp_.set_requires_code(false);
  this->acp_.set_away_partitions(0x03, 0x04, 0x00);
  this->connect();
  this->command(ACP_STATE_ARMED_AWAY);
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x03, 0x04, 0x00, 0x00}));
  this->panel_.set_armed(0x03, 0x04);
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_ARMED_AWAY);
}

TEST_F(AlarmControlPanelTest, RefusesAWrongOrMissingCode) {
  this->connect();
  this->command(ACP_STATE_ARMED_AWAY, "4321");
  this->command(ACP_STATE_ARMED_AWAY);
  EXPECT_TRUE(this->panel_.writes.empty());
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
}

TEST_F(AlarmControlPanelTest, ArmsWithoutACodeWhenNotRequired) {
  this->acp_.set_requires_code_to_arm(false);
  EXPECT_TRUE(this->acp_.get_requires_code());
  EXPECT_FALSE(this->acp_.get_requires_code_to_arm());
  this->connect();
  this->command(ACP_STATE_ARMED_AWAY);
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x0f, 0x00, 0x00, 0x00}));
  // Disarming still needs it
  this->panel_.set_armed(0x0f);
  this->poll();
  this->command(ACP_STATE_DISARMED);
  EXPECT_EQ(this->panel_.writes.size(), 1u);

  this->acp_.set_requires_code(false);
  EXPECT_FALSE(this->acp_.get_requires_code());
  this->acp_.set_requires_code_to_arm(true);
  EXPECT_FALSE(this->acp_.get_requires_code_to_arm());
  this->command(ACP_STATE_DISARMED);
  EXPECT_EQ(this->written(), (std::vector<uint8_t>{0x00, 0x00, 0x00, 0x0f}));
}

TEST_F(AlarmControlPanelTest, ShowsThePanelStateWhenItDoesNotChange) {
  this->acp_.set_requires_code(false);
  this->connect();
  // About 1.2 s for the write and the close request
  this->command(ACP_STATE_ARMED_AWAY);
  this->run(7000);
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_ARMING);
  this->run(2000);
  this->poll();
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
}

TEST_F(AlarmControlPanelTest, GoesBackWhenTheCommandFails) {
  this->acp_.set_requires_code(false);
  this->connect();
  this->panel_.silent = 1;
  this->command(ACP_STATE_ARMED_AWAY);
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
  EXPECT_TRUE(this->panel_.writes.empty());

  // The communication is lost while arming (two failed reads, then the write): the state before the command
  this->panel_.silent = 0;
  this->poll();
  this->panel_.silent = -1;
  this->poll();
  EXPECT_TRUE(this->status().status_valid);
  this->command(ACP_STATE_ARMED_AWAY);
  EXPECT_FALSE(this->status().status_valid);
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
}

TEST_F(AlarmControlPanelTest, IgnoresCommandsItCannotRun) {
  this->acp_.set_requires_code(false);
  // Status unknown
  this->command(ACP_STATE_DISARMED);
  this->connect();
  // Not a mode of the panel
  this->command(ACP_STATE_ARMED_CUSTOM_BYPASS);
  // No partition of the panel in the mode
  this->acp_.set_night_partitions(0x10, 0, 0);
  this->command(ACP_STATE_ARMED_NIGHT);
  // No state
  this->acp_.control(AlarmControlPanelCall(&this->acp_));
  EXPECT_EQ(this->hub_.queued(), 0u);
  EXPECT_TRUE(this->panel_.writes.empty());
  EXPECT_EQ(this->acp_.get_state(), ACP_STATE_DISARMED);
}

TEST_F(AlarmControlPanelTest, DumpsTheConfiguration) {
  this->acp_.dump_config();
  KyoAlarmControlPanel away_only;
  away_only.dump_config();
}

}  // namespace esphome::kyo_alarm::testing
