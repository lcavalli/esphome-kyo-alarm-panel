#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "esphome/components/kyo_alarm/kyo_protocol.h"

namespace esphome::kyo_alarm::testing {

TEST(Protocol, Checksum) {
  const uint8_t data[] = {0xf0, 0x04, 0xf0, 0x0a, 0x00};
  EXPECT_EQ(kyo_checksum(data, sizeof(data)), 0xee);
  EXPECT_EQ(kyo_checksum(data, 0), 0x00);
}

TEST(Protocol, VerifyChecksum) {
  const uint8_t good[] = {0x01, 0x02, 0x03};
  const uint8_t bad[] = {0x01, 0x02, 0x04};
  EXPECT_TRUE(kyo_verify_checksum(good, sizeof(good)));
  EXPECT_FALSE(kyo_verify_checksum(bad, sizeof(bad)));
  EXPECT_FALSE(kyo_verify_checksum(good, 0));
}

TEST(Protocol, EncodeHeader) {
  uint8_t out[KYO_HEADER_SIZE];
  kyo_encode_header(KYO_OP_READ, KYO_REG_REALTIME, KYO_REG_REALTIME_SIZE, out);
  EXPECT_EQ(std::vector<uint8_t>(out, out + KYO_HEADER_SIZE),
            (std::vector<uint8_t>{0xf0, 0x04, 0xf0, 0x0a, 0x00, 0xee}));
}

TEST(Protocol, EncodeClose) {
  uint8_t out[KYO_HEADER_SIZE];
  kyo_encode_close(out);
  EXPECT_EQ(std::vector<uint8_t>(out, out + KYO_HEADER_SIZE),
            (std::vector<uint8_t>{0x3c, 0x03, 0x00, 0x00, 0x00, 0x3f}));
}

std::vector<uint8_t> parse(const char *hex, size_t max_size = 8) {
  std::vector<uint8_t> out(max_size);
  out.resize(kyo_parse_hex(hex, out.data(), max_size));
  return out;
}

TEST(Protocol, ParseHex) {
  EXPECT_EQ(parse("F0 03 f0"), (std::vector<uint8_t>{0xf0, 0x03, 0xf0}));
  EXPECT_EQ(parse("f003F0"), (std::vector<uint8_t>{0xf0, 0x03, 0xf0}));
  EXPECT_EQ(parse("0xAb,0X0c:\t09-  12"), (std::vector<uint8_t>{0xab, 0x0c, 0x09, 0x12}));
  EXPECT_EQ(parse("0x0102"), (std::vector<uint8_t>{0x01, 0x02}));
}

TEST(Protocol, ParseHexRejectsInvalidInput) {
  EXPECT_TRUE(parse("").empty());
  EXPECT_TRUE(parse("   ").empty());
  // Not a hex digit
  EXPECT_TRUE(parse("F0 0G").empty());
  EXPECT_TRUE(parse("F0 0g").empty());
  EXPECT_TRUE(parse("F0.01").empty());
  EXPECT_TRUE(parse("F0;01").empty());
  // Odd number of digits
  EXPECT_TRUE(parse("F0 3").empty());
  // A prefix with no digits
  EXPECT_TRUE(parse("0x").empty());
  EXPECT_TRUE(parse("F0 0x 01").empty());
  // Too long
  EXPECT_TRUE(parse("01 02 03", 2).empty());
  EXPECT_EQ(parse("01 02", 2).size(), 2u);
}

TEST(Protocol, Zones) {
  const uint8_t bitmap[] = {0x80, 0x00, 0x01, 0x05};
  EXPECT_EQ(kyo_decode_zones(bitmap), 0x80000105u);
  uint8_t out[4];
  kyo_encode_zones(0x80000105u, out);
  EXPECT_EQ(std::vector<uint8_t>(out, out + 4), std::vector<uint8_t>(bitmap, bitmap + 4));
}

TEST(Protocol, EmptyEvent) {
  //                           code        day   month year  hour  minute
  const uint8_t valid[] = {0x00, 0x00, 0x01, 0x0c, 0x1a, 0x00, 0x00};
  const uint8_t no_day[] = {0x00, 0x00, 0x00, 0x01, 0x1a, 0x00, 0x00};
  const uint8_t no_month[] = {0x00, 0x00, 0x01, 0x00, 0x1a, 0x00, 0x00};
  const uint8_t bad_month[] = {0x00, 0x00, 0x01, 0x0d, 0x1a, 0x00, 0x00};
  EXPECT_FALSE(kyo_event_empty(valid));
  EXPECT_TRUE(kyo_event_empty(no_day));
  EXPECT_TRUE(kyo_event_empty(no_month));
  EXPECT_TRUE(kyo_event_empty(bad_month));
}

void expect_event(uint16_t code, KyoEventType type, KyoEventSubject subject, uint8_t number) {
  KyoEventCode event = kyo_decode_event(code);
  EXPECT_EQ(event.type, type) << "code " << code;
  EXPECT_EQ(event.subject, subject) << "code " << code;
  EXPECT_EQ(event.number, number) << "code " << code;
}

TEST(Protocol, DecodeEvent) {
  expect_event(0, KyoEventType::ALARM_PARTITION, KyoEventSubject::PARTITION, 1);
  expect_event(7, KyoEventType::ALARM_PARTITION, KyoEventSubject::PARTITION, 8);
  expect_event(8, KyoEventType::ALARM_ZONE, KyoEventSubject::ZONE, 1);
  expect_event(39, KyoEventType::ALARM_ZONE, KyoEventSubject::ZONE, 32);
  expect_event(64, KyoEventType::BYPASS_ZONE, KyoEventSubject::ZONE, 9);
  expect_event(120, KyoEventType::VALID_CODE, KyoEventSubject::CODE, 1);
  expect_event(271, KyoEventType::VALID_KEY, KyoEventSubject::KEY, 128);
  expect_event(352, KyoEventType::FAILED_CALL, KyoEventSubject::NUMBER, 1);
  expect_event(423, KyoEventType::RESTORE_ZONE, KyoEventSubject::ZONE, 32);
  expect_event(444, KyoEventType::REMOTE_COMMAND, KyoEventSubject::NONE, 0);
  // Codes outside the documented ranges
  expect_event(424, KyoEventType::UNKNOWN, KyoEventSubject::NONE, 0);
  expect_event(445, KyoEventType::UNKNOWN, KyoEventSubject::NONE, 0);
  expect_event(0xffff, KyoEventType::UNKNOWN, KyoEventSubject::NONE, 0);
}

TEST(Protocol, EventNameAddress) {
  EXPECT_EQ(kyo_event_name_address(kyo_decode_event(1)), KYO_REG_PARTITION_NAMES + KYO_NAME_SIZE);
  EXPECT_EQ(kyo_event_name_address(kyo_decode_event(8)), KYO_REG_ZONE_NAMES);
  EXPECT_EQ(kyo_event_name_address(kyo_decode_event(121)), KYO_REG_CODE_NAMES + KYO_NAME_SIZE);
  EXPECT_EQ(kyo_event_name_address(kyo_decode_event(146)), KYO_REG_KEY_NAMES + 2 * KYO_NAME_SIZE);
  EXPECT_EQ(kyo_event_name_address(kyo_decode_event(352)), 0);
  EXPECT_EQ(kyo_event_name_address(kyo_decode_event(444)), 0);
}

TEST(Protocol, EncodePin) {
  uint32_t pin = 0;
  EXPECT_TRUE(kyo_encode_pin("1234", &pin));
  EXPECT_EQ(pin, 0x1234ffu);
  EXPECT_TRUE(kyo_encode_pin("12345", &pin));
  EXPECT_EQ(pin, 0x12345fu);
  EXPECT_TRUE(kyo_encode_pin("123456", &pin));
  EXPECT_EQ(pin, 0x123456u);
}

TEST(Protocol, EncodePinRejectsInvalidPins) {
  uint32_t pin = 0xdeadbeef;
  EXPECT_FALSE(kyo_encode_pin("", &pin));
  EXPECT_FALSE(kyo_encode_pin("123", &pin));
  EXPECT_FALSE(kyo_encode_pin("1234567", &pin));
  EXPECT_FALSE(kyo_encode_pin("12a4", &pin));
  EXPECT_FALSE(kyo_encode_pin("12/4", &pin));
  EXPECT_EQ(pin, 0xdeadbeefu);
}

}  // namespace esphome::kyo_alarm::testing
