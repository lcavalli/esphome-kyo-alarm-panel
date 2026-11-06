#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "common.h"

namespace esphome::kyo_alarm::testing {

// Codes of the BIS-KYO table
static const uint16_t ALARM_PARTITION_1 = 0;
static const uint16_t BYPASS_ZONE_9 = 64;
static const uint16_t DISARM_PARTITION_1 = 312;
static const uint16_t FAILED_CALL_1 = 352;
static const uint16_t REMOTE_COMMAND = 444;

/// A hub with the on_event automation; with the last event text sensor unless `without_text_sensor()` is called
/// before `connect()`.
class EventTest : public HubTest {
 protected:
  void SetUp() override {
    HubTest::SetUp();
    this->hub_.set_last_event_text_sensor(&this->last_event_);
    this->hub_.add_on_event_callback([this](const KyoEvent &event) { this->events_.push_back(event); });
  }
  void without_text_sensor() { this->hub_.set_last_event_text_sensor(nullptr); }
  void set_zone_name(uint8_t zone, const char *name) {
    this->panel_.set_memory(KYO_REG_ZONE_NAMES + (zone - 1) * KYO_NAME_SIZE, name);
  }
  std::vector<std::string> descriptions() {
    std::vector<std::string> out;
    for (const auto &event : this->events_)
      out.push_back(std::string(event.description) + "|" + std::to_string(event.number) + "|" + event.name);
    return out;
  }

  text_sensor::TextSensor last_event_;
  std::vector<KyoEvent> events_;
};

TEST_F(EventTest, ShowsTheLastEventAtBoot) {
  this->set_zone_name(9, "Kitchen window  ");
  this->panel_.set_event(2, BYPASS_ZONE_9);
  this->panel_.set_event_next(3);
  this->connect();
  EXPECT_EQ(this->last_event_.state, "2026-11-01 11:59 Bypass Zone: Kitchen window");
  // Not sent: it was logged before
  EXPECT_TRUE(this->events_.empty());
  this->poll();
  EXPECT_TRUE(this->events_.empty());
}

TEST_F(EventTest, SendsNewEvents) {
  this->connect();
  this->panel_.set_memory(KYO_REG_PARTITION_NAMES, "Home            ");
  this->panel_.set_event(0, REMOTE_COMMAND, 6, 11, 26, 21, 5);
  this->panel_.set_event(1, DISARM_PARTITION_1, 6, 11, 26, 21, 6);
  this->panel_.set_event_next(2);
  this->poll();
  ASSERT_EQ(this->events_.size(), 2u);
  EXPECT_EQ(this->events_[0].code, REMOTE_COMMAND);
  EXPECT_STREQ(this->events_[0].description, "Remote Command");
  EXPECT_EQ(this->events_[0].number, 0);
  EXPECT_STREQ(this->events_[0].name, "");
  EXPECT_STREQ(this->events_[0].time, "2026-11-06 21:05");
  EXPECT_EQ(this->events_[1].code, DISARM_PARTITION_1);
  EXPECT_STREQ(this->events_[1].description, "Disarm Partition");
  EXPECT_EQ(this->events_[1].number, 1);
  EXPECT_STREQ(this->events_[1].name, "Home");
  EXPECT_EQ(this->last_event_.state, "2026-11-06 21:06 Disarm Partition: Home");
  this->poll();
  EXPECT_EQ(this->events_.size(), 2u);
}

TEST_F(EventTest, WithoutTheTextSensorStartsFromTheNextSlot) {
  this->without_text_sensor();
  this->panel_.set_event(2, BYPASS_ZONE_9);
  this->panel_.set_event_next(3);
  this->connect();
  EXPECT_EQ(this->panel_.frames.size(), 4u);
  this->panel_.set_event(3, ALARM_PARTITION_1);
  this->panel_.set_event_next(4);
  this->poll();
  EXPECT_EQ(this->descriptions(), std::vector<std::string>{"Alarm Partition|1|"});
  EXPECT_FALSE(this->last_event_.has_state());
}

TEST_F(EventTest, SkipsEmptySlots) {
  this->connect();
  this->panel_.set_event(0, REMOTE_COMMAND);
  this->panel_.set_event(1, REMOTE_COMMAND, 0);
  this->panel_.set_event(2, REMOTE_COMMAND);
  this->panel_.set_event_next(3);
  this->poll();
  EXPECT_EQ(this->events_.size(), 2u);
}

TEST_F(EventTest, ReadsAtMostOneBlockPerPoll) {
  this->connect();
  for (uint8_t slot = 0; slot < 12; slot++)
    this->panel_.set_event(slot, REMOTE_COMMAND);
  this->panel_.set_event_next(12);
  this->poll();
  EXPECT_EQ(this->events_.size(), 9u);
  this->poll();
  EXPECT_EQ(this->events_.size(), 12u);
}

TEST_F(EventTest, ReadsAcrossTheEndOfTheRing) {
  this->without_text_sensor();
  this->panel_.set_event_next(250);
  this->connect();
  for (int slot = 250; slot < 259; slot++)
    this->panel_.set_event(uint8_t(slot), REMOTE_COMMAND, 1, 11, 26, 11, slot - 250);
  this->panel_.set_event_next(3);
  this->poll();
  ASSERT_EQ(this->events_.size(), 6u);
  EXPECT_STREQ(this->events_[5].time, "2026-11-01 11:05");
  this->poll();
  ASSERT_EQ(this->events_.size(), 9u);
  EXPECT_STREQ(this->events_[8].time, "2026-11-01 11:08");
}

TEST_F(EventTest, CleansUpNames) {
  this->connect();
  this->set_zone_name(9, "Zone\x01\xe8\x7f A   x   ");
  this->panel_.set_event(0, BYPASS_ZONE_9);
  this->set_zone_name(10, "                ");
  this->panel_.set_event(1, BYPASS_ZONE_9 + 1);
  this->panel_.set_event_next(2);
  this->poll();
  EXPECT_EQ(this->descriptions(), (std::vector<std::string>{"Bypass Zone|9|Zone??? A   x", "Bypass Zone|10|"}));
  EXPECT_EQ(this->last_event_.state, "2026-11-01 11:59 Bypass Zone 10");
}

TEST_F(EventTest, ShowsTheNumberWhenTheNameCannotBeRead) {
  this->connect();
  this->set_zone_name(9, "Kitchen window  ");
  this->panel_.set_event(0, BYPASS_ZONE_9);
  this->panel_.set_event_next(1);
  this->panel_.silent_addresses.push_back(KYO_REG_ZONE_NAMES + 8 * KYO_NAME_SIZE);
  this->poll();
  EXPECT_EQ(this->descriptions(), std::vector<std::string>{"Bypass Zone|9|"});
}

TEST_F(EventTest, DescribesEventsWithoutAName) {
  this->connect();
  this->panel_.set_event(0, FAILED_CALL_1);
  this->panel_.set_event(1, 500);
  this->panel_.set_event_next(2);
  this->poll();
  EXPECT_EQ(this->descriptions(), (std::vector<std::string>{"Failed Call|1|", "Event|0|"}));
  EXPECT_EQ(this->events_[1].code, 500);
  EXPECT_EQ(this->last_event_.state, "2026-11-01 11:59 Event 500");
}

TEST_F(EventTest, ReadsTheEventsAgainAfterAFailure) {
  // At boot, the last event
  this->panel_.set_event(0, REMOTE_COMMAND);
  this->panel_.set_event_next(1);
  this->panel_.silent_addresses.push_back(KYO_REG_EVENT_LOG);
  this->connect();
  EXPECT_FALSE(this->last_event_.has_state());
  this->panel_.silent_addresses.clear();
  this->poll();
  EXPECT_EQ(this->last_event_.state, "2026-11-01 11:59 Remote Command");

  // New events
  this->panel_.set_event(1, REMOTE_COMMAND);
  this->panel_.set_event_next(2);
  this->panel_.silent_addresses.push_back(KYO_REG_EVENT_LOG + KYO_EVENT_SIZE);
  this->poll();
  EXPECT_TRUE(this->events_.empty());
  this->panel_.silent_addresses.clear();
  this->poll();
  EXPECT_EQ(this->events_.size(), 1u);
}

TEST_F(EventTest, WaitsForTheEventsBeingRead) {
  this->connect();
  size_t queued = this->hub_.queued();
  this->hub_.status_read_while_reading_events(5);
  EXPECT_EQ(this->hub_.queued(), queued);
}

/// Run a status poll until the request at `address` is in progress.
static void run_until_request(FakePanel &panel, TestHub &hub, uint16_t address) {
  hub.update();
  for (int i = 0; i < 1000; i++) {
    if (!panel.frames.empty() && (panel.frames.back()[1] | panel.frames.back()[2] << 8) == address)
      return;
    now_ms += 10;
    hub.loop();
  }
  FAIL() << "No request at " << address;
}

TEST_F(EventTest, ShowsTheNumberWhenTheQueueIsFull) {
  this->connect();
  this->set_zone_name(9, "Kitchen window  ");
  this->panel_.set_event(0, BYPASS_ZONE_9);
  this->panel_.set_event_next(1);
  run_until_request(this->panel_, this->hub_, KYO_REG_EVENT_LOG);
  for (int i = 0; i < 8; i++)
    this->hub_.reset_memory();
  this->settle();
  EXPECT_EQ(this->descriptions(), std::vector<std::string>{"Bypass Zone|9|"});
}

TEST_F(EventTest, ReadsTheEventsAgainWhenTheQueueIsFull) {
  this->connect();
  this->panel_.set_event(0, REMOTE_COMMAND);
  this->panel_.set_event_next(1);
  run_until_request(this->panel_, this->hub_, KYO_REG_STATUS);
  for (int i = 0; i < 8; i++)
    this->hub_.reset_memory();
  this->settle();
  EXPECT_TRUE(this->events_.empty());
  this->poll();
  EXPECT_EQ(this->events_.size(), 1u);
}

}  // namespace esphome::kyo_alarm::testing
