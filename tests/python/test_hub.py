"""The kyo_alarm hub and its actions."""

from conftest import TIME, TWO_UARTS, WRONG_UART


def test_minimal(esphome):
    generated = esphome.generate("kyo_alarm:\n  id: kyo\n")
    assert generated.calls("kyo") == [
        "set_update_interval(1000)",
        "set_uart_parent(uart_bus)",
        'set_model("KYO32G", 32)',
    ]
    assert "USE_KYO_ALARM_EVENT_LOG" not in generated.defines
    assert "USE_KYO_ALARM_EVENT_LANGUAGE_IT" not in generated.defines


def test_options(esphome):
    generated = esphome.generate(
        TIME
        + """
kyo_alarm:
  id: kyo
  model: kyo32g
  time_id: host_time
  clock_max_drift: 1min
  language: it
  update_interval: 2s
  on_event:
    - logger.log: event
"""
    )
    assert generated.calls("kyo") == [
        "set_update_interval(2000)",
        "set_uart_parent(uart_bus)",
        'set_model("KYO32G", 32)',
        "set_time(host_time)",
        "set_clock_max_drift(60)",
        "add_on_event_callback(TriggerForwarder<const kyo_alarm::KyoEvent &>{automation_id})",
    ]
    assert "#define USE_KYO_ALARM_EVENT_LOG" in generated.defines
    assert "#define USE_KYO_ALARM_EVENT_LANGUAGE_IT" in generated.defines


def test_default_clock_drift(esphome):
    generated = esphome.generate(TIME + "kyo_alarm:\n  id: kyo\n  time_id: host_time\n")
    assert "set_clock_max_drift(30)" in generated.calls("kyo")


def test_two_hubs(esphome):
    generated = esphome.generate(
        """
kyo_alarm:
  - id: kyo_1
    uart_id: uart_bus
  - id: kyo_2
    uart_id: uart_2
""",
        TWO_UARTS,
    )
    assert "set_uart_parent(uart_bus)" in generated.calls("kyo_1")
    assert "set_uart_parent(uart_2)" in generated.calls("kyo_2")


def test_invalid_options(esphome):
    assert "Unknown value 'KYO8G'" in esphome.invalid("kyo_alarm:\n  model: KYO8G\n")
    assert "Unknown value 'fr'" in esphome.invalid("kyo_alarm:\n  language: fr\n")
    assert "must be at least 5s" in esphome.invalid(
        TIME + "kyo_alarm:\n  time_id: host_time\n  clock_max_drift: 4s\n"
    )
    assert "must be at most 30min" in esphome.invalid(
        TIME + "kyo_alarm:\n  time_id: host_time\n  clock_max_drift: 31min\n"
    )


def test_uart_settings(esphome):
    output = esphome.invalid("kyo_alarm:\n", WRONG_UART)
    assert (
        "Component kyo_alarm requires parity EVEN for the uart referenced by uart_id"
        in output
    )


def test_read_raw_action(esphome):
    generated = esphome.generate(
        """
kyo_alarm:
  id: kyo

script:
  - id: read_fixed
    then:
      - kyo_alarm.read_raw:
          id: kyo
          request: F0 00 00 0B 00
  - id: read_variable
    parameters:
      request: string
    then:
      - kyo_alarm.read_raw:
          id: kyo
          request: !lambda return request;
"""
    )
    assert generated.calls("kyo_alarm_readrawaction_id") == [
        "set_parent(kyo)",
        'set_request(ESPHOME_F("F0 00 00 0B 00"))',
    ]
    assert generated.calls("kyo_alarm_readrawaction_id_2")[0] == "set_parent(kyo)"
    assert (
        "kyo_alarm_readrawaction_id_2->set_request([](std::string request) -> std::string {"
        in generated.main
    )
