# Security review

Reviewed 2026-11-22, firmware with the full command set (about 110 PMBus commands).

## Threat model

The device sits on a PMBus/SMBus segment. Whoever can drive that bus (a host CPU, a BMC,
a compromised host, anything clipped onto the wires) can send it arbitrary bytes. PMBus has
**no authentication**: `WRITE_PROTECT` guards against mistakes, not attackers, since anyone
may clear it. So the questions are:

1. Can bus traffic corrupt memory, crash, or hang the firmware? (Must not.)
2. Can bus traffic do lasting damage, like wearing out flash or bricking the device? (Bound it.)
3. Can bus traffic make the device lie in ways a real PSU couldn't? (Make it switchable.)
4. What does physical access give? (Everything; document it.)

Controlling the simulated output (turning it off, changing set points) is *intended*
PMBus behaviour and not a finding.

## Attack surface

| Entry point | Trust | Code |
|---|---|---|
| I2C bytes on LPI2C0 (J5) | Untrusted | `app/src/pmbus_i2c.c` → `lib/pmbus/src/target.c` → `device.c` |
| The stored config record in flash | Semi-trusted (CRC-checked; altered only via SWD) | `app/src/storage.c`, `pmbus_device_config_valid()` |
| Zephyr shell on the MCU-Link USB serial port | Physical access | `app/src/shell_cmds.c` |
| SWD through the on-board MCU-Link | Physical access | — |

## Findings and fixes

| # | Severity | Finding | Fix |
|---|---|---|---|
| 1 | High | **LINEAR11 decode overflowed int32.** `y * 1000 * 2^n` for the largest words (e.g. 0x7BFF, 1023 × 2^15) wrapped to a negative value. A host could set a "huge" load that read back negative and **slipped past the overcurrent fault**, or limits that inverted. | Decoding saturates to the int32 range (`format.c`); telemetry arithmetic saturates too (`sat32()` in `device.c`). Tests: `huge_values`. |
| 2 | High | **Flash wear-out by `STORE_DEFAULT_ALL`.** Any host could loop the command; each one erased a flash sector. Flash survives a limited number of erases (typically 10k–100k), so a hostile host could wear out the storage sector in hours. | A store that changes nothing writes nothing; flash writes are at least `CONFIG_PMBUS_STORE_MIN_INTERVAL_MS` (5 s) apart, the device staying BUSY meanwhile; at most `CONFIG_PMBUS_STORE_MAX_PER_BOOT` (64) writes per boot, then `STATUS_CML` memory fault until reset. |
| 3 | Medium | **Race between two stores.** A second `STORE_DEFAULT_ALL` while the first was still writing overwrote the buffer being flashed, from interrupt context: a torn record (rejected by the CRC, so the saved config was silently lost). | The device refuses `STORE_DEFAULT_ALL` and `RESTORE_DEFAULT_ALL` while BUSY (`STATUS_CML` invalid data). Test: `store_hardening`. |
| 4 | Medium | **Stored record trusted blindly.** Only the CRC was checked. A record with a valid CRC but a string length above its buffer (20 bytes) would make `MFR_*` block reads copy past the buffer: an over-read leaking RAM onto the bus. Needs SWD to plant, so defence in depth. | `pmbus_device_config_valid()` checks version, every length, and enum-like fields before a stored record is used; an invalid one falls back to the built-in defaults. |
| 5 | Medium | **Large stack copies.** `RESTORE_DEFAULT_ALL` copied the whole config onto the **interrupt** stack; `pmbus status` copied the whole device (~1.1 KB with the new features) onto the 2 KB shell stack. | Restore now works in place (only the 21-byte serial is saved on the stack); the shell copies just the fields it prints. Measured worst case: shell 51 %, main 42 %, ISR 11 % (+ ~450 B estimated for the I2C path, from `-fstack-usage`). |
| 6 | Medium | **No stack overflow detection.** An overflow would have silently corrupted memory. | `CONFIG_HW_STACK_PROTECTION=y` (ARMv8-M stack limit registers): an overflow faults at once. Stack canaries were left off: they need an entropy source this SoC lacks in Zephyr, and a fixed canary is guessable. |
| 7 | Low | **Simulation knobs on the bus.** `MFR_SPECIFIC_00–03` let any host fake loads, input voltage, fan failures and arbitrary status bits, e.g. false fault alarms that a real PSU couldn't raise. | `CONFIG_PMBUS_SIM_KNOBS` (default on, for the bench) removes them from the bus; they then read as unsupported. Test: `sim_knobs_off`. |
| 8 | Low | **Shell accepted junk numbers.** `strtol` without checks: `pmbus load abc` became 0 A, out-of-range values were taken as is. | `parse_long()` rejects junk and checks ranges. |

## Checked and found sound

- **No dynamic memory.** No `malloc`/`k_malloc`, `CONFIG_HEAP_MEM_POOL_SIZE=0`: memory leaks
  can't happen, and RAM use is fixed at link time (14.6 KB of 24 KB).
- **Receive buffer.** `pmbus_target_write_byte()` NACKs and flags overflow at 36 bytes; an
  overflowed write is rejected whole.
- **Block lengths.** A block write's count byte must match the bytes actually received;
  handlers then bound-check against their own buffers (20 bytes for MFR strings, 16 for
  USER_DATA). Process calls take exactly one byte.
- **Transmit buffer.** Handlers get `max` (1, 2 or 32) and the engine rejects anything
  longer; count + 32 + PEC fits the 34-byte buffer.
- **Indexes.** Every table index comes from a lookup over a constant table, never straight
  from a bus byte.
- **Division.** Every divisor is a constant or checked non-zero (`set > 0`, `vin > 0`,
  ramp times).
- **Timers.** Time words saturate at about 36 minutes; timer arithmetic can't overflow.
- **Liveness.** No recursion; the output state machine is bounded (8 steps); a host
  holding SCL low gets the interface reset after 30 ms.
- **Fuzzing.** `fuzz_bus` drives the engine with 1,000,000 random bus events (starts,
  bytes biased to real commands, reads, Alert Response reads, aborts, time) under
  AddressSanitizer and UndefinedBehaviorSanitizer: no reports, and the device works
  afterwards.

## Residual risks (by design, or physical access)

- **PMBus has no authentication.** Any bus host can turn the simulated output off, change
  limits and clear `WRITE_PROTECT`. That's the protocol; real PSUs are the same.
- **Bounded flash wear remains.** 64 writes per boot, 5 s apart. A host that can also
  power-cycle the board could keep wearing the sector; a wear-levelled log would stretch
  that further.
- **Interrupts are off during a flash erase,** so the target stretches SCL. A host with a
  short timeout may give up on that transaction (the store still completes).
- **Physical access is total.** The MCU-Link's SWD can read and rewrite all of flash, and
  the shell steers the simulation. For a deployed board: build with `CONFIG_SHELL=n` and
  `CONFIG_PMBUS_SIM_KNOBS=n`, and consider the MCX A153's debug access restrictions (see
  NXP's reference manual; misconfiguring them can lock the part).
- **`MFR_SERIAL` exposes the chip's unique ID** on the bus, as real PSUs expose their serial
  numbers.
- **Supply chain:** Zephyr is pinned by tag (`west.yml`) and `uv` by version and checksum;
  the Python packages `bootstrap.sh` installs are not pinned.
