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

- `KyoAlarmComponent` (hub, `kyo_alarm:`) owns the UART link. Requests go through a non-blocking queue
  processed in `loop()`; status is polled every `update_interval`.
- Entities are declared with the `kyo_alarm` platform of each entity domain (`alarm_control_panel`,
  `binary_sensor`, `switch`, `button`, `text_sensor`), as in ESPHome core hub components. They register with
  the hub as `KyoAlarmListener`s and receive every status update; those that act on the panel (alarm control
  panel, bypass switches, reset button) do it through the hub. Each platform's code lives in its own
  directory and is only compiled when the platform is used.
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
  - source: github://lcavalli/esphome-kyo-alarm-panel
    components: [kyo_alarm]

kyo_alarm:
  id: kyo
  model: KYO32G
  time_id: ha_time            # optional, keeps the panel clock in sync

alarm_control_panel:
  - platform: kyo_alarm
    name: "Alarm"
    partitions:
      away: [1, 2, 3, 4]      # default: all partitions
      home: [1, 4]
      night: 0x0d             # bitmask, bit 0 is partition 1
      # vacation: [...]
    requires_code: true       # codes are checked against the PINs stored in the panel
    requires_code_to_arm: true

binary_sensor:
  - platform: kyo_alarm
    communication:
      name: "Communication"
    low_battery:
      name: "Low battery"
    zones:
      - zone: 1
        open:
          name: "Front door"
          device_class: door
        alarm_memory:
          name: "Front door alarm memory"
        tamper:
          name: "Front door tamper"
        tamper_memory:
          name: "Front door tamper memory"
      - zone: 2
        open:
          name: "Dining room"
          device_class: motion

switch:
  - platform: kyo_alarm
    zones:
      - zone: 1
        bypass:
          name: "Front door bypass"   # on when the zone is excluded

button:
  - platform: kyo_alarm
    reset_memory:
      name: "Reset alarm memory"

text_sensor:
  - platform: kyo_alarm
    panel_model:
      name: "Panel model"
    firmware_version:
      name: "Firmware version"
```

Every entity accepts the full schema of its domain (`name`, `icon`, `internal`, filters, ...). With more than
one `kyo_alarm` hub, set `kyo_alarm_id` on each platform.

### Hub

| Key | Description |
| --- | --- |
| `model` | Panel model, currently `KYO32G`. |
| `update_interval` | Status polling interval, default `1s`. |
| `time_id` | Time source used to set the panel clock on every time sync. |

### Platforms

| Platform | Keys |
| --- | --- |
| `alarm_control_panel` | The panel entity, with `partitions` (`away`, `home`, `night`, `vacation`), `requires_code` and `requires_code_to_arm`. Arming modes without partitions are not offered. |
| `binary_sensor` | `communication` (off after 3 failed requests); warnings `mains_failure`, `bpi_missing`, `fuse_fault`, `low_battery`, `phone_line_fault`, `default_codes`, `wireless_fault`; tampers `tamper_zone`, `tamper_false_key`, `tamper_bpi`, `tamper_system`, `tamper_rf_jam`, `tamper_wireless`; `zones`, a list of `zone` with `open`, `tamper`, `alarm_memory`, `tamper_memory`. |
| `switch` | `zones`, a list of `zone` with `bypass`. |
| `button` | `reset_memory`, resets alarm and tamper memories. |
| `text_sensor` | `panel_model`, `firmware_version`. |

### Alarm control panel states

- `triggered` when any partition is in alarm;
- `disarmed` when no partition is armed;
- `armed_away` / `armed_home` / `armed_night` / `armed_vacation` when the armed partitions match the mode;
- `armed_custom_bypass` when the panel is armed (e.g. from a keypad) with a combination not matching any mode;
- `arming` / `disarming` while a command is in progress;
- `pending` until the panel status has been read.

Zones can only be bypassed while the panel is disarmed.
