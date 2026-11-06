"""The alarm control panel platform: arming modes and codes."""

ACP = """
kyo_alarm:
  id: kyo

alarm_control_panel:
  - platform: kyo_alarm
    id: acp
    name: Alarm
"""


def test_defaults(esphome):
    generated = esphome.generate(ACP)
    assert generated.calls("acp") == [
        "set_parent(kyo)",
        "set_away_partitions(0xFF, 0, 0)",
        "set_requires_code(true)",
        "set_requires_code_to_arm(true)",
    ]
    assert "register_listener(acp)" in generated.calls("kyo")


def test_arming_modes(esphome):
    generated = esphome.generate(
        ACP
        + """
    partitions:
      away: [1, 2, 3, 4]
      home: 0x09
      night:
        total: [1, 4]
        partial_no_delay: 0x04
      vacation:
        partial: [2]
    requires_code: false
    requires_code_to_arm: false
"""
    )
    assert generated.calls("acp") == [
        "set_parent(kyo)",
        # Lists are written in decimal, hex values as given
        "set_away_partitions(15, 0, 0)",
        "set_home_partitions(0x09, 0, 0)",
        "set_night_partitions(9, 0, 0x04)",
        "set_vacation_partitions(0, 2, 0)",
        "set_requires_code(false)",
        "set_requires_code_to_arm(false)",
    ]


def test_invalid_partitions(esphome):
    assert "value must be at most 8" in esphome.invalid(
        ACP + "    partitions:\n      home: [1, 9]\n"
    )
    assert (
        "Must contain at least one of total, partial, partial_no_delay"
        in esphome.invalid(ACP + "    partitions:\n      home: {}\n")
    )
    assert (
        "Partitions 1, 4 are armed with more than one arming type"
        in esphome.invalid(
            ACP
            + "    partitions:\n      home:\n        total: [1, 2, 4]\n        partial: [1, 4]\n"
        )
    )


def test_modes_the_panel_cannot_tell_apart(esphome):
    output = esphome.valid(
        ACP
        + """
    partitions:
      away: [1, 2]
      home: []
      night: 0x03
"""
    )
    assert (
        "'night' arms the same partitions as 'away' with the same arming types, it will be reported as 'away'"
        in output
    )
    assert "'home'" not in output
