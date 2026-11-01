# ESPHome Kyo Alarm External Component

Simple ESPHome external component for Bentel KYO 32G alarm panels connected over RS-232 (9600 8E1).
It is the successor of [esp-kyo-alarm-generic](https://github.com/lcavalli/esp-kyo-alarm-generic) compatibile with latest ESPHome releases.

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
  panel, bypass switches, buttons) do it through the hub. Each platform's code lives in its own
  directory and is only compiled when the platform is used.
- `kyo_protocol.*` holds the frame encoding, independent of ESPHome.

## Configuration

Requires ESPHome 2026.10.0 or later.

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
  language: en                # of the event descriptions, en or it

alarm_control_panel:
  - platform: kyo_alarm
    name: "Alarm"
    partitions:               # see Arming modes
      away: [1, 2, 3, 4]      # armed total, default: all partitions
      home: [1, 4]
      night:                  # by arming type
        partial_no_delay: 0x05  # bitmask, bit 0 is partition 1
        total: [4]
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
        sensor:
          name: "Front door"
          device_class: door
        alarm_memory:
          name: "Front door alarm memory"
        tamper:
          name: "Front door tamper"
        tamper_memory:
          name: "Front door tamper memory"
      - zone: 2
        sensor:
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
    sync_clock:
      name: "Sync panel clock"

text_sensor:
  - platform: kyo_alarm
    panel_model:
      name: "Panel model"
    firmware_version:
      name: "Firmware version"
    partition_status:
      name: "Partition status"   # see Configuring the arming modes
    last_event:
      name: "Last event"         # see Event log
```

Every entity accepts the full schema of its domain (`name`, `icon`, `internal`, filters, ...). With more than
one `kyo_alarm` hub, set `kyo_alarm_id` on each platform.

### Hub

| Key | Description |
| --- | --- |
| `model` | Panel model, currently `KYO32G`. |
| `update_interval` | Status polling interval, default `1s`. |
| `time_id` | Time source used to set the panel clock. |
| `clock_max_drift` | At every time sync the panel clock is read, and set only when it differs from the time source by more than this, default `30s`, from `5s` to `30min`. DST changes are corrected the same way; failed reads and writes are retried on the next time sync. |
| `language` | Language of the event descriptions, `en` (default) or `it`, the same for all hubs. Only the texts of this language are compiled in. |
| `on_event` | Automations run for each new event of the panel event log, see [Event log](#event-log). |

### Platforms

| Platform | Keys |
| --- | --- |
| `alarm_control_panel` | The panel entity, with `partitions` (`away`, `home`, `night`, `vacation`, see [Arming modes](#arming-modes)), `requires_code` and `requires_code_to_arm`. |
| `binary_sensor` | `communication` (off after 3 failed requests, see [Communication loss](#communication-loss)); warnings `mains_failure`, `bpi_missing`, `fuse_fault`, `low_battery`, `phone_line_fault`, `default_codes`, `wireless_fault`; tampers `tamper_zone`, `tamper_false_key`, `tamper_bpi`, `tamper_system`, `tamper_rf_jam`, `tamper_wireless`; `zones`, a list of `zone` with `sensor`, `tamper`, `alarm_memory`, `tamper_memory`. |
| `switch` | `zones`, a list of `zone` with `bypass`. |
| `button` | `reset_memory`, resets alarm and tamper memories; `sync_clock`, sets the panel clock from the time source now, regardless of `clock_max_drift` (requires `time_id` on the hub). |
| `text_sensor` | `panel_model`, `firmware_version`; `partition_status`, the partitions armed with each arming type, see [Configuring the arming modes](#configuring-the-arming-modes); `last_event`, the last event of the panel event log, see [Event log](#event-log). |

### Arming modes

Each arming mode of the alarm control panel (`away`, `home`, `night`, `vacation`) arms a set of partitions, each with
one of the arming types of the panel:

| Arming type | Key | Effect, as programmed in the panel |
| --- | --- | --- |
| Total | `total` | All the zones of the partition are armed. |
| Partial | `partial` | Zones programmed as internal are not armed. |
| Partial with no delay | `partial_no_delay` | As partial, and delayed zones trigger the alarm with no entry delay. |

A mode is a list of partition numbers or a bitmask (bit 0 is partition 1), armed total, or a map of partitions by
arming type:

```yaml
partitions:
  away: [1, 2, 3, 4]          # all total
  night:
    partial_no_delay: [1, 3]
    total: [4]
```

- Configure the modes as the arming modes programmed in the panel, so that arming from a keypad is reported as the
  right mode, see [Configuring the arming modes](#configuring-the-arming-modes).
- `away` arms all the partitions total unless configured. `home`, `night` and `vacation` are offered in Home
  Assistant only when configured.
- A partition can only have one arming type in a mode. Modes arming the same partitions with the same types cannot
  be told apart: the configuration shows a warning, and the panel is reported in the first of them (in the order
  `away`, `home`, `night`, `vacation`).
- A mode sets the whole panel: arming it arms its partitions with their arming type and, in the same command,
  disarms the armed partitions that are not part of it. With `away: [1, 2]` and `home: {partial: [1]}`, switching
  from away to home arms partition 1 partial and disarms partition 2.
- Partitions already armed with another type are switched to their type in the mode without being disarmed first.
  This has not been verified on a panel yet.
- Partitions configured in a mode but not present in the panel are ignored; a mode with none of them is refused.

### Configuring the arming modes

The keypad only shows whether each partition is armed, not its arming type, and the arming modes are programmed in
the panel by the installer. The `partition_status` text sensor shows what the panel reports, so that the modes can
be copied into the configuration:

```yaml
text_sensor:
  - platform: kyo_alarm
    partition_status:
      name: "Partition status"
```

Its state is the first four bytes of the status register (partitions armed total, partial and partial with no delay,
and disarmed partitions, bit 0 is partition 1), followed by the partitions of the panel armed with each arming type,
written as a mode of `partitions`:

| State | Meaning |
| --- | --- |
| `00 00 00 FF (disarmed)` | No partition armed. |
| `09 00 00 F6 (total: [1, 4])` | Partitions 1 and 4 armed total. |
| `08 00 05 F2 (total: [4], partial_no_delay: [1, 3])` | Partition 4 armed total, 1 and 3 partial with no delay. |

Arming the panel makes it operational: before each step, make sure the zones of the partitions being armed are at
rest and that nobody walks through them, and disarm as soon as the state has been noted. Then:

1. Add `partition_status` to the configuration, install it and check that, with the panel disarmed, the sensor shows
   `(disarmed)`.
2. Arm the panel from a keypad (or from the keys, remote controls or apps used every day) with each of its arming
   modes, e.g. the total arming and the modes of the keypad keys. For each one, once the panel is armed, note the
   state of the sensor, from Home Assistant or the device logs, then disarm the panel.
3. Assign each arming mode of the panel to a mode of the alarm control panel (`away`, `home`, `night`, `vacation`)
   and copy the noted partitions:

   | Arming mode of the panel | Sensor state | Configuration |
   | --- | --- | --- |
   | Total | `0F 00 00 F0 (total: [1, 2, 3, 4])` | `away: [1, 2, 3, 4]` |
   | Keypad key A | `09 00 00 F6 (total: [1, 4])` | `home: [1, 4]` |
   | Keypad key B | `08 00 05 F2 (total: [4], partial_no_delay: [1, 3])` | `night: {total: [4], partial_no_delay: [1, 3]}` |

   The text in parentheses is a valid mode between braces; when all the partitions are armed total, the list alone is
   enough.
4. Install the new configuration and repeat step 2: the alarm control panel must show the assigned mode for each
   arming mode of the panel, not `armed_custom_bypass`. Then arm each mode from Home Assistant, and check on the
   keypad and on the sensor that the panel is armed as with the keypad.

The sensor can be removed afterwards, or kept to see how the panel is armed at any time.

### Alarm control panel states

- `triggered` when any partition is in alarm;
- `disarmed` when no partition is armed;
- `armed_away` / `armed_home` / `armed_night` / `armed_vacation` when the partitions of the mode, and no others, are
  armed with their arming type;
- `armed_custom_bypass` when the panel is armed (e.g. from a keypad) with a combination not matching any mode;
- `arming` / `disarming` while a command is in progress;
- `pending` until the panel status has been read.

Zones can only be bypassed while the panel is disarmed.

### Communication loss

After 3 failed requests in a row the `communication` binary sensor turns off and the last panel status is no
longer trusted:

- the other binary sensors (zones, memories, warnings, tampers) become unknown;
- the alarm control panel and the bypass switches keep their last state (ESPHome cannot report them as
  unknown), but commands are refused until the status is read again;
- the hub keeps polling, starting again from the panel model and partitions, in case the panel was restarted or
  reprogrammed in the meantime.

Recovery is automatic: as soon as the panel answers, `communication` turns on and every entity is updated with
the new status.

### Event log

The panel logs its last 256 events, the log shown by the keypad: what happened, the partition, zone, user code or key
involved, and when. The hub reads the new events as the panel logs them, and makes each one available:

- in the `last_event` text sensor, e.g. `2026-11-01 11:59 Bypass Zone: Kitchen window`;
- to the `on_event` automations of the hub, as `event`:

| Field | Content |
| --- | --- |
| `event.code` | Event code, from the BIS-KYO documentation of the panel, e.g. `64`. |
| `event.description` | Description in the configured `language`, e.g. `Bypass Zone` (`Esclusione Zona` in Italian). |
| `event.number` | Number of the partition, zone, user code, key or failed call, e.g. `9`; `0` if the event has none. |
| `event.name` | Name stored in the panel for the partition, zone, user code or key, e.g. `Kitchen window`; empty if the event has none or the name could not be read. |
| `event.time` | Time of the event, by the panel clock, `YYYY-MM-DD hh:mm`. |

- At startup the text sensor shows the last event logged, without running the automations: the events logged while
  the ESP was off are not sent. Those logged while the communication with the panel was lost are sent once it answers
  again, as long as they are still in the log.
- Each command from Home Assistant is logged too, preceded by a `Remote Command` event. Panel clock settings are not
  logged.
- Names are read from the panel with each event; characters other than printable ASCII are shown as `?`.
- Home Assistant does not record a state equal to the previous one: two identical events in the same minute appear
  once in the history of the text sensor, while `on_event` runs for both.

To show the events in Home Assistant, add a *Logbook* card with the text sensor (use its entity id):

```yaml
type: logbook
title: Alarm events
target:
  entity_id: sensor.kyo_alarm_last_event
hours_to_show: 168
```

To use them in Home Assistant automations, send them as events with `homeassistant.event`. The device must be allowed
to perform Home Assistant actions: enable it in *Settings → Devices & services → ESPHome*, *Configure* on the device.

```yaml
kyo_alarm:
  on_event:
    - homeassistant.event:
        event: esphome.kyo_alarm_event
        data:
          code: !lambda return event.code;
          description: !lambda return event.description;
          number: !lambda return event.number;
          name: !lambda return event.name;
          time: !lambda return event.time;
```

Then, e.g., a notification for each event:

```yaml
automation:
  - alias: Alarm event notification
    triggers:
      - trigger: event
        event_type: esphome.kyo_alarm_event
    actions:
      - action: notify.notify
        data:
          message: "{{ trigger.event.data.description }} {{ trigger.event.data.name }}"
```

### Raw reads (diagnostics)

The `kyo_alarm.read_raw` action sends a read request written in hex and waits for the reply, to explore the panel
memory from Home Assistant. Expose it as a Home Assistant action that returns the reply:

```yaml
api:
  actions:
    - action: read_raw
      variables:
        request: string
      then:
        - kyo_alarm.read_raw:
            id: kyo
            request: !lambda return request;
        - api.respond:
            success: !lambda return id(kyo).get_raw_error().empty();
            error_message: !lambda return id(kyo).get_raw_error();
            data: !lambda root["reply"] = id(kyo).get_raw_reply();
```

Then call it from *Developer tools → Actions* (`esphome.<device>_read_raw`), e.g. with
`request: "F0 00 00 0B 00"` to read the 12 bytes of panel model and firmware at `0x0000`. The request is the 5 header bytes
(`F0 <address lo> <address hi> <size - 1> 00`), with or without the checksum, as pairs of hex digits optionally
separated by spaces, commas, colons or dashes (`0x` prefixes are accepted). The reply holds the data and the
checksum sent by the panel, e.g. `{"reply": "4B 59 4F 33 32 47 20 20 32 2E 31 33 A3"}` (`KYO32G  2.13`).

Only reads of at most 64 bytes are allowed; writes and reads of the PIN table are refused, and one raw read
runs at a time. Failures (refused request, no reply, checksum mismatch) are returned as errors and logged
with the bytes received.

## Development

ESPHome and the tools of the CI checks, at the same versions, in a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/pip install -c .github/requirements-ci.txt esphome clang-format ruff
```

Then, with `.venv/bin` in the `PATH` (`source .venv/bin/activate`):

```sh
esphome compile kyo-alarm-example.yaml   # needs a secrets.yaml next to it, not in the repository
git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror
ruff format --check . && ruff check .
```

## Comparison with ESPKyoGate

[ESPKyoGate](https://github.com/lorenzo-deluca/espkyogate) by Lorenzo De Luca is another awesome ESPHome component for
Bentel KYO panels. The main differences:

| | This component | ESPKyoGate |
| --- | --- | --- |
| Panel models | KYO32G. | KYO32G, KYO32 (non-G), KYO8G, KYO8W, KYO8, KYO4. |
| Alarm control panel | One for the whole panel. Each arming mode arms a set of partitions, each with its own arming type, matching the arming modes programmed in the panel. | One for each partition, with fixed arming types (away is total, home is partial, night is partial with no delay), and buttons that arm several partitions together. |
| Codes | Checked against the PINs stored in the panel. | A list in the YAML configuration. |
| Clock | Kept in sync automatically, set only when it drifts; also from a button. | Set from a button. |
| Zone bypass | A switch for each zone. | Include and exclude buttons, and a bypass binary sensor. |
| Outputs | No. | State, activation and pulse. |
| Siren state | No. | Yes. |
| Event log | New events sent to Home Assistant as they are logged, the last one in a text sensor. | Read from a button and written to the ESPHome logs. |
| Programming data | No. | Zone and output names, zone types and partitions, entry and exit delays and siren timer of each partition. |
| Polling | Always on. | A switch stops it, to free the serial port. |

Use the one that best fits your needs!
