"""A simulated KYO32G panel: answers the requests of the component on a TCP connection, from its memory."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
import time

# The protocol, see components/kyo_alarm/kyo_protocol.h
HEADER_SIZE = 6
OP_READ = 0xF0
OP_WRITE = 0x0F
OP_CLOSE = 0x3C

REG_PANEL_INFO = 0x0000
REG_PINS_1 = 0x01B4
REG_PINS_2 = 0x01F4
REG_PARTITIONS = 0x01FF
REG_STATUS = 0x1500
REG_CTRL_PARTITIONS = 0xF000
REG_ZONE_BYPASS = 0xF001
REG_DATETIME = 0xF003
REG_REALTIME = 0xF004
REG_RESET = 0xF005

# Offsets in the status register
ST_ARMED_TOTAL = 2
ST_ARMED_PARTIAL = 3
ST_ARMED_PARTIAL_NO_DELAY = 4
ST_DISARMED = 5
ST_ZONE_BYPASS = 9
ST_ZONE_ALARM_MEMORY = 13
ST_END = 21

# The panel ignores a request sent right after its previous reply, the component waits 50 ms
MIN_REQUEST_GAP = 0.02


def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def zones_to_bytes(zones: int) -> bytes:
    """A zone bitmap (bit 0 is zone 1) as the panel stores it."""
    return zones.to_bytes(4, "big")


@dataclass(frozen=True)
class Write:
    """A write received by the panel."""

    address: int
    data: bytes


class Panel:
    """A KYO32G with partitions 1 to 4 and PIN 1234, disarmed, listening on a local TCP port once started."""

    def __init__(self, port: int) -> None:
        self.port = port
        self.memory = bytearray(0x10000)
        self.memory[REG_PANEL_INFO : REG_PANEL_INFO + 12] = b"KYO32G  2.13"
        self.memory[REG_PARTITIONS + 1] = 0x0F
        self.memory[REG_PINS_1 : REG_PINS_1 + 3] = bytes([0x12, 0x34, 0xFF])
        # The registers above 0xf000 are not memory
        self.realtime = bytearray(11)
        self.clock = bytearray([1, 1, 26, 0, 0, 0])
        self.set_armed(0)
        # False: the requests get no reply, as with a broken cable
        self.answering = True
        # Addresses read, writes and closes received
        self.reads: list[int] = []
        self.writes: list[Write] = []
        self.closes = 0
        # Protocol errors of the component: a wrong header checksum, a request right after the previous reply
        self.invalid_requests = 0
        self.early_requests = 0
        self._server: asyncio.Server | None = None
        self._connections: set[asyncio.StreamWriter] = set()
        self._last_reply = 0.0

    async def start(self) -> None:
        self._server = await asyncio.start_server(self._serve, "127.0.0.1", self.port)

    async def stop(self) -> None:
        """Close the port and the connection, as a panel switched off."""
        if self._server is None:
            return
        self._server.close()
        for writer in self._connections:
            writer.close()
        await self._server.wait_closed()
        self._server = None

    def set_armed(
        self, total: int, partial: int = 0, partial_no_delay: int = 0
    ) -> None:
        """Partitions armed with each arming type, as bitmasks (bit 0 is partition 1)."""
        status = REG_STATUS + ST_ARMED_TOTAL
        armed = total | partial | partial_no_delay
        self.memory[status : status + 4] = bytes(
            [total, partial, partial_no_delay, ~armed & 0xFF]
        )

    def set_zones_active(self, zones: int) -> None:
        """The zones whose sensor is active, as a bitmap (bit 0 is zone 1)."""
        self.realtime[0:4] = zones_to_bytes(zones)

    @property
    def zone_bypass(self) -> int:
        start = REG_STATUS + ST_ZONE_BYPASS
        return int.from_bytes(self.memory[start : start + 4], "big")

    async def _serve(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        self._connections.add(writer)
        try:
            while True:
                header = await reader.readexactly(HEADER_SIZE)
                op, address, size = header[0], header[1] | header[2] << 8, header[3] + 1
                data = await reader.readexactly(size + 1) if op == OP_WRITE else b""
                if checksum(header[:-1]) != header[-1] or (
                    data and checksum(data[:-1]) != data[-1]
                ):
                    self.invalid_requests += 1
                    continue
                if time.monotonic() - self._last_reply < MIN_REQUEST_GAP:
                    self.early_requests += 1
                    continue
                if not self.answering:
                    continue
                # The header echo, then the data of a read
                writer.write(header)
                if op == OP_READ:
                    self.reads.append(address)
                    reply = self._read(address, size)
                    writer.write(reply + bytes([checksum(reply)]))
                elif op == OP_WRITE:
                    self._write(address, data[:-1])
                elif op == OP_CLOSE:
                    self.closes += 1
                await writer.drain()
                self._last_reply = time.monotonic()
        except (asyncio.IncompleteReadError, ConnectionError):
            pass
        finally:
            self._connections.discard(writer)
            writer.close()

    def _read(self, address: int, size: int) -> bytes:
        if address == REG_REALTIME:
            return bytes(self.realtime[:size])
        if address == REG_DATETIME:
            return bytes(self.clock[:size])
        return bytes(self.memory[address : address + size])

    def _write(self, address: int, data: bytes) -> None:
        self.writes.append(Write(address, data))
        status = REG_STATUS
        if address == REG_CTRL_PARTITIONS:
            # Partitions to arm with each type, then partitions to disarm: an armed partition keeps its other bits
            total, partial, partial_no_delay, disarm = data
            arm = total | partial | partial_no_delay
            current = self.memory[status + ST_ARMED_TOTAL : status + ST_DISARMED]
            keep = ~(arm | disarm) & 0xFF
            self.set_armed(
                (current[0] & keep) | total,
                (current[1] & keep) | partial,
                (current[2] & keep) | partial_no_delay,
            )
        elif address == REG_ZONE_BYPASS:
            bypass = int.from_bytes(data[0:4], "big")
            restore = int.from_bytes(data[4:8], "big")
            zones = (self.zone_bypass | bypass) & ~restore
            start = status + ST_ZONE_BYPASS
            self.memory[start : start + 4] = zones_to_bytes(zones)
        elif address == REG_RESET:
            # Alarm and tamper memories
            self.memory[status + ST_ZONE_ALARM_MEMORY : status + ST_END] = bytes(
                ST_END - ST_ZONE_ALARM_MEMORY
            )
        elif address == REG_DATETIME:
            self.clock[:] = data
