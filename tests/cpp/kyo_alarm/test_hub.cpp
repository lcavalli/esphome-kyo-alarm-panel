#include <gtest/gtest.h>

#include <vector>

#include "common.h"

namespace esphome::kyo_alarm::testing {

/// Records the results of the callbacks of the commands.
struct Results {
  std::function<void(bool)> callback() {
    return [this](bool success) { this->values.push_back(success); };
  }
  std::vector<bool> values;
};

std::vector<uint8_t> read_frame(uint16_t address, uint8_t size) {
  std::vector<uint8_t> frame(KYO_HEADER_SIZE);
  kyo_encode_header(KYO_OP_READ, address, size, frame.data());
  return frame;
}

TEST(Status, UnknownFieldsAreEmpty) {
  KyoAlarmStatus status;
  EXPECT_EQ(*status.get(StatusField::ONLINE), 0u);
  for (auto field : {StatusField::ZONE_ACTIVE, StatusField::ZONE_TAMPER, StatusField::PARTITION_ALARM,
                     StatusField::WARNINGS, StatusField::TAMPERS, StatusField::ZONE_BYPASS,
                     StatusField::ZONE_ALARM_MEMORY, StatusField::ZONE_TAMPER_MEMORY}) {
    EXPECT_FALSE(status.get(field).has_value());
  }
  EXPECT_FALSE(status.get(static_cast<StatusField>(0xff)).has_value());
}

TEST(Status, KnownFields) {
  KyoAlarmStatus status;
  status.online = true;
  status.realtime_valid = true;
  status.status_valid = true;
  status.zone_active = 1;
  status.zone_tamper = 2;
  status.partition_alarm = 3;
  status.warnings = 4;
  status.tampers = 5;
  status.zone_bypass = 6;
  status.zone_alarm_memory = 7;
  status.zone_tamper_memory = 8;
  EXPECT_EQ(*status.get(StatusField::ONLINE), 1u);
  EXPECT_EQ(*status.get(StatusField::ZONE_ACTIVE), 1u);
  EXPECT_EQ(*status.get(StatusField::ZONE_TAMPER), 2u);
  EXPECT_EQ(*status.get(StatusField::PARTITION_ALARM), 3u);
  EXPECT_EQ(*status.get(StatusField::WARNINGS), 4u);
  EXPECT_EQ(*status.get(StatusField::TAMPERS), 5u);
  EXPECT_EQ(*status.get(StatusField::ZONE_BYPASS), 6u);
  EXPECT_EQ(*status.get(StatusField::ZONE_ALARM_MEMORY), 7u);
  EXPECT_EQ(*status.get(StatusField::ZONE_TAMPER_MEMORY), 8u);
}

TEST(Status, ArmedPartitionsOfThePanel) {
  KyoAlarmStatus status;
  status.partitions = 0x0f;
  status.armed_total = 0x11;
  status.armed_partial = 0x02;
  status.armed_partial_no_delay = 0x40;
  EXPECT_EQ(status.armed(), 0x03);
}

TEST_F(HubTest, ReadsPanelInfoPartitionsAndStatus) {
  this->panel_.set_memory(KYO_REG_REALTIME, {0x80, 0, 0, 0x01, 0, 0, 0, 0x02, 0x08, 0x01, 0x20});
  this->panel_.set_armed(0x01);
  this->panel_.set_memory(KYO_REG_STATUS + KYO_ST_ZONE_BYPASS, {0, 0, 0, 0x04, 0, 0, 0, 0x01, 0, 0, 0, 0x02});
  this->connect();

  EXPECT_EQ(this->panel_.frames, (std::vector<std::vector<uint8_t>>{
                                     read_frame(KYO_REG_PANEL_INFO, KYO_REG_PANEL_INFO_SIZE),
                                     read_frame(KYO_REG_PARTITIONS, KYO_REG_PARTITIONS_SIZE),
                                     read_frame(KYO_REG_REALTIME, KYO_REG_REALTIME_SIZE),
                                     read_frame(KYO_REG_STATUS, KYO_REG_STATUS_SIZE),
                                 }));
  const auto &status = this->status();
  EXPECT_TRUE(status.online);
  EXPECT_EQ(status.partitions, 0x0f);
  EXPECT_EQ(status.zone_active, 0x80000001u);
  EXPECT_EQ(status.zone_tamper, 0x00000002u);
  EXPECT_EQ(status.warnings, 0x08);
  EXPECT_EQ(status.partition_alarm, 0x01);
  EXPECT_EQ(status.tampers, 0x20);
  EXPECT_EQ(status.armed_total, 0x01);
  EXPECT_EQ(status.disarmed, 0xfe);
  EXPECT_EQ(status.zone_bypass, 0x04u);
  EXPECT_EQ(status.zone_alarm_memory, 0x01u);
  EXPECT_EQ(status.zone_tamper_memory, 0x02u);
  EXPECT_FALSE(this->hub_.status_has_warning());
  EXPECT_EQ(this->hub_.get_max_zones(), 32);
  EXPECT_EQ(this->hub_.get_setup_priority(), setup_priority::DATA);
  // Online, then the real-time and the status registers
  ASSERT_EQ(this->listener_.updates.size(), 3u);
  EXPECT_TRUE(this->listener_.updates[0].online);
  EXPECT_FALSE(this->listener_.updates[0].realtime_valid);

  // The next polls read only the status
  this->poll();
  EXPECT_EQ(this->panel_.frames.size(), 6u);
  EXPECT_EQ(this->panel_.frames[4], read_frame(KYO_REG_REALTIME, KYO_REG_REALTIME_SIZE));
}

TEST_F(HubTest, PollIsSkippedWhileBusy) {
  this->connect();
  this->hub_.update();
  // Requests queued
  this->hub_.update();
  EXPECT_EQ(this->hub_.queued(), 2u);
  this->run(10);  // real-time read sent
  EXPECT_EQ(this->hub_.queued(), 1u);
  this->run(60);  // status read sent
  EXPECT_EQ(this->hub_.queued(), 0u);
  // A request in progress
  this->hub_.update();
  EXPECT_EQ(this->hub_.queued(), 0u);
  this->settle();
}

TEST_F(HubTest, WaitsBetweenRequests) {
  this->connect();
  size_t frames = this->panel_.frames.size();
  this->hub_.update();
  this->run(10);  // real-time read sent
  EXPECT_EQ(this->panel_.frames.size(), frames + 1);
  this->run(10);  // reply received
  this->run(30);
  EXPECT_EQ(this->panel_.frames.size(), frames + 1);
  this->run(20);  // 50 ms after the reply
  EXPECT_EQ(this->panel_.frames.size(), frames + 2);
  this->settle();
}

TEST_F(HubTest, DropsStaleBytesBeforeARequest) {
  this->connect();
  this->panel_.rx.assign({0x01, 0x02, 0x03});
  this->poll();
  EXPECT_TRUE(this->status().status_valid);
  EXPECT_EQ(this->hub_.queued(), 0u);
}

TEST_F(HubTest, DiscardsBytesBeyondTheLargestFrame) {
  this->connect();
  Results results;
  this->panel_.extra = KYO_MAX_FRAME_SIZE;
  this->hub_.read_raw("F0 00 00 3F 00", results.callback());
  this->settle();
  EXPECT_EQ(results.values, std::vector<bool>{true});
}

TEST_F(HubTest, ReportsThePanelOfflineAfterThreeFailures) {
  this->connect();
  this->listener_.updates.clear();
  this->panel_.silent = -1;
  // Two failed requests
  this->poll();
  EXPECT_TRUE(this->status().online);
  EXPECT_TRUE(this->listener_.updates.empty());
  // Third and fourth
  this->poll();
  EXPECT_FALSE(this->status().online);
  EXPECT_FALSE(this->status().realtime_valid);
  EXPECT_FALSE(this->status().status_valid);
  EXPECT_TRUE(this->hub_.status_has_warning());
  ASSERT_EQ(this->listener_.updates.size(), 1u);
  EXPECT_FALSE(this->listener_.updates[0].online);
  this->poll();
  EXPECT_EQ(this->listener_.updates.size(), 1u);

  // The panel is read again from its model
  this->panel_.silent = 0;
  this->panel_.frames.clear();
  this->connect();
  EXPECT_EQ(this->panel_.frames[0], read_frame(KYO_REG_PANEL_INFO, KYO_REG_PANEL_INFO_SIZE));
  EXPECT_TRUE(this->status().online);
  EXPECT_FALSE(this->hub_.status_has_warning());
}

TEST_F(HubTest, ReportsThePanelOfflineFromBoot) {
  this->panel_.silent = -1;
  this->poll();
  this->poll();
  EXPECT_TRUE(this->listener_.updates.empty());
  this->poll();
  ASSERT_EQ(this->listener_.updates.size(), 1u);
  EXPECT_FALSE(this->listener_.updates[0].online);
  EXPECT_TRUE(this->hub_.status_has_warning());
}

TEST_F(HubTest, RejectsRepliesWithAWrongChecksum) {
  this->panel_.corrupt = 1;
  this->poll();
  EXPECT_FALSE(this->status().online);
  EXPECT_EQ(this->panel_.frames.size(), 1u);
  this->connect();
}

TEST_F(HubTest, ArmsAndDisarmsPartitions) {
  this->connect();
  Results results;
  KyoPartitionControl control;
  control.arm_total = 0x11;
  control.arm_partial = 0x02;
  control.arm_partial_no_delay = 0x04;
  control.disarm = 0xff;
  EXPECT_EQ(control.arm(), 0x17);
  this->hub_.control_partitions(control, results.callback());
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].address, KYO_REG_CTRL_PARTITIONS);
  // Partitions outside the panel are dropped, partitions both armed and disarmed are only armed
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{0x01, 0x02, 0x04, 0x08}));
  EXPECT_EQ(this->panel_.closes, 1);
  EXPECT_EQ(results.values, std::vector<bool>{true});
}

TEST_F(HubTest, WriteNotAcknowledgedFails) {
  this->connect();
  Results results;
  this->panel_.silent = 1;
  this->hub_.reset_memory(results.callback());
  this->settle();
  EXPECT_EQ(results.values, std::vector<bool>{false});
  EXPECT_EQ(this->panel_.frames.back()[0], KYO_OP_WRITE);
  // Without a callback
  this->panel_.silent = 1;
  this->hub_.reset_memory();
  this->settle();
  EXPECT_EQ(this->panel_.frames.back()[0], KYO_OP_WRITE);
}

TEST_F(HubTest, WriteSucceedsEvenIfTheCloseFails) {
  this->connect();
  Results results;
  this->hub_.reset_memory(results.callback());
  this->run(10);
  EXPECT_EQ(this->panel_.frames.back()[0], KYO_OP_WRITE);
  this->panel_.silent = 1;
  this->settle();
  EXPECT_EQ(this->panel_.frames.back()[0], KYO_OP_CLOSE);
  EXPECT_EQ(this->panel_.closes, 0);
  EXPECT_EQ(results.values, std::vector<bool>{true});
}

TEST_F(HubTest, ResetsTheMemories) {
  this->connect();
  this->hub_.reset_memory();
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].address, KYO_REG_RESET);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{0x0f, 0x00}));
  EXPECT_EQ(this->panel_.closes, 1);
}

TEST_F(HubTest, BypassesAndRestoresZones) {
  this->connect();
  Results results;
  this->hub_.set_zone_bypass(2, true, results.callback());
  this->settle();
  this->hub_.set_zone_bypass(31, false, results.callback());
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 2u);
  EXPECT_EQ(this->panel_.writes[0].address, KYO_REG_ZONE_BYPASS);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{0, 0, 0, 0x04, 0, 0, 0, 0}));
  EXPECT_EQ(this->panel_.writes[1].data, (std::vector<uint8_t>{0, 0, 0, 0, 0x80, 0, 0, 0}));
  EXPECT_EQ(results.values, (std::vector<bool>{true, true}));
}

TEST_F(HubTest, RefusesBypassesThePanelCannotDo) {
  Results results;
  // Status not read yet
  this->hub_.set_zone_bypass(0, true, results.callback());
  this->connect();
  // Zone out of range
  this->hub_.set_zone_bypass(32, true, results.callback());
  this->hub_.set_zone_bypass(32, true);
  // Panel armed
  this->panel_.set_armed(0x01);
  this->poll();
  this->hub_.set_zone_bypass(0, true, results.callback());
  EXPECT_EQ(results.values, (std::vector<bool>{false, false, false}));
  EXPECT_EQ(this->hub_.queued(), 0u);
  EXPECT_TRUE(this->panel_.writes.empty());
}

TEST_F(HubTest, SetsThePanelClock) {
  this->connect();
  Results results;
  this->hub_.set_datetime(ESPTime::from_epoch_utc(epoch(2026, 11, 6, 21, 30, 15)), results.callback());
  this->settle();
  ASSERT_EQ(this->panel_.writes.size(), 1u);
  EXPECT_EQ(this->panel_.writes[0].address, KYO_REG_DATETIME);
  EXPECT_EQ(this->panel_.writes[0].data, (std::vector<uint8_t>{6, 11, 26, 21, 30, 15}));
  EXPECT_EQ(results.values, std::vector<bool>{true});
}

TEST_F(HubTest, RefusesAnInvalidTime) {
  Results results;
  this->hub_.set_datetime(ESPTime{}, results.callback());
  this->hub_.set_datetime(ESPTime::from_epoch_utc(epoch(2100, 1, 1, 0, 0, 0)), results.callback());
  this->hub_.set_datetime(ESPTime{});
  EXPECT_EQ(results.values, (std::vector<bool>{false, false}));
  EXPECT_EQ(this->hub_.queued(), 0u);
}

TEST_F(HubTest, CannotSyncTheClockWithoutATimeSource) {
  Results results;
  this->hub_.sync_clock(results.callback());
  this->hub_.sync_clock();
  EXPECT_EQ(results.values, std::vector<bool>{false});
  EXPECT_EQ(this->hub_.queued(), 0u);
  // Nothing to register
  this->hub_.setup();
}

TEST_F(HubTest, VerifiesCodesAgainstThePanelPins) {
  this->connect();
  // PIN 3 is 98765
  this->panel_.set_memory(KYO_REG_PINS_1 + 6, {0x98, 0x76, 0x5f});
  // PIN 24, in the second read
  this->panel_.set_memory(KYO_REG_PINS_2 + 5, {0x55, 0x55, 0x55});
  Results results;
  for (const char *code : {"1234", "98765", "555555", "4321"}) {
    this->hub_.verify_code(code, results.callback());
    this->settle();
  }
  EXPECT_EQ(results.values, (std::vector<bool>{true, true, true, false}));
  EXPECT_EQ(this->panel_.frames.back(), read_frame(KYO_REG_PINS_2, KYO_REG_PINS_2_SIZE));
}

TEST_F(HubTest, RefusesInvalidCodes) {
  Results results;
  this->hub_.verify_code(nullptr, results.callback());
  this->hub_.verify_code("12", results.callback());
  // The only PIN that matches an empty slot of the PIN table (0)
  this->hub_.verify_code("000000", results.callback());
  EXPECT_EQ(results.values, (std::vector<bool>{false, false, false}));
  EXPECT_EQ(this->hub_.queued(), 0u);
}

TEST_F(HubTest, CodeNotVerifiedWhenAReadFails) {
  this->connect();
  Results results;
  this->panel_.silent = 1;
  this->hub_.verify_code("1234", results.callback());
  this->settle();
  // The first read is answered, the second is not
  this->hub_.verify_code("1234", results.callback());
  this->run(20);
  this->panel_.silent = 1;
  this->settle();
  EXPECT_EQ(results.values, (std::vector<bool>{false, false}));
}

TEST_F(HubTest, DropsRequestsWhenTheQueueIsFull) {
  this->connect();
  Results results;
  for (int i = 0; i < 8; i++)
    this->hub_.reset_memory();
  this->hub_.reset_memory(results.callback());
  this->hub_.reset_memory();
  this->hub_.verify_code("1234", results.callback());
  this->hub_.read_raw("F0 00 00 0B 00", results.callback());
  EXPECT_EQ(results.values, (std::vector<bool>{false, false, false}));
  EXPECT_EQ(this->hub_.get_raw_error(), "Request queue full");
  EXPECT_EQ(this->hub_.get_raw_reply(), "");
  this->settle();
  EXPECT_EQ(this->panel_.writes.size(), 8u);
}

TEST_F(HubTest, DropsRequestsAtTheFrontWhenTheQueueIsFull) {
  this->connect();
  Results results;
  // The close request of a write
  this->hub_.reset_memory(results.callback());
  this->run(10);
  for (int i = 0; i < 8; i++)
    this->hub_.reset_memory();
  this->settle();
  EXPECT_EQ(results.values, std::vector<bool>{true});
  EXPECT_EQ(this->panel_.writes.size(), 9u);
  EXPECT_EQ(this->panel_.closes, 8);

  // The second read of a code check
  this->hub_.verify_code("1234", results.callback());
  this->run(10);
  for (int i = 0; i < 8; i++)
    this->hub_.reset_memory();
  this->settle();
  EXPECT_EQ(results.values, (std::vector<bool>{true, false}));
}

TEST_F(HubTest, PublishesPanelInfo) {
  text_sensor::TextSensor model;
  text_sensor::TextSensor firmware;
  this->hub_.set_panel_model_text_sensor(&model);
  this->hub_.set_firmware_version_text_sensor(&firmware);
  this->connect();
  EXPECT_EQ(model.state, "KYO32G");
  EXPECT_EQ(firmware.state, "2.13");
}

TEST_F(HubTest, ReadsThePartitionsAgainWhenTheReadFails) {
  this->panel_.silent_addresses.push_back(KYO_REG_PARTITIONS);
  this->poll();
  this->poll();
  EXPECT_EQ(this->panel_.frames.size(), 3u);
  EXPECT_EQ(this->panel_.frames.back(), read_frame(KYO_REG_PARTITIONS, KYO_REG_PARTITIONS_SIZE));
  this->panel_.silent_addresses.clear();
  this->connect();
  EXPECT_EQ(this->status().partitions, 0x0f);
}

TEST_F(HubTest, PublishesABlankPanelInfo) {
  text_sensor::TextSensor model;
  text_sensor::TextSensor firmware;
  this->hub_.set_panel_model_text_sensor(&model);
  this->hub_.set_firmware_version_text_sensor(&firmware);
  this->panel_.set_memory(KYO_REG_PANEL_INFO, "            ");
  this->connect();
  EXPECT_EQ(model.state, "");
  EXPECT_EQ(firmware.state, "");
}

TEST_F(HubTest, AcceptsAnotherPanelModel) {
  this->panel_.set_memory(KYO_REG_PANEL_INFO, "KYO8G   1.0 ");
  this->connect();
  EXPECT_TRUE(this->status().online);
}

TEST_F(HubTest, PublishesThePartitionStatus) {
  text_sensor::TextSensor partition_status;
  this->hub_.set_partition_status_text_sensor(&partition_status);
  int published = 0;
  partition_status.add_on_state_callback([&published](const std::string &) { published++; });
  this->panel_.set_armed(0x00);
  this->connect();
  EXPECT_EQ(partition_status.state, "00 00 00 FF (disarmed)");
  this->poll();
  EXPECT_EQ(published, 1);

  // Partition 5 is not in the panel
  this->panel_.set_armed(0x19, 0x02, 0x04);
  this->poll();
  EXPECT_EQ(partition_status.state, "19 02 04 E0 (total: [1, 4], partial: [2], partial_no_delay: [3])");
  this->panel_.set_armed(0x00, 0x00, 0x06);
  this->poll();
  EXPECT_EQ(partition_status.state, "00 00 06 F9 (partial_no_delay: [2, 3])");
  EXPECT_EQ(published, 3);
}

TEST_F(HubTest, DumpsTheConfiguration) {
  this->hub_.dump_config();
  text_sensor::TextSensor model;
  text_sensor::TextSensor firmware;
  text_sensor::TextSensor partition_status;
  text_sensor::TextSensor last_event;
  FakeClock clock;
  this->hub_.set_panel_model_text_sensor(&model);
  this->hub_.set_firmware_version_text_sensor(&firmware);
  this->hub_.set_partition_status_text_sensor(&partition_status);
  this->hub_.set_last_event_text_sensor(&last_event);
  this->hub_.set_time(&clock);
  this->hub_.set_clock_max_drift(30);
  this->hub_.dump_config();
  // The listener has the default dump_config()
  this->listener_.KyoAlarmListener::dump_config();
}

}  // namespace esphome::kyo_alarm::testing
