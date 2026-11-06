#include <gtest/gtest.h>

#include <vector>

#include "common.h"

namespace esphome::kyo_alarm::testing {

/// A hub with a time source; the panel clock is 1 Jan 2026 00:00:00.
class ClockTest : public HubTest {
 protected:
  void SetUp() override {
    HubTest::SetUp();
    this->clock_.now = epoch(2026, 1, 1, 0, 0, 0);
    this->hub_.set_time(&this->clock_);
    this->hub_.set_clock_max_drift(30);
    this->hub_.setup();
    this->connect();
  }

  /// The time source gets the time: the hub reads the panel clock.
  void sync() {
    this->clock_.sync();
    this->settle();
  }

  FakeClock clock_;
};

TEST_F(ClockTest, LeavesTheClockWithinTheMaximumDrift) {
  this->clock_.now += 30;
  this->sync();
  this->clock_.now -= 60;
  this->sync();
  EXPECT_TRUE(this->panel_.writes.empty());
  std::vector<uint8_t> frame(KYO_HEADER_SIZE);
  kyo_encode_header(KYO_OP_READ, KYO_REG_DATETIME, KYO_REG_DATETIME_SIZE, frame.data());
  EXPECT_EQ(this->panel_.frames.back(), frame);
}

TEST_F(ClockTest, SetsTheClockWhenItDrifts) {
  this->clock_.now = epoch(2026, 3, 29, 3, 0, 31);
  this->sync();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].address, KYO_REG_DATETIME);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{29, 3, 26, 3, 0, 31}));
}

TEST_F(ClockTest, SetsAnInvalidClock) {
  this->panel_.set_memory(KYO_REG_DATETIME, {31, 2, 26, 0, 0, 0});
  this->sync();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{1, 1, 26, 0, 0, 0}));
}

TEST_F(ClockTest, WaitsForAValidTime) {
  this->clock_.now = 0;
  this->sync();
  EXPECT_TRUE(this->panel_.writes.empty());
}

TEST_F(ClockTest, LeavesTheClockWhenItCannotBeRead) {
  this->clock_.now += 3600;
  this->panel_.silent = 1;
  this->sync();
  EXPECT_TRUE(this->panel_.writes.empty());
}

TEST_F(ClockTest, SyncsTheClockOnRequest) {
  std::vector<bool> results;
  this->clock_.now = epoch(2026, 12, 31, 23, 59, 59);
  this->hub_.sync_clock([&results](bool success) { results.push_back(success); });
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{31, 12, 26, 23, 59, 59}));
  EXPECT_EQ(results, std::vector<bool>{true});
}

}  // namespace esphome::kyo_alarm::testing
