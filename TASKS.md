# TASKS – MeshTemp firmware

Persistent session memory for Claude Code. Check off with [x] and add notes
below each item in the same commit as the code. Never remove items, strike them
instead.

## Working rules

* A report of a successful build must always be accompanied by the build output
  (last lines, including the SUCCESS line) and the resulting files' timestamps.
  `pio run` alone does not produce a `.uf2` - that needs the `create_uf2` target
  run explicitly (see Round 1 note below) - so "build succeeded" is not proof a
  new `.uf2` exists. Incident: a rebuild was reported after the channel rename
  to `#tempsensortest` without actually running `create_uf2` or checking the
  `.uf2` timestamp, so no new flashable file existed despite the claim.

## Round 1 – Build environment and baseline (no custom code yet)

* [x] Fork meshcore-dev/MeshCore, clone locally, create branch `meshtemp`
      Fork: https://github.com/Pakmel/MeshCore, cloned 2026-07-15. Upstream main
      at clone time: commit 219812b9 (2026-07-13). Branch created as `badboj`,
      renamed to `meshtemp` on 2026-07-15 (project renamed from Badboj to
      MeshTemp for public sharing in English); old `badboj` branch deleted on
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
* [x] Copy examples/simple_sensor to examples/meshtemp, own env in
      platformio.ini, build again with zero errors
      Copied directory as-is (no internal renames - SensorMesh.cpp/h etc.
      keep their names, only the containing example folder is new). Added
      `[env:RAK_4631_meshtemp]` in `variants/rak4631/platformio.ini`,
      extending `rak4631`, building `+<../examples/meshtemp>`. Left out
      `DISPLAY_CLASS=SSD1306Display` (present in the stock `RAK_4631_sensor`
      env) since CLAUDE.md's Hardware section lists no display for this
      board - RAK19007 baseplate only. Set `ADVERT_NAME` to `"MeshTemp"`.
      Built via `pio run -e RAK_4631_meshtemp`: zero errors, zero warnings
      (grepped full log, 0 hits for both). RAM 12.1% (28488/235520 bytes),
      Flash 60.9% (496376/815104 bytes) - slightly smaller than
      RAK_4631_sensor since the display driver isn't linked in.
* [x] Create src/meshtemp_version.h (0.1.0) and src/meshtemp_config.h with channel
      name, interval, and retry window as constants
      `src/meshtemp_version.h`: `#define MESHTEMP_VERSION "0.1.0"`.
      `src/meshtemp_config.h`: `MESHTEMP_CHANNEL_NAME "#watertemp"` (asked
      project owner to confirm since CLAUDE.md only gave `#badtemp` as an
      "e.g." example, not a firm decision; `#watertemp` was chosen - English,
      describes content not project name), `MESHTEMP_SEND_INTERVAL_SECS`
      (60UL*60UL = 1 hour, per architecture decision), `RETRY_WINDOW_S 30`
      (per CLAUDE.md/TASKS Round 4 starting value). Not yet wired into any
      code - these are declarations only, consumed starting Round 3/4.

## Round 2 – DS18B20 on WB_IO1

* [x] Add OneWire and DallasTemperature to lib_deps for meshtemp
      Checked actual PlatformIO registry versions instead of guessing:
      `paulstoffregen/OneWire @ ^2.3.8` and `milesburton/DallasTemperature @ ^4.0.6`
      (both current latest at time of writing, confirmed via `pio pkg search`).
      Added to `lib_deps` of `[env:RAK_4631_meshtemp]` in
      `variants/rak4631/platformio.ini`.
* [x] Init 1-Wire bus on WB_IO1 behind build flag MESHTEMP_DS18B20, 9-bit
      resolution, detection check at boot (flag is only set if the probe responds)
      `-D MESHTEMP_DS18B20=1` added to the env's build_flags. Confirmed
      `WB_IO1` is defined in this repo's own `variants/rak4631/variant.h`
      (`WB_IO1 = 17`, WisBlock base GPIO) before using it - did not pull the
      pin number from memory. New `examples/meshtemp/WaterTempSensor.{h,cpp}`
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
      `meshtemp_config.h`. `WaterTempSensor::readResult()` replaced the old
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
      Moved to "Weekend hardware pass" below - project owner does hardware
      passes on weekends, see that section for exact steps.
* [x] Version 0.2.0
      Bumped ahead of the hardware verification above, per explicit
      project-owner instruction: code builds with zero errors/warnings
      (`pio run -e RAK_4631_meshtemp`), so the version bump gate in
      CLAUDE.md's "Versioning" section is satisfied even though the
      DS18B20 accuracy check against a reference thermometer is still
      pending. `src/meshtemp_version.h` updated to `"0.2.0"`. Also noticed
      CLAUDE.md already claimed "the version is written to serial at boot"
      but nothing actually printed it - wired that up now
      (`Serial.println(MESHTEMP_VERSION)` in `main.cpp` setup(), right
      after `Serial.begin()`) so the claim is true rather than aspirational.

## Weekend hardware pass

Pending manual verification - project owner runs hardware steps on
weekends and pastes results back.

* [ ] Connect DS18B20 to the RAK4631: VDD -> 3.3V, GND -> GND, data -> WB_IO1,
      with a 4.7k pullup resistor between data and VDD (see CLAUDE.md Hardware).
* [ ] Double-tap reset for bootloader, copy
      `.pio/build/RAK_4631_meshtemp/firmware.uf2` to the drive that appears
      (labeled `RAK4631` last time, e.g. `E:\`).
* [ ] Open a serial terminal at 115200 baud **before** resetting again, so the
      boot lines aren't missed (RAK4631's USB CDC doesn't buffer).
* [ ] Confirm the boot log shows `DS18B20 detected on WB_IO1` (not
      `...NOT detected...` - that means a wiring or pullup problem).
* [ ] Watch the every-10-second test lines, now case-labelled, e.g.
      `[DS18B20 test] case 1 (normal): Water: 18.5C Batt: 3.91V`. Expect
      case 1 lines in normal room-temperature air; case 2/3 would only be
      expected if something is actually wrong (out of the -5..45C plausible
      range, or the probe misbehaving).
* [ ] Dip the probe in a glass of water alongside a reference thermometer,
      compare readings - deviation must be under 1 C. Note the actual
      deviation here once measured.
* [ ] Confirm boot also logs `[channel key self-check] #test -> ... PASS`
      (see Round 3) - if it says FAIL, stop and report back before trusting
      any channel message, something is wrong with the key derivation.
* [ ] In the MeshCore mobile app, add a channel matching `MESHTEMP_CHANNEL_NAME`
      in `src/meshtemp_config.h` byte-for-byte, including the `#` (currently
      `#tempsensortest` - see note below, name may still change before release).
* [ ] Watch for `[channel send] Water: ...` lines every 2 minutes in serial,
      and confirm the same message shows up in the app's channel via at least
      one KSD repeater (not just a direct link to your phone).
* [ ] Verify the message also shows up in the MeshMonitor channel feed for
      that channel.
* [ ] Retry field test (Round 4): temporarily power off/move out of range
      the nearest KSD repeater, then watch serial for a full send cycle.
      Expect `[channel send] ...` followed by `[retry] retry sent` roughly
      `RETRY_WINDOW_S` (30s) later, since no repeater is around to echo the
      packet back. Turn the repeater back on and confirm a later cycle logs
      `[retry] repeat heard` instead, well before the 30s window elapses.
      Note actual timing observed here.
* [ ] Sleep-cycle power measurement (Round 5): flash
      `RAK_4631_meshtemp_sleep` (not the bench-test env), then disconnect
      USB entirely and run on battery power only - USB CDC keeps the nRF52
      from ever reaching its lowest sleep current and will skew any
      measurement taken over USB. Use a multimeter or PPK in series with
      the battery feed. Expect brief current spikes at each hourly wake
      (DS18B20 conversion, TX, up to 30s of RX during the retry window)
      and near-zero current the rest of the hour once
      `[sleep] ... powering off radio` has logged (note: you won't see
      that serial line once USB is disconnected - use the multimeter
      trace itself to identify the awake/asleep boundary, or leave USB
      connected for one first run just to confirm the awake-time log
      values look sane, then redo the real measurement on battery only).
      Target: under 0.5 mA average over at least 6 hours. Note actual
      sleep mA, RX mA, TX peak, and average here once measured - this is
      also Round 5's own "note measured values" line.

## Round 3 – Channel push

* [x] Implement key derivation for hashtag channel: first 16 bytes of
      SHA256 of the channel name incl. #. Unit test against known example:
      `#test` should give `9cd8fcf22a47333b591d96a2b848b73f`
      Researched existing codebase before writing anything (see agent
      research in chat): `mesh::Utils::sha256()` (`src/Utils.h`/`.cpp`) is
      the sanctioned SHA256 entry point, already used throughout the repo -
      no new crypto library pulled in. No existing code derives a
      `GroupChannel` key from a hashtag name (that pattern only existed for
      the unrelated `TransportKeyStore` region-key feature), so
      `WaterChannel::begin()` (new `examples/meshtemp/WaterChannel.{h,cpp}`)
      implements it fresh, mirroring `BaseChatMesh::setChannel`'s 128-bit-key
      path exactly: `channel.secret[0..16)` = sha256(channel name),
      `secret[16..32)` zeroed, `channel.hash` (1 byte) = sha256(secret, 16).
      The PlatformIO native/gtest env (`env:native`) can't be used for a
      real known-answer test here - its `test/mocks/SHA256.h` is an
      intentional no-op stub (checked before assuming it would work), so a
      real SHA256 only exists in the on-device Crypto library. Implemented
      as `WaterChannel::selfCheckKeyDerivation()`, a boot-time check that
      hashes the literal string `#test` and compares against the 16 expected
      bytes, logging PASS/FAIL to serial - matches CLAUDE.md's own phrasing
      ("verify the derivation against a known example"). Independently
      cross-checked the expected value with Python's hashlib before trusting
      it (`sha256(b'#test').digest()[:16].hex() ==
      '9cd8fcf22a47333b591d96a2b848b73f'` - confirmed true), so the
      algorithm is verified correct even before the on-device check has been
      run for real.
* [x] Set channel name in meshtemp_config.h, add the same hashtag channel in the
      mobile app
      Channel name (`#watertemp`) was already set in `meshtemp_config.h`
      during Round 1. Adding it in the mobile app is a manual step - see
      "Weekend hardware pass" below.

      **Update:** changed to `#tempsensortest` (project-owner instruction,
      scope change to a generic sensor node - see CLAUDE.md). The final
      channel name is not locked in - it's decided at release, and
      `MESHTEMP_CHANNEL_NAME` in `src/meshtemp_config.h` remains the only
      place it lives, so it can keep changing without touching any other
      file.

      First rebuild report (zero errors/warnings, both envs) was made
      without running the `create_uf2` target or checking file timestamps -
      no new `.uf2` actually existed, only `.elf`/`.hex` (see Round 1's own
      note that `pio run` alone doesn't produce a `.uf2`). Caught and
      corrected: re-ran `pio run -e RAK_4631_meshtemp -t create_uf2` and
      `pio run -e RAK_4631_meshtemp_sleep -t create_uf2`, both SUCCESS,
      confirmed `.pio\build\<env>\firmware.uf2` timestamps are current and
      `tempsensortest` (via `grep -a -o`) is present in both `.uf2` files.
      New working rule added above so this doesn't happen again.
* [x] Implement sending of PAYLOAD_TYPE_GRP_TXT with the format
      `Water: 18.5C Batt: 3.91V` (contract, see CLAUDE.md)
      `WaterChannel::formatMessage()` is now the single place that turns a
      `WaterReading` into the contract string (case 1/2/3), reused by both
      the test-mode serial log and the actual send, so the two can't drift
      apart. Send path in `main.cpp` loop(): builds `[timestamp(4) +
      flags(1) + message]` per the existing GRP_TXT plaintext convention
      (matched against `examples/simple_secure_chat/main.cpp`'s "public "
      command and `BaseChatMesh::sendGroupMessage`), calls
      `the_mesh.createGroupDatagram(PAYLOAD_TYPE_GRP_TXT, water_channel.channel, ...)`
      then `the_mesh.sendFlood(pkt)` - both public on `Mesh`, which
      `SensorMesh`/`MyMesh` already extend, so no new base class needed.
* [x] Test interval 2 minutes, verify the message appears in the app via at least
      one repeater (not just direct link)
      Code side done: sends every 2 minutes (`MESHTEMP_TEST_SEND_INTERVAL_MS`,
      local to `main.cpp`, distinct from the real hourly
      `MESHTEMP_SEND_INTERVAL_SECS` in `meshtemp_config.h` which is Round 5's
      job). App-side verification is manual - see "Weekend hardware pass".
* [ ] Verify in the MeshMonitor channel feed
      Manual - see "Weekend hardware pass" below.
* [x] Version 0.3.0
      Bumped ahead of the app/MeshMonitor verification above, same
      rationale as the 0.2.0 bump: hardware/app passes happen on weekends,
      code builds with zero errors/warnings
      (`pio run -e RAK_4631_meshtemp`). `src/meshtemp_version.h` updated to
      `"0.3.0"`.

## Round 4 – Retry via own echo

* [x] After TX: stay in RX for RETRY_WINDOW_S (start 30 s), match incoming
      packets against own packet hash
      Researched the RX pipeline before writing anything (see chat): MeshCore's
      own flood-dedup (`SimpleMeshTables::hasSeen()`, called inside
      `Mesh::onRecvPacket()`) already marks our own just-sent packet as "seen"
      right after `sendFlood()`, so a normal app-level receive callback would
      never see the echo - it'd be silently dropped as a duplicate before
      reaching us. Used `Dispatcher::logRx(Packet*, int, float)` instead: a
      protected virtual hook (`src/Dispatcher.h:159`) that fires for every
      raw incoming packet strictly before any dedup, already used by
      `examples/simple_repeater/MyMesh.cpp` for packet logging - same
      mechanism, different purpose here. New `MyMesh::logRx()` override in
      `main.cpp` feeds `Packet::calculatePacketHash()` (existing primitive,
      `src/Packet.cpp`, SHA256 of payload type + payload bytes) into a new
      `EchoRetry` class (`examples/meshtemp/EchoRetry.{h,cpp}`) that just
      holds the armed hash, a millis()-timed deadline, and an echo-heard
      flag - no Mesh dependency, easy to reason about independently. No
      explicit "stay in RX" code needed: confirmed the Dispatcher's normal
      `loop()`/`checkRecv()` re-arms `startReceive()` automatically once
      idle after a TX completes, as long as nothing calls radio/board sleep
      during the window - which nothing in meshtemp does yet (that's
      Round 5's job, and it will need to respect this window, see the
      Round 5 plan).
* [x] No repeat heard: resend ONCE, then done regardless
      `send_cycle_state` (IDLE / WAITING_FOR_ECHO) in `main.cpp` loop()
      gates this: on window expiry with no echo, `echo_retry.disarm()` is
      called *before* the retry send (so the retry's own transmission can
      never re-trigger this branch), then exactly one
      `meshtempSendChannelData()` call resends the cached `pending_data`
      bytes (same timestamp, same message - a real retry of the same
      attempt, not a fresh reading). State returns to IDLE immediately
      after, regardless of whether the retry send itself succeeded - no
      second echo wait.
* [x] Log outcome to serial: "repeat heard" / "retry sent" / "gave up"
      Three distinct outcomes, all logged: `[retry] repeat heard` (echo
      matched within the window), `[retry] retry sent` (no echo, retry
      packet built and transmitted), `[retry] gave up` (no echo, AND the
      retry's own `createGroupDatagram()` failed - distinct failure path
      from a successful retry transmission).
* [ ] Test by temporarily turning off the nearest repeater and observe the retry
      firing
      Moved to "Weekend hardware pass" below.
* [x] Version 0.4.0
      Bumped ahead of the field test above, same rationale as 0.2.0/0.3.0:
      code builds with zero errors/warnings
      (`pio run -e RAK_4631_meshtemp`). `src/meshtemp_version.h` updated to
      `"0.4.0"`.

## Round 5 – Sleep cycle and power budget

* [x] RTC wakeup every full hour (System ON sleep, RTC must survive)
      Researched existing sleep/wake primitives before designing anything
      (see chat): `NRF52Board::sleep(secs)` ignores `secs` entirely on
      nRF52 - it's an untimed WFE halt that wakes on any interrupt. There
      is no existing RTC2-timer-based timed wake anywhere in this
      codebase (only ESP32 boards have that, via ESP-IDF APIs that don't
      port to nRF52) - programming `NRF_RTC2` compare registers directly
      would need to be written from scratch against verified Nordic
      documentation, which per project decision is deferred unless
      Saturday's power measurement misses budget. Implemented instead as
      a loop of short `board.sleep(0)` naps, checked each time against a
      rollover-safe millis() deadline (same idiom as `EchoRetry`) -
      built entirely from already-tested primitives, at the cost of some
      uncertainty about how close it gets to the 0.5 mA target versus a
      real hardware timer (the eventual escalation path if needed).
      `VolatileRTCClock` (confirmed this is what real hardware uses - no
      I2C RTC chip on RAK19007, `AutoDiscoverRTCClock` always falls
      through to it) survives this fine since it's just a millis()-delta
      accumulator and millis() keeps running through WFE sleep; confirmed
      it does NOT survive SYSTEMOFF, consistent with why `initiateShutdown()`
      (the only existing SYSTEMOFF path in this repo, via
      `RAK4631Board::initiateShutdown`) is never called anywhere in this
      feature.
* [x] Sequence: wake, start DS18B20 conversion, read battery, send, retry window,
      sleep. Total awake time under 60 s per hour
      Major refactor of `examples/meshtemp/main.cpp` to satisfy the hard
      project requirement that `RAK_4631_meshtemp` (bench test) and
      `RAK_4631_meshtemp_sleep` (new, production) envs share byte-identical
      read/send/retry code, differing only in the scheduler layer and the
      interval constant (verified: grepped the file for
      `MESHTEMP_SLEEP_CYCLE`, all 4 hits are cleanly isolated to
      scheduler-only code - test-only timer state, the sleep/reinit helper
      functions, and the two loop() scheduler branches; `meshtempStartCycle()`/
      `meshtempCycleTick()`/`meshtempCycleIdle()` and everything they call
      are entirely unconditional). Fixed interval counted from cycle
      *start* (`cycle_started_at`), per project decision - avoids drift
      when a cycle needs the full retry window. First cycle now runs
      immediately at the end of setup() in both envs (was previously
      test-env-only timer-based).

      New production env `RAK_4631_meshtemp_sleep` in
      `variants/rak4631/platformio.ini`: `extends = env:RAK_4631_meshtemp`,
      build_flags = the base env's build_flags plus exactly one new define
      (`MESHTEMP_SLEEP_CYCLE=1`) - nothing else differs.

      Also fixed a latent gap while refactoring: previously, if the
      DS18B20 was never detected at boot, the periodic send simply never
      happened - CLAUDE.md's case 3 ("probe not responding") was
      unreachable for that specific fault. `meshtempStartCycle()` now
      always runs the cycle regardless of `water_sensor.detected()`,
      sending an immediate `ERR(-127)` when there's no probe, so a wiring
      fault is visible on the mesh instead of the node going silent
      forever.
* [x] Verify the radio is actually down between cycles (power measurement with
      multimeter or PPK, target under 0.5 mA average over at least 6 h)
      Code side: verified via source (not assumed) that
      `CustomSX1262Wrapper::powerOff()` calls the radio's real cold sleep
      (`sleep(false)`, ~160 nA per RadioLib/Semtech docs, config lost) and
      is directly callable on `radio_driver` (its static type, from
      `WRAPPER_CLASS`, already resolves to `CustomSX1262Wrapper` - no cast
      needed). Found and fixed a real bug before it ever ran: `powerOff()`
      does NOT reset `RadioLibWrapper`'s internal RX/TX state tracking (a
      file-static in `RadioLibWrappers.cpp`, untouched by `powerOff()`), so
      calling only `radio_init()` on wake would have left the dispatcher
      believing the radio was still in RX and it would never have called
      `startReceive()` again - a genuinely silent failure that would only
      have shown up as "stopped receiving after the first sleep cycle" with
      no error anywhere. Fixed by also calling `radio_driver.begin()`
      (resets that tracking to IDLE) and reapplying
      `radio_driver.setParams()`/`setTxPower()` from
      `the_mesh.getNodePrefs()` on every wake - see
      `meshtempReinitRadioAfterSleep()`. Actual current measurement is a
      hardware step - see "Weekend hardware pass".
* [ ] Note measured values here: sleep mA, RX mA, TX peak, average
      Pending - see "Weekend hardware pass".
* [x] Version 0.5.0
      Code builds with zero errors/warnings in both
      `RAK_4631_meshtemp` and `RAK_4631_meshtemp_sleep`.
      `src/meshtemp_version.h` updated to `"0.5.0"`. Actual power
      measurement pending (see above) - same "code clean, hardware
      pending" pattern as 0.2.0-0.4.0.
* [x] `ver` CLI command distinguishable from stock firmware
      The stock `FIRMWARE_VERSION` in `examples/meshtemp/SensorMesh.h` was
      still the literal upstream string `"v1.16.0"` - identical to what
      unmodified simple_sensor reports, so `ver` gave no way to tell a
      MeshTemp node from stock firmware over serial. Renamed the upstream
      value to `MESHCORE_UPSTREAM_VERSION` (kept as its own constant, not
      deleted) and redefined `FIRMWARE_VERSION` as
      `"MeshTemp " MESHTEMP_VERSION " (MeshCore " MESHCORE_UPSTREAM_VERSION ")"`,
      built from `MESHTEMP_VERSION` in `src/meshtemp_version.h` (the
      project's single version source, see CLAUDE.md "Versioning") so it
      can't drift out of sync. `ver` now answers
      `MeshTemp 0.5.0 (MeshCore v1.16.0) (Build: 6 Jun 2026)` - the
      trailing `(Build: ...)` group comes from `CommonCLI.cpp`'s existing
      `sprintf("%s (Build: %s)", ...)`, which was deliberately left
      untouched (shared file, out of scope) rather than merged into one
      parenthesized group, to keep upstream merges clean.
      Rebuilt both `RAK_4631_meshtemp` and `RAK_4631_meshtemp_sleep`
      (`-t create_uf2`): both SUCCESS, zero errors/warnings, new
      `firmware.uf2` timestamps confirmed current in both
      `.pio\build\<env>\` dirs, and the string
      `MeshTemp 0.5.0 (MeshCore v1.16.0)` confirmed present (via
      `grep -a -o`) in both `.uf2` files.

## Rename – MeshBuoy to MeshTemp (2026-07-18)

* [x] Rename project from MeshBuoy to MeshTemp - scope is a generic MeshCore
      temperature sensor, the name should match (buoy enclosure remains a
      possible follow-up, see CLAUDE.md "Future: buoy enclosure", not deleted)
      Branch: `git branch -m meshbuoy meshtemp`, pushed as `meshtemp` to
      origin, old `meshbuoy` branch deleted on origin (same pattern as the
      earlier `badboj` -> `meshbuoy` branch rename in Round 1).

      Files/folders: `src/meshbuoy_config.h` -> `src/meshtemp_config.h`,
      `src/meshbuoy_version.h` -> `src/meshtemp_version.h`,
      `examples/meshbuoy/` -> `examples/meshtemp/` (all via `git mv`, history
      preserved). All `MESHBUOY_*` macros renamed to `MESHTEMP_*`
      (`MESHTEMP_VERSION`, `MESHTEMP_DS18B20`, `MESHTEMP_SLEEP_CYCLE`,
      `MESHTEMP_CHANNEL_NAME`, `MESHTEMP_MSG_MAX_LEN`,
      `MESHTEMP_SEND_INTERVAL_SECS`, `MESHTEMP_TEST_SEND_INTERVAL_MS`,
      `MESHTEMP_TEST_READ_INTERVAL_MS`), all `meshbuoy*` function/file names
      to `meshtemp*` (e.g. `meshbuoySendChannelData` ->
      `meshtempSendChannelData`), and all `MeshBuoy`/`meshbuoy`/`MESHBUOY`
      prose in CLAUDE.md, TASKS.md, and the README to `MeshTemp`/`meshtemp`/
      `MESHTEMP`. Confirmed with `grep -rli meshbuoy` across the repo after
      the rename: zero hits. Standalone "buoy" (the "Future: buoy enclosure"
      section and its Parked-list pointer) was deliberately left untouched -
      only the project-name token changed.

      **Note on `MESHTEMP_DEFAULT_NAME`:** there was never a macro by that
      name (or `MESHBUOY_DEFAULT_NAME`) - the default node name has always
      come from the generic `ADVERT_NAME` build flag in
      `variants/rak4631/platformio.ini`, shared verbatim with every other
      example/env in this repo (`RAK_4631_repeater`, `RAK_4631_sensor`,
      etc.), not something this project owns or should rename. Its value
      changed from `"MeshBuoy"` to `"MeshTemp"` via the same blanket rename
      above, satisfying "the default node name constant becomes MeshTemp"
      without introducing an unused parallel constant that platformio.ini
      (plain text, no C preprocessor) couldn't actually source from a
      header anyway.

      Envs renamed `RAK_4631_meshbuoy` -> `RAK_4631_meshtemp`,
      `RAK_4631_meshbuoy_sleep` -> `RAK_4631_meshtemp_sleep`. Both rebuilt
      clean from scratch (new env names -> new `.pio/build/` dirs, full
      rebuild, ~2.5 min and ~1.5 min respectively): `RAK_4631_meshtemp`
      SUCCESS in 145.6s, `RAK_4631_meshtemp_sleep` SUCCESS in 96.4s, zero
      real compiler/linker errors or warnings (grepped both full logs;
      the only regex hits were a `git` package-manager note about a
      non-commit tag ref while fetching an unrelated library dependency,
      and the filename `SensirionErrors.cpp` - neither is a build error).
      Both `firmware.uf2` confirmed written just now (timestamps
      2026-07-18 11:27:29 and 11:29:30, checked at 11:29:39) at
      `.pio\build\RAK_4631_meshtemp\firmware.uf2` and
      `.pio\build\RAK_4631_meshtemp_sleep\firmware.uf2`. String
      `MeshTemp 0.5.0 (MeshCore v1.16.0)` confirmed present (`grep -a -o`)
      in both.

## Round 6 – Field test before deployment

* [ ] 48 h dry test on balcony on battery only, all hourly transmissions received
* [ ] Version 1.0.0 when the definition of done in CLAUDE.md is fulfilled

## Parked / later

* [ ] Parsing in Home Assistant into a real sensor entity (regex on channel message)
* [ ] Winter test: icing, battery in cold
* [ ] Possible YouTube video when 1.0.0 is in the water
* [ ] Buoy enclosure (parked with scope change, see CLAUDE.md "Future: buoy enclosure"):
  * [ ] Solar panels connected, verify charging (red LED / rising voltage)
  * [ ] Open-circuit voltage per panel measured in full sunlight, under 5.5 V after
        diode
  * [ ] Deployment in water/lake environment, verify link via KSD repeater
  * [ ] Floating/self-righting enclosure design
