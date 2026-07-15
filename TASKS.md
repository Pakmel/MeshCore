# TASKS – MeshBuoy firmware

Persistent session memory for Claude Code. Check off with [x] and add notes
below each item in the same commit as the code. Never remove items, strike them
instead.

## Round 1 – Build environment and baseline (no custom code yet)

* [x] Fork meshcore-dev/MeshCore, clone locally, create branch `meshbuoy`
      Fork: https://github.com/Pakmel/MeshCore, cloned 2026-07-15. Upstream main
      at clone time: commit 219812b9 (2026-07-13). Branch created as `badboj`,
      renamed to `meshbuoy` on 2026-07-15 (project renamed from Badboj to
      MeshBuoy for public sharing in English); old `badboj` branch deleted on
      origin after the rename push. CLAUDE.md and TASKS.md moved into the repo
      root in the same commit. Note: `C:\Users\pakme` is itself a (likely
      unintentional) empty git repo root with no commits, not touched by this
      work.
* [x] Perform RAK's BSP patch for PlatformIO per their guide, document the exact
      steps here as a note (versions, paths) for reproducibility
      PlatformIO Core was not installed (`pio` not found in PATH). Installed via
      the official installer script per docs.platformio.org
      (https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py),
      run with the system Python 3.13.14 (`python.exe get-platformio.py`).
      Result: PlatformIO Core 6.1.19 in a venv at `C:\Users\pakme\.platformio\penv`;
      added `C:\Users\pakme\.platformio\penv\Scripts` to the user PATH so `pio`
      resolves in both PowerShell and bash. CLI only, no VS Code extension.

      Investigated RAK's official BSP patch (RAK_PATCH_V2, from
      github.com/RAKWireless/WisBlock/tree/master/PlatformIO) before applying it:
      it drops a project-local `rakwireless/` folder with board JSON + variant
      files (variant folder literally named `rak4630`) and expects
      `board = rak4630` in platformio.ini. This MeshCore fork does not use that
      mechanism at all: it ships its own `boards/rak4631.json`
      (`"variant": "WisCore_RAK4631_Board"`) and `variants/rak4631/` in the repo
      root, and pins a custom Arduino core fork via `platform_packages` in the
      root platformio.ini (`nrf52_base`):
      `framework-arduinoadafruitnrf52 @ https://github.com/meshcore-dev/Adafruit_nRF52_Arduino#d541301`.
      Checked that fork's `variants/` directory on GitHub at commit d541301 -
      no `WisCore_RAK4631_Board` folder exists there either (that folder only
      exists in RAKWireless/RAK-nRF52-Arduino, RAK's full separate BSP, which is
      NOT what this repo references).

      Tested empirically instead of assuming: ran `pio run -e RAK_4631_sensor`
      with zero manual patching. Build succeeded, RAM/flash report printed,
      confirmed via `pio pkg list` that the installed
      `framework-arduinoadafruitnrf52` is version 1.10701.0 (meshcore-dev fork)
      and its local `variants/` folder (under
      `C:\Users\pakme\.platformio\packages\framework-arduinoadafruitnrf52\variants\`)
      has no RAK entry at all, confirming the build does not depend on the
      low-level Arduino "variant" folder mechanism for this board - MeshCore's
      own `RAK4631Board.cpp/h` fully cover board init.
      **Conclusion: no manual RAK BSP patch step is needed for this fork/repo
      setup.** CLAUDE.md's "Byggmiljö" section states this as a required known
      pitfall; flagged to project owner as inaccurate for this specific setup
      (see chat) rather than silently rewritten, since it touches a documented
      assumption.
* [x] Build stock `simple_sensor` for the RAK4631 target with zero errors
      Built via `pio run -e RAK_4631_sensor` (env defined in
      `variants/rak4631/platformio.ini`, builds `examples/simple_sensor`).
      Result: zero errors, zero warnings (build_flags include `-w`, so the
      compiler's own warnings are suppressed by project config; no PlatformIO/
      linker errors or warnings either way, checked by grepping the full log
      for "error"/"warning" - 0 hits both). RAM 12.2% (28712/235520 bytes),
      Flash 62.9% (512936/815104 bytes). Toolchain: platform nordicnrf52 @
      10.12.0, toolchain-gccarmnoneeabi @ 1.70201.0. Build artifacts in
      `.pio/build/RAK_4631_sensor/`: firmware.elf, firmware.hex, firmware.zip
      (DFU OTA package), and firmware.uf2 (1,026,048 bytes, generated via
      `pio run -e RAK_4631_sensor -t create_uf2` - this target is not run
      automatically by a plain `pio run`). UF2 is ready for manual flashing.
* [x] Flash and verify boot over serial (115200), note firmware version
      Flashed via UF2 (double-tap reset, drive appeared as `E:\` labeled
      `RAK4631`, firmware.uf2 copied manually by project owner). Serial
      verified on COM40 @ 115200 via PuTTY. Boot line `Sensor ID: <hex>`
      confirmed present. CLI responsive; `ver` command (in
      `src/helpers/CommonCLI.cpp`) confirmed as the version query - firmware
      is v1.16.0 (build 6 Jun 2026), matching `FIRMWARE_VERSION` in
      `examples/simple_sensor/SensorMesh.h`. That's >=1.11, so the CPU-temp
      pull-telemetry bug noted in CLAUDE.md's known pitfalls applies to this
      base and is relevant once telemetry is touched later. Repeated
      `RadioLibWrapper: readData(-7)` in the debug log is
      `RADIOLIB_ERR_CRC_MISMATCH` (confirmed in `RadioLib/src/TypeDef.h`) -
      expected noise from other mesh traffic on a shared frequency, not a
      fault.
* [x] Copy examples/simple_sensor to examples/meshbuoy, own env in
      platformio.ini, build again with zero errors
      Copied directory as-is (no internal renames - SensorMesh.cpp/h etc.
      keep their names, only the containing example folder is new). Added
      `[env:RAK_4631_meshbuoy]` in `variants/rak4631/platformio.ini`,
      extending `rak4631`, building `+<../examples/meshbuoy>`. Left out
      `DISPLAY_CLASS=SSD1306Display` (present in the stock `RAK_4631_sensor`
      env) since CLAUDE.md's Hardware section lists no display for this
      board - RAK19007 baseplate only. Set `ADVERT_NAME` to `"MeshBuoy"`.
      Built via `pio run -e RAK_4631_meshbuoy`: zero errors, zero warnings
      (grepped full log, 0 hits for both). RAM 12.1% (28488/235520 bytes),
      Flash 60.9% (496376/815104 bytes) - slightly smaller than
      RAK_4631_sensor since the display driver isn't linked in.
* [x] Create src/meshbuoy_version.h (0.1.0) and src/meshbuoy_config.h with channel
      name, interval, and retry window as constants
      `src/meshbuoy_version.h`: `#define MESHBUOY_VERSION "0.1.0"`.
      `src/meshbuoy_config.h`: `MESHBUOY_CHANNEL_NAME "#watertemp"` (asked
      project owner to confirm since CLAUDE.md only gave `#badtemp` as an
      "e.g." example, not a firm decision; `#watertemp` was chosen - English,
      describes content not project name), `MESHBUOY_SEND_INTERVAL_SECS`
      (60UL*60UL = 1 hour, per architecture decision), `RETRY_WINDOW_S 30`
      (per CLAUDE.md/TASKS Round 4 starting value). Not yet wired into any
      code - these are declarations only, consumed starting Round 3/4.

## Round 2 – DS18B20 on WB_IO1

* [x] Add OneWire and DallasTemperature to lib_deps for meshbuoy
      Checked actual PlatformIO registry versions instead of guessing:
      `paulstoffregen/OneWire @ ^2.3.8` and `milesburton/DallasTemperature @ ^4.0.6`
      (both current latest at time of writing, confirmed via `pio pkg search`).
      Added to `lib_deps` of `[env:RAK_4631_meshbuoy]` in
      `variants/rak4631/platformio.ini`.
* [x] Init 1-Wire bus on WB_IO1 behind build flag MESHBUOY_DS18B20, 9-bit
      resolution, detection check at boot (flag is only set if the probe responds)
      `-D MESHBUOY_DS18B20=1` added to the env's build_flags. Confirmed
      `WB_IO1` is defined in this repo's own `variants/rak4631/variant.h`
      (`WB_IO1 = 17`, WisBlock base GPIO) before using it - did not pull the
      pin number from memory. New `examples/meshbuoy/WaterTempSensor.{h,cpp}`
      wraps OneWire+DallasTemperature; `begin()` calls `getDeviceCount()` +
      `getAddress()` and only sets `_detected = true` if a device actually
      answered, then `setResolution(9)`. `main.cpp` setup() logs
      "DS18B20 detected on WB_IO1" or "...NOT detected..." accordingly -
      never assumes presence.
* [x] Asynchronous reading: start conversion, fetch the value without blocking
      `setWaitForConversion(false)` + `requestTemperatures()` in
      `startConversion()`, which only starts the conversion and returns.
      `conversionDone()` gates the read on elapsed millis() since
      `startConversion()` vs. `DallasTemperature::millisToWaitForConversion(9)`
      (the library's own constant - 94 ms for 9-bit - not a hand-typed
      datasheet number). `readResult()` is only ever called after
      `conversionDone()` returns true, avoiding the classic 85.0C
      power-on-reset stale-read bug from reading too early.
* [x] Write temperature and battery voltage to serial every 10 seconds in test mode
      `main.cpp` loop() runs a small millis()-driven state machine (not
      loop-count-driven): `next_test_read_due` is an absolute millis()
      deadline, compared via the rollover-safe `(long)(now - due) >= 0`
      idiom. On each 10s tick it starts a conversion; once
      `conversionDone()`, classifies and prints the reading (see below) and
      reschedules 10s out.

      **Message contract, all 3 cases (added after initial Round 2 pass,
      once the full contract was specified):** CLAUDE.md "Message format"
      now documents case 1 (normal), case 2 (implausible, outside
      `PLAUSIBLE_MIN_C`/`PLAUSIBLE_MAX_C` = -5.0/45.0, sent with a `?` right
      after `C`), and case 3 (sensor error, `ERR(-127)` or `ERR(85)`, no
      temperature figure ever sent). Both constants added to
      `meshbuoy_config.h`. `WaterTempSensor::readResult()` replaced the old
      float-returning `readTempC()`: it reads the RAW scratchpad value via
      `DallasTemperature::getTemp()` rather than `getTempC()`, because this
      library version (4.0.6) already collapses both "disconnected" and
      "power-on-reset" down to the same `DEVICE_DISCONNECTED_C` (-127) at
      the `getTempC()` level - the two can only be told apart using the raw
      sentinels `DEVICE_DISCONNECTED_RAW` and `DEVICE_POWER_ON_RESET_RAW`
      (checked in `DallasTemperature.cpp`, not assumed). Verified this by
      reading the library source rather than guessing. The test-mode serial
      line now prints the exact contract string per case, prefixed with
      "case N (...)" so the applicable case is explicit in the log, e.g.
      `[DS18B20 test] case 1 (normal): Water: 18.5C Batt: 3.91V`.
* [ ] Verify against reference thermometer in a glass of water, deviation under 1 C
* [ ] Version 0.2.0

## Round 3 – Channel push

* [ ] Implement key derivation for hashtag channel: first 16 bytes of
      SHA256 of the channel name incl. #. Unit test against known example:
      `#test` should give `9cd8fcf22a47333b591d96a2b848b73f`
* [ ] Set channel name in meshbuoy_config.h, add the same hashtag channel in the
      mobile app
* [ ] Implement sending of PAYLOAD_TYPE_GRP_TXT with the format
      `Water: 18.5C Batt: 3.91V` (contract, see CLAUDE.md)
* [ ] Test interval 2 minutes, verify the message appears in the app via at least
      one repeater (not just direct link)
* [ ] Verify in the MeshMonitor channel feed
* [ ] Version 0.3.0

## Round 4 – Retry via own echo

* [ ] After TX: stay in RX for RETRY_WINDOW_S (start 30 s), match incoming
      packets against own packet hash
* [ ] No repeat heard: resend ONCE, then done regardless
* [ ] Log outcome to serial: "repeat heard" / "retry sent" / "gave up"
* [ ] Test by temporarily turning off the nearest repeater and observe the retry
      firing
* [ ] Version 0.4.0

## Round 5 – Sleep cycle and power budget

* [ ] RTC wakeup every full hour (System ON sleep, RTC must survive)
* [ ] Sequence: wake, start DS18B20 conversion, read battery, send, retry window,
      sleep. Total awake time under 60 s per hour
* [ ] Verify the radio is actually down between cycles (power measurement with
      multimeter or PPK, target under 0.5 mA average over at least 6 h)
* [ ] Note measured values here: sleep mA, RX mA, TX peak, average
* [ ] Version 0.5.0

## Round 6 – Field test before deployment

* [ ] 48 h dry test on balcony on battery only, all hourly transmissions received
* [ ] Solar panels connected, verify charging (red LED / rising voltage)
* [ ] Open-circuit voltage per panel measured in full sunlight, under 5.5 V after
      diode
* [ ] Deployment in water temperature environment, verify link via KSD repeater
* [ ] Version 1.0.0 when the definition of done in CLAUDE.md is fulfilled

## Parked / later

* [ ] Parsing in Home Assistant into a real sensor entity (regex on channel message)
* [ ] Winter test: icing, battery in cold
* [ ] Possible YouTube video when 1.0.0 is in the water
