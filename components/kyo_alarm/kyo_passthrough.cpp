#include "kyo_passthrough.h"

#ifdef USE_KYO_ALARM_PASSTHROUGH

#include "kyo_alarm.h"

namespace esphome::kyo_alarm {

KyoPassthroughPort::KyoPassthroughPort(KyoAlarmComponent *hub) : hub_(hub) {
  // The settings of the panel UART, checked by the validation of the hub. A uart_tcp uses the baud rate to pace its
  // writes when the panel UART does not report its free space
  this->baud_rate_ = 9600;
  this->data_bits_ = 8;
  this->parity_ = uart::UART_CONFIG_PARITY_EVEN;
  this->stop_bits_ = 1;
}

void KyoPassthroughPort::write_array(const uint8_t *data, size_t len) { this->hub_->passthrough_write_(data, len); }

bool KyoPassthroughPort::peek_byte(uint8_t *data) {
  return this->hub_->passthrough_active_ && this->hub_->parent_->peek_byte(data);
}

bool KyoPassthroughPort::read_array(uint8_t *data, size_t len) { return this->hub_->passthrough_read_(data, len); }

size_t KyoPassthroughPort::available() {
  return this->hub_->passthrough_active_ ? this->hub_->parent_->available() : 0;
}

size_t KyoPassthroughPort::available_for_write() { return this->hub_->parent_->available_for_write(); }

uart::UARTFlushResult KyoPassthroughPort::flush() { return this->hub_->parent_->flush(); }

}  // namespace esphome::kyo_alarm

#endif
