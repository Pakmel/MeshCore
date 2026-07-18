# MeshBuoy

A generic MeshCore water/outdoor temperature sensor node, built as a YouTube project.
It reads temperature from a DS18B20 probe and pushes it once an hour as a regular
encrypted channel message on a MeshCore mesh — no phone, no pull telemetry, no
custom payload. The node sleeps deeply between transmissions to run on battery.

This example is a fork of `examples/simple_sensor`, copied into its own directory so
upstream MeshCore merges stay clean. Full project background and architecture
decisions live in the repo root [`CLAUDE.md`](../../CLAUDE.md).

## Hardware

* RAK4631 (nRF52840 + SX1262) on a RAK19007 baseplate
* DS18B20 waterproof temperature probe, wired as:
  * `VDD` → 3.3V
  * `GND` → GND
  * `DATA` → `WB_IO1`
  * a 4.7k pullup resistor between `DATA` and `VDD`

Do not use `WB_IO2` for the 1-Wire data line — on the RAK19007 it controls 3.3V power
to certain WisBlock modules, not general-purpose I/O.

The probe runs at 9-bit resolution (0.5°C steps, under 100ms conversion), read
asynchronously so it never blocks the mesh loop.

## Building

Two PlatformIO environments, both defined in `variants/rak4631/platformio.ini`:

* **`RAK_4631_meshbuoy`** — bench-test build. Sends every 2 minutes instead of every
  hour, USB serial stays up, no deep sleep. Use this for wiring checks, message
  verification, and retry testing.
* **`RAK_4631_meshbuoy_sleep`** — production build. Identical read/send/retry code,
  but sends once per hour and puts the radio to sleep between cycles for
  sub-0.5 mA average current. Flash this one, then run on battery only — USB keeps
  the nRF52 from reaching its lowest sleep current and will skew any power
  measurement.

```
pio run -e RAK_4631_meshbuoy
pio run -e RAK_4631_meshbuoy_sleep
```

Flashing: double-tap reset for the bootloader, then copy the built `.uf2` from
`.pio/build/<env>/` to the drive that appears.

## Configuration

All parameters are compile-time constants in `src/meshbuoy_config.h` — there is no
remote administration, any change needs a USB reflash:

* `MESHBUOY_CHANNEL_NAME` — the hashtag channel name, including the `#`. Currently
  set to a test value; the final name is decided at release — this constant is the
  only place it lives.
* `MESHBUOY_SEND_INTERVAL_SECS` — push interval (1 hour)
* `RETRY_WINDOW_S` — how long to listen for a repeater echo before retrying
* `PLAUSIBLE_MIN_C` / `PLAUSIBLE_MAX_C` — plausibility bounds for the reading

## How the channel name works

MeshBuoy uses a **hashtag channel** — the same mechanism the MeshCore mobile app and
MeshMonitor use for quick, no-setup shared channels. There's no separate secrets
file: the channel name *is* the key material. At boot, the node derives the
channel's 128-bit key as the first 16 bytes of SHA256 of the channel name string,
including the `#`. This is verified against a known example at every boot
(`#test` → key `9cd8fcf22a47333b591d96a2b848b73f`) and logged as
`[channel key self-check] ... PASS`/`FAIL`.

To receive the node's messages, add a channel with the **exact same name**
(byte-for-byte, including the `#`) in the MeshCore app or MeshMonitor.

Hashtag channels are public by design — anyone who knows the name can read and
write to it. That's an accepted tradeoff for an outdoor temperature reading; don't
reuse this pattern for anything sensitive.

## Message format

The payload is a plain `PAYLOAD_TYPE_GRP_TXT` channel message — readable directly
in the MeshCore app and the MeshMonitor channel feed, no special decoding needed.
Battery voltage is always included. There are exactly three cases:

**Case 1 — normal reading:**
```
Water: 18.5C Batt: 3.91V
```
Temperature to one decimal, voltage to two decimals.

**Case 2 — implausible but real reading** (outside `PLAUSIBLE_MIN_C`..`PLAUSIBLE_MAX_C`,
-5.0 to 45.0°C by default). The value is still sent — it's a real sensor reading, not
an error — but flagged with a `?` directly after the `C`:
```
Water: 52.3C? Batt: 3.91V
```

**Case 3 — sensor error.** No temperature figure is ever sent, only the raw error
code in parentheses after `ERR`, so it can never be mistaken for a measurement:
```
Water: ERR(-127) Batt: 3.91V
```
`-127` — probe not responding (check wiring/pullup). `85` — power-on default value,
read too early (timing bug, should not occur in normal operation).

This format is a contract with downstream parsing (Home Assistant / MeshMonitor) and
does not change in patch releases — see `CLAUDE.md` "Versioning".

## Retry

After sending, the node listens for `RETRY_WINDOW_S` seconds for a repeater to
bounce its own packet back (matched by packet hash). If no echo is heard, it resends
exactly once, then sleeps regardless of outcome. Serial logs one of:
`[retry] repeat heard`, `[retry] retry sent`, `[retry] gave up`.
