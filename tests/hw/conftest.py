"""Fixtures for the device test suite.

Pick the bus with PMBUS_TRANSPORT (default "sim") and the address with PMBUS_ADDR:

    make sim-lib && pytest tests/hw
    PMBUS_TRANSPORT=hub:/dev/ttyACM1 pytest tests/hw
    PMBUS_TRANSPORT=linux:3 pytest tests/hw          # e.g. on the LX2160ARDB
"""

import os
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "tools"))

from pmbus import PMBusClient, open_transport  # noqa: E402
from pmbus import client as c  # noqa: E402

TRANSPORT = os.environ.get("PMBUS_TRANSPORT", "sim")
ADDR = int(os.environ.get("PMBUS_ADDR", "0x58"), 0)


@pytest.fixture(scope="session")
def transport():
    return open_transport(TRANSPORT, ADDR)


@pytest.fixture
def dev(transport):
    """A device in its power-on state; anything a test changes is put back afterwards."""
    d = PMBusClient(transport, ADDR)
    reset(d)
    yield d
    reset(d)


def reset(d):
    d.write_byte(c.WRITE_PROTECT, 0)
    d.send_byte(c.RESTORE_DEFAULT_ALL)
    # The simulation knobs are not part of the stored config.
    d.write_linear11(c.MFR_SIM_LOAD, 5.0)
    d.write_linear11(c.MFR_SIM_VIN, 54.0)
    d.write_byte(c.MFR_SIM_FAN, 0)
    d.write_byte(c.OPERATION, 0)
    d.write_byte(c.OPERATION, 0x80)
    d.send_byte(c.CLEAR_FAULTS)
    while d.alert_response() is not None:
        pass


@pytest.fixture
def sim(transport):
    if not TRANSPORT.startswith("sim"):
        pytest.skip("simulator only")
    return transport
