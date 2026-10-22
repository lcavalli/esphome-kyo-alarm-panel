# ESPHome Kyo Alarm External Component

ESPHome external component for Bentel KYO alarm panels connected over RS-232 (9600 8E1).
It is the successor of [esp-kyo-alarm-generic](https://github.com/lcavalli/esp-kyo-alarm-generic).

*This project is not related to or endorsed by the KYO alarm manufacturer. Any intervention on the alarm
system must be performed by qualified personnel. The software is provided "as is", without warranty of any kind.*

The panel is exposed as a single Home Assistant alarm control panel; zone sensors, alarm memories and
bypass switches are generated from the configuration.

The code was developed with the help of [Claude Code](https://claude.com/claude-code),
following the design and directions of the author.

## Architecture

- `KyoAlarmComponent` (hub) owns the UART link. Requests go through a non-blocking queue processed in
  `loop()`; status is polled every `update_interval`.
- Entities register with the hub as `KyoAlarmListener`s and receive every status update. Entities that act
  on the panel (alarm control panel, bypass switches, reset button) do it through the hub.
- `kyo_protocol.*` holds the frame encoding, independent of ESPHome.

## Configuration

```yaml
uart:
  tx_pin: GPIO5
  rx_pin: GPIO4
  baud_rate: 9600
  data_bits: 8
  parity: EVEN

external_components:
  - source: github://lcavalli/kyo-alarm-external-component
    components: [kyo_alarm]

kyo_alarm:
  model: KYO32G
  time_id: ha_time            # optional, keeps the panel clock in sync

  alarm_control_panel:
    name: "Alarm"
    partitions:
      away: [1, 2, 3, 4]      # default: all partitions
      home: [1, 4]
      night: 0x0d             # bitmask, bit 0 is partition 1
      # vacation: [...]
    requires_code: true       # codes are checked against the PINs stored in the panel
    requires_code_to_arm: true

  zones:
    - zone: 1
      name: "Front door"
      device_class: door
      alarm_memory: true      # binary sensor "Front door alarm memory"
      tamper: true            # binary sensor "Front door tamper"
      tamper_memory: true     # binary sensor "Front door tamper memory"
      bypass: true            # switch "Front door bypass", on when the zone is excluded
    - zone: 2
      name: "Dining room"
      device_class: motion
      bypass:
        name: "Dining room excluded"
```

Every optional entity accepts `true` (entity with a default name) or the full entity schema
(`name`, `icon`, `internal`, filters, ...).

### Hub options

| Key | Description |
| --- | --- |
| `model` | Panel model, currently `KYO32G`. |
| `update_interval` | Status polling interval, default `1s`. |
| `time_id` | Time source used to set the panel clock on every time sync. |
| `alarm_control_panel` | The panel entity. Arming modes without partitions are not offered. |
| `panel_model`, `firmware_version` | Diagnostic text sensors. |
| `communication` | Connectivity binary sensor, off after 3 failed requests. |
| `reset_memory` | Button that resets alarm and tamper memories. |
| `warnings` | `mains_failure`, `bpi_missing`, `fuse_fault`, `low_battery`, `phone_line_fault`, `default_codes`, `wireless_fault`. |
| `tampers` | `zone`, `false_key`, `bpi`, `system`, `rf_jam`, `wireless`. |
| `zones` | List of zones, see above. |

### Alarm control panel states

- `triggered` when any partition is in alarm;
- `disarmed` when no partition is armed;
- `armed_away` / `armed_home` / `armed_night` / `armed_vacation` when the armed partitions match the mode;
- `armed_custom_bypass` when the panel is armed (e.g. from a keypad) with a combination not matching any mode;
- `arming` / `disarming` while a command is in progress;
- `pending` until the panel status has been read.

Zones can only be bypassed while the panel is disarmed.
