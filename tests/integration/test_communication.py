"""The communication with the panel: missing at boot, lost and found again."""

from aioesphomeapi import AlarmControlPanelState
from conftest import Firmware, running


async def test_panel_switched_off_at_boot(firmware: Firmware) -> None:
    async with running(firmware, panel_on=False) as (panel, device):
        await device.wait_state("Communication", False)
        # Until the panel status is read
        assert device.state("Alarm") == AlarmControlPanelState.PENDING
        assert device.state("Zone 1") is None
        assert "Panel not responding" in "\n".join(device.log)

        await panel.start()
        await device.wait_state("Communication", True)
        await device.wait_state("Alarm", AlarmControlPanelState.DISARMED)
        await device.wait_state("Zone 1", False)


async def test_panel_not_answering_at_boot(firmware: Firmware) -> None:
    async with running(firmware) as (panel, device):
        panel.answering = False
        await device.wait_state("Communication", False)
        assert device.state("Alarm") == AlarmControlPanelState.PENDING

        panel.answering = True
        await device.wait_state("Communication", True)
        await device.wait_state("Alarm", AlarmControlPanelState.DISARMED)


async def test_panel_lost_and_found(firmware: Firmware) -> None:
    async with running(firmware) as (panel, device):
        panel.set_armed(0x03)
        await device.wait_state("Alarm", AlarmControlPanelState.ARMED_AWAY)
        await device.wait_state("Zone 1", False)

        await panel.stop()
        await device.wait_state("Communication", False)
        # The alarm keeps its last state, the zones become unknown
        await device.wait_state("Zone 1", None)
        assert device.state("Alarm") == AlarmControlPanelState.ARMED_AWAY

        # Disarmed while the communication was lost
        panel.set_armed(0)
        await panel.start()
        await device.wait_state("Communication", True)
        await device.wait_state("Alarm", AlarmControlPanelState.DISARMED)
        await device.wait_state("Zone 1", False)
