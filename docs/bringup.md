# First hardware bring-up

The firmware and tests were written and checked without a board (host tests, simulator, CI).
This is the checklist for the first session with a real FRDM-MCXA153.

## Set up a fresh Linux host

```bash
mkdir -p ~/mcxa153-workspace && cd ~/mcxa153-workspace
git clone https://github.com/wrouwet/pmbus-enablement.git
cd pmbus-enablement
sudo scripts/install-host-deps.sh             # apt packages, udev rules for MCU-Link (1fc9)
scripts/bootstrap.sh                          # Python, west, Zephyr v4.5.0, Arm toolchain
make test && make build
```

`make flash` uses LinkServer from `/usr/local/LinkServer` (NXP download, needs a licence click).
Replug the board after the udev rules go in. On J15 (MCU-Link) it shows up as `1fc9:0143` plus a
`ttyACM` console.

## Checklist

1. `make flash`, then `make monitor`: the log shows `PMBus target at 0x58`; `pmbus status` works.
2. Temperatures: `pmbus status` shows READ_TEMPERATURE_1 (P3T1755 on I3C0) and _2 (die). If the
   P3T1755 fails, check the I3C pinctrl and the sensor alias in the overlay.
3. SMBALERT# pin: check that P2_5 really is the mikroBUS INT pin on J5 (board schematic). Fix
   `smbalert-gpios` in `app/boards/frdm_mcxa153.overlay` if not.
4. Bus traffic: a second FRDM-MCXA153 with the
   [usb-i2c-hub](https://github.com/wrouwet/frdm-mcxa153-usb-i2c-hub) firmware, J5 to J5 (SDA, SCL,
   GND, INT), pull-ups to 3.3 V. Then
   `make pytest PMBUS_TRANSPORT=hub:/dev/ttyACMx`: all 19 tests should pass as on `sim`.
5. Alert Response Address: `tests/hw` covers it. If it fails, the suspect is
   `is_alert_response_read()` in `app/src/pmbus_i2c.c` (SASR RADDR/ANV behaviour is unverified).
6. SMBus timeout: stop the host mid-transaction (or hold SCL low > 35 ms); the log shows
   `SMBus timeout, resetting the target interface` and the next transaction works.
7. STORE_DEFAULT_ALL: measure how long SCL is stretched during the flash erase; compare with the
   host's timeout.

Results go into the README's "Not yet verified on hardware" list.
