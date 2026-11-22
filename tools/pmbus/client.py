"""SMBus/PMBus host framing on top of a raw transport: PEC, words, blocks, number formats."""

from . import transport as _t

# Command codes this project uses (PMBus Part II, rev 1.3.1).
PAGE = 0x00
OPERATION = 0x01
ON_OFF_CONFIG = 0x02
CLEAR_FAULTS = 0x03
WRITE_PROTECT = 0x10
STORE_DEFAULT_ALL = 0x11
RESTORE_DEFAULT_ALL = 0x12
CAPABILITY = 0x19
QUERY = 0x1A
SMBALERT_MASK = 0x1B
VOUT_MODE = 0x20
VOUT_COMMAND = 0x21
VOUT_MAX = 0x24
VOUT_MARGIN_HIGH = 0x25
VOUT_MARGIN_LOW = 0x26
VOUT_MIN = 0x2B
VIN_ON = 0x35
VIN_OFF = 0x36
FAN_CONFIG_12 = 0x3A
FAN_COMMAND_1 = 0x3B
VOUT_OV_FAULT_LIMIT = 0x40
VOUT_OV_FAULT_RESPONSE = 0x41
VOUT_OV_WARN_LIMIT = 0x42
VOUT_UV_WARN_LIMIT = 0x43
VOUT_UV_FAULT_LIMIT = 0x44
VOUT_UV_FAULT_RESPONSE = 0x45
IOUT_OC_FAULT_LIMIT = 0x46
IOUT_OC_FAULT_RESPONSE = 0x47
IOUT_OC_LV_FAULT_LIMIT = 0x48
IOUT_OC_LV_FAULT_RESPONSE = 0x49
IOUT_OC_WARN_LIMIT = 0x4A
OT_FAULT_LIMIT = 0x4F
OT_FAULT_RESPONSE = 0x50
OT_WARN_LIMIT = 0x51
UT_WARN_LIMIT = 0x52
UT_FAULT_LIMIT = 0x53
VIN_OV_FAULT_LIMIT = 0x55
VIN_OV_WARN_LIMIT = 0x57
VIN_UV_WARN_LIMIT = 0x58
VIN_UV_FAULT_LIMIT = 0x59
IIN_OC_FAULT_LIMIT = 0x5B
IIN_OC_WARN_LIMIT = 0x5D
POWER_GOOD_ON = 0x5E
POWER_GOOD_OFF = 0x5F
TON_DELAY = 0x60
TON_RISE = 0x61
TON_MAX_FAULT_LIMIT = 0x62
TOFF_DELAY = 0x64
TOFF_FALL = 0x65
POUT_OP_FAULT_LIMIT = 0x68
POUT_OP_WARN_LIMIT = 0x6A
PIN_OP_WARN_LIMIT = 0x6B
STATUS_BYTE = 0x78
STATUS_WORD = 0x79
STATUS_VOUT = 0x7A
STATUS_IOUT = 0x7B
STATUS_INPUT = 0x7C
STATUS_TEMPERATURE = 0x7D
STATUS_CML = 0x7E
STATUS_OTHER = 0x7F
STATUS_MFR_SPECIFIC = 0x80
STATUS_FANS_1_2 = 0x81
READ_VIN = 0x88
READ_IIN = 0x89
READ_VOUT = 0x8B
READ_IOUT = 0x8C
READ_TEMPERATURE_1 = 0x8D
READ_TEMPERATURE_2 = 0x8E
READ_FAN_SPEED_1 = 0x90
READ_POUT = 0x96
READ_PIN = 0x97
PMBUS_REVISION = 0x98
MFR_ID = 0x99
MFR_MODEL = 0x9A
MFR_REVISION = 0x9B
MFR_LOCATION = 0x9C
MFR_DATE = 0x9D
MFR_SERIAL = 0x9E
MFR_VIN_MIN = 0xA0
MFR_VIN_MAX = 0xA1
MFR_IIN_MAX = 0xA2
MFR_PIN_MAX = 0xA3
MFR_VOUT_MIN = 0xA4
MFR_VOUT_MAX = 0xA5
MFR_IOUT_MAX = 0xA6
MFR_POUT_MAX = 0xA7
IC_DEVICE_ID = 0xAD
IC_DEVICE_REV = 0xAE
USER_DATA_00 = 0xB0
MFR_MAX_TEMP_1 = 0xC0
MFR_MAX_TEMP_2 = 0xC1
MFR_SIM_LOAD = 0xD0  # this device: simulated load current, LINEAR11 amperes
MFR_SIM_VIN = 0xD1   # this device: simulated input voltage, LINEAR11 volts
MFR_SIM_FAN = 0xD2   # this device: bit 0 stops the simulated fan
MFR_INJECT = 0xD3    # this device: word, low byte a STATUS_x code, high byte bits to latch

ALERT_RESPONSE_ADDR = 0x0C

NackError = _t.NackError


class PMBusError(IOError):
    pass


class PecError(PMBusError):
    pass


def crc8(data, crc=0):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def linear11_decode(word):
    n = (word >> 11) & 0x1F
    y = word & 0x7FF
    if n & 0x10:
        n -= 32
    if y & 0x400:
        y -= 2048
    return y * 2.0 ** n


def linear11_encode(value):
    for n in range(-16, 16):
        y = round(value / 2.0 ** n)
        if -1024 <= y <= 1023:
            return ((n & 0x1F) << 11) | (y & 0x7FF)
    raise ValueError(f"{value} does not fit LINEAR11")


def ulinear16_decode(word, vout_mode):
    n = vout_mode & 0x1F
    if n & 0x10:
        n -= 32
    return word * 2.0 ** n


def ulinear16_encode(value, vout_mode):
    n = vout_mode & 0x1F
    if n & 0x10:
        n -= 32
    y = round(value / 2.0 ** n)
    if not 0 <= y <= 0xFFFF:
        raise ValueError(f"{value} does not fit ULINEAR16 with exponent {n}")
    return y


class PMBusClient:
    def __init__(self, transport, addr=0x58, pec=True):
        self.t = transport
        self.addr = addr
        self.pec = pec
        self._vout_mode = None

    # --- SMBus protocols ---

    def _write(self, data, pec=None):
        data = bytes(data)
        if self.pec if pec is None else pec:
            data += bytes([crc8(bytes([self.addr << 1]) + data)])
        self.t.xfer(self.addr, data, 0)

    def _read(self, write, n, pec=None):
        use_pec = self.pec if pec is None else pec
        raw = self.t.xfer(self.addr, bytes(write), n + (1 if use_pec else 0))
        if use_pec:
            expect = crc8(bytes([self.addr << 1]) + bytes(write) + bytes([(self.addr << 1) | 1])
                          + raw[:n])
            if raw[n] != expect:
                raise PecError(f"PEC 0x{raw[n]:02x}, expected 0x{expect:02x} "
                               f"(command 0x{write[0]:02x})")
        return raw[:n]

    def send_byte(self, cmd):
        self._write([cmd])

    def write_byte(self, cmd, value):
        self._write([cmd, value & 0xFF])

    def write_word(self, cmd, value):
        self._write([cmd, value & 0xFF, (value >> 8) & 0xFF])

    def read_byte(self, cmd, pec=None):
        return self._read([cmd], 1, pec)[0]

    def read_word(self, cmd, pec=None):
        b = self._read([cmd], 2, pec)
        return b[0] | (b[1] << 8)

    def block_write(self, cmd, data):
        data = bytes(data)
        self._write(bytes([cmd, len(data)]) + data)

    def block_read(self, cmd):
        # Raw transports need the length up front: read the count, then the whole block.
        count = self._read([cmd], 1, pec=False)[0]
        if count == 0xFF:
            raise PMBusError(f"command 0x{cmd:02x}: no block (unsupported?)")
        return self._read([cmd], 1 + count)[1:]

    def process_call_block(self, cmd, data):
        data = bytes(data)
        write = bytes([cmd, len(data)]) + data
        count = self._read(write, 1, pec=False)[0]
        return self._read(write, 1 + count)[1:]

    def read_alert_mask(self, status_cmd):
        return self.process_call_block(SMBALERT_MASK, [status_cmd])[0]

    def write_alert_mask(self, status_cmd, mask):
        self.write_word(SMBALERT_MASK, status_cmd | (mask << 8))

    def query(self, cmd):
        return self.process_call_block(QUERY, [cmd])[0]

    def alert_response(self):
        """Reads the Alert Response Address; returns the alerting address or None."""
        try:
            b = self.t.xfer(ALERT_RESPONSE_ADDR, b"", 1)[0]
        except NackError:
            return None
        return None if b == 0xFF else b >> 1

    # --- PMBus values ---

    def vout_mode(self):
        if self._vout_mode is None:
            self._vout_mode = self.read_byte(VOUT_MODE)
        return self._vout_mode

    def read_linear11(self, cmd):
        return linear11_decode(self.read_word(cmd))

    def write_linear11(self, cmd, value):
        self.write_word(cmd, linear11_encode(value))

    def read_vout(self, cmd=READ_VOUT):
        return ulinear16_decode(self.read_word(cmd), self.vout_mode())

    def write_vout(self, cmd, volts):
        self.write_word(cmd, ulinear16_encode(volts, self.vout_mode()))

    def read_string(self, cmd):
        return self.block_read(cmd).decode(errors="replace")
