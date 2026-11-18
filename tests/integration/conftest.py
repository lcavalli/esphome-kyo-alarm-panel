"""Runs the firmware of kyo-host.yaml on the host against the simulated panel of panel.py, and reads and controls its
entities through the native API, as Home Assistant does.

The firmware is compiled once per session; each test starts it again, and runs as a coroutine in an event loop of its
own.
"""

from __future__ import annotations

import asyncio
from collections.abc import AsyncIterator, Callable
from contextlib import asynccontextmanager, suppress
from dataclasses import dataclass
import inspect
from pathlib import Path
import re
import socket
import subprocess
import sys
from typing import Any

from aioesphomeapi import (
    AlarmControlPanelCommand,
    APIClient,
    APIConnectionError,
    EntityState,
)
from panel import Panel
import pytest

CONFIG = Path(__file__).parent / "kyo-host.yaml"
PROGRAM = (
    CONFIG.parent
    / ".esphome"
    / "build"
    / "kyo-integration"
    / ".pioenvs"
    / "kyo-integration"
    / "program"
)

# Longest wait for a state: the component reports the panel offline after three failed polls, one per second
TIMEOUT = 10.0
# Lines of the firmware log shown when a wait fails
LOG_TAIL = 40

ANSI_ESCAPE = re.compile(r"\x1b\[[0-9;]*m")


@pytest.hookimpl(tryfirst=True)
def pytest_pyfunc_call(pyfuncitem: pytest.Function) -> bool | None:
    """Run each async test in an event loop of its own."""
    if not inspect.iscoroutinefunction(pyfuncitem.obj):
        return None
    parameters = inspect.signature(pyfuncitem.obj).parameters
    asyncio.run(
        pyfuncitem.obj(**{name: pyfuncitem.funcargs[name] for name in parameters})
    )
    return True


@dataclass(frozen=True)
class Firmware:
    api_port: int
    panel_port: int


def free_ports(count: int) -> list[int]:
    """Local TCP ports free at the moment, all different."""
    sockets = [socket.socket() for _ in range(count)]
    try:
        for sock in sockets:
            sock.bind(("127.0.0.1", 0))
        return [sock.getsockname()[1] for sock in sockets]
    finally:
        for sock in sockets:
            sock.close()


@pytest.fixture(scope="session")
def firmware() -> Firmware:
    """The firmware, compiled with free ports: only its main.cpp is compiled again in the next sessions."""
    api_port, panel_port = free_ports(2)
    subprocess.run(
        [
            sys.executable,
            "-m",
            "esphome",
            "-s",
            "api_port",
            str(api_port),
            "-s",
            "panel_port",
            str(panel_port),
            "compile",
            str(CONFIG),
        ],
        check=True,
    )
    return Firmware(api_port, panel_port)


class Device:
    """The firmware running, with the states of its entities as the native API reports them."""

    def __init__(self, firmware: Firmware) -> None:
        self._firmware = firmware
        self._client = APIClient("127.0.0.1", firmware.api_port, None)
        self._process: asyncio.subprocess.Process | None = None
        self._log_task: asyncio.Task[None] | None = None
        self._keys: dict[str, int] = {}
        self._names: dict[int, str] = {}
        self._changed = asyncio.Event()
        self.states: dict[str, EntityState] = {}
        self.log: list[str] = []

    async def start(self) -> None:
        self._process = await asyncio.create_subprocess_exec(
            PROGRAM, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.STDOUT
        )
        self._log_task = asyncio.create_task(self._read_log())
        # The API server listens as soon as the firmware has started
        async with asyncio.timeout(TIMEOUT):
            while True:
                try:
                    await self._client.connect(login=True, log_errors=False)
                    break
                except APIConnectionError:
                    await asyncio.sleep(0.1)
        entities, _ = await self._client.list_entities_services()
        self._keys = {entity.name: entity.key for entity in entities}
        self._names = {entity.key: entity.name for entity in entities}
        self._client.subscribe_states(self._on_state)

    async def stop(self) -> None:
        await self._client.disconnect()
        if self._process is not None:
            self._process.terminate()
            await self._process.wait()
        if self._log_task is not None:
            await self._log_task

    async def _read_log(self) -> None:
        assert self._process is not None
        assert self._process.stdout is not None
        async for line in self._process.stdout:
            self.log.append(ANSI_ESCAPE.sub("", line.decode(errors="replace").rstrip()))

    def _on_state(self, state: EntityState) -> None:
        self.states[self._names[state.key]] = state
        self._changed.set()

    def state(self, name: str) -> Any:
        """The state of an entity: its value, or None while unknown."""
        state = self.states.get(name)
        if state is None or getattr(state, "missing_state", False):
            return None
        return state.state

    async def wait_state(self, name: str, expected: Any) -> None:
        """Wait until the state of an entity is `expected` (None for unknown)."""
        await self.wait(lambda: self.state(name) == expected and name in self.states)

    async def wait(self, condition: Callable[[], bool]) -> None:
        """Wait until `condition` is true, checking it when a state changes or every 50 ms (for the panel)."""
        try:
            async with asyncio.timeout(TIMEOUT):
                while not condition():
                    self._changed.clear()
                    with suppress(TimeoutError):
                        await asyncio.wait_for(self._changed.wait(), 0.05)
        except TimeoutError:
            states = "\n".join(
                f"  {name}: {self.state(name)!r}" for name in sorted(self.states)
            )
            log = "\n".join(self.log[-LOG_TAIL:])
            raise AssertionError(
                f"Timed out after {TIMEOUT} s. States:\n{states}\nFirmware log:\n{log}"
            ) from None

    def alarm(self, command: AlarmControlPanelCommand, code: str | None = None) -> None:
        self._client.alarm_control_panel_command(self._keys["Alarm"], command, code)

    def switch(self, name: str, state: bool) -> None:
        self._client.switch_command(self._keys[name], state)


@asynccontextmanager
async def running(
    firmware: Firmware, *, panel_on: bool = True
) -> AsyncIterator[tuple[Panel, Device]]:
    """The firmware running against the simulated panel, switched on or not."""
    panel = Panel(firmware.panel_port)
    if panel_on:
        await panel.start()
    device = Device(firmware)
    try:
        await device.start()
        yield panel, device
    finally:
        await device.stop()
        await panel.stop()
    # Every request was well formed and waited after the previous reply
    assert panel.invalid_requests == 0
    assert panel.early_requests == 0
