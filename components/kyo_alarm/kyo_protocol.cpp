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
