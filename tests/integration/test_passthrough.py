"""The passthrough: the panel UART forwarded to a TCP client, as for the programming software of the panel."""

import asyncio
from collections.abc import AsyncIterator
from contextlib import asynccontextmanager

from aioesphomeapi import AlarmControlPanelCommand, AlarmControlPanelState
from conftest import TIMEOUT, Device, Firmware, running
from panel import HEADER_SIZE, OP_READ, REG_PANEL_INFO, checksum
import pytest

PANEL_INFO = b"KYO32G  2.13"


def read_request(address: int, size: int) -> bytes:
    header = bytes([OP_READ, address & 0xFF, address >> 8, size - 1, 0])
    return header + bytes([checksum(header)])


@asynccontextmanager
async def client(
    firmware: Firmware,
) -> AsyncIterator[tuple[asyncio.StreamReader, asyncio.StreamWriter]]:
    """A connection to the passthrough port, as the programming software through a virtual serial port."""
    reader, writer = await asyncio.open_connection(
        "127.0.0.1", firmware.passthrough_port
    )
    try:
        yield reader, writer
    finally:
        writer.close()


async def passthrough_on(device: Device) -> None:
    device.switch("Passthrough", True)
    await device.wait_state("Passthrough", True)
    # The status is unknown as for a lost communication, the alarm keeps its last state
    await device.wait_state("Communication", False)
    await device.wait_state("Zone 1", None)
    assert device.state("Alarm") == AlarmControlPanelState.DISARMED
    # The panel ignores a request sent right after the reply to the last poll
    await asyncio.sleep(0.1)


async def test_forwards_the_panel_uart(firmware: Firmware) -> None:
    async with (
        running(firmware) as (panel, device),
        client(firmware) as (reader, writer),
    ):
        await device.wait_state("Alarm", AlarmControlPanelState.DISARMED)
        await passthrough_on(device)
        reads = len(panel.reads)

        request = read_request(REG_PANEL_INFO, len(PANEL_INFO))
        writer.write(request)
        reply = await asyncio.wait_for(
            reader.readexactly(HEADER_SIZE + len(PANEL_INFO) + 1), TIMEOUT
        )
        assert reply == request + PANEL_INFO + bytes([checksum(PANEL_INFO)])
        # The traffic is logged, a frame per line
        await device.wait(
            lambda: (
                "Passthrough client > panel: F0 00 00 0B 00 FB" in "\n".join(device.log)
            )
        )
        await device.wait(
            lambda: (
                "Passthrough panel > client: F0 00 00 0B 00 FB 4B 59 4F 33 32 47 20 20 32 2E 31 33 A3"
                in "\n".join(device.log)
            )
        )

        # The component does not poll, and refuses commands
        device.alarm(AlarmControlPanelCommand.ARM_AWAY, "1234")
        await asyncio.sleep(1.5)
        assert panel.reads[reads:] == [REG_PANEL_INFO]
        assert panel.writes == []

        device.switch("Passthrough", False)
        await device.wait_state("Passthrough", False)
        # Panel model and partitions read again, then the status
        await device.wait_state("Communication", True)
        await device.wait_state("Zone 1", False)
        assert panel.reads[reads + 1] == REG_PANEL_INFO


async def test_drops_the_client_bytes_when_off(firmware: Firmware) -> None:
    async with (
        running(firmware) as (panel, device),
        client(firmware) as (reader, writer),
    ):
        await device.wait_state("Passthrough", False)
        await device.wait_state("Communication", True)

        writer.write(read_request(REG_PANEL_INFO, len(PANEL_INFO)))
        # No reply: the request did not reach the panel, which read the panel info only at boot
        with pytest.raises(TimeoutError):
            await asyncio.wait_for(reader.read(1), 1.5)
        assert panel.reads.count(REG_PANEL_INFO) == 1
