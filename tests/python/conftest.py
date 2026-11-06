"""Runs ESPHome in the test process on a configuration of the component, to check its validation and code generation."""

from dataclasses import dataclass
from pathlib import Path
import re
import sys
from types import ModuleType

import pytest

from esphome.__main__ import run_esphome
from esphome.core import CORE

COMPONENTS = Path(__file__).parents[2] / "components"

# A host device, the component is loaded from the repository
BASE = f"""
esphome:
  name: test

host:

logger:

external_components:
  - source: {COMPONENTS}
"""

# The UART of the panel
UART = """
uart:
  - id: uart_bus
    port: /dev/null
    baud_rate: 9600
    parity: EVEN
"""

# Two panels
TWO_UARTS = (
    UART
    + """
  - id: uart_2
    port: /dev/null
    baud_rate: 9600
    parity: EVEN
"""
)

# A UART without the settings of the panel
WRONG_UART = """
uart:
  - id: uart_bus
    port: /dev/null
    baud_rate: 19200
"""

# A time source, needed by the clock sync
TIME = """
time:
  - platform: host
    id: host_time
"""


@dataclass
class Generated:
    """The code generated from a configuration."""

    main: str
    defines: str

    def calls(self, variable: str) -> list[str]:
        """The statements of main.cpp that call a method of `variable`, e.g. ['set_model("KYO32G", 32)']."""
        return re.findall(rf"^  {re.escape(variable)}->(.*);$", self.main, re.MULTILINE)


class ESPHome:
    def __init__(
        self,
        path: Path,
        capfd: pytest.CaptureFixture[str],
        caplog: pytest.LogCaptureFixture,
    ) -> None:
        self._path = path
        self._capfd = capfd
        self._caplog = caplog

    def _run(self, body: str, uart: str, *command: str) -> tuple[int, str]:
        config = self._path / "test.yaml"
        config.write_text(BASE + uart + body, encoding="utf-8")
        self._capfd.readouterr()
        self._caplog.clear()
        # Each run starts from scratch, as in a new process
        CORE.reset()
        result = run_esphome(["esphome", *command, str(config)])
        # The validation errors are printed, the warnings logged
        captured = self._capfd.readouterr()
        return result, captured.out + captured.err + self._caplog.text

    def valid(self, body: str, uart: str = UART) -> str:
        """Validate a configuration that must be valid, return the output."""
        result, output = self._run(body, uart, "config")
        assert result == 0, output
        return output

    def invalid(self, body: str, uart: str = UART) -> str:
        """Validate a configuration that must be invalid, return the output."""
        result, output = self._run(body, uart, "config")
        assert result != 0, output
        return output

    def generate(self, body: str, uart: str = UART) -> Generated:
        """Generate the code of a valid configuration."""
        result, output = self._run(body, uart, "compile", "--only-generate")
        assert result == 0, output
        src = self._path / ".esphome" / "build" / "test" / "src"
        return Generated(
            (src / "main.cpp").read_text(encoding="utf-8"),
            (src / "esphome" / "core" / "defines.h").read_text(encoding="utf-8"),
        )


@pytest.fixture
def esphome(
    tmp_path: Path,
    capfd: pytest.CaptureFixture[str],
    caplog: pytest.LogCaptureFixture,
) -> ESPHome:
    return ESPHome(tmp_path, capfd, caplog)


@pytest.fixture
def hub_module(esphome: ESPHome) -> ModuleType:
    """The Python module of the hub, as loaded by ESPHome."""
    esphome.valid("kyo_alarm:\n")
    init = COMPONENTS / "kyo_alarm" / "__init__.py"
    return next(
        module
        for module in list(sys.modules.values())
        if getattr(module, "__file__", None) == str(init)
    )
