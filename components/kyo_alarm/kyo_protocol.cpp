#include "kyo_protocol.h"

namespace esphome::kyo_alarm {

uint8_t kyo_checksum(const uint8_t *data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++)
    sum += data[i];
  return sum;
}

bool kyo_verify_checksum(const uint8_t *data, size_t len) {
  if (len == 0)
    return false;
  return kyo_checksum(data, len - 1) == data[len - 1];
}

void kyo_encode_header(uint8_t op, uint16_t address, uint8_t size, uint8_t *out) {
  out[0] = op;
  out[1] = address & 0xff;
  out[2] = address >> 8;
  out[3] = size - 1;
  out[4] = 0x00;
  out[5] = kyo_checksum(out, KYO_HEADER_SIZE - 1);
}

void kyo_encode_close(uint8_t *out) {
  out[0] = KYO_OP_CLOSE;
  out[1] = 0x03;
  out[2] = 0x00;
  out[3] = 0x00;
  out[4] = 0x00;
  out[5] = kyo_checksum(out, KYO_HEADER_SIZE - 1);
}

static int hex_digit(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

static bool is_hex_separator(char c) { return c == ' ' || c == '\t' || c == ',' || c == ':' || c == '-'; }

size_t kyo_parse_hex(const char *hex, uint8_t *out, size_t max_size) {
  size_t size = 0;
  while (*hex != '\0') {
    if (is_hex_separator(*hex)) {
      hex++;
      continue;
    }
    // A token is an optional 0x prefix and an even number of digits
    if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X'))
      hex += 2;
    size_t digits = 0;
    for (; hex[digits] != '\0' && !is_hex_separator(hex[digits]); digits++) {
      if (hex_digit(hex[digits]) < 0)
        return 0;
    }
    if (digits == 0 || digits % 2 != 0 || size + digits / 2 > max_size)
      return 0;
    for (size_t i = 0; i < digits; i += 2)
      out[size++] = uint8_t(hex_digit(hex[i]) << 4 | hex_digit(hex[i + 1]));
    hex += digits;
  }
  return size;
}

uint32_t kyo_decode_zones(const uint8_t *data) {
  return (uint32_t(data[0]) << 24) | (uint32_t(data[1]) << 16) | (uint32_t(data[2]) << 8) | uint32_t(data[3]);
}

void kyo_encode_zones(uint32_t zones, uint8_t *out) {
  out[0] = zones >> 24;
  out[1] = zones >> 16;
  out[2] = zones >> 8;
  out[3] = zones;
}

bool kyo_event_empty(const uint8_t *record) {
  // Event code 0 is a valid event (alarm in partition 1): empty slots are recognized by their date
  return record[KYO_EV_DAY] == 0 || record[KYO_EV_MONTH] == 0 || record[KYO_EV_MONTH] > 12;
}

namespace {
/// Event codes of each type: code = first + number - 1
struct EventRange {
  uint16_t first;
  uint8_t count;
  KyoEventType type;
  KyoEventSubject subject;
};

// KYO32 table of the BIS-KYO documentation. Remote command (444) is not documented, it is logged by the panel before
// each command received over the serial line
const EventRange EVENT_RANGES[] = {
    {0, 8, KyoEventType::ALARM_PARTITION, KyoEventSubject::PARTITION},
    {8, 32, KyoEventType::ALARM_ZONE, KyoEventSubject::ZONE},
    {40, 8, KyoEventType::INACTIVITY_PARTITION, KyoEventSubject::PARTITION},
    {48, 8, KyoEventType::NEGLIGENCE_PARTITION, KyoEventSubject::PARTITION},
    {56, 32, KyoEventType::BYPASS_ZONE, KyoEventSubject::ZONE},
    {88, 32, KyoEventType::UNBYPASS_ZONE, KyoEventSubject::ZONE},
    {120, 24, KyoEventType::VALID_CODE, KyoEventSubject::CODE},
    {144, 128, KyoEventType::VALID_KEY, KyoEventSubject::KEY},
    {272, 32, KyoEventType::AUTO_BYPASS_ZONE, KyoEventSubject::ZONE},
    {304, 8, KyoEventType::ARM_PARTITION, KyoEventSubject::PARTITION},
    {312, 8, KyoEventType::DISARM_PARTITION, KyoEventSubject::PARTITION},
    {320, 8, KyoEventType::REQUEST_ARM_PARTITION, KyoEventSubject::PARTITION},
    {328, 8, KyoEventType::REQUEST_DISARM_PARTITION, KyoEventSubject::PARTITION},
    {336, 8, KyoEventType::RESET_MEMORY_PARTITION, KyoEventSubject::PARTITION},
    {344, 8, KyoEventType::DISARM_DURESS_PARTITION, KyoEventSubject::PARTITION},
    {352, 8, KyoEventType::FAILED_CALL, KyoEventSubject::NUMBER},
    {360, 32, KyoEventType::TAMPER_ZONE, KyoEventSubject::ZONE},
    {392, 32, KyoEventType::RESTORE_ZONE, KyoEventSubject::ZONE},
    {444, 1, KyoEventType::REMOTE_COMMAND, KyoEventSubject::NONE},
};
}  // namespace

KyoEventCode kyo_decode_event(uint16_t code) {
  KyoEventCode event;
  for (const auto &range : EVENT_RANGES) {
    if (code >= range.first && code < range.first + range.count) {
      event.type = range.type;
      event.subject = range.subject;
      if (range.subject != KyoEventSubject::NONE)
        event.number = code - range.first + 1;
      break;
    }
  }
  return event;
}

uint16_t kyo_event_name_address(const KyoEventCode &event) {
  uint16_t table;
  switch (event.subject) {
    case KyoEventSubject::PARTITION:
      table = KYO_REG_PARTITION_NAMES;
      break;
    case KyoEventSubject::ZONE:
      table = KYO_REG_ZONE_NAMES;
      break;
    case KyoEventSubject::CODE:
      table = KYO_REG_CODE_NAMES;
      break;
    case KyoEventSubject::KEY:
      table = KYO_REG_KEY_NAMES;
      break;
    default:
      return 0;
  }
  return table + (event.number - 1) * KYO_NAME_SIZE;
}

bool kyo_encode_pin(const char *pin, uint32_t *out) {
  uint32_t value = 0;
  size_t len = 0;
  for (; pin[len] != '\0'; len++) {
    if (len >= 6 || pin[len] < '0' || pin[len] > '9')
      return false;
    value = (value << 4) | uint32_t(pin[len] - '0');
  }
  if (len < 4)
    return false;
  for (; len < 6; len++)
    value = (value << 4) | 0xf;
  *out = value;
  return true;
}

}  // namespace esphome::kyo_alarm
