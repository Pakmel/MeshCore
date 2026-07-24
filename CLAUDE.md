# MeshTemp – MeshCore water/outdoor temperature sensor with channel push

## What this is

A generic MeshCore water/outdoor temperature sensor node, built as a YouTube project.
A RAK4631 on a RAK19007 baseplate reads temperature with a DS18B20 and pushes the value
once an hour as a regular encrypted channel message through the KSD MeshCore network.
The node sleeps deeply between transmissions.

The buoy enclosure (floating housing, self-righting, solar charging, lake deployment)
is out of scope for now — see "Future: buoy enclosure" at the bottom. For now this is a
bench/field node running on battery.

This is a fork of meshcore-dev/MeshCore. The base is the `examples/simple_sensor` example,
copied to a dedicated example `examples/meshtemp` so that upstream merges stay clean.

## Architecture decisions (do not change without discussion)

* **Push, not pull.** Standard MeshCore sensors are pull-based and require constant
  reception (8 to 10 mA). This node pushes and sleeps, targeting under 0.5 mA average.
* **Channel message as text.** The payload is a regular PAYLOAD_TYPE_GRP_TXT packet on a
  private channel. No custom payload design, no special decoding. The message must be
  readable in the MeshCore app and in the MeshMonitor channel feed.
* **Message format, exact.** The format is a contract, parsing happens downstream in
  Home Assistant/MeshMonitor. Never change it in a patch - any change is a protocol
  change (see Versioning). Battery is always included, in all three cases.

  Case 1, normal reading: `Water: 18.5C Batt: 3.91V`
  Temperature with one decimal, voltage with two decimals.

  Case 2, implausible but real reading (outside `PLAUSIBLE_MIN_C`..`PLAUSIBLE_MAX_C`,
  defined in `meshtemp_config.h` as -5.0 and 45.0): `Water: 52.3C? Batt: 3.91V`
  The value is still sent, with a `?` directly after `C` as the uncertainty marker.

  Case 3, sensor error, no temperature figure is ever sent:
  `Water: ERR(-127) Batt: 3.91V` — probe not responding (wiring, pullup)
  `Water: ERR(85) Batt: 3.91V` — power-on default, read too early (timing bug)
  `ERR` is always followed by the raw code in parentheses, never a number that could
  be mistaken for a measurement.
* **Retry via own echo.** After sending: listen for RETRY_WINDOW_S seconds for a repeater
  to bounce our own packet (match on packet hash). If no repeat is heard: resend exactly
  once. Then sleep regardless of outcome. Duplicates on the channel are accepted.
* **System ON sleep, not SYSTEMOFF.** The RTC must survive the sleep so that timestamps
  and intervals hold. Wakeup via RTC timer.
* **No remote administration.** Configuration changes require USB. All parameters,
  including the channel name, are compile-time constants in `src/meshtemp_config.h`.

## Hardware

* RAK4631 (nRF52840 + SX1262) on RAK19007 baseplate
* DS18B20 waterproof probe: VDD 3.3V, GND, data to WB_IO1, 4.7k pullup on data to VDD
* DS18B20 runs at 9-bit resolution (0.5 C steps, under 100 ms conversion).
  The conversion must not block the mesh loop, must start asynchronously and must not
  sleep during the wait.
* Battery voltage via board.getBattMilliVolts() (already present in SensorMesh)

## Radio

Same preset as the KSD network in Karlstad (EU/UK). Values go in meshtemp_config.h and
are verified against an existing KSD node before the first field test.

**Channel: hashtag channel.** The channel name (e.g. `#badtemp`) is a compile-time constant
in meshtemp_config.h. The key is derived at boot as the first 16 bytes of SHA256 of the
full name including #, exactly the same derivation the MeshCore apps use. No secrets file
is needed, the name is the key. Verify the derivation against a known example:
`#test` should give the key `9cd8fcf22a47333b591d96a2b848b73f`. Use MeshCore's own
SHA256 helper function, do not pull in a new crypto library.

Deliberate choice: hashtag channels are public by design, anyone with the name can
read and write. Accepted for water temperature. NEVER switch to this channel type for
anything sensitive in other projects.

**Region: transport-code scope.** `MESHTEMP_REGION` (compile-time constant in
meshtemp_config.h, default `se17`) is applied to every outgoing channel message and
to the first boot advert, as a MeshCore transport code (`Mesh::sendFlood`/
`sendZeroHop`'s transport-codes overload, `ROUTE_TYPE_TRANSPORT_FLOOD`/
`ROUTE_TYPE_TRANSPORT_DIRECT`). This is separate from the channel key: it doesn't
gate who can decrypt the message, only which repeaters relay it. A repeater that
hasn't allowed the region (`RegionMap`/`region` CLI commands, see
`src/helpers/RegionMap.cpp`) silently drops the packet instead of forwarding it -
this is deliberate upstream behavior, not a bug, and must be verified against a
real KSD repeater before relying on it in the field. Empty string means unscoped
(the plain, non-transport route every repeater forwards regardless of region).
See `examples/meshtemp/RegionScope.h` and the README "Region scoping" section.

Duty cycle: one transmission per hour plus max one retry is well under 10 percent
on 869 MHz. The retry window must never trigger more than one retransmission.

## Versioning

* Single source: `src/meshtemp_version.h` with `#define MESHTEMP_VERSION "0.1.0"`
* Major stays at 0 until the definition of "done" below is fulfilled — only then
  does the version become 1.0.0.
* While major is 0 (0.x): patch is for bugfixes only. Minor covers every other
  change, including format or protocol changes.
* From 1.0.0 onward: patch is a bugfix without behavior change, minor is a new
  feature, major is a format or protocol change (e.g. changed message format).
* No automatic rollover, in either versioning scheme.
* The version is written to serial at boot and is NOT included in the channel message.
* The build must pass with zero errors and zero warnings in PlatformIO before every
  version bump. CLAUDE.md is updated in the same commit as the code.

## Build environment

* PlatformIO in VS Code. RAK4631 targets require RAK's board support patch per
  RAK Wireless's guide "How to Perform Installation of Board Support Package in
  PlatformIO" before the first build. This is a known requirement, not a bug.
* New lib_deps for this example: paulstoffregen/OneWire, milesburton/DallasTemperature
* Flashing: double-tap reset for bootloader, copy UF2

## Known pitfalls

* WB_IO2 controls 3.3V power to certain WisBlock modules, do not use it for 1-Wire.
* A stray strand from the DS18B20's stranded wire can bridge the IO1 pad to the
  adjacent IO2 pad and produce the exact same symptom as a fully disconnected
  probe (`ERR(-127)`) - a real failure hit while building this project, not
  hypothetical. Tin wire ends before inserting/soldering. See
  `examples/meshtemp/RegionScope.h`'s sibling `PinScanDebug.h` (the
  `RAK_4631_meshtemp_pindebug` env) for the empirical bus-scan tool built to
  diagnose exactly this, and the README "Troubleshooting" section.
* Firmware 1.11+ has a known bug where the RAK4631 reports CPU temperature instead
  of sensor temperature in the pull telemetry. Our push reads the DS18B20 directly and
  does not depend on that code path, but always verify the first reading against a
  reference.
* The radio must be in RX during the retry window but in sleep/idle for the rest of
  the hour. Verify with power measurement, not assumptions.

## Definition of "done"

The node runs on battery, sends the correct temperature to the channel every hour
through at least one repeater, with measured average current below 0.5 mA.

## Future: buoy enclosure

Out of scope for now, kept here so the original vision isn't lost. Once the node
itself meets the definition of "done" above, this is the next phase:

* **Floating enclosure.** A waterproof housing that floats the node in Lake
  Vänern/Karlstad, with the DS18B20 probe hanging in the water and the antenna/solar
  panels above the surface. Not yet designed.
* **Self-righting.** The enclosure should return itself upright if capsized by waves
  or wake — likely a low center of gravity / ballast approach. Not yet designed.
* **Solar charging.** 4x solar panels in parallel (approx. 30 mA each, verified
  measurement, not the advertised 300 mA) into the P1 solar connector. Charge range
  4.4 to 5.5 V, Schottky diode per panel.
* **Lake deployment.** Final placement in the water, verifying the link back via at
  least one KSD repeater from the actual deployment site, and a long-duration (48h+)
  soak test for water ingress and drift/capsize behavior.
