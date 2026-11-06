"""The entity platforms of the component, except the alarm control panel."""

from conftest import TIME


def test_binary_sensors(esphome):
    generated = esphome.generate(
        """
kyo_alarm:
  id: kyo

binary_sensor:
  - platform: kyo_alarm
    communication:
      id: communication
      name: Communication
    mains_failure:
      id: mains_failure
      name: Mains failure
    wireless_fault:
      id: wireless_fault
      name: Wireless fault
    tamper_zone:
      id: tamper_zone
      name: Zone tamper
    tamper_wireless:
      id: tamper_wireless
      name: Wireless tamper
    zones:
      - zone: 3
        sensor:
          id: zone_3
          name: Zone 3
        tamper:
          id: zone_3_tamper
          name: Zone 3 tamper
        alarm_memory:
          id: zone_3_alarm_memory
          name: Zone 3 alarm memory
        tamper_memory:
          id: zone_3_tamper_memory
          name: Zone 3 tamper memory
      - zone: 32
"""
    )
    for variable, field, bit in [
        ("communication", "ONLINE", 0),
        ("mains_failure", "WARNINGS", 0),
        ("wireless_fault", "WARNINGS", 6),
        ("tamper_zone", "TAMPERS", 2),
        ("tamper_wireless", "TAMPERS", 7),
        ("zone_3", "ZONE_ACTIVE", 2),
        ("zone_3_tamper", "ZONE_TAMPER", 2),
        ("zone_3_alarm_memory", "ZONE_ALARM_MEMORY", 2),
        ("zone_3_tamper_memory", "ZONE_TAMPER_MEMORY", 2),
    ]:
        assert (
            f"new({variable}) kyo_alarm::KyoStatusBinarySensor(kyo_alarm::StatusField::{field}, {bit});"
            in generated.main
        )
        assert f"register_listener({variable})" in generated.calls("kyo")


def test_zones_once(esphome):
    output = esphome.invalid(
        """
kyo_alarm:

binary_sensor:
  - platform: kyo_alarm
    zones:
      - zone: 3
      - zone: 3
"""
    )
    assert "Zone 3 is configured more than once" in output


def test_zones_of_the_model(esphome, hub_module, monkeypatch):
    assert "value must be at most 32" in esphome.invalid(
        "kyo_alarm:\n\nswitch:\n  - platform: kyo_alarm\n    zones:\n      - zone: 33\n"
    )
    # A model with fewer zones than the largest one
    monkeypatch.setitem(hub_module.MODELS, "KYO32G", 8)
    output = esphome.invalid(
        "kyo_alarm:\n\nswitch:\n  - platform: kyo_alarm\n    zones:\n      - zone: 8\n      - zone: 9\n"
    )
    assert "Zone 9 out of range, KYO32G has 8 zones" in output


def test_switches(esphome):
    generated = esphome.generate(
        """
kyo_alarm:
  id: kyo

switch:
  - platform: kyo_alarm
    zones:
      - zone: 1
        bypass:
          id: bypass_1
          name: Zone 1 bypass
      - zone: 32
        bypass:
          id: bypass_32
          name: Zone 32 bypass
      - zone: 5
"""
    )
    assert "new(bypass_1) kyo_alarm::KyoZoneBypassSwitch(0);" in generated.main
    assert "new(bypass_32) kyo_alarm::KyoZoneBypassSwitch(31);" in generated.main
    assert "set_parent(kyo)" in generated.calls("bypass_1")
    # The state comes from the panel, never restored
    assert "set_restore_mode(switch_::SWITCH_RESTORE_DISABLED)" in generated.calls(
        "bypass_1"
    )
    assert "register_listener(bypass_32)" in generated.calls("kyo")


def test_buttons(esphome):
    generated = esphome.generate(
        TIME
        + """
kyo_alarm:
  id: kyo
  time_id: host_time

button:
  - platform: kyo_alarm
    reset_memory:
      id: reset_memory
      name: Reset memory
    sync_clock:
      id: sync_clock
      name: Sync clock
"""
    )
    assert "new(reset_memory) kyo_alarm::KyoResetMemoryButton();" in generated.main
    assert "new(sync_clock) kyo_alarm::KyoSyncClockButton();" in generated.main
    assert "set_parent(kyo)" in generated.calls("sync_clock")
    assert "register_listener(reset_memory)" in generated.calls("kyo")
    assert "register_listener(sync_clock)" in generated.calls("kyo")


def test_sync_clock_needs_a_time_source(esphome):
    esphome.valid(
        "kyo_alarm:\n\nbutton:\n  - platform: kyo_alarm\n    reset_memory:\n      name: Reset\n"
    )
    output = esphome.invalid(
        "kyo_alarm:\n\nbutton:\n  - platform: kyo_alarm\n    sync_clock:\n      name: Sync\n"
    )
    assert "'sync_clock' requires 'time_id' on the kyo_alarm hub" in output


def test_text_sensors(esphome):
    generated = esphome.generate(
        """
kyo_alarm:
  id: kyo

text_sensor:
  - platform: kyo_alarm
    panel_model:
      id: panel_model
      name: Panel model
    firmware_version:
      id: firmware_version
      name: Firmware version
    partition_status:
      id: partition_status
      name: Partition status
"""
    )
    assert generated.calls("kyo")[-3:] == [
        "set_panel_model_text_sensor(panel_model)",
        "set_firmware_version_text_sensor(firmware_version)",
        "set_partition_status_text_sensor(partition_status)",
    ]
    assert "USE_KYO_ALARM_EVENT_LOG" not in generated.defines


def test_last_event(esphome):
    generated = esphome.generate(
        """
kyo_alarm:
  id: kyo

text_sensor:
  - platform: kyo_alarm
    last_event:
      id: last_event
      name: Last event
"""
    )
    assert "set_last_event_text_sensor(last_event)" in generated.calls("kyo")
    assert "#define USE_KYO_ALARM_EVENT_LOG" in generated.defines
