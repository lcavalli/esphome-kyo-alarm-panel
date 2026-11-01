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
// Next slot of the event log and an unknown byte, then the status register (0x1502), read together
static const uint16_t KYO_REG_STATUS = 0x1500;
static const uint8_t KYO_REG_STATUS_SIZE = 21;
static const uint16_t KYO_REG_REALTIME = 0xf004;
static const uint8_t KYO_REG_REALTIME_SIZE = 11;
// Event log: a ring of KYO_EVENT_LOG_SLOTS records, the next one is written at the slot given by the status register
static const uint16_t KYO_REG_EVENT_LOG = 0x0d3d;
static const uint16_t KYO_EVENT_LOG_SLOTS = 256;
// Names of partitions, zones, user codes and keys, KYO_NAME_SIZE characters each, padded with spaces
static const uint16_t KYO_REG_PARTITION_NAMES = 0x1750;
static const uint16_t KYO_REG_ZONE_NAMES = 0x19b0;
static const uint16_t KYO_REG_CODE_NAMES = 0x1bb0;
static const uint16_t KYO_REG_KEY_NAMES = 0x1d30;
static const uint8_t KYO_NAME_SIZE = 16;

// Writable registers and their sizes
static const uint16_t KYO_REG_CTRL_PARTITIONS = 0xf000;
static const uint8_t KYO_REG_CTRL_PARTITIONS_SIZE = 4;
static const uint16_t KYO_REG_ZONE_BYPASS = 0xf001;
static const uint8_t KYO_REG_ZONE_BYPASS_SIZE = 8;
// Panel clock, also readable: day, month, year - 2000, hour, minute, second (binary, local time)
static const uint16_t KYO_REG_DATETIME = 0xf003;
static const uint8_t KYO_REG_DATETIME_SIZE = 6;
static const uint16_t KYO_REG_RESET = 0xf005;
static const uint8_t KYO_REG_RESET_SIZE = 2;

// Partition control register layout: partition bitmasks, in the order of the first bytes of the status register.
// Each bit set acts on its partition, partitions with no bit set are left as they are.
static const size_t KYO_CTRL_ARM_TOTAL = 0;
static const size_t KYO_CTRL_ARM_PARTIAL = 1;
static const size_t KYO_CTRL_ARM_PARTIAL_NO_DELAY = 2;
static const size_t KYO_CTRL_DISARM = 3;

// Real-time status register layout
static const size_t KYO_RT_ZONE_ACTIVE = 0;  // 4 bytes zone bitmap
static const size_t KYO_RT_ZONE_TAMPER = 4;  // 4 bytes zone bitmap
static const size_t KYO_RT_WARNINGS = 8;
static const size_t KYO_RT_PARTITION_ALARM = 9;
static const size_t KYO_RT_TAMPERS = 10;

// Status register layout, from KYO_REG_STATUS
static const size_t KYO_ST_EVENT_NEXT = 0;  // slot of the next event in the event log
static const size_t KYO_ST_ARMED_TOTAL = 2;
static const size_t KYO_ST_ARMED_PARTIAL = 3;
static const size_t KYO_ST_ARMED_PARTIAL_NO_DELAY = 4;
static const size_t KYO_ST_DISARMED = 5;
static const size_t KYO_ST_ZONE_BYPASS = 9;          // 4 bytes zone bitmap
static const size_t KYO_ST_ZONE_ALARM_MEMORY = 13;   // 4 bytes zone bitmap
static const size_t KYO_ST_ZONE_TAMPER_MEMORY = 17;  // 4 bytes zone bitmap

// Event log record layout: event code (big-endian), then day, month, year - 2000, hour, minute (binary, local time)
static const size_t KYO_EVENT_SIZE = 7;
static const size_t KYO_EV_CODE = 0;
static const size_t KYO_EV_DAY = 2;
static const size_t KYO_EV_MONTH = 3;
static const size_t KYO_EV_YEAR = 4;
static const size_t KYO_EV_HOUR = 5;
static const size_t KYO_EV_MINUTE = 6;

/// What the number of an event refers to.
enum class KyoEventSubject : uint8_t {
  NONE,
  PARTITION,
  ZONE,
  CODE,
  KEY,
  /// A number with no name in the panel (e.g. a failed call)
  NUMBER,
};

/// Types of event, in the order of their codes. The texts of each language follow this order.
enum class KyoEventType : uint8_t {
  ALARM_PARTITION,
  ALARM_ZONE,
  INACTIVITY_PARTITION,
  NEGLIGENCE_PARTITION,
  BYPASS_ZONE,
  UNBYPASS_ZONE,
  VALID_CODE,
  VALID_KEY,
  AUTO_BYPASS_ZONE,
  ARM_PARTITION,
  DISARM_PARTITION,
  REQUEST_ARM_PARTITION,
  REQUEST_DISARM_PARTITION,
  RESET_MEMORY_PARTITION,
  DISARM_DURESS_PARTITION,
  FAILED_CALL,
  TAMPER_ZONE,
  RESTORE_ZONE,
  REMOTE_COMMAND,
  UNKNOWN,
};

/// An event code decoded.
struct KyoEventCode {
  KyoEventType type{KyoEventType::UNKNOWN};
  KyoEventSubject subject{KyoEventSubject::NONE};
  /// Partition, zone, code, key or call, 1-based; 0 if the event has none
  uint8_t number{0};
};

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

/// True if `record` (KYO_EVENT_SIZE bytes) is an empty slot of the event log.
bool kyo_event_empty(const uint8_t *record);
/// Decode the code of an event log record.
KyoEventCode kyo_decode_event(uint16_t code);
/// Address of the name of the subject of `event` in the panel, 0 if it has none.
uint16_t kyo_event_name_address(const KyoEventCode &event);

/// Encode a 4 to 6 digit PIN the way the panel stores it (BCD, padded with 0xf). Returns false if invalid.
bool kyo_encode_pin(const char *pin, uint32_t *out);

}  // namespace esphome::kyo_alarm
