"""Commands from Home Assistant, and the zones."""

from aioesphomeapi import AlarmControlPanelCommand, AlarmControlPanelState
from conftest import Firmware, running
from panel import REG_CTRL_PARTITIONS, REG_PINS_2, REG_ZONE_BYPASS, Write


async def test_arms_and_disarms_with_a_panel_pin(firmware: Firmware) -> None:
    async with running(firmware) as (panel, device):
        await device.wait_state("Alarm", AlarmControlPanelState.DISARMED)

        # A wrong code is checked against the PINs of the panel, and nothing is written
        device.alarm(AlarmControlPanelCommand.ARM_AWAY, "9999")
        await device.wait(lambda: REG_PINS_2 in panel.reads)
        assert panel.writes == []
        assert device.state("Alarm") == AlarmControlPanelState.DISARMED

        device.alarm(AlarmControlPanelCommand.ARM_AWAY, "1234")
        await device.wait_state("Alarm", AlarmControlPanelState.ARMED_AWAY)
        # Partitions 1 and 2 armed total, the write session closed
        assert panel.writes == [Write(REG_CTRL_PARTITIONS, bytes([0x03, 0, 0, 0]))]
        assert panel.closes == 1

        device.alarm(AlarmControlPanelCommand.DISARM, "1234")
        await device.wait_state("Alarm", AlarmControlPanelState.DISARMED)

        device.alarm(AlarmControlPanelCommand.ARM_HOME, "1234")
        await device.wait_state("Alarm", AlarmControlPanelState.ARMED_HOME)


async def test_zone_sensor(firmware: Firmware) -> None:
    async with running(firmware) as (panel, device):
        await device.wait_state("Zone 1", False)
        panel.set_zones_active(0x01)
        await device.wait_state("Zone 1", True)
        panel.set_zones_active(0)
        await device.wait_state("Zone 1", False)


async def test_bypasses_a_zone(firmware: Firmware) -> None:
    async with running(firmware) as (panel, device):
        await device.wait_state("Zone 1 bypass", False)

        device.switch("Zone 1 bypass", True)
        await device.wait_state("Zone 1 bypass", True)
        assert panel.zone_bypass == 0x01
        assert panel.writes == [
            Write(REG_ZONE_BYPASS, bytes([0, 0, 0, 0x01, 0, 0, 0, 0]))
        ]

        device.switch("Zone 1 bypass", False)
        await device.wait_state("Zone 1 bypass", False)
        assert panel.zone_bypass == 0
