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
* [ ] Perform RAK's BSP patch for PlatformIO per their guide, document the exact
      steps here as a note (versions, paths) for reproducibility
* [ ] Build stock `simple_sensor` for the RAK4631 target with zero errors
* [ ] Flash and verify boot over serial (115200), note firmware version
* [ ] Copy examples/simple_sensor to examples/meshbuoy, own env in
      platformio.ini, build again with zero errors
* [ ] Create src/meshbuoy_version.h (0.1.0) and src/meshbuoy_config.h with channel
      name, interval, and retry window as constants

## Round 2 – DS18B20 on WB_IO1

* [ ] Add OneWire and DallasTemperature to lib_deps for meshbuoy
* [ ] Init 1-Wire bus on WB_IO1 behind build flag MESHBUOY_DS18B20, 9-bit
      resolution, detection check at boot (flag is only set if the probe responds)
* [ ] Asynchronous reading: start conversion, fetch the value without blocking
* [ ] Write temperature and battery voltage to serial every 10 seconds in test mode
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
