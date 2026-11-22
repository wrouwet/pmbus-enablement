"""Black-box tests of the PMBus target, over any transport (see conftest.py).

They only use the bus, so they run unchanged against the simulator, the board through
the USB-to-I2C hub, or the board wired to the LX2160ARDB. The few that need to set a
temperature use the simulator's controls and skip elsewhere.
"""

import time

import pytest

from pmbus import PecError
from pmbus import client as c

# The device evaluates limits every CONFIG_PMBUS_UPDATE_PERIOD_MS (200 ms); give it two.
SETTLE = 0.45


def settle(dev):
    if not hasattr(dev.t, "lib"):
        time.sleep(SETTLE)


def wait(dev, seconds):
    """Lets time pass on the device: simulated on sim, real on hardware."""
    if hasattr(dev.t, "advance"):
        dev.t.advance(seconds * 1000)
    else:
        time.sleep(seconds)


def test_identification(dev):
    assert dev.read_byte(c.PMBUS_REVISION) == 0x33
    cap = dev.read_byte(c.CAPABILITY)
    assert cap & 0x80, "PEC supported"
    assert dev.read_byte(c.VOUT_MODE) == 0x17
    assert dev.read_string(c.MFR_MODEL) == "MCXA153-PMB-1"
    assert len(dev.read_string(c.MFR_SERIAL)) > 0


def test_telemetry_defaults(dev):
    assert dev.read_vout() == pytest.approx(12.0, abs=0.002)
    assert dev.read_linear11(c.READ_VIN) == pytest.approx(54.0, abs=0.1)
    assert dev.read_linear11(c.READ_IOUT) == pytest.approx(5.0, abs=0.01)
    assert dev.read_linear11(c.READ_POUT) == pytest.approx(60.0, abs=0.1)
    pin = dev.read_linear11(c.READ_PIN)
    assert 60.0 < pin < 70.0
    assert dev.read_linear11(c.READ_IIN) == pytest.approx(pin / 54.0, rel=0.01)
    assert -40 < dev.read_linear11(c.READ_TEMPERATURE_1) < 85
    assert dev.read_word(c.STATUS_WORD) == 0


def test_pec_and_no_pec_agree(dev):
    assert dev.read_word(c.READ_VOUT, pec=True) == dev.read_word(c.READ_VOUT, pec=False)


def test_bad_pec_write_is_rejected(dev):
    before = dev.read_word(c.VOUT_COMMAND)
    dev.t.xfer(dev.addr, bytes([c.VOUT_COMMAND, 0x00, 0x19, 0x5A]), 0)  # 0x5A: wrong PEC
    assert dev.read_word(c.VOUT_COMMAND) == before
    assert dev.read_byte(c.STATUS_CML) & 0x20
    assert dev.read_byte(c.STATUS_BYTE) & 0x02
    dev.send_byte(c.CLEAR_FAULTS)
    assert dev.read_byte(c.STATUS_CML) == 0


def test_unsupported_command(dev):
    with pytest.raises(PecError):
        dev.read_word(0x8A)  # READ_VCAP
    assert dev.read_byte(c.STATUS_CML) & 0x80
    assert dev.query(0x8A) == 0


def test_query(dev):
    assert dev.query(c.READ_VOUT) == 0xA0
    assert dev.query(c.VOUT_COMMAND) == 0xE0
    assert dev.query(c.CLEAR_FAULTS) == 0xDC


def test_vout_command_and_margins(dev):
    dev.write_vout(c.VOUT_COMMAND, 12.5)
    assert dev.read_vout() == pytest.approx(12.5, abs=0.002)
    dev.write_byte(c.OPERATION, 0xA0)  # on, margin high
    assert dev.read_vout() == pytest.approx(12.6, abs=0.002)
    dev.write_byte(c.OPERATION, 0x90)  # on, margin low
    assert dev.read_vout() == pytest.approx(11.4, abs=0.002)


def test_operation_off(dev):
    dev.write_byte(c.OPERATION, 0x00)
    assert dev.read_vout() == 0
    assert dev.read_byte(c.STATUS_BYTE) & 0x40  # OFF
    assert dev.read_word(c.STATUS_WORD) & 0x0800  # POWER_GOOD#


def test_limits_read_back_exactly(dev):
    for cmd in (c.OT_WARN_LIMIT, c.IOUT_OC_WARN_LIMIT, c.VIN_UV_WARN_LIMIT):
        word = c.linear11_encode(33.25)
        dev.write_word(cmd, word)
        assert dev.read_word(cmd) == word


def test_mfr_block_write(dev):
    dev.block_write(c.MFR_LOCATION, b"RACK-7")
    assert dev.read_string(c.MFR_LOCATION) == "RACK-7"


def test_write_protect(dev):
    dev.write_byte(c.WRITE_PROTECT, 0x80)
    before = dev.read_word(c.VOUT_COMMAND)
    dev.write_vout(c.VOUT_COMMAND, 11.0)
    assert dev.read_word(c.VOUT_COMMAND) == before
    assert dev.read_byte(c.STATUS_CML) & 0x80
    dev.write_byte(c.WRITE_PROTECT, 0)


def test_overcurrent_warning_then_fault(dev):
    dev.write_linear11(c.MFR_SIM_LOAD, 9.0)
    settle(dev)
    assert dev.read_byte(c.STATUS_IOUT) & 0x20
    assert dev.read_byte(c.STATUS_BYTE) == 0x01  # NONE_OF_THE_ABOVE: a warning
    dev.write_linear11(c.MFR_SIM_LOAD, 11.0)
    settle(dev)
    assert dev.read_byte(c.STATUS_IOUT) & 0x80
    assert dev.read_byte(c.STATUS_BYTE) & 0x50 == 0x50  # OFF and IOUT_OC_FAULT
    assert dev.read_vout() == 0


def test_fault_latches_until_operation_cycles(dev):
    dev.write_vout(c.VOUT_COMMAND, 14.0)  # above VOUT_OV_FAULT_LIMIT (13.8 V)
    settle(dev)
    assert dev.read_byte(c.STATUS_VOUT) & 0x80
    dev.write_vout(c.VOUT_COMMAND, 12.0)
    dev.send_byte(c.CLEAR_FAULTS)
    settle(dev)
    assert dev.read_byte(c.STATUS_BYTE) & 0x40, "still latched off"
    dev.write_byte(c.OPERATION, 0x00)
    dev.write_byte(c.OPERATION, 0x80)
    settle(dev)
    assert dev.read_byte(c.STATUS_BYTE) == 0
    assert dev.read_vout() == pytest.approx(12.0, abs=0.002)


def test_input_undervoltage(dev):
    dev.write_linear11(c.MFR_SIM_VIN, 38.0)
    settle(dev)
    assert dev.read_byte(c.STATUS_INPUT) & 0x10
    assert dev.read_byte(c.STATUS_BYTE) & 0x48 == 0x48  # OFF and VIN_UV_FAULT


def test_alert_and_alert_response(dev):
    """A warning pulls SMBALERT#; the Alert Response Address names this device once."""
    dev.write_linear11(c.OT_WARN_LIMIT, -50.0)  # every temperature is above this
    settle(dev)
    assert dev.read_byte(c.STATUS_TEMPERATURE) & 0x40
    assert dev.alert_response() == dev.addr
    assert dev.alert_response() is None
    assert dev.read_byte(c.STATUS_TEMPERATURE) & 0x40, "status stays latched"


def test_store_and_restore(dev):
    dev.write_linear11(c.OT_WARN_LIMIT, 70.0)
    dev.send_byte(c.STORE_DEFAULT_ALL)
    deadline = time.time() + 2
    while dev.read_byte(c.STATUS_BYTE) & 0x80 and time.time() < deadline:  # BUSY
        time.sleep(0.05)
    dev.write_linear11(c.OT_WARN_LIMIT, 60.0)
    dev.send_byte(c.RESTORE_DEFAULT_ALL)
    assert dev.read_linear11(c.OT_WARN_LIMIT) == 70.0
    # Put the stored defaults back, so the next run starts clean.
    dev.write_linear11(c.OT_WARN_LIMIT, 85.0)
    dev.send_byte(c.STORE_DEFAULT_ALL)
    time.sleep(0.2)


def test_sim_temperature_fault(dev, sim):
    sim.set_temps(101000, 30000)
    assert dev.read_byte(c.STATUS_TEMPERATURE) & 0x80
    assert dev.read_byte(c.STATUS_BYTE) & 0x44 == 0x44
    assert sim.alert()
    sim.set_temps(25000, 30000)


def test_sim_power_cycle_keeps_stored_config(dev, sim):
    dev.write_linear11(c.OT_WARN_LIMIT, 66.0)
    dev.send_byte(c.STORE_DEFAULT_ALL)
    sim.power_cycle(dev.addr)
    assert dev.read_linear11(c.OT_WARN_LIMIT) == 66.0
    dev.write_linear11(c.OT_WARN_LIMIT, 85.0)
    dev.send_byte(c.STORE_DEFAULT_ALL)


def test_wrong_address_nacks(dev):
    with pytest.raises(OSError):
        dev.t.xfer(dev.addr ^ 0x01, bytes([c.STATUS_BYTE]), 1)


# --- Commands for the Linux driver and a realistic converter model ---


def test_ratings_and_identity(dev):
    assert dev.read_linear11(c.MFR_VIN_MIN) == 40.0
    assert dev.read_linear11(c.MFR_VIN_MAX) == 60.0
    assert dev.read_linear11(c.MFR_IOUT_MAX) == 10.0
    assert dev.read_linear11(c.MFR_POUT_MAX) == 120.0
    assert dev.read_vout(c.MFR_VOUT_MAX) == pytest.approx(14.4, abs=0.002)
    assert dev.read_linear11(c.MFR_MAX_TEMP_1) == 100.0
    assert dev.read_string(c.IC_DEVICE_ID) == "MCXA153"


def test_user_data(dev):
    dev.block_write(c.USER_DATA_00 + 5, b"asset-1234")
    assert dev.block_read(c.USER_DATA_00 + 5) == b"asset-1234"
    dev.block_write(c.USER_DATA_00 + 5, b"x" * 17)  # one byte too many
    assert dev.read_byte(c.STATUS_CML) & 0x40
    assert dev.block_read(c.USER_DATA_00 + 5) == b"asset-1234"


def test_query_new_commands(dev):
    assert dev.query(c.SMBALERT_MASK) == 0xFC      # supported, read, write, not numeric
    assert dev.query(c.READ_FAN_SPEED_1) == 0xA0   # supported, read, linear
    assert dev.query(c.MFR_INJECT) == 0xDC         # write only


def test_smbalert_mask(dev):
    dev.write_alert_mask(c.STATUS_TEMPERATURE, 0x40)  # mask OT_WARN
    assert dev.read_alert_mask(c.STATUS_TEMPERATURE) == 0x40
    dev.write_linear11(c.OT_WARN_LIMIT, -50.0)
    settle(dev)
    assert dev.read_byte(c.STATUS_TEMPERATURE) & 0x40
    assert dev.alert_response() is None, "masked: no alert"
    dev.write_alert_mask(c.STATUS_TEMPERATURE, 0)


def test_inject_status_other(dev):
    dev.write_word(c.MFR_INJECT, c.STATUS_OTHER | (0x01 << 8))
    assert dev.read_byte(c.STATUS_OTHER) == 0x01
    assert dev.read_word(c.STATUS_WORD) & 0x0200
    assert dev.alert_response() == dev.addr
    dev.write_byte(c.STATUS_OTHER, 0x01)
    assert dev.read_word(c.STATUS_WORD) == 0


def test_vout_max_clamps(dev):
    dev.write_vout(c.VOUT_MAX, 13.0)
    dev.write_vout(c.VOUT_COMMAND, 13.5)
    assert dev.read_vout() == pytest.approx(13.0, abs=0.002)
    assert dev.read_byte(c.STATUS_VOUT) & 0x08


def test_power_limits_and_input_current(dev):
    dev.write_linear11(c.MFR_SIM_LOAD, 9.8)  # 117.6 W out
    settle(dev)
    assert dev.read_byte(c.STATUS_IOUT) & 0x01  # POUT_OP_WARN
    assert dev.read_linear11(c.POUT_OP_WARN_LIMIT) == 115.0
    dev.write_linear11(c.IIN_OC_WARN_LIMIT, 2.0)
    settle(dev)
    assert dev.read_byte(c.STATUS_INPUT) & 0x02  # IIN_OC_WARN


def test_sequencing(dev):
    dev.write_linear11(c.TON_DELAY, 300)
    dev.write_linear11(c.TON_RISE, 300)
    dev.write_byte(c.OPERATION, 0x00)
    dev.write_byte(c.OPERATION, 0x80)
    assert dev.read_byte(c.STATUS_BYTE) & 0x40, "off during TON_DELAY"
    wait(dev, 0.8)
    assert dev.read_vout() == pytest.approx(12.0, abs=0.002)
    assert dev.read_word(c.STATUS_WORD) == 0
    dev.write_linear11(c.TOFF_DELAY, 300)
    dev.write_byte(c.OPERATION, 0x40)  # soft off
    assert dev.read_vout() == pytest.approx(12.0, abs=0.002), "still on during TOFF_DELAY"
    wait(dev, 0.5)
    assert dev.read_byte(c.STATUS_BYTE) & 0x40


def test_retry_then_latch(dev):
    dev.write_byte(c.VOUT_OV_FAULT_RESPONSE, 0x80 | (2 << 3) | 2)  # 2 retries, 200 ms apart
    dev.write_vout(c.VOUT_OV_FAULT_LIMIT, 11.0)
    settle(dev)
    assert dev.read_byte(c.STATUS_VOUT) & 0x80
    wait(dev, 1.5)
    assert dev.read_byte(c.STATUS_BYTE) & 0x40, "latched off after the retries"
    dev.write_vout(c.VOUT_OV_FAULT_LIMIT, 13.8)
    wait(dev, 0.5)
    assert dev.read_byte(c.STATUS_BYTE) & 0x40, "stays latched"
    dev.write_byte(c.OPERATION, 0x00)
    dev.write_byte(c.OPERATION, 0x80)
    settle(dev)
    assert dev.read_vout() == pytest.approx(12.0, abs=0.002)


def test_current_limit(dev):
    dev.write_byte(c.IOUT_OC_FAULT_RESPONSE, 0x00)  # constant current
    dev.write_byte(c.VOUT_UV_FAULT_RESPONSE, 0x00)
    dev.write_linear11(c.MFR_SIM_LOAD, 12.0)
    settle(dev)
    assert dev.read_linear11(c.READ_IOUT) == pytest.approx(10.0, abs=0.01)
    assert dev.read_vout() == pytest.approx(10.0, abs=0.01)
    assert dev.read_byte(c.STATUS_IOUT) & 0x80


def test_fan(dev):
    assert dev.read_byte(c.FAN_CONFIG_12) & 0x80, "fan 1 installed"
    assert dev.read_linear11(c.READ_FAN_SPEED_1) > 1000
    dev.write_linear11(c.FAN_COMMAND_1, 100)
    wait(dev, 4)
    assert dev.read_linear11(c.READ_FAN_SPEED_1) > 17000
    dev.write_byte(c.MFR_SIM_FAN, 1)
    wait(dev, 5)
    assert dev.read_byte(c.STATUS_FANS_1_2) & 0x80
    assert dev.read_word(c.STATUS_WORD) & 0x0400
    dev.write_linear11(c.FAN_COMMAND_1, 0)
