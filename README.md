# FRDM-MCXA153 PMBus target

Turns an NXP [FRDM-MCXA153](https://www.nxp.com/design/design-center/development-boards-and-designs/FRDM-MCXA153)
into a **PMBus 1.3 device** (an SMBus target) that a host such as a SONiC switch's
control-plane CPU can monitor and control over two wires. The device presents itself as a
simple 54 V to 12 V converter: its electrical side is simulated, its two temperatures are
real (the board's P3T1755 sensor and the MCU die).

The goal is a realistic PMBus endpoint for bringing up the host side: the Linux `pmbus`
hwmon driver, SONiC's PSU and thermal plumbing, and alert handling, on the
[LX2160ARDB SONiC port](https://github.com/wrouwet/lx2160-sonic).

> **Status (2026-11-22):** runs on the FRDM-MCXA153: boots, both temperature sensors read,
> status, alerts and the simulation work from the shell. The library passes 255 host unit
> checks and 30 device tests on the simulator. **The bus side is not yet tested against a
> real host.** See [Not yet verified on hardware](#not-yet-verified-on-hardware).

## What it implements

| Area | Details |
|---|---|
| SMBus protocols | Send Byte, Read/Write Byte, Read/Write Word, Block Read/Write (32 bytes), Block Write-Block Read Process Call (`QUERY`, `SMBALERT_MASK`) |
| PEC | Checked on every write that carries it, appended to every read; a bad PEC rejects the write and sets `STATUS_CML` |
| Alerts | `SMBALERT#` (open drain) on new status bits, maskable per bit with `SMBALERT_MASK`; answers the Alert Response Address 0x0C while alerting |
| SMBus timeout | A transaction stalled for 30 ms resets the target interface (SMBus: 25–35 ms) |
| Group Command | Writes take effect at STOP, so group commands execute together |
| Status | `STATUS_BYTE/WORD/VOUT/IOUT/INPUT/TEMPERATURE/CML/OTHER/MFR_SPECIFIC/FANS_1_2`, latched, write-1-to-clear, `CLEAR_FAULTS` |
| Control | `OPERATION` (on, immediate or soft off, margins, margin fault handling), `ON_OFF_CONFIG` (incl. a CONTROL input), `VOUT_COMMAND`, `VOUT_MAX/MIN`, margins, `WRITE_PROTECT` |
| Sequencing | `TON_DELAY/RISE/MAX_FAULT`, `TOFF_DELAY/FALL/MAX_WARN`, `VIN_ON/OFF`, `POWER_GOOD_ON/OFF`, on a 10 ms tick |
| Limits | VOUT OV/UV, IOUT OC (and OC low-voltage), OT/UT, VIN OV/UV, IIN OC, POUT OP, PIN OP, each warning and fault |
| Fault responses | All eleven `*_FAULT_RESPONSE` bytes: continue, delayed shutdown, shutdown with 0–6 or endless retries, inhibit while present; constant-current limiting for IOUT OC |
| Telemetry | `READ_VIN/IIN/PIN/VOUT/IOUT/POUT` (simulated), `READ_TEMPERATURE_1` (P3T1755), `READ_TEMPERATURE_2` (die), `READ_FAN_SPEED_1` (simulated fan) |
| Fan | `FAN_CONFIG_1_2`, `FAN_COMMAND_1` (duty or RPM), a thermal curve, fault and warning on a failed fan |
| Identification | `PMBUS_REVISION` 1.3, `CAPABILITY`, `MFR_ID/MODEL/REVISION/LOCATION/DATE` (dummy, writable), `MFR_SERIAL` (from the chip UID), `IC_DEVICE_ID/REV`, `USER_DATA_00–15` |
| Ratings | `MFR_VIN_MIN/MAX`, `MFR_IIN/PIN/IOUT/POUT_MAX`, `MFR_VOUT_MIN/MAX`, `MFR_MAX_TEMP_1/2`, which Linux shows as hwmon `*_rated_*` attributes |
| Storage | `STORE_DEFAULT_ALL` / `RESTORE_DEFAULT_ALL` to the 16 KB flash storage partition; a stored record is validated before use |
| Simulation knobs | `MFR_SPECIFIC_00` load current, `_01` input voltage, `_02` fan failure, `_03` status injection, plus shell commands |

About 110 commands in all. The full command list is in [docs/commands.md](docs/commands.md).

## Hardware

- FRDM-MCXA153, flashed over its on-board MCU-Link.
- PMBus on the **mikroBUS header J5**: pin 5 = SCL (P3_27), pin 6 = SDA (P3_28),
  pin 8 = GND, plus SMBALERT# on the mikroBUS INT pin (P2_5). These are the same pins
  [frdm-mcxa153-usb-i2c-hub](https://github.com/wrouwet/frdm-mcxa153-usb-i2c-hub) uses, so
  two FRDM boards wire header to header. P3_27/P3_28 are the MCX A153's only 5 V tolerant
  pins.
- Address **0x58** (`CONFIG_PMBUS_ADDRESS`), 100 or 400 kHz.

The security review (threat model, findings, residual risks) is in
[docs/security.md](docs/security.md).

Wiring to the hub and to the LX2160ARDB, and binding the Linux driver, are in
[docs/hookup.md](docs/hookup.md).

## Quick start

```sh
sudo scripts/install-host-deps.sh   # once: Ubuntu packages, udev rule for the probe
make setup                          # once: Python venv, west, Zephyr v4.5.0, toolchain
make test                           # host unit tests + the device suite on the simulator
make flash RUNNER=pyocd             # build and flash (or RUNNER=linkserver with NXP LinkServer)
make monitor                        # the Zephyr shell on the MCU-Link serial port
```

The workspace is the parent directory (`~/mcxa153-workspace`): `zephyr/`, `modules/`,
`.venv/` and `.tools/` live next to this repo.

On the shell:

```
uart:~$ pmbus status
address 0x58, output on, power good
VIN  54.000 V   IIN  1.178 A   PIN  63.657 W
VOUT 12.000 V   IOUT 5.000 A   POUT 60.000 W
TEMP1 24.375 C   TEMP2 23.920 C   FAN1 5400 RPM
...
uart:~$ pmbus load 9000        # 9 A: IOUT warning, SMBALERT# goes low
uart:~$ pmbus temp 1 90000     # pretend sensor 1 reads 90 °C
uart:~$ pmbus temp 1 off
uart:~$ pmbus fan fail          # fan fault after 2 s
uart:~$ pmbus inject other 0x01 # latch any status bit, to test a host's alert handling
```

## Talking to it from a host

`tools/pmbus_tool.py` and the test suite share a small PMBus client with three transports:

| Transport | Use |
|---|---|
| `sim` | The firmware's PMBus library compiled for the PC (`make sim-lib`), no hardware |
| `hub:/dev/ttyACMx` | A second FRDM-MCXA153 running frdm-mcxa153-usb-i2c-hub |
| `linux:<bus>` | `/dev/i2c-<bus>` on any Linux host, e.g. the LX2160ARDB |

```sh
python tools/pmbus_tool.py -t hub:/dev/ttyACM1 dump
python tools/pmbus_tool.py -t linux:12 read 0x8d     # READ_TEMPERATURE_1
python tools/pmbus_tool.py -t linux:12 alert         # who pulled SMBALERT#?
make pytest PMBUS_TRANSPORT=hub:/dev/ttyACM1         # the device suite, on real hardware
```

The tools need Python 3 only; the hub transport also needs `pyserial`.

## Layout

```
lib/pmbus/        Portable PMBus library, no OS dependencies
  src/target.c      SMBus transaction engine: framing, PEC, ARA, execute-at-STOP
  src/device.c      The device model: command table, limits, status, simulation
  src/format.c      LINEAR11, ULINEAR16, CRC-8
app/              Zephyr application for the FRDM-MCXA153
  src/pmbus_i2c.c   LPI2C target glue, SMBus timeout, Alert Response Address
  src/storage.c     STORE/RESTORE_DEFAULT_ALL on the flash storage partition
  src/main.c        Sensors, LEDs, the update loop
  src/shell_cmds.c  `pmbus` shell commands
tests/host/       C unit tests of the library (ASan/UBSan), incl. the Linux probe sequence
tests/sim/        The library as a shared object for the Python simulator transport
tests/hw/         pytest device suite: runs on sim, hub or linux transports
tools/            pmbus_tool.py and the Python PMBus client
docs/             Hookup, bring-up, command reference, security review
```

The library is kept apart from Zephyr so it can be tested on the PC and, later, reused on
other MCUs or proposed upstream as a Zephyr PMBus target sample.

## Verified on hardware

On a FRDM-MCXA153, flashed with pyOCD (`make flash RUNNER=pyocd`, after
`pyocd pack install mcxa153`):

- Boot, the shell, and `PMBus target at 0x58`.
- The P3T1755 over I3C and the die temperature through the ADC.
- Status latching, fault shutdown, the simulated fan and status injection, from the shell.
- P2_5 is the mikroBUS INT pin (Zephyr's board devicetree agrees); not yet measured.

## Not yet verified on hardware

These need a PMBus host on J5 (a second FRDM board with the hub firmware, the LX2160ARDB,
or a Linux I2C adapter):

- LPI2C0 target traffic on P3_27/P3_28 with Zephyr's LPI2C target driver: repeated starts,
  the extra byte it prefetches at the end of a read, clock stretching per byte.
- Answering the Alert Response Address by toggling `SCFGR1[SAEN]` while the bus is idle.
- The SMBus timeout recovery (unregister and re-register the target).
- `STORE_DEFAULT_ALL`: the flash erase blocks interrupts, so the target stretches SCL while
  it runs; the length needs measuring against the host's timeout.

See [docs/bringup.md](docs/bringup.md) for the checklist.

## License

Apache-2.0
