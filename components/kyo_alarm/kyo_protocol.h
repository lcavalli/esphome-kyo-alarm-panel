#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::kyo_alarm {

/*
 * KYO serial protocol (RS-232, 9600 8E1, little-endian addresses).
 *
 * Request header: <op> <addr lo> <addr hi> <len - 1> 0x00 <checksum>
 *   op: 0xf0 read, 0x0f write, 0x3c close (ends a write session)
 * Write requests append <len data bytes> <checksum>.
 * The panel echoes the 6-byte header, then (for reads) sends <len data bytes> <checksum>.
 * The checksum is the 8-bit sum of the bytes it follows (header and data are summed separately).
 */

static const size_t KYO_HEADER_SIZE = 6;
static const size_t KYO_MAX_FRAME_SIZE = 80;

static const uint8_t KYO_OP_READ = 0xf0;
static const uint8_t KYO_OP_WRITE = 0x0f;
static const uint8_t KYO_OP_CLOSE = 0x3c;

static const uint8_t KYO_MAX_ZONES = 32;
static const uint8_t KYO_MAX_PARTITIONS = 8;
static const uint8_t KYO_MAX_PINS = 24;
/// Largest read the panel answers (the official software never reads more)
static const uint8_t KYO_MAX_READ_SIZE = 64;

// Readable registers and their sizes (data bytes, checksum excluded)
static const uint16_t KYO_REG_PANEL_INFO = 0x0000;
static const uint8_t KYO_REG_PANEL_INFO_SIZE = 12;
static const uint16_t KYO_REG_PINS_1 = 0x01b4;
static const uint8_t KYO_REG_PINS_1_SIZE = 64;
static const uint16_t KYO_REG_PINS_2 = 0x01f4;
static const uint8_t KYO_REG_PINS_2_SIZE = 8;
static const uint16_t KYO_REG_INSTALLER_PIN = 0x01fc;
static const uint8_t KYO_REG_INSTALLER_PIN_SIZE = 3;
static const uint16_t KYO_REG_PARTITIONS = 0x01ff;
static const uint8_t KYO_REG_PARTITIONS_SIZE = 2;
static const uint16_t KYO_REG_STATUS = 0x1502;
static const uint8_t KYO_REG_STATUS_SIZE = 19;
static const uint16_t KYO_REG_REALTIME = 0xf004;
static const uint8_t KYO_REG_REALTIME_SIZE = 11;

// Writable registers and their sizes
static const uint16_t KYO_REG_CTRL_PARTITIONS = 0xf000;
static const uint8_t KYO_REG_CTRL_PARTITIONS_SIZE = 4;
static const uint16_t KYO_REG_ZONE_BYPASS = 0xf001;
static const uint8_t KYO_REG_ZONE_BYPASS_SIZE = 8;
static const uint16_t KYO_REG_DATETIME = 0xf003;
static const uint8_t KYO_REG_DATETIME_SIZE = 6;
static const uint16_t KYO_REG_RESET = 0xf005;
static const uint8_t KYO_REG_RESET_SIZE = 2;

// Real-time status register layout
static const size_t KYO_RT_ZONE_ACTIVE = 0;  // 4 bytes zone bitmap
static const size_t KYO_RT_ZONE_TAMPER = 4;  // 4 bytes zone bitmap
static const size_t KYO_RT_WARNINGS = 8;
static const size_t KYO_RT_PARTITION_ALARM = 9;
static const size_t KYO_RT_TAMPERS = 10;

// Status register layout
static const size_t KYO_ST_ARMED_TOTAL = 0;
static const size_t KYO_ST_ARMED_PARTIAL = 1;
static const size_t KYO_ST_ARMED_PARTIAL_NO_DELAY = 2;
static const size_t KYO_ST_DISARMED = 3;
static const size_t KYO_ST_ZONE_BYPASS = 7;          // 4 bytes zone bitmap
static const size_t KYO_ST_ZONE_ALARM_MEMORY = 11;   // 4 bytes zone bitmap
static const size_t KYO_ST_ZONE_TAMPER_MEMORY = 15;  // 4 bytes zone bitmap

/// 8-bit sum of `len` bytes.
uint8_t kyo_checksum(const uint8_t *data, size_t len);
/// True if the last of `len` bytes is the checksum of the preceding ones.
bool kyo_verify_checksum(const uint8_t *data, size_t len);

/// Encode a request header for `op` on `size` bytes at `address` into `out` (KYO_HEADER_SIZE bytes).
void kyo_encode_header(uint8_t op, uint16_t address, uint8_t size, uint8_t *out);
/// Encode a close request into `out` (KYO_HEADER_SIZE bytes).
void kyo_encode_close(uint8_t *out);

/// Parse bytes written in hex ("F0 03 F0", "f003f0", "0xf0, 0x03, 0xf0") into `out`, at most `max_size` bytes.
/// Bytes are pairs of hex digits, optionally separated by spaces, commas, colons or dashes.
/// Returns the number of bytes, 0 if `hex` is empty, invalid or too long.
size_t kyo_parse_hex(const char *hex, uint8_t *out, size_t max_size);

/// Decode a 4 byte zone bitmap (zones 32..25 first) into a mask where bit 0 is zone 1.
uint32_t kyo_decode_zones(const uint8_t *data);
/// Encode a zone mask (bit 0 is zone 1) into a 4 byte zone bitmap.
void kyo_encode_zones(uint32_t zones, uint8_t *out);

/// Encode a 4 to 6 digit PIN the way the panel stores it (BCD, padded with 0xf). Returns false if invalid.
bool kyo_encode_pin(const char *pin, uint32_t *out);

}  // namespace esphome::kyo_alarm
