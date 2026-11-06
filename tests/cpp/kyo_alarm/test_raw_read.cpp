#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"
#include "esphome/components/kyo_alarm/automation.h"

namespace esphome::kyo_alarm::testing {

class RawReadTest : public HubTest {
 protected:
  void SetUp() override {
    HubTest::SetUp();
    this->connect();
  }

  /// Run a raw read to its end, return the result passed to the callback.
  bool read(const std::string &request) {
    bool result = false;
    bool called = false;
    this->hub_.read_raw(request, [&](bool success) {
      result = success;
      called = true;
    });
    this->settle();
    EXPECT_TRUE(called);
    return result;
  }
  void expect_refused(const std::string &request, const std::string &error) {
    size_t frames = this->panel_.frames.size();
    EXPECT_FALSE(this->read(request)) << request;
    EXPECT_EQ(this->hub_.get_raw_error(), error) << request;
    EXPECT_EQ(this->hub_.get_raw_reply(), "") << request;
    EXPECT_EQ(this->panel_.frames.size(), frames) << request;
  }
};

TEST_F(RawReadTest, ReadsThePanelMemory) {
  EXPECT_TRUE(this->read("F0 00 00 0B 00"));
  EXPECT_EQ(this->hub_.get_raw_reply(), "4B 59 4F 33 32 47 20 20 32 2E 31 33 A3");
  EXPECT_EQ(this->hub_.get_raw_error(), "");
  // With the header checksum, right before and right after the PIN table
  EXPECT_TRUE(this->read("F0 00 00 0B 00 FB"));
  EXPECT_TRUE(this->read("F0 A4 01 0F 00"));
  EXPECT_TRUE(this->read("F0 FF 01 01 00"));
}

TEST_F(RawReadTest, RefusesInvalidRequests) {
  this->expect_refused("", "Expected a read request header in hex, e.g. F0 00 00 0B 00");
  this->expect_refused("F0 00 00 0B", "Expected a read request header in hex, e.g. F0 00 00 0B 00");
  this->expect_refused("0F 00 00 0B 00", "Only read requests (F0) are allowed");
  this->expect_refused("F0 00 00 40 00", "Invalid size, reads are at most 64 bytes (3F 00)");
  this->expect_refused("F0 00 00 0B 01", "Invalid size, reads are at most 64 bytes (3F 00)");
  this->expect_refused("F0 00 00 0B 00 00", "Header checksum mismatch");
  this->expect_refused("F0 B4 01 00 00", "The PIN table cannot be read");
  this->expect_refused("F0 A4 01 10 00", "The PIN table cannot be read");
  this->expect_refused("F0 FE 01 00 00", "The PIN table cannot be read");
}

TEST_F(RawReadTest, RefusesASecondReadInProgress) {
  std::vector<bool> results;
  this->hub_.read_raw("F0 00 00 0B 00", nullptr);
  this->hub_.read_raw("F0 00 00 0B 00", [&results](bool success) { results.push_back(success); });
  EXPECT_EQ(results, std::vector<bool>{false});
  EXPECT_EQ(this->hub_.get_raw_error(), "Another raw read is in progress");
  // The first one completes
  this->settle();
  EXPECT_EQ(this->hub_.get_raw_error(), "");
  EXPECT_EQ(this->hub_.get_raw_reply(), "4B 59 4F 33 32 47 20 20 32 2E 31 33 A3");
}

TEST_F(RawReadTest, ReportsAMissingReply) {
  this->panel_.silent = 1;
  EXPECT_FALSE(this->read("F0 00 00 0B 00"));
  EXPECT_EQ(this->hub_.get_raw_error(), "No reply from the panel");
  EXPECT_EQ(this->hub_.get_raw_reply(), "");
}

TEST_F(RawReadTest, ReportsAnIncompleteReply) {
  this->panel_.truncate = 3;
  EXPECT_FALSE(this->read("F0 00 00 0B 00"));
  EXPECT_EQ(this->hub_.get_raw_error(), "Incomplete reply from the panel");
  EXPECT_EQ(this->hub_.get_raw_reply(), "4B 59 4F");
}

TEST_F(RawReadTest, ReportsAWrongChecksum) {
  this->panel_.corrupt = 1;
  EXPECT_FALSE(this->read("F0 00 00 0B 00"));
  EXPECT_EQ(this->hub_.get_raw_error(), "Reply checksum mismatch");
  EXPECT_EQ(this->hub_.get_raw_reply(), "4B 59 4F 33 32 47 20 20 32 2E 31 33 5C");
}

TEST_F(RawReadTest, WorksWithoutACallback) {
  this->hub_.read_raw("F0", nullptr);
  EXPECT_NE(this->hub_.get_raw_error(), "");
  this->hub_.read_raw("F0 00 00 0B 00", nullptr);
  this->settle();
  EXPECT_EQ(this->hub_.get_raw_error(), "");
}

/// The action, with play() reachable.
template<typename... Ts> class TestReadRawAction : public ReadRawAction<Ts...> {
 public:
  using ReadRawAction<Ts...>::play;
};

TEST_F(RawReadTest, ActionContinuesWhenTheReadEnds) {
  TestReadRawAction<> action;
  action.set_parent(&this->hub_);
  action.set_request(std::string("F0 00 00 0B 00"));
  action.play_complex();
  EXPECT_TRUE(action.is_running());
  this->settle();
  EXPECT_FALSE(action.is_running());
  EXPECT_EQ(this->hub_.get_raw_reply(), "4B 59 4F 33 32 47 20 20 32 2E 31 33 A3");
  // Does nothing, the action runs from play_complex()
  action.play();
}

}  // namespace esphome::kyo_alarm::testing
