# Supported PMBus commands

Single page (`PAGE` accepts 0 and 0xFF). `VOUT_MODE` is 0x17: linear, exponent −9, so
output voltages (marked **V** below) are ULINEAR16 in 1/512 V steps. Every other numeric
value is LINEAR11; times are in milliseconds. Any command not listed reads as all ones
(with no valid PEC) and sets `STATUS_CML` bit 7, invalid command. `QUERY` reports exactly
this table.

## Control and configuration

| Code | Command | Type | Access | Default |
|---|---|---|---|---|
| 0x00 | PAGE | byte | R/W | 0 |
| 0x01 | OPERATION | byte | R/W | 0x80 (on). Bits 7:6 = 00 immediate off, 01 soft off (TOFF_*), 10 on; 5:4 margin low/high; 3:2 = 01 ignore VOUT faults while margined |
| 0x02 | ON_OFF_CONFIG | byte | R/W | 0x18: obey OPERATION, ignore CONTROL. Bit 0: CONTROL turns off immediately (1) or via TOFF_* (0) |
| 0x03 | CLEAR_FAULTS | send byte | W | |
| 0x10 | WRITE_PROTECT | byte | R/W | 0x00. Accepts 0x00, 0x20, 0x40, 0x80 |
| 0x11 | STORE_DEFAULT_ALL | send byte | W | Writes flash; `STATUS_BYTE` BUSY while it runs; refused while busy |
| 0x12 | RESTORE_DEFAULT_ALL | send byte | W | Stored config if valid, else the built-in one |
| 0x19 | CAPABILITY | byte | R | 0xB0: PEC, 400 kHz, SMBALERT# |
| 0x1A | QUERY | block process call | R | |
| 0x1B | SMBALERT_MASK | write word / process call read | R/W | 0 for every register. Write `[status code, mask]`; read with the status code as the process call's byte |
| 0x20 | VOUT_MODE | byte | R | 0x17 |

## Output voltage

| Code | Command | Type | Access | Default |
|---|---|---|---|---|
| 0x21 | VOUT_COMMAND | word **V** | R/W | 12.0 V |
| 0x24 | VOUT_MAX | word **V** | R/W | 14.4 V. Set points above it are clamped and set `STATUS_VOUT` bit 3 |
| 0x25 | VOUT_MARGIN_HIGH | word **V** | R/W | 12.6 V |
| 0x26 | VOUT_MARGIN_LOW | word **V** | R/W | 11.4 V |
| 0x2B | VOUT_MIN | word **V** | R/W | 9.6 V. Set points below it are clamped and set `STATUS_VOUT` bit 3 |
| 0x5E | POWER_GOOD_ON | word **V** | R/W | 11.0 V |
| 0x5F | POWER_GOOD_OFF | word **V** | R/W | 10.5 V |

## Input

| Code | Command | Type | Access | Default |
|---|---|---|---|---|
| 0x35 | VIN_ON | word | R/W | 43 V: the unit may start once VIN reaches this |
| 0x36 | VIN_OFF | word | R/W | 41 V: the unit stops below this (`STATUS_INPUT` bit 3) |

## Sequencing

| Code | Command | Type | Access | Default |
|---|---|---|---|---|
| 0x60 | TON_DELAY | word | R/W | 0 ms |
| 0x61 | TON_RISE | word | R/W | 0 ms. VOUT ramps linearly |
| 0x62 | TON_MAX_FAULT_LIMIT | word | R/W | 0 (no limit). VOUT must pass VOUT_UV_FAULT_LIMIT this long after the rise starts |
| 0x63 | TON_MAX_FAULT_RESPONSE | byte | R/W | 0x80 |
| 0x64 | TOFF_DELAY | word | R/W | 0 ms |
| 0x65 | TOFF_FALL | word | R/W | 0 ms |
| 0x66 | TOFF_MAX_WARN_LIMIT | word | R/W | 0 (no limit). VOUT must fall below 12.5 % this long after the fall starts |

The defaults are zero so the output follows commands at once; set them for realistic
ramps. The firmware evaluates the sequencing every 10 ms (`CONFIG_PMBUS_TICK_MS`).

## Limits and fault responses

| Code | Command | Default | Code | Response | Default |
|---|---|---|---|---|---|
| 0x40 | VOUT_OV_FAULT_LIMIT **V** | 13.8 V | 0x41 | VOUT_OV_FAULT_RESPONSE | 0x80 |
| 0x42 | VOUT_OV_WARN_LIMIT **V** | 13.2 V | | | |
| 0x43 | VOUT_UV_WARN_LIMIT **V** | 10.8 V | | | |
| 0x44 | VOUT_UV_FAULT_LIMIT **V** | 10.2 V | 0x45 | VOUT_UV_FAULT_RESPONSE | 0x80 |
| 0x46 | IOUT_OC_FAULT_LIMIT | 10 A | 0x47 | IOUT_OC_FAULT_RESPONSE | 0xC0 |
| 0x48 | IOUT_OC_LV_FAULT_LIMIT **V** | 6.0 V | 0x49 | IOUT_OC_LV_FAULT_RESPONSE | 0x80 |
| 0x4A | IOUT_OC_WARN_LIMIT | 8 A | | | |
| 0x4F | OT_FAULT_LIMIT | 100 °C | 0x50 | OT_FAULT_RESPONSE | 0x80 |
| 0x51 | OT_WARN_LIMIT | 85 °C | | | |
| 0x52 | UT_WARN_LIMIT | −20 °C | | | |
| 0x53 | UT_FAULT_LIMIT | −40 °C | 0x54 | UT_FAULT_RESPONSE | 0x00 |
| 0x55 | VIN_OV_FAULT_LIMIT | 60 V | 0x56 | VIN_OV_FAULT_RESPONSE | 0x80 |
| 0x57 | VIN_OV_WARN_LIMIT | 58 V | | | |
| 0x58 | VIN_UV_WARN_LIMIT | 44 V | | | |
| 0x59 | VIN_UV_FAULT_LIMIT | 40 V | 0x5A | VIN_UV_FAULT_RESPONSE | 0xC0 |
| 0x5B | IIN_OC_FAULT_LIMIT | 3.0 A | 0x5C | IIN_OC_FAULT_RESPONSE | 0x00 |
| 0x5D | IIN_OC_WARN_LIMIT | 2.5 A | | | |
| 0x68 | POUT_OP_FAULT_LIMIT | 132 W | 0x69 | POUT_OP_FAULT_RESPONSE | 0x00 |
| 0x6A | POUT_OP_WARN_LIMIT | 115 W | | | |
| 0x6B | PIN_OP_WARN_LIMIT | 125 W | | | |

### Response bytes (PMBus Part II section 10.5)

- Bits 7:6: **00** continue; **01** continue for the delay, then shut down; **10** shut
  down; **11** off while the fault is present, back on when it clears.
- Bits 5:3, retries after a shutdown: **000** latch off (until OPERATION or CONTROL turns
  the unit off and on); **001–110** that many restart attempts, then latch off; **111**
  retry forever.
- Bits 2:0: the delay, and the time between restart attempts, in units of **100 ms**.

`IOUT_OC_FAULT_RESPONSE` bits 7:6 differ: **00** constant current at the limit; **01**
constant current while VOUT stays above `IOUT_OC_LV_FAULT_LIMIT`, then the LV response;
**10** constant current for the delay, then shut down; **11** shut down. The load is
resistive, so in constant current VOUT drops to `VOUT × limit / demand`.

`IIN_OC_FAULT_RESPONSE` and `POUT_OP_FAULT_RESPONSE` are interpreted in the standard format
above (no current or power limiting modes). A unit that is commanded off records faults
in the status registers but doesn't latch off for them.

## Status

| Code | Command | Type | Access | Notes |
|---|---|---|---|---|
| 0x78 | STATUS_BYTE | byte | R | |
| 0x79 | STATUS_WORD | word | R | High byte adds VOUT, IOUT/POUT, INPUT, MFR, POWER_GOOD#, FANS, OTHER |
| 0x7A | STATUS_VOUT | byte | R/W | OV/UV fault and warning, VOUT_MAX/MIN warning, TON_MAX fault, TOFF_MAX warning |
| 0x7B | STATUS_IOUT | byte | R/W | OC fault, OC-LV fault, OC warning, POUT OP fault and warning |
| 0x7C | STATUS_INPUT | byte | R/W | VIN OV/UV fault and warning, off for low input, IIN OC fault and warning, PIN OP warning |
| 0x7D | STATUS_TEMPERATURE | byte | R/W | OT/UT fault and warning |
| 0x7E | STATUS_CML | byte | R/W | Invalid command, invalid data, PEC, memory |
| 0x7F | STATUS_OTHER | byte | R/W | Nothing sets it except `MFR_SPECIFIC_03` |
| 0x80 | STATUS_MFR_SPECIFIC | byte | R/W | Nothing sets it except `MFR_SPECIFIC_03` |
| 0x81 | STATUS_FANS_1_2 | byte | R/W | Fan 1 fault (below 10 % of target for 2 s) and warning (below 50 %) |

Status bits latch; write 1 to a bit to clear it, or send `CLEAR_FAULTS`.

## Telemetry

| Code | Command | Type | Access | Source |
|---|---|---|---|---|
| 0x88 | READ_VIN | word | R | Simulated, 54 V |
| 0x89 | READ_IIN | word | R | PIN / VIN |
| 0x8B | READ_VOUT | word **V** | R | Follows the sequencing and current limiting |
| 0x8C | READ_IOUT | word | R | The simulated load, scaled with VOUT |
| 0x8D | READ_TEMPERATURE_1 | word | R | P3T1755 on the board |
| 0x8E | READ_TEMPERATURE_2 | word | R | MCU die |
| 0x90 | READ_FAN_SPEED_1 | word | R | Simulated fan, RPM |
| 0x96 | READ_POUT | word | R | VOUT × IOUT |
| 0x97 | READ_PIN | word | R | POUT / 95 % + 0.5 W standby |

## Fan

| Code | Command | Type | Access | Default |
|---|---|---|---|---|
| 0x3A | FAN_CONFIG_1_2 | byte | R/W | 0x90: fan 1 installed, commanded in % duty, 2 pulses per turn. Fan 2 bits must be 0 |
| 0x3B | FAN_COMMAND_1 | word | R/W | 0: % duty, or RPM if FAN_CONFIG bit 6 is set |

The fan follows a thermal curve on the hotter sensor (30 % at 30 °C to 100 % at 70 °C,
18000 RPM at 100 %) or `FAN_COMMAND_1`, whichever is faster, with a 1 s time constant.

## Identification and ratings

| Code | Command | Type | Access | Default / source |
|---|---|---|---|---|
| 0x98 | PMBUS_REVISION | byte | R | 0x33 (1.3) |
| 0x99 | MFR_ID | block | R/W | "EXAMPLE" |
| 0x9A | MFR_MODEL | block | R/W | "MCXA153-PMB-1" |
| 0x9B | MFR_REVISION | block | R/W | "A0" |
| 0x9C | MFR_LOCATION | block | R/W | "LAB" |
| 0x9D | MFR_DATE | block | R/W | "261122" |
| 0x9E | MFR_SERIAL | block | R/W | The first 8 bytes of the chip UID, in hex |
| 0xA0–0xA7 | MFR_VIN_MIN, _VIN_MAX, _IIN_MAX, _PIN_MAX, _VOUT_MIN **V**, _VOUT_MAX **V**, _IOUT_MAX, _POUT_MAX | word | R | 40 V, 60 V, 3 A, 130 W, 9.6 V, 14.4 V, 10 A, 120 W |
| 0xAD | IC_DEVICE_ID | block | R | "MCXA153" |
| 0xAE | IC_DEVICE_REV | block | R | "0" |
| 0xB0–0xBF | USER_DATA_00 … _15 | block | R/W | Empty; up to 16 bytes each, stored by STORE_DEFAULT_ALL |
| 0xC0 | MFR_MAX_TEMP_1 | word | R | 100 °C |
| 0xC1 | MFR_MAX_TEMP_2 | word | R | 105 °C |

The MFR strings hold up to 20 bytes.

## Simulation knobs (manufacturer specific)

| Code | Command | Type | Access | Meaning |
|---|---|---|---|---|
| 0xD0 | MFR_SPECIFIC_00 | word | R/W | Load current at the set point, 5 A |
| 0xD1 | MFR_SPECIFIC_01 | word | R/W | Input voltage, 54 V |
| 0xD2 | MFR_SPECIFIC_02 | byte | R/W | Bit 0: the fan has failed |
| 0xD3 | MFR_SPECIFIC_03 | word | W | Inject status: low byte a STATUS_x command code, high byte the bits to latch |

These are not part of the stored config. The shell has the same knobs: `pmbus load`,
`vin`, `temp`, `fan`, `inject`.

## Behaviour

- **Writes take effect at STOP**, which gives Group Command semantics.
- Writes with the wrong number of data bytes set `STATUS_CML` bit 6; a wrong PEC sets bit 5.
  Either way the write is ignored.
- **WRITE_PROTECT** 0x80/0x40/0x20 follow PMBus 1.3, except that `CLEAR_FAULTS` and `PAGE`
  stay writable at every level, so a host that clears faults after probing (Linux does)
  doesn't trip over a write-protected device.
- **SMBALERT#** goes low when a status bit not masked by `SMBALERT_MASK` newly sets.
  Reading the Alert Response Address releases it (and reports 0x58); so does clearing
  every unmasked status bit.
- Out-of-range numbers saturate rather than wrap: a LINEAR11 word too large for the
  device's milli-unit arithmetic counts as the largest value it can hold.
