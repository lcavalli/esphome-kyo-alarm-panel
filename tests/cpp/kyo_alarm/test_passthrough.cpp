#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

#include "common.h"
#include "esphome/components/kyo_alarm/kyo_passthrough.h"
#include "esphome/components/kyo_alarm/switch/kyo_passthrough_switch.h"

namespace esphome::kyo_alarm::testing {

static const uint32_t IDLE_TIMEOUT_MS = 60000;

/// A read request of the panel model and firmware, as the programming software would send it.
static std::vector<uint8_t> read_panel_info() {
  std::vector<uint8_t> frame(KYO_HEADER_SIZE);
  kyo_encode_header(KYO_OP_READ, KYO_REG_PANEL_INFO, KYO_REG_PANEL_INFO_SIZE, frame.data());
  return frame;
}

/// The passthrough port, with access to its protected methods.
class TestPort : public KyoPassthroughPort {
 public:
  using KyoPassthroughPort::KyoPassthroughPort;
  /// Nothing to check: the port has no pins
  void check() { this->check_logger_conflict(); }
};

/// A hub with a passthrough port, connected to the panel.
class PassthroughTest : public HubTest {
 protected:
  void SetUp() override {
    HubTest::SetUp();
    this->hub_.set_passthrough_port(&this->port_);
    this->hub_.set_passthrough_idle_timeout(IDLE_TIMEOUT_MS);
    this->connect();
  }

  /// Turn the passthrough on and wait for it to start.
  void start() {
    this->hub_.set_passthrough(true);
    this->settle();
  }
  /// Send a request from the client, return the reply of the panel.
  std::vector<uint8_t> exchange(const std::vector<uint8_t> &request) {
    this->port_.write_array(request.data(), request.size());
    std::vector<uint8_t> reply(this->port_.available());
    EXPECT_TRUE(this->port_.read_array(reply.data(), reply.size()));
    return reply;
  }

  TestPort port_{&this->hub_};
};

TEST_F(PassthroughTest, StartsOnceTheRequestsInProgressAreDone) {
  // A status poll in progress
  this->hub_.update();
  this->run(10);
  size_t frames = this->panel_.frames.size();
  this->hub_.set_passthrough(true);
  this->hub_.set_passthrough(true);
  EXPECT_TRUE(this->status().passthrough);
  EXPECT_TRUE(this->status().online);
  EXPECT_EQ(this->port_.available(), 0u);

  // Both reads of the poll, then the passthrough starts, as for a lost communication
  this->panel_.extra = 3;
  this->settle();
  EXPECT_EQ(this->panel_.frames.size(), frames + 1);
  EXPECT_FALSE(this->status().online);
  EXPECT_FALSE(this->status().realtime_valid);
  EXPECT_FALSE(this->status().status_valid);
  EXPECT_TRUE(this->hub_.status_has_warning());
  EXPECT_FALSE(this->listener_.updates.back().online);
  EXPECT_TRUE(this->listener_.updates.back().passthrough);
  // The bytes after the last reply are dropped
  EXPECT_EQ(this->port_.available(), 0u);
  this->hub_.dump_config();
}

TEST_F(PassthroughTest, RefusesPollsAndCommands) {
  this->start();
  size_t frames = this->panel_.frames.size();
  this->hub_.update();
  this->run(2000);
  bool result = true;
  this->hub_.reset_memory([&](bool success) { result = success; });
  EXPECT_FALSE(result);
  bool valid = true;
  this->hub_.verify_code("1234", [&](bool success) { valid = success; });
  EXPECT_FALSE(valid);
  this->hub_.read_raw("F0 00 00 0B 00", [&](bool success) { result = success; });
  EXPECT_EQ(this->hub_.get_raw_error(), "Passthrough on");
  this->settle();
  EXPECT_EQ(this->panel_.frames.size(), frames);
}

TEST_F(PassthroughTest, ForwardsTheBytesBothWays) {
  this->start();
  auto request = read_panel_info();
  this->port_.write_array(request.data(), request.size());
  EXPECT_EQ(this->panel_.frames.back(), request);
  // The header echo, the data and the checksum
  ASSERT_EQ(this->port_.available(), KYO_HEADER_SIZE + KYO_REG_PANEL_INFO_SIZE + 1);
  uint8_t first;
  EXPECT_TRUE(this->port_.peek_byte(&first));
  EXPECT_EQ(first, KYO_OP_READ);
  std::array<uint8_t, KYO_HEADER_SIZE + KYO_REG_PANEL_INFO_SIZE + 1> reply{};
  EXPECT_TRUE(this->port_.read_array(reply.data(), reply.size()));
  EXPECT_TRUE(std::equal(request.begin(), request.end(), reply.begin()));
  EXPECT_EQ(std::memcmp(reply.data() + KYO_HEADER_SIZE, "KYO32G  2.13", 12), 0);
  EXPECT_EQ(this->port_.available(), 0u);
  EXPECT_FALSE(this->port_.peek_byte(&first));
  EXPECT_FALSE(this->port_.read_array(reply.data(), 1));
  // The frames are logged after a pause
  this->run(100);

  // More bytes than a log line in the same direction, then a pause between two requests
  for (int i = 0; i < 14; i++)
    this->port_.write_array(request.data(), request.size());
  now_ms += 60;
  this->exchange(request);

  // The panel UART settings and state
  EXPECT_EQ(this->port_.get_baud_rate(), 9600u);
  EXPECT_EQ(this->port_.get_parity(), uart::UART_CONFIG_PARITY_EVEN);
  EXPECT_EQ(this->port_.available_for_write(), SIZE_MAX);
  EXPECT_EQ(this->port_.flush(), uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS);
  this->port_.check();
}

TEST_F(PassthroughTest, DropsTheClientBytesUntilItStarts) {
  this->hub_.update();
  this->run(10);
  this->hub_.set_passthrough(true);
  size_t frames = this->panel_.frames.size();
  auto request = read_panel_info();
  this->port_.write_array(request.data(), request.size());
  EXPECT_EQ(this->panel_.frames.size(), frames);

  // Turned off before it started: the poll goes on
  this->hub_.set_passthrough(false);
  EXPECT_FALSE(this->status().passthrough);
  this->settle();
  EXPECT_TRUE(this->status().online);
  EXPECT_TRUE(this->status().status_valid);
}

TEST_F(PassthroughTest, PollsAgainWhenOff) {
  this->start();
  this->exchange(read_panel_info());
  this->hub_.set_passthrough(false);
  EXPECT_FALSE(this->status().passthrough);
  EXPECT_FALSE(this->hub_.status_has_warning());
  EXPECT_EQ(this->port_.available(), 0u);

  // After the gap between requests, model and partitions are read again, then the status
  size_t frames = this->panel_.frames.size();
  this->run(40);
  EXPECT_EQ(this->panel_.frames.size(), frames);
  this->settle();
  ASSERT_GT(this->panel_.frames.size(), frames);
  EXPECT_EQ(this->panel_.frames[frames], read_panel_info());
  EXPECT_TRUE(this->status().online);
  EXPECT_TRUE(this->status().status_valid);
}

TEST_F(PassthroughTest, ReportsAPanelNotAnsweringWhenOff) {
  this->start();
  this->panel_.silent = -1;
  this->hub_.set_passthrough(false);
  this->settle();
  this->poll();
  this->poll();
  EXPECT_FALSE(this->status().online);
  EXPECT_TRUE(this->hub_.status_has_warning());
  EXPECT_FALSE(this->listener_.updates.back().online);
}

TEST_F(PassthroughTest, TurnsOffWhenIdle) {
  this->start();
  this->run(IDLE_TIMEOUT_MS - 1000);
  EXPECT_TRUE(this->status().passthrough);
  // The traffic restarts the timeout
  this->exchange(read_panel_info());
  this->run(IDLE_TIMEOUT_MS - 1000);
  EXPECT_TRUE(this->status().passthrough);
  this->run(1000);
  EXPECT_FALSE(this->status().passthrough);
  this->settle();
  EXPECT_TRUE(this->status().online);
}

TEST_F(HubTest, PassthroughNeedsAPort) {
  this->connect();
  this->hub_.set_passthrough(true);
  EXPECT_FALSE(this->status().passthrough);
}

TEST_F(PassthroughTest, SwitchTurnsThePassthroughOnAndOff) {
  KyoPassthroughSwitch passthrough;
  passthrough.set_parent(&this->hub_);
  this->hub_.register_listener(&passthrough);
  this->poll();
  EXPECT_TRUE(passthrough.has_state());
  EXPECT_FALSE(passthrough.state);

  passthrough.turn_on();
  EXPECT_TRUE(passthrough.state);
  this->settle();
  EXPECT_FALSE(this->status().online);
  passthrough.turn_off();
  EXPECT_FALSE(passthrough.state);
  passthrough.dump_config();
}

}  // namespace esphome::kyo_alarm::testing
