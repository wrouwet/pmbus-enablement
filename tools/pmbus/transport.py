"""Raw I2C transports. Each moves bytes only; PEC and SMBus framing live in client.py.

A transport has one method:

    xfer(addr, write: bytes, read_len: int) -> bytes

which writes `write`, then (after a repeated start, if both are non-empty) reads
`read_len` bytes. It raises NackError if nobody answers.

Transports are named by a spec string, as the CLI and the test suite take them:

    sim                 the C library in-process (build it with `make sim-lib`)
    hub:/dev/ttyACM1    the FRDM-MCXA153 USB-to-I2C hub (wrouwet/frdm-mcxa153-usb-i2c-hub)
    linux:3             /dev/i2c-3 on a Linux host, e.g. the LX2160ARDB
"""

import ctypes
import fcntl
import os
import pathlib


class NackError(IOError):
    pass


class SimTransport:
    """The firmware's PMBus library, compiled for the host (tests/sim/sim_shim.c)."""

    def __init__(self, addr=0x58, lib=None):
        if lib is None:
            lib = pathlib.Path(__file__).resolve().parents[2] / "build" / "host" / "libpmbus_sim.so"
        self.lib = ctypes.CDLL(str(lib))
        self.lib.sim_xfer.argtypes = [ctypes.c_uint8, ctypes.c_char_p, ctypes.c_int,
                                      ctypes.c_char_p, ctypes.c_int]
        self.lib.sim_set_temps.argtypes = [ctypes.c_int32, ctypes.c_int32]
        self.lib.sim_advance.argtypes = [ctypes.c_uint32]
        self.lib.sim_init(addr)

    def xfer(self, addr, write, read_len):
        rbuf = ctypes.create_string_buffer(max(read_len, 1))
        if self.lib.sim_xfer(addr, bytes(write), len(write), rbuf, read_len) < 0:
            raise NackError(f"no ACK from 0x{addr:02x}")
        return rbuf.raw[:read_len]

    # Simulator-only controls, for tests.
    def set_temps(self, t1_mc, t2_mc):
        self.lib.sim_set_temps(t1_mc, t2_mc)

    def advance(self, ms):
        """Lets simulated time pass."""
        self.lib.sim_advance(int(ms))

    def alert(self):
        return bool(self.lib.sim_alert())

    def power_cycle(self, addr=0x58):
        self.lib.sim_init(addr)


class HubTransport:
    """The text protocol of frdm-mcxa153-usb-i2c-hub: W, R and X commands over USB CDC."""

    def __init__(self, port, baud=115200, timeout=2.0):
        import serial  # pyserial

        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.ser.reset_input_buffer()

    def _cmd(self, line):
        self.ser.write((line + "\r\n").encode())
        while True:
            reply = self.ser.readline().decode(errors="replace").strip()
            if not reply:
                raise IOError(f"no reply to {line!r}")
            if reply.startswith("OK") or reply.startswith("ERR"):
                break
        if reply.startswith("ERR"):
            if "nak" in reply:
                raise NackError(reply)
            raise IOError(reply)
        return bytes(int(b, 16) for b in reply.split()[1:])

    def xfer(self, addr, write, read_len):
        data = " ".join(f"{b:02x}" for b in write)
        if write and read_len:
            return self._cmd(f"X {addr:02x} {read_len} {data}")
        if read_len:
            return self._cmd(f"R {addr:02x} {read_len}")
        self._cmd(f"W {addr:02x} {data}".rstrip())
        return b""


class LinuxTransport:
    """/dev/i2c-N through the I2C_RDWR ioctl, so repeated starts work on any adapter."""

    I2C_RDWR = 0x0707
    I2C_M_RD = 0x0001

    class _Msg(ctypes.Structure):
        _fields_ = [("addr", ctypes.c_uint16), ("flags", ctypes.c_uint16),
                    ("len", ctypes.c_uint16), ("buf", ctypes.c_void_p)]

    class _RdwrData(ctypes.Structure):
        _fields_ = [("msgs", ctypes.c_void_p), ("nmsgs", ctypes.c_uint32)]

    def __init__(self, bus):
        self.fd = os.open(f"/dev/i2c-{int(bus)}", os.O_RDWR)

    def xfer(self, addr, write, read_len):
        msgs = []
        wbuf = ctypes.create_string_buffer(bytes(write), max(len(write), 1))
        rbuf = ctypes.create_string_buffer(max(read_len, 1))
        if write or not read_len:
            msgs.append(self._Msg(addr, 0, len(write), ctypes.addressof(wbuf)))
        if read_len:
            msgs.append(self._Msg(addr, self.I2C_M_RD, read_len, ctypes.addressof(rbuf)))
        arr = (self._Msg * len(msgs))(*msgs)
        data = self._RdwrData(ctypes.addressof(arr), len(msgs))
        try:
            fcntl.ioctl(self.fd, self.I2C_RDWR, data)
        except OSError as e:
            raise NackError(f"0x{addr:02x}: {e}") from e
        return rbuf.raw[:read_len]


def open_transport(spec, addr=0x58):
    kind, _, arg = spec.partition(":")
    if kind == "sim":
        return SimTransport(addr, arg or None)
    if kind == "hub":
        return HubTransport(arg)
    if kind == "linux":
        return LinuxTransport(arg)
    raise ValueError(f"unknown transport {spec!r} (sim, hub:<port>, linux:<bus>)")
