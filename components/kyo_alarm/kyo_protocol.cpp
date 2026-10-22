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
