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
* [x] Confirm boot also logs `[channel key self-check] #test -> ... PASS`
      (see Round 3) - if it says FAIL, stop and report back before trusting
      any channel message, something is wrong with the key derivation.
      Confirmed directly from captured serial output during the pin-mapping
      investigation (2026-07-24), repeated across multiple boots:
      `[channel key self-check] #test -> 9CD8FCF22A47333B591D96A2B848B73F  PASS`.
* [ ] In the MeshCore mobile app, add a channel matching `MESHTEMP_CHANNEL_NAME`
      in `src/meshtemp_config.h` byte-for-byte, including the `#` (now
      `#meshtemp`, final, all lowercase - see "Production identity set" and
      "Channel name lowercased" below). The app doesn't accept uppercase in a
      hashtag channel name at all.
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

## Production identity set (2026-07-24)

* [x] `MESHTEMP_CHANNEL_NAME` changed from `#tempsensortest` to `#MeshTemp` -
      this is the final channel name (not a placeholder like the earlier
      `#watertemp`/`#tempsensortest` choices). Hashtag channel names are case
      sensitive: the app/MeshMonitor entry must match `#MeshTemp` byte-for-byte;
      `#meshtemp` would derive a different key and silently receive nothing.
      `examples/meshtemp/README.md` updated everywhere the channel is
      mentioned (Configuration section, "How the channel name works" section).

      **Update (2026-07-24):** wrong premise - the MeshCore app does not
      accept uppercase at all in a hashtag channel name (not a case-sensitivity
      footnote, an app-side input restriction), so `#MeshTemp` was never
      enterable. Changed to all-lowercase `#meshtemp` - see "Channel name
      lowercased" below.
* [x] Default node name (`ADVERT_NAME` in `variants/rak4631/platformio.ini`,
      the only place this lives - see the Rename section's note on
      `MESHTEMP_DEFAULT_NAME` not existing as a separate macro) changed from
      `"MeshTemp"` to `"MeshTemp1"`.
* [x] Both envs rebuilt with `-t create_uf2`, proof recorded per the build
      working rule (build SUCCESS output, `.uf2` timestamps, `MeshTemp` string
      check) - see chat/commit for the actual output. Version left at 0.5.0 -
      not bumped as part of this change since it wasn't requested; flagging
      that CLAUDE.md's versioning rule would call a channel-key change at
      least a minor bump while major is 0, if/when a version bump is wanted.

## Region scoping (se17) (2026-07-24)

* [x] Research first, before writing anything: how does the v1.16 base implement
      region scope on outgoing flood packets, and what's available to our
      createGroupDatagram path
      Read the actual mechanism instead of guessing (`src/helpers/RegionMap.{h,cpp}`,
      `src/helpers/TransportKeyStore.{h,cpp}`, `src/Packet.h`, `src/Mesh.{h,cpp}`,
      `examples/simple_repeater/MyMesh.cpp`). Findings:
      - Packets carry `transport_codes[2]` (`src/Packet.h`), only meaningful when
        the packet's 2-bit route type is `ROUTE_TYPE_TRANSPORT_FLOOD` or
        `ROUTE_TYPE_TRANSPORT_DIRECT` (`Packet::hasTransportCodes()`) - set via the
        `Mesh::sendFlood(pkt, transport_codes[2], ...)` /
        `Mesh::sendZeroHop(pkt, transport_codes[2], ...)` overloads (both public,
        `src/Mesh.h`/`.cpp`), distinct from the plain (unscoped) overloads our code
        already calls.
      - A transport code is `TransportKey::calcTransportCode(packet)`
        (`src/helpers/TransportKeyStore.cpp`): an HMAC-SHA256 of payload-type +
        payload, keyed by a 16-byte `TransportKey`, truncated to 2 bytes (codes
        0x0000/0xFFFF reserved). This runs per-packet (payload-dependent), unlike
        the channel key which is derived once.
      - The 16-byte `TransportKey` for a *region* is itself derived like a hashtag
        channel key: `TransportKeyStore::getAutoKeyFor()` does
        `sha256(name)[0..16)`. `RegionMap::getTransportKeysFor()`'s "implicit auto
        hashtag region" path (region name has neither `$` nor `#` prefix) computes
        this from `"#" + region_name` - so a region called `se17` and one called
        `#se17` resolve to the identical key. Verified by reading the branch, not
        assumed.
      - This whole mechanism is enforced entirely on the **receiving/relaying**
        side: `MyMesh::filterRecvFloodPacket()` (simple_repeater) resolves the
        packet's region via `RegionMap::findMatch()`, and
        `MyMesh::allowPacketForward()` refuses to forward any *flood*-routed
        packet whose region didn't resolve (`recv_pkt_region == NULL`) - silent
        drop, no error, no ack. New regions default to `REGION_DENY_FLOOD`
        (`RegionMap::putRegion()`) until an operator explicitly runs
        `region allowf <name>`. Confirms the requirement's premise: an
        unconfigured region really does make messages die at the first repeater
        that doesn't know it.
      - `examples/meshtemp/SensorMesh.{h,cpp}` (inherited from `simple_sensor`)
        already has `region_map`/`key_store`/`default_scope` members and computes
        `default_scope` in `begin()` from a `DEFAULT_FLOOD_SCOPE_NAME` build flag
        IF one is defined - but confirmed via `diff` against
        `examples/simple_sensor/SensorMesh.cpp` that this is byte-identical
        upstream code that computes `default_scope` and then never applies it to
        any outgoing send anywhere in the file (dead/latent upstream feature).
        Deliberately did NOT wire our region through this path: `region_map` is
        loaded from a persisted `/regions2` file and re-configurable at runtime
        via the CLI's `region default <name>` command (`CommonCLI::handleRegionCmd`,
        `src/helpers/CommonCLI.cpp`) - that's a live "remote administration"-shaped
        override surface (reachable over the same USB serial CLI everything else
        uses, but a *runtime* one) for something CLAUDE.md says must stay a
        compile-time constant. Implemented independently instead (see below), so
        `MESHTEMP_REGION` can never be moved by a CLI command.
* [x] Implement: `MESHTEMP_REGION` constant in meshtemp_config.h, applied to every
      channel message and the first boot advert
      `MESHTEMP_REGION "se17"` added to `src/meshtemp_config.h` (empty string ->
      unscoped, sits right next to `MESHTEMP_CHANNEL_NAME`, same "compile-time
      only" treatment). New `examples/meshtemp/RegionScope.{h,cpp}`: `begin()`
      derives the 16-byte region key once at boot (`mesh::Utils::sha256()` of
      `"#" + MESHTEMP_REGION` - the exact same call WaterChannel.cpp already uses
      for the channel key, no new crypto path), `active()` is just
      `MESHTEMP_REGION[0] != 0`, `codesFor(pkt, codes)` calls
      `TransportKey::calcTransportCode()` (reused as-is from
      `src/helpers/TransportKeyStore.h` - already compiled into this env via the
      generic `src/helpers/*` sources, confirmed via existing `.pio/build/.../
      TransportKeyStore.cpp.o`, no new lib_deps needed) and sets `codes[1] = 0`,
      mirroring `simple_repeater/MyMesh::sendFloodScoped()`'s own convention
      exactly. `main.cpp`: `RegionScope::begin()` called in `setup()` right after
      `the_mesh.begin(fs)`, before the boot advert; `meshtempSendChannelData()`
      now branches on `RegionScope::active()` to call the scoped or plain
      `sendFlood()` overload. `SensorMesh.cpp`'s `sendSelfAdvertisement()`: the
      `flood == false` branch (confirmed via grep - the *only* call site is
      `main.cpp`'s boot-time `sendSelfAdvertisement(16000, false)`, so this
      exactly and only covers "the first boot advert", nothing else) now uses the
      scoped `sendZeroHop()` overload when `RegionScope::active()`.
* [x] Document in README: what region scoping does, that repeaters must allow the
      region or messages die, viewers set their own region or empty
      New "Region scoping" section in `examples/meshtemp/README.md` (between "How
      the channel name works" and "Message format") plus a `MESHTEMP_REGION` bullet
      in "Configuration". Also added to CLAUDE.md's "Radio" section (its own house
      rule: "CLAUDE.md is updated in the same commit as the code") since this is an
      architecture-level radio decision, same tier as the channel-key one already
      documented there.
* [x] Rebuild both envs with proof, commit, push
      Both `-t create_uf2`: `RAK_4631_meshtemp` SUCCESS (49.75s, incremental -
      only the 5 touched/new files recompiled), `RAK_4631_meshtemp_sleep` SUCCESS
      (42.30s). Zero errors/warnings grepped in both logs. `.uf2` timestamps
      confirmed current (13:56:59 / 13:57:48, checked at 13:57:55) at
      `.pio\build\RAK_4631_meshtemp\firmware.uf2` and
      `.pio\build\RAK_4631_meshtemp_sleep\firmware.uf2`. String `se17` confirmed
      present (`grep -a -o`) in both `.uf2` files - proves the literal constant
      reached the binary; the HMAC/derivation logic itself is verified by reading
      the source (see research notes above), not by a runtime self-check, since
      unlike the channel key there's no independently-known-answer test to check
      a *transport code* against (it depends on the exact packet bytes, which
      differ per send). Version left at 0.5.0 - same "flagged, not assumed" pattern
      as the channel-name change above; this one changes on-wire packet header
      bits (route type + transport codes) for every send, which reads more clearly
      as a protocol-level change than the channel rename did.

## Channel name lowercased (2026-07-24)

* [x] `MESHTEMP_CHANNEL_NAME` changed from `#MeshTemp` to `#meshtemp` - the
      MeshCore app does not accept uppercase in a hashtag channel name at all
      (not a case-sensitivity nuance as the "Production identity set" entry
      above assumed - the app-side input simply rejects/can't produce an
      uppercase character there), so `#MeshTemp` could never actually be
      entered on the receiving end. `src/meshtemp_config.h` updated.
* [x] `examples/meshtemp/README.md` updated everywhere the channel is
      mentioned (Configuration bullet, "How the channel name works" section)
      and the case-sensitivity note rewritten: channel names should be all
      lowercase since the app enforces it, and the firmware constant must
      match exactly whatever's entered in the app.
* [x] Rebuild both envs with proof, commit, push
      Both `-t create_uf2`: `RAK_4631_meshtemp` SUCCESS (41.99s),
      `RAK_4631_meshtemp_sleep` SUCCESS (42.33s). Zero errors/warnings grepped
      in both logs. `.uf2` timestamps confirmed current (14:08:27 / 14:09:17,
      checked at 14:09:25). String `#meshtemp` confirmed present (`grep -a -o`)
      in both `.uf2` files; the old `#MeshTemp` string confirmed absent from
      both.

## Pin mapping investigation - WB_IO1 vs RAK19007 IO1 pad (2026-07-24)

Reported symptom: `Water: ERR(-127)` (probe not responding) despite wiring
double-checked against the RAK19007 pad silkscreened "IO1". `-127` is
`DEVICE_DISCONNECTED_RAW` (see `WaterTempSensor::readResult()`) - the DS18B20
genuinely isn't answering on the pin the firmware is polling, which is
consistent with either a real wiring fault *or* the firmware polling the wrong
nRF52 GPIO for that pad.

* [x] Verify pin mapping end to end: what GPIO does `WB_IO1` resolve to in our
      variant files, and what does RAK's own documentation say the IO1 pad
      connects to
      Our side: `variants/rak4631/variant.h:44` -
      `static const uint8_t WB_IO1 = 17;`. `variants/rak4631/variant.cpp`'s
      `g_ADigitalPinMap[]` is the identity map (Arduino pin N = nRF52 pin N, P0
      for 0-31, P1 for 32-47), so Arduino pin 17 = **P0.17** with no indirection
      to check for tampering there.

      RAK's side (web research, not assumed - see chat for exact sources):
      the RAK4631 module's own WisConnector (40-pin board-to-board) datasheet
      lists connector **pin 29 as the "IO1" signal**. Meshtastic's
      community-maintained RAK GPIO mapping table (independently cross-checked
      against ours, not derived from it) states **WB_IO1 = P0.17** explicitly -
      matching our variant.h exactly. The RAK19007 datasheet confirms the Core
      module sits in a single **fixed** connector slot (distinct from the
      plug-in sensor slots A-D, whose IO1/IO2 assignment *does* vary by slot -
      that variability doesn't apply here), so the base board's own "IO1"
      solder pad should trace straight to WisConnector pin 29 = P0.17,
      unconditionally.
      **Conclusion: no pin-mapping bug found.** Our `WB_IO1` constant and RAK's
      documented IO1 signal agree (P0.17) via two independent sources. Could
      not obtain RAK's full schematic to trace the pad-to-connector-pin copper
      trace itself (datasheet text only), so this isn't hardware-schematic
      certain - if the empirical scan below also comes up empty on both pins,
      that would be the next thing to press RAK support or the schematic for.
      Given the docs check came back clean, ERR(-127) is more likely a wiring/
      pullup/continuity/power issue on this specific board or cable than a
      firmware pin-mapping bug - see the debug scan below for empirical proof
      either way.
* [x] Add a temporary debug build: scan the 1-Wire bus at boot on WB_IO1, and
      the same scan on the WB_IO2 pin number for comparison
      New `examples/meshtemp/PinScanDebug.{h,cpp}` (entire `.cpp` body guarded
      behind `MESHTEMP_PIN_DEBUG`, so it compiles to nothing in
      `RAK_4631_meshtemp`/`_sleep` - confirmed via string check, see below).
      `pinScanDebug()` runs a raw `OneWire::search()` loop (not
      `DallasTemperature`'s device-count scan - this logs *every* ROM found,
      not just "a DS18B20 responded", and CRC-checks each one so bus noise on a
      floating pin doesn't get misread as a device) separately on `WB_IO1` and
      on the `WB_IO2` pin number, printing each ROM's 8 bytes + a CRC OK/
      MISMATCH verdict, called from `main.cpp` `setup()` right after
      `board.begin()` (matches where the real `water_sensor.begin()` runs
      later - before radio/mesh init, so it still logs even if a later step
      hangs). The WB_IO2 scan is prefixed with a printed caveat: CLAUDE.md's
      own "Known pitfalls" already flags WB_IO2 as a 3.3V power-switch pin on
      some WisBlock modules, not guaranteed general-purpose 1-Wire data - a
      clean/empty result there doesn't fully rule out mis-wiring, only that
      nothing answers on that GPIO number.
      New env `RAK_4631_meshtemp_pindebug` in `variants/rak4631/platformio.ini`
      (`extends = env:RAK_4631_meshtemp`, adds `-D MESHTEMP_PIN_DEBUG=1` only) -
      marked TEMPORARY in a comment, delete once this investigation is closed.
      **Caught mid-build:** the first `-t create_uf2` after guarding the `.cpp`
      recompiled `PinScanDebug.cpp.o` but the linker did NOT rerun (`firmware.elf`/
      `.hex` stayed at their previous timestamp; `create_uf2` silently converted
      the stale `.hex` anyway) - exactly the class of trap the build working
      rule at the top of this file exists to catch, just one layer deeper than
      the original incident (there the `.uf2` itself was never regenerated;
      here the `.uf2` *was* regenerated, but from a stale `.hex`). Caught by
      checking `.o`/`.elf`/`.hex`/`.uf2` timestamps relative to each other, not
      just relative to "now". Fixed by deleting
      `.pio/build/RAK_4631_meshtemp_pindebug` and rebuilding clean; re-verified
      the two normal envs' rebuilds afterwards actually show `Linking .../
      firmware.elf` + `Building .../firmware.hex` in the log (not just
      `create_uf2_action`) before trusting their timestamps either.
      All three envs (`RAK_4631_meshtemp`, `RAK_4631_meshtemp_sleep`,
      `RAK_4631_meshtemp_pindebug`) rebuilt clean, zero real errors/warnings,
      `.uf2` timestamps confirmed current and internally consistent
      (`.o` < `.elf`/`.hex` < `.uf2`, all within the same build). String
      `"pin scan"` confirmed present in the pindebug `.uf2` only, confirmed
      **absent** from both `RAK_4631_meshtemp` and `RAK_4631_meshtemp_sleep` -
      the debug code adds nothing to the two real firmware builds.
* [x] Flash `RAK_4631_meshtemp_pindebug`, capture the boot serial log, compare
      ROM codes/CRC results on WB_IO1 vs WB_IO2, decide next step (wiring fix,
      or escalate to RAK support/schematic if both come up empty)
      **First capture attempt lost the boot output entirely** - resetting the
      RAK4631 resets its USB peripheral too, so the host's CDC connection
      drops and has to re-enumerate; `pio device monitor`'s handle went stale
      across that drop and silently stopped receiving (task stayed "running",
      no error, just no new lines - confirmed by checking task status +
      `pio device list` mid-investigation rather than assuming). Rebuilding
      the connection before each reset didn't help either, since the *next*
      press killed the new connection the same way - the failure mode is
      inherent to resetting while attached, not a stale pre-existing handle.
      Switched to a small auto-reconnecting Python capture script (retries
      `serial.Serial(...)` open on any exception) instead of `pio device
      monitor` - this survives the drop, but the first successful capture
      through it still only picked up output from partway through `setup()`
      onward (GPS/I2C sensor sweep, channel self-check, etc.) - the
      `[pin scan]` lines themselves, which run right after `board.begin()`,
      were already gone by the time the reconnect completed. Root cause: a
      one-shot boot print is racing a real USB re-enumeration, and that race
      isn't reliably winnable from the host side.
      **Fix (code, not just capture tooling):** `main.cpp`'s
      `MESHTEMP_PIN_DEBUG` block now waits for `Serial` (`operator bool()` on
      `Adafruit_USBD_CDC`, which reflects `tud_cdc_n_connected()`) before
      calling `pinScanDebug()` - this is the framework's own documented
      `while (!Serial) {}` idiom (see the comment above that exact line in
      `Adafruit_USBD_CDC.cpp`), capped at 30s so a debug build left running
      with nothing attached doesn't hang forever, plus a 300ms settle delay
      so the host's reader loop is actually pumping before the scan starts.
      Rebuilt `RAK_4631_meshtemp_pindebug` after this fix - caught the exact
      same stale-linker trap as the first pindebug build (recompiled `.o`,
      but `Linking`/`Building .hex` didn't appear in the log the *first* time
      `-t create_uf2` ran after the edit); this time just re-ran it and
      confirmed `Linking .../firmware.elf` + `Building .../firmware.hex` both
      appear before trusting the result - zero errors/warnings, `.o` < `.elf`/
      `.hex` < `.uf2` timestamps all fresh and consistent.

      **Result, reflashed and captured cleanly (twice, same result both
      times):**
      ```
      [pin scan] starting WB_IO1 / WB_IO2 comparison scan...
      [pin scan] WB_IO1 (pin 17):
        (no devices found)
      [pin scan] NOTE: WB_IO2 is documented as a 3.3V power-switch pin on some
      [pin scan]       WisBlock modules, not guaranteed general-purpose 1-Wire data.
      [pin scan] WB_IO2 (pin 34):
        (no devices found)
      [pin scan] done
      ```
      Zero ROM codes on *either* pin - not a CRC mismatch/noise case, a raw
      `OneWire::search()` found nothing to even mis-read. Combined with the
      docs research above (WB_IO1 = P0.17 confirmed from two independent
      sources, matching our variant.h), this rules out "wired to IO2 instead
      of IO1 by mistake" as well, since IO2 came up empty too.
      **Interpretation:** not a pin-mapping bug - it's a physical-layer fault:
      most likely the 4.7k pullup (missing, wrong value, or wired
      DATA-to-GND instead of DATA-to-VDD would produce exactly this "nothing
      answers" symptom), a bad/incomplete solder joint or continuity fault on
      one of the three DS18B20 leads, VDD not actually reaching 3.3V at the
      probe, or a dead probe. Next step is a multimeter continuity/voltage
      check on the actual DS18B20 leads (not just visual wiring inspection),
      and/or swapping in a second probe to rule out a dead sensor - see
      "Weekend hardware pass" for the pattern to fold this into.

## v0.6.0 release readiness check (2026-07-24)

Before tagging the v0.6.0 pre-release (video release), an honest pass over
what's actually confirmed versus still open, rather than checking off the
whole "Weekend hardware pass" list on the strength of "it works now":

**Confirmed this session (direct evidence, not just project-owner report):**
* Channel key self-check `PASS` - captured directly in serial output multiple
  times (see the now-checked item above).
* The read/send/retry code path structurally works end to end (probe read ->
  `formatMessage()` -> `createGroupDatagram()` -> `sendFlood()`/echo-retry) -
  captured `[channel send] Water: ERR(-127) Batt: ...` and
  `[retry] repeat heard` lines live during the pin-mapping investigation.
  The *content* was an error case at the time (probe not responding), but the
  mechanism itself (detect -> format -> send -> retry-listen) was exercised
  and did not fault.
* Region scoping and the lowercase channel name are baked into this exact
  release build (verified by string check in the built `.uf2`s - see below).

**NOT independently confirmed this session (project owner reported "it works
now" after reflashing following the wiring fix, but no serial capture or
reference-thermometer comparison was made here to verify it):**
* `DS18B20 detected on WB_IO1` boot line (vs. `NOT detected`).
* Reference-thermometer water-glass accuracy check
  (`PLAUSIBLE`/deviation-under-1C item, "Weekend hardware pass" above) -
  still unchecked above; "it works now" confirms the probe responds again,
  not that its accuracy has been (re-)verified against a reference.
* Channel message actually arriving in the MeshCore app / MeshMonitor via a
  real KSD repeater (vs. just the send path executing without fault).

**Explicitly still open, and these gate 1.0.0 per CLAUDE.md's "Definition of
done" (correct temperature every hour through at least one repeater, average
current under 0.5 mA):**
* [ ] Round 4's retry field test on real hardware (temporarily disable the
      nearest repeater, confirm `[retry] retry sent` then `[retry] repeat
      heard` on a later cycle once the repeater's back) - still open, see
      "Weekend hardware pass" above.
* [ ] Round 5's sleep-cycle power measurement on `RAK_4631_meshtemp_sleep`,
      on battery only, target under 0.5 mA average over 6h+ - still open,
      see "Weekend hardware pass" above.

v0.6.0 is tagged as a **pre-release** specifically because of these two open
items - see the release notes.

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
