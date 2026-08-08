# PoolTemp

A generic MeshCore water/outdoor temperature sensor node, built as a YouTube project.
It reads temperature from a DS18B20 probe and pushes it once an hour as a regular
encrypted channel message on a MeshCore mesh — no phone, no pull telemetry, no
custom payload. The node sleeps deeply between transmissions to run on battery.

This example is a fork of `examples/simple_sensor`, copied into its own directory so
upstream MeshCore merges stay clean.

PoolTemp is itself a fork of `examples/meshtemp` (MeshTemp 0.9.0), which is left in
place and untouched. Everything below is MeshTemp's behaviour, renamed — with exactly
one deliberate difference: the DS18B20 1-Wire data pin. MeshTemp reads the probe on
`WB_IO1`; PoolTemp reads it on the RAK19003 header pad silkscreened `TX0`
(`PIN_SERIAL2_TX`, nRF52840 `P0.20`). Transmit timing, repeater echo detection,
stay-awake/sleep logic, the message format and everything else are unchanged.

## Parts list

* RAK4631 WisBlock Core module (nRF52840 + SX1262 LoRa radio)
* RAK19003 WisBlock Base Board Mini (the pad this build reads the probe on is on
  its 2.54 mm extension header — see "Hardware" below)
* A waterproof DS18B20 temperature probe (the 3-wire, stainless-steel-tipped
  cable type, not the bare TO-92 chip)
* One 4.7 kΩ resistor (color bands **yellow, violet, red, gold** — 4-7-×100-±5%)
* Solder, or a small length of wire and a way to join it (screw terminal,
  crimp connector, etc.) — see "Hardware" below, either works
* USB cable (data-capable, not charge-only) for flashing and for the bench build
* A LiPo/Li-ion battery for running untethered on the production build, or USB
  power for bench testing

## Hardware

* RAK4631 (nRF52840 + SX1262) on a RAK19003 baseplate
* DS18B20 waterproof temperature probe, wired as:
  * `VDD` (red) → 3.3V pad
  * `GND` (black) → GND pad
  * `DATA` (yellow) → the header pad silkscreened `TX0`
  * a 4.7 kΩ pullup resistor between the `TX0` pad and the `VDD` pad
    (external, as it was on MeshTemp — the firmware does not enable an
    internal pullup)

### Which pin `TX0` actually is

The `TX0` pad is nRF52840 `P0.20`. In `variants/rak4631/variant.h` that is
`PIN_SERIAL2_TX` — the WisBlock connector's `TXD0` signal, which the variant
comments as "TXD0 RXD0 on Base Board". The firmware uses the macro, not the
number: `static WaterTempSensor water_sensor(PIN_SERIAL2_TX);` in `main.cpp`.

**It is `Serial2`, not `Serial1`, despite the "0" in the silkscreen.** RAK's
label counts UARTs from 0 (`UART0` = `TXD0`/`RXD0`), while the Arduino core
counts its `Uart` objects from 1, so RAK's `UART0` is the core's `Serial2`
(`P0.19`/`P0.20`) and RAK's `UART1` is `Serial1` (`P0.15`/`P0.16`).

**Board revision matters.** RAK moved this header's signals: RAK19003 Ver.B
silkscreens it `RX1`/`TX1` and wires it to `UART1` (= `Serial1`,
`P0.15`/`P0.16`); Ver.D and later silkscreen it `RX0`/`TX0` and wire it to
`UART0` (= `Serial2`, `P0.19`/`P0.20`), because `RX1`/`TX1` are already used on
the sensor slots. This build targets the `TX0` silkscreen, i.e. Ver.D/E. If
your baseplate's header reads `TX1`, change `PIN_SERIAL2_TX` to
`PIN_SERIAL1_TX` in `main.cpp` — and then read the next paragraph, because on
that pin the conflict is real.

**No UART is holding `P0.20` in this firmware, so nothing had to be freed.**
The Arduino core does instantiate a global `Uart Serial2` on
`UARTE1`/`P0.19`/`P0.20`, but a `Uart` only routes its pins in
`Uart::begin()`, and nothing in PoolTemp ever calls `Serial2.begin()` — only
the unrelated `RAK_4631_repeater_bridge_rs232_serial2` env does. `Serial1` is a
different story: it *is* begun in this build, on `P0.15`/`P0.16`, by
`EnvironmentSensorManager::initBasicGPS()` (under `ENV_INCLUDE_GPS`, reached
from `sensors.begin()` in `setup()`). That is exactly why `Serial1`'s pins are
the wrong ones to hand to `OneWire` here without calling `Serial1.end()` first,
and why `P0.20` is safe as-is.

Do not use `WB_IO2` for the 1-Wire data line — on WisBlock baseplates it controls
3.3V power to certain WisBlock modules, not general-purpose I/O.

**⚠️ Stray strand warning (a real failure we hit building this project):** the
pads on these headers sit close together, and a single loose strand from a
stranded wire — invisible at a glance, easy to miss even on a "verified
correct" wiring check — can bridge to a neighbouring pad and cause exactly the
same symptom as a fully disconnected probe (`ERR(-127)`, see Troubleshooting
below), because it either shorts the data line or interferes with the pullup.
On MeshTemp this happened between the adjacent `IO1` and `IO2` pads; the
failure mode is a property of stranded wire and closely-spaced pads, not of
those two pads specifically, so it applies to the `TX0` pad and its neighbours
just as much. **Tin the wire ends before inserting or soldering them** — a
quick pass with a soldering iron and a little solder fuses the strands into one
solid conductor, and it's the single easiest way to avoid this failure mode
entirely. After wiring, a visual inspection under good light (or magnification)
for any stray strand bridging to a neighbouring pad is worth the extra minute.

**Cheap probes can use nonstandard wire colors.** The red/black/yellow
convention above is common but not universal — some inexpensive DS18B20
probes (particularly from budget marketplace sellers) use different colors
for `VDD`/`GND`/`DATA`. **Always verify with your probe's actual datasheet or
a continuity/resistance check before soldering**, rather than trusting color
alone. A DS18B20 wired with `VDD` and `GND` swapped is at minimum unresponsive
and at worst damaged.

**[Photo placeholder: wiring photo goes here]**

The probe runs at 9-bit resolution (0.5°C steps, under 100ms conversion), read
asynchronously so it never blocks the mesh loop.

## Building

Two PlatformIO environments, both defined in `variants/rak4631/platformio.ini`:

* **`RAK_4631_pooltemp`** — bench-test build. Sends every 2 minutes instead of every
  hour, USB serial stays up, no deep sleep. Use this for wiring checks, message
  verification, and retry testing.
* **`RAK_4631_pooltemp_sleep`** — production build. Identical read/send/retry code,
  but sends once per hour and powers the radio down between cycles to keep average
  current low. Flash this one, then run on battery only — USB keeps the nRF52 from
  reaching its lowest sleep current and will skew any power measurement you take.

There's also a third, temporary debug environment — see "Troubleshooting" below.

### Option A: build from scratch

Requires [PlatformIO](https://docs.platformio.org) (CLI or the VS Code extension).
No manual RAK board-support-package patch is needed for this fork — it ships its
own board/variant files and pins its own Arduino core fork, confirmed by building
clean with zero manual setup.

```
pio run -e RAK_4631_pooltemp
pio run -e RAK_4631_pooltemp_sleep
```

The result is `.pio/build/<env>/firmware.uf2`. The `.uf2` is the only file the
bootloader accepts, and it's produced automatically by every build — if the
conversion fails, the build fails with it, so a successful build always leaves a
flashable file behind.

As of 0.7.0 this happens on a plain `pio run`. Older versions required a separate
`-t create_uf2` step, and running `pio run` without it finished successfully while
producing no `.uf2` at all — worth knowing if you're building from a 0.6.0 or
earlier checkout, where a "successful" build can leave you with nothing to flash
and an older file still on the board.

### Option B: no build, use the release UF2

Every tagged release on the
[Releases page](https://github.com/Pakmel/MeshCore/releases) ships pre-built `.uf2` files for both
environments — download the one you want and skip straight to flashing. The
two builds in this repo's `builds/` folder are `pooltemp_2min.uf2` (bench,
`RAK_4631_pooltemp`) and `pooltemp_1h.uf2` (production,
`RAK_4631_pooltemp_sleep`); the two differ by a single build flag and are
near-identical in size, so the filename is the only thing telling them apart at
flash time. Release-tagged copies additionally carry `<version>-<commit>` in
the name, which is deliberate: it tells you exactly which build you have, so an
older download sitting in your Downloads folder can't be mistaken for the
current one. This is the
easiest route if you just want to run the node as-is; use Option A if you're
changing `pooltemp_config.h` (see "Make it your own" below), since that always
needs a rebuild.

### Flashing

Double-tap reset for the bootloader, then copy the `.uf2` file to the drive
that appears (labeled `RAK4631`). Open a serial terminal at 115200 baud
**before** resetting again if you want to see the boot log — the RAK4631's USB
CDC doesn't buffer, so lines printed before the terminal connects are lost.

## Make it your own

Almost everything is a compile-time constant in `src/pooltemp_config.h` — there
is no remote configuration of these, any change needs a USB reflash:

| Constant | Where it lives | Default | What it controls |
|---|---|---|---|
| `POOLTEMP_CHANNEL_NAME` | `pooltemp_config.h` | `"#pooltemp"` | Hashtag channel name — also the key material (see "How the channel name works"). **Must be all lowercase**: the MeshCore app doesn't accept uppercase in a hashtag channel name at all, so anything but lowercase can't even be entered on the receiving end. |
| `POOLTEMP_REGION` | `pooltemp_config.h` | `"se1780"` | Transport-code region scope (see "Region scoping"). Set it to your own local region code, or `""` (empty string) to send unscoped so every repeater forwards it regardless of region configuration. |
| `POOLTEMP_SEND_INTERVAL_SECS` | `pooltemp_config.h` | `60UL * 60UL` (1 hour) | Production push interval. **Duty-cycle etiquette:** don't shorten this drastically on a shared mesh. The one-hour interval plus at most one retry keeps this node's airtime well under 10% on 869 MHz — a much shorter interval on a real deployment eats into other nodes' share of the same shared spectrum. |
| `RETRY_WINDOW_S` | `pooltemp_config.h` | `30` (seconds) | How long to listen for a repeater echo of this node's own packet before resending exactly once. |
| `PLAUSIBLE_MIN_C` / `PLAUSIBLE_MAX_C` | `pooltemp_config.h` | `-5.0` / `45.0` | Plausibility bounds for a reading. Outside this range the value is still sent (it's real sensor data, not an error) but flagged with `?` — see "Message format" case 2. |
| `ADVERT_NAME` | `variants/rak4631/platformio.ini` (a build flag, **not** in `pooltemp_config.h` — there's no separate default-name macro) | `"PoolTemp1"` | The node's default advertised name. Can be changed after flashing without a reflash — see "Renaming your node" below. |
| `POOLTEMP_RX_BOOSTED_GAIN` | `pooltemp_config.h` | `1` (on) | Boosted receive gain on the SX1262. The stock MeshCore default is off; this build turns it on, trading a little current for receive sensitivity — a worthwhile trade here because the production build powers the radio down between hourly cycles, so the receiver is only active for a few seconds an hour. Set to `0` for the stock behaviour. **First-boot default only:** it's written to the persisted settings on a fresh filesystem and won't override a value already stored — after changing it, either erase the filesystem or use `set radio.rxgain on`/`off`. |
| Radio preset (frequency, bandwidth, SF, CR) | `platformio.ini`, `[arduino_base]` build flags — **not** in `pooltemp_config.h` | `869.618` MHz, `62.5` kHz, SF `8`, CR `8` | The EU/UK Narrow air configuration, matching the KSD network in Karlstad. These four belong together — changing one without the others puts the node on a different air configuration than its neighbours. Note that a wrong CR is invisible in practice: LoRa's explicit header carries it, so a receiver auto-detects it and a mismatched node still communicates, differing only in airtime and error resilience. |
| `POOLTEMP_SYNC_AWAKE_MAX_SECS` | `pooltemp_config.h` | `3600` (1 hour) | See "Time sync" below. How long a freshly-booted node stays awake hunting for a clock before giving up, sending with timestamp `0`, and entering normal cycling. Bounds the one-off power cost on a mesh with no reachable repeater. |
| `POOLTEMP_SYNC_RETRY_SECS` | `pooltemp_config.h` | `150` (2.5 min) | See "Time sync" below. Retry cadence during the boot phase. Kept above the repeater anon rate limit's window (four per 180s, per repeater) with margin. |
| `TIME_AGREEMENT_WINDOW_SECS` | `pooltemp_config.h` | `600` (10 min) | See "Time sync" below. Max difference allowed between two independent time sources before they're trusted together on first sync. |
| `MAX_TIME_SYNC_CANDIDATES` | `pooltemp_config.h` | `6` | See "Time sync" below. How many distinct sources are tracked at once while hunting for an agreeing pair on first sync. Not "how many are needed" (still 2) - bounds the pool so a single fast/wrong source can't monopolize the comparison against every other source that answers. |
| `TIME_SANITY_MAX_JUMP_SECS` | `pooltemp_config.h` | `300` (5 min) | See "Time sync" below. Once synced, the largest single adjustment (either direction) accepted from any one source before it's rejected as implausible. |
| `TIME_DISTRUST_THRESHOLD` | `pooltemp_config.h` | `3` | See "Time sync" below. Once synced, how many rejected proposals (with at least two distinct sources agreeing with each other) it takes before the node concludes its OWN clock is the outlier and re-seeds from that consensus. |

### Renaming your node

The node name isn't locked to `ADVERT_NAME` after first boot — it's stored in
`NodePrefs` and can be changed live:

1. **Over serial (the way this project expects):** open a terminal at 115200
   baud and send `set name YourNodeName`. Takes effect immediately.
2. **Remote admin over the mesh:** the underlying MeshCore firmware also
   supports renaming (and other admin commands) via the app's remote
   administration feature over LoRa, authenticated with the admin password
   (`ADMIN_PASSWORD` build flag in `variants/rak4631/platformio.ini`, default
   `"password"`). This project's own stance is USB-only configuration — if
   you do enable remote admin, **change the default password first**, since
   the default is public in this repo and anyone who's read it can
   authenticate as admin against your node.

Either way, the new name (and password, if you change it) is saved to the
nRF52's internal filesystem — a separate flash region from the application
code — so it **survives reflashing**. Re-flashing a new `.uf2` only replaces
the firmware; your node name and any other saved settings stay put.

## How the channel name works

PoolTemp uses a **hashtag channel** — the same mechanism the MeshCore mobile app and
MeshMonitor use for quick, no-setup shared channels. There's no separate secrets
file: the channel name *is* the key material. At boot, the node derives the
channel's 128-bit key as the first 16 bytes of SHA256 of the channel name string,
including the `#`. This is verified against a known example at every boot
(`#test` → key `9cd8fcf22a47333b591d96a2b848b73f`) and logged as
`[channel key self-check] ... PASS`/`FAIL`.

To receive the node's messages, add a channel with the **exact same name**
(byte-for-byte, including the `#`) in the MeshCore app or MeshMonitor. Hashtag
channel names should be all lowercase — the MeshCore app doesn't accept uppercase
in a hashtag channel name at all, so `#pooltemp` (not `#PoolTemp`) is the only
form that works. The firmware constant must match whatever's entered in the app
exactly, since any difference derives a different key.

Hashtag channels are public by design — anyone who knows the name can read and
write to it. That's an accepted tradeoff for an outdoor temperature reading; don't
reuse this pattern for anything sensitive.

## Region scoping

Every outgoing channel message, and the first boot advert, carry a **transport
code** derived from `POOLTEMP_REGION` in `src/pooltemp_config.h` (default
`"se17"`). This is a separate mechanism from the channel key above — it doesn't
affect who can *decrypt* the message, only which **repeaters** will relay it.

MeshCore repeaters can be configured (via their own `region` CLI commands) to
only forward flood packets whose transport code matches a region they've
explicitly allowed. **If a repeater along the path hasn't allowed your
region, it silently drops the packet instead of forwarding it** — there's no
error, the message just doesn't get any further. This is the flip side of the
same mechanism: it lets a shared repeater infrastructure carry multiple
projects' traffic without every project's packets flooding every region.

If you're building your own node from this project: set `POOLTEMP_REGION` to
your own local region code (whatever the repeaters you rely on have agreed to
allow), or set it to `""` (empty string) to send unscoped — the same plain,
unscoped flood/zero-hop route this firmware used before this feature existed,
which every repeater forwards regardless of its region configuration.

The transport code itself isn't a secret and adds no confidentiality — it's
just a routing tag, derived the same way a hashtag channel key is (SHA256 of
the region name, with a leading `#`), so a repeater operator can allow a
region by name (e.g. `region put se17`) without any extra coordination.

## Message format

The payload is a plain `PAYLOAD_TYPE_GRP_TXT` channel message — readable directly
in the MeshCore app and the MeshMonitor channel feed, no special decoding needed.
Battery voltage is always included. There are exactly three cases:

**Case 1 — normal reading:**
```
Temperature: 18.5C Batt: 3.91V
```
Temperature to one decimal, voltage to two decimals.

**Case 2 — implausible but real reading** (outside `PLAUSIBLE_MIN_C`..`PLAUSIBLE_MAX_C`,
-5.0 to 45.0°C by default). The value is still sent — it's a real sensor reading, not
an error — but flagged with a `?` directly after the `C`:
```
Temperature: 52.3C? Batt: 3.91V
```

**Case 3 — sensor error.** No temperature figure is ever sent, only the raw error
code in parentheses after `ERR`, so it can never be mistaken for a measurement:
```
Temperature: ERR(-127) Batt: 3.91V
```
`-127` — probe not responding (check wiring/pullup). `85` — power-on default value,
read too early (timing bug, should not occur in normal operation).

This format is a contract with downstream parsing (Home Assistant / MeshMonitor) —
it's treated as a protocol and does not change without a version bump.

## Retry

After sending, the node listens for `RETRY_WINDOW_S` seconds for a repeater to
bounce its own packet back (matched by packet hash). If no echo is heard, it resends
exactly once, then sleeps regardless of outcome. Serial logs one of:
`[retry] repeat heard`, `[retry] retry sent`, `[retry] gave up`.

## Time sync

There's no battery-backed RTC on the baseplate, so the clock starts every
cold boot at a fixed, wrong default and has to be set from the mesh. Rather
than trust the first thing that answers, it's a source-skeptical two-phase
process:

**Phase 1 — first sync (clock never yet trusted).** Both an active
mechanism and a passive one feed into the same decision:

* **Active:** at each cycle while unsynced, the node broadcasts one
  zero-hop node-discovery request asking for repeaters, then sends a
  MeshCore "remote clock" request (`ANON_REQ_TYPE_BASIC`) to each distinct
  repeater that answers, up to `MAX_TIME_SYNC_CANDIDATES` at once.
* **Passive:** any repeater's regular self-advert also carries a
  timestamp, for free, no request needed.

Both feed the same pool of candidate sources, accumulated across the whole
boot session — the boot attempt *and* every retry cycle, never wiped by a
mere disagreement. The decision:

* **Any two candidates in the pool agree** (within
  `TIME_AGREEMENT_WINDOW_SECS`, default 600s, of each other) — sync
  immediately: apply the **earlier** of that pair, unrestricted direction
  (nothing was trusted yet, so there's no "forward-only" to violate). Every
  *other* candidate in the pool at that moment is logged and ignored as an
  outlier — a lone bad source can never block consensus between two good
  ones, no matter how many attempts it took for both of them to show up.
* **A pair disagrees** beyond that window — neither is discarded (a third
  source might still agree with one of them), but both are marked
  distrusted for the rest of the boot session. This only affects the
  single-source fallback below; either one can still sync normally by later
  agreeing with some third source.
* **Only one distinct source ever answers**, after the first cycle plus one
  full retry cycle — accepted alone, logged as `single-source` — *unless*
  that source has already lost a disagreement this session, in which case
  it's explicitly withheld (logged as such) rather than trusted. A source
  that's already been contradicted once doesn't get an unearned second
  chance to seed the clock alone just because whoever it disagreed with
  didn't answer this particular round.
* **No source answers** — keep retrying, no limit. See the boot phase below
  for what happens meanwhile.

**Boot phase (0.9.0 and later).** After a cold boot the node stays awake
continuously and sends nothing until the clock is trusted, retrying every
`POOLTEMP_SYNC_RETRY_SECS` (default 150s). The moment the first sync
succeeds it takes a reading immediately — carrying a real timestamp — and
drops into normal cycling permanently. On a mesh with reachable repeaters
the first message therefore arrives within minutes, correctly stamped.

If nothing usable is heard within `POOLTEMP_SYNC_AWAKE_MAX_SECS` (default
3600s, one hour) the node says so plainly in the log, sends the reading with
timestamp `0` (an unambiguous "unset" marker, never a plausible-looking wrong
date) and enters normal cycling anyway. It is never permanently silent, and
the per-cycle retry plus the passive advert path keep working from there.

Retries rotate between known repeaters rather than hammering one, and
discovery is only re-broadcast while no repeater has been heard at all —
both to stay clear of the repeater anon rate limit (four per 180s, per
repeater) now that retries come every 150s instead of once an hour.

Why the phase exists: the production build powers the radio down as soon as
a send cycle resolves, about two seconds on a healthy mesh. That left the
active ladder roughly two seconds per hour to broadcast discovery, hear a
reply, request a clock and receive it — steps measured at 2-4 seconds each.
Before 0.9.0 a sleeping node could not sync in the field at all, and sent
every reading with timestamp `0` indefinitely. The bench build never showed
it, because the bench build never sleeps.

**Phase 2 — already synced.** Every subsequently proposed time (from a
repeater's advert, most commonly — the active mechanism stops once synced,
this ongoing correction is the passive path's job) is checked against a
**symmetric sanity window**: applied immediately, forward *or* backward, if
it's within `TIME_SANITY_MAX_JUMP_SECS` (default 300s) of the current
clock; rejected and logged (naming the source) if the jump is bigger than
that in either direction. Backward correction is fully supported — nothing
in this project's own sync path is forward-only.

**Self-distrust escalation (phase 2, mandatory).** The sanity window above
protects against any one bad proposal, but not against the trusted clock
itself being the wrong one (e.g. it seeded from a bad `single-source`
answer before a corroborating source ever showed up) — every legitimate
correction from the real world would otherwise keep getting rejected as
"implausible" forever, with no way out. Every rejected proposal is recorded
by distinct source identity (most recent value per source). Once at least
`TIME_DISTRUST_THRESHOLD` (default 3) rejections have accumulated since the
last sync/re-seed, *and* at least two distinct rejecting sources agree with
each other (within `TIME_AGREEMENT_WINDOW_SECS`), the node concludes its
own clock — not them — is the outlier: it drops the current time and
re-seeds from that agreeing pair in one motion, fully logged
(`SELF-DISTRUST ESCALATION`). It's never observably unsynced in between —
the active discovery ladder does not resume. This deliberately requires
both a minimum count (not trigger-happy on the first couple of stray
rejections) and real corroboration (a single persistently-wrong source
spamming rejections can't trigger it alone).

Both request types the active mechanism uses are answered by repeater
firmware without any password (confirmed by reading
`examples/simple_repeater/MyMesh.cpp` — gated only by rate limiters). That's
exactly why phase 1 never trusts a single answer alone: anyone can stand up
a repeater and answer these.

**Known limitation:** the rejected-proposal count behind self-distrust
escalation never decays — it's a pure accumulator since the last
sync/re-seed, with no time window. A source that was wrong hours ago and a
source that's wrong right now count the same toward the threshold. In
practice this hasn't mattered (the threshold requires real corroboration
between two distinct sources, not just volume), but it's worth knowing if
escalation ever seems to fire on stale evidence.

**Boot diagnostics:** every boot logs `[boot] reset reason: <reason>`,
read from the nRF52840's `RESETREAS` register via the existing
`NRF52Board`/power-management path (`checkBootVoltage()` →
`initPowerMgr()`), surfaced late enough in `setup()` to survive the
USB-CDC reconnect that follows any reset. Caveat: this silicon doesn't
expose a distinct brownout bit in that register, so a brownout reset and a
genuine cold power-on both read as `"Cold Boot"` — the log can tell you
*that* something reset the board, and rule out some causes (watchdog, CPU
lockup, debugger), but can't distinguish "power was actually removed" from
"voltage sagged momentarily" on its own.

`set name`-style admin commands (`clock sync`, `time <epoch>` — see
"Renaming your node" above for how remote admin access works) are also not
forward-only: since those require a password-authenticated admin
deliberately issuing the command, they're allowed to move the clock either
direction without the sanity window that applies to unauthenticated mesh
sources.

## Troubleshooting

**`Temperature: ERR(-127) Batt: ...`** — the probe isn't responding at all (wiring,
pullup, or continuity fault). Before assuming firmware, check the physical
connection first:

* Re-check for a **stray wire strand shorting the `TX0` pad to a neighbouring
  header pad** — this is a real failure mode we hit building this project (on
  MeshTemp's `IO1`/`IO2` pads), not a hypothetical. It looks identical to a
  fully disconnected probe. Tin the wire ends (see "Hardware" above) and
  inspect closely under good light.
* Confirm the 4.7 kΩ pullup is actually between `TX0` and `VDD`, not `TX0`
  and `GND`, and that it's the right value (yellow-violet-red-gold).
* Confirm your baseplate's header really is silkscreened `TX0` and not
  `TX1` — see "Which pin `TX0` actually is" above. On a Ver.B RAK19003 that
  header is `RX1`/`TX1` (`Serial1`, `P0.15`/`P0.16`) and this firmware is
  driving a pin your probe isn't connected to.
* Verify continuity and voltage with a multimeter on the actual probe leads,
  not just a visual check — "looks wired correctly" and "is electrically
  continuous" aren't the same thing.
* If your probe is a cheap/no-name one, double check its wire colors against
  its own datasheet rather than assuming red/black/yellow.
* If all of that checks out, try a second probe to rule out a dead sensor.

**`Temperature: ERR(85) Batt: ...`** — the power-on-reset default scratchpad value
was read before a real conversion completed (a timing bug). This shouldn't
happen in normal operation, since the firmware always waits for
`conversionDone()` before reading — if you see this repeatedly, it points at
a firmware/timing issue rather than wiring, and is worth reporting.

**Still not sure if it's a pin-mapping problem specifically?** There's a
temporary diagnostic build for exactly this: `RAK_4631_pooltemp_pindebug`
(`variants/rak4631/platformio.ini`, `-D POOLTEMP_PIN_DEBUG=1`). At boot it
scans the 1-Wire bus separately on `WB_IO1` and on the `WB_IO2` pin number and
logs any ROM codes (with a CRC check) found on each — useful for confirming
empirically whether the probe answers on a different pin than expected,
rather than trusting documentation alone. Build and flash it the same way as
the other environments; watch the serial log at boot for the `[pin scan]`
lines.

Note that this tool is carried over from MeshTemp **unchanged**, so it scans
`WB_IO1`/`WB_IO2` — not PoolTemp's `TX0`/`P0.20` data pin. That is deliberate:
the shipped firmware carries exactly one behavioural change from MeshTemp, and
repointing a debug tool would have been a second. Neither shipped variant
compiles it in. To scan the new pin, change the `scanPin()` arguments in
`PinScanDebug.cpp` first.

**Message isn't showing up in the app/MeshMonitor at all:**

* Check `[channel key self-check] #test -> ... PASS/FAIL` in the boot log —
  if it says `FAIL`, something is wrong with the key derivation itself and no
  channel message will ever be readable; stop and investigate before trusting
  anything else.
* Double-check the channel name in the app matches `POOLTEMP_CHANNEL_NAME`
  **byte-for-byte, all lowercase** — the MeshCore app doesn't accept
  uppercase in a hashtag channel name, so a mismatch here means the app is
  listening on a different channel (different key) than the node is sending
  on, with no error to indicate the mismatch.
* If you changed `POOLTEMP_REGION`, remember a repeater between you and the
  receiver has to have explicitly *allowed* that region — an unconfigured
  region silently drops the packet instead of forwarding it, with no error
  logged anywhere. Try setting `POOLTEMP_REGION` to `""` (unscoped) as a test
  to rule region filtering in or out.

**Serial port / COM port notes (Windows):**

* The RAK4631 enumerates as a native USB CDC device — it does **not** use a
  separate USB-serial bridge chip. This means resetting the board (including
  the reset that happens right after flashing) also resets its USB
  peripheral, which drops and re-enumerates the COM port.
* If your terminal program was already connected before a reset, its
  connection can go stale across that drop and silently stop receiving even
  though the board is running fine — closing and reopening the terminal
  usually fixes it.
* Open the terminal **before** resetting if you want to catch the boot log:
  the RAK4631's USB CDC doesn't buffer, so anything printed before a host
  is connected and reading is lost, not queued.
