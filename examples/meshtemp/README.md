# MeshTemp

A generic MeshCore water/outdoor temperature sensor node, built as a YouTube project.
It reads temperature from a DS18B20 probe and pushes it once an hour as a regular
encrypted channel message on a MeshCore mesh — no phone, no pull telemetry, no
custom payload. The node sleeps deeply between transmissions to run on battery.

This example is a fork of `examples/simple_sensor`, copied into its own directory so
upstream MeshCore merges stay clean.

## Parts list

* RAK4631 WisBlock Core module (nRF52840 + SX1262 LoRa radio)
* RAK19007 WisBlock Base Board 2nd Gen
* A waterproof DS18B20 temperature probe (the 3-wire, stainless-steel-tipped
  cable type, not the bare TO-92 chip)
* One 4.7 kΩ resistor (color bands **yellow, violet, red, gold** — 4-7-×100-±5%)
* Solder, or a small length of wire and a way to join it (screw terminal,
  crimp connector, etc.) — see "Hardware" below, either works
* USB cable (data-capable, not charge-only) for flashing and for the bench build
* A LiPo/Li-ion battery for running untethered — the target is an average
  current draw under 0.5 mA on the production build — or USB power for bench
  testing

## Hardware

* RAK4631 (nRF52840 + SX1262) on a RAK19007 baseplate
* DS18B20 waterproof temperature probe, wired as:
  * `VDD` (red) → 3.3V pad
  * `GND` (black) → GND pad
  * `DATA` (yellow) → `IO1` pad
  * a 4.7 kΩ pullup resistor between the `IO1` pad and the `VDD` pad

Do not use `WB_IO2` for the 1-Wire data line — on the RAK19007 it controls 3.3V power
to certain WisBlock modules, not general-purpose I/O. Verified against RAK's own
documentation and cross-checked with the community-maintained RAK GPIO mapping
table: `WB_IO1` resolves to nRF52840 pin `P0.17`, matching RAK's IO1 signal
exactly, so this is a firmware-verified assignment, not a guess.

**⚠️ Stray strand warning (a real failure we hit building this project):** the
`IO1` and `IO2` pads sit right next to each other on the RAK19007. A single
loose strand from a stranded wire — invisible at a glance, easy to miss even on
a "verified correct" wiring check — can bridge across to the adjacent `IO2` pad
and cause exactly the same symptom as a fully disconnected probe
(`ERR(-127)`, see Troubleshooting below), because it either shorts the data
line or interferes with the pullup. **Tin the wire ends before inserting or
soldering them** — a quick pass with a soldering iron and a little solder fuses
the strands into one solid conductor, and it's the single easiest way to avoid
this failure mode entirely. After wiring, a visual inspection under good light
(or magnification) for any stray strand bridging to the neighbouring pad is
worth the extra minute.

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

* **`RAK_4631_meshtemp`** — bench-test build. Sends every 2 minutes instead of every
  hour, USB serial stays up, no deep sleep. Use this for wiring checks, message
  verification, and retry testing.
* **`RAK_4631_meshtemp_sleep`** — production build. Identical read/send/retry code,
  but sends once per hour and puts the radio to sleep between cycles for
  sub-0.5 mA average current. Flash this one, then run on battery only — USB keeps
  the nRF52 from reaching its lowest sleep current and will skew any power
  measurement.

There's also a third, temporary debug environment — see "Troubleshooting" below.

### Option A: build from scratch

Requires [PlatformIO](https://docs.platformio.org) (CLI or the VS Code extension).
No manual RAK board-support-package patch is needed for this fork — it ships its
own board/variant files and pins its own Arduino core fork, confirmed by building
clean with zero manual setup.

```
pio run -e RAK_4631_meshtemp
pio run -e RAK_4631_meshtemp_sleep
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
environments (`meshtemp_bench_<version>-<commit>.uf2` and
`meshtemp_sleep_<version>-<commit>.uf2`) — download the one you want and skip
straight to flashing. The short commit hash in the filename is deliberate: it
tells you exactly which build you have, so an older download sitting in your
Downloads folder can't be mistaken for the current one. This is the
easiest route if you just want to run the node as-is; use Option A if you're
changing `meshtemp_config.h` (see "Make it your own" below), since that always
needs a rebuild.

### Flashing

Double-tap reset for the bootloader, then copy the `.uf2` file to the drive
that appears (labeled `RAK4631`). Open a serial terminal at 115200 baud
**before** resetting again if you want to see the boot log — the RAK4631's USB
CDC doesn't buffer, so lines printed before the terminal connects are lost.

## Make it your own

Almost everything is a compile-time constant in `src/meshtemp_config.h` — there
is no remote configuration of these, any change needs a USB reflash:

| Constant | Where it lives | Default | What it controls |
|---|---|---|---|
| `MESHTEMP_CHANNEL_NAME` | `meshtemp_config.h` | `"#meshtemp"` | Hashtag channel name — also the key material (see "How the channel name works"). **Must be all lowercase**: the MeshCore app doesn't accept uppercase in a hashtag channel name at all, so anything but lowercase can't even be entered on the receiving end. |
| `MESHTEMP_REGION` | `meshtemp_config.h` | `"se17"` | Transport-code region scope (see "Region scoping"). Set it to your own local region code, or `""` (empty string) to send unscoped so every repeater forwards it regardless of region configuration. |
| `MESHTEMP_SEND_INTERVAL_SECS` | `meshtemp_config.h` | `60UL * 60UL` (1 hour) | Production push interval. **Duty-cycle etiquette:** don't shorten this drastically on a shared mesh. The one-hour interval plus at most one retry keeps this node's airtime well under 10% on 869 MHz — a much shorter interval on a real deployment eats into other nodes' share of the same shared spectrum. |
| `RETRY_WINDOW_S` | `meshtemp_config.h` | `30` (seconds) | How long to listen for a repeater echo of this node's own packet before resending exactly once. |
| `PLAUSIBLE_MIN_C` / `PLAUSIBLE_MAX_C` | `meshtemp_config.h` | `-5.0` / `45.0` | Plausibility bounds for a reading. Outside this range the value is still sent (it's real sensor data, not an error) but flagged with `?` — see "Message format" case 2. |
| `ADVERT_NAME` | `variants/rak4631/platformio.ini` (a build flag, **not** in `meshtemp_config.h` — there's no separate default-name macro) | `"MeshTemp1"` | The node's default advertised name. Can be changed after flashing without a reflash — see "Renaming your node" below. |
| `TIME_AGREEMENT_WINDOW_SECS` | `meshtemp_config.h` | `600` (10 min) | See "Time sync" below. Max difference allowed between two independent time sources before they're trusted together on first sync. |
| `MAX_TIME_SYNC_CANDIDATES` | `meshtemp_config.h` | `6` | See "Time sync" below. How many distinct sources are tracked at once while hunting for an agreeing pair on first sync. Not "how many are needed" (still 2) - bounds the pool so a single fast/wrong source can't monopolize the comparison against every other source that answers. |
| `TIME_SANITY_MAX_JUMP_SECS` | `meshtemp_config.h` | `300` (5 min) | See "Time sync" below. Once synced, the largest single adjustment (either direction) accepted from any one source before it's rejected as implausible. |
| `TIME_DISTRUST_THRESHOLD` | `meshtemp_config.h` | `3` | See "Time sync" below. Once synced, how many rejected proposals (with at least two distinct sources agreeing with each other) it takes before the node concludes its OWN clock is the outlier and re-seeds from that consensus. |

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

MeshTemp uses a **hashtag channel** — the same mechanism the MeshCore mobile app and
MeshMonitor use for quick, no-setup shared channels. There's no separate secrets
file: the channel name *is* the key material. At boot, the node derives the
channel's 128-bit key as the first 16 bytes of SHA256 of the channel name string,
including the `#`. This is verified against a known example at every boot
(`#test` → key `9cd8fcf22a47333b591d96a2b848b73f`) and logged as
`[channel key self-check] ... PASS`/`FAIL`.

To receive the node's messages, add a channel with the **exact same name**
(byte-for-byte, including the `#`) in the MeshCore app or MeshMonitor. Hashtag
channel names should be all lowercase — the MeshCore app doesn't accept uppercase
in a hashtag channel name at all, so `#meshtemp` (not `#MeshTemp`) is the only
form that works. The firmware constant must match whatever's entered in the app
exactly, since any difference derives a different key.

Hashtag channels are public by design — anyone who knows the name can read and
write to it. That's an accepted tradeoff for an outdoor temperature reading; don't
reuse this pattern for anything sensitive.

## Region scoping

Every outgoing channel message, and the first boot advert, carry a **transport
code** derived from `MESHTEMP_REGION` in `src/meshtemp_config.h` (default
`"se17"`). This is a separate mechanism from the channel key above — it doesn't
affect who can *decrypt* the message, only which **repeaters** will relay it.

MeshCore repeaters can be configured (via their own `region` CLI commands) to
only forward flood packets whose transport code matches a region they've
explicitly allowed. **If a repeater along the path hasn't allowed your
region, it silently drops the packet instead of forwarding it** — there's no
error, the message just doesn't get any further. This is the flip side of the
same mechanism: it lets a shared repeater infrastructure carry multiple
projects' traffic without every project's packets flooding every region.

If you're building your own node from this project: set `MESHTEMP_REGION` to
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

This format is a contract with downstream parsing (Home Assistant / MeshMonitor) —
it's treated as a protocol and does not change without a version bump.

## Retry

After sending, the node listens for `RETRY_WINDOW_S` seconds for a repeater to
bounce its own packet back (matched by packet hash). If no echo is heard, it resends
exactly once, then sleeps regardless of outcome. Serial logs one of:
`[retry] repeat heard`, `[retry] retry sent`, `[retry] gave up`.

## Time sync

There's no battery-backed RTC on the RAK19007, so the clock starts every
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
* **No source answers** — keep retrying every cycle, no limit. Sending
  never waits on this: unsynced readings go out with timestamp `0` (an
  unambiguous "unset" marker) rather than being held back or sent with a
  plausible-looking wrong date.

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

**`Water: ERR(-127) Batt: ...`** — the probe isn't responding at all (wiring,
pullup, or continuity fault). Before assuming firmware, check the physical
connection first:

* Re-check for a **stray wire strand shorting to the adjacent `IO2` pad** —
  this is a real failure mode we hit building this project, not a
  hypothetical. It looks identical to a fully disconnected probe. Tin the
  wire ends (see "Hardware" above) and inspect closely under good light.
* Confirm the 4.7 kΩ pullup is actually between `IO1` and `VDD`, not `IO1`
  and `GND`, and that it's the right value (yellow-violet-red-gold).
* Verify continuity and voltage with a multimeter on the actual probe leads,
  not just a visual check — "looks wired correctly" and "is electrically
  continuous" aren't the same thing.
* If your probe is a cheap/no-name one, double check its wire colors against
  its own datasheet rather than assuming red/black/yellow.
* If all of that checks out, try a second probe to rule out a dead sensor.

**`Water: ERR(85) Batt: ...`** — the power-on-reset default scratchpad value
was read before a real conversion completed (a timing bug). This shouldn't
happen in normal operation, since the firmware always waits for
`conversionDone()` before reading — if you see this repeatedly, it points at
a firmware/timing issue rather than wiring, and is worth reporting.

**Still not sure if it's a pin-mapping problem specifically?** There's a
temporary diagnostic build for exactly this: `RAK_4631_meshtemp_pindebug`
(`variants/rak4631/platformio.ini`, `-D MESHTEMP_PIN_DEBUG=1`). At boot it
scans the 1-Wire bus separately on `WB_IO1` and on the `WB_IO2` pin number and
logs any ROM codes (with a CRC check) found on each — useful for confirming
empirically whether the probe answers on a different pin than expected,
rather than trusting documentation alone. Build and flash it the same way as
the other environments; watch the serial log at boot for the `[pin scan]`
lines.

**Message isn't showing up in the app/MeshMonitor at all:**

* Check `[channel key self-check] #test -> ... PASS/FAIL` in the boot log —
  if it says `FAIL`, something is wrong with the key derivation itself and no
  channel message will ever be readable; stop and investigate before trusting
  anything else.
* Double-check the channel name in the app matches `MESHTEMP_CHANNEL_NAME`
  **byte-for-byte, all lowercase** — the MeshCore app doesn't accept
  uppercase in a hashtag channel name, so a mismatch here means the app is
  listening on a different channel (different key) than the node is sending
  on, with no error to indicate the mismatch.
* If you changed `MESHTEMP_REGION`, remember a repeater between you and the
  receiver has to have explicitly *allowed* that region — an unconfigured
  region silently drops the packet instead of forwarding it, with no error
  logged anywhere. Try setting `MESHTEMP_REGION` to `""` (unscoped) as a test
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
