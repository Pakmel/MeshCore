#pragma once

// Compile-time constants for PoolTemp. No remote administration: changing
// any of these requires a USB reflash. See examples/pooltemp/README.md for
// what each one does and how to change it.

// Hashtag channel name, including the '#'. The channel key is derived at
// boot as the first 16 bytes of SHA256 of this exact string.
#define POOLTEMP_CHANNEL_NAME "#pooltemp"

// Push interval: once per hour.
#define POOLTEMP_SEND_INTERVAL_SECS (60UL * 60UL)

// How long to stay in RX after TX, listening for a repeater to bounce our
// own packet, before giving up and sending exactly one retry.
#define RETRY_WINDOW_S 30

// Plausibility bounds for a water temperature reading. Outside this range
// the reading is still sent (it's a real value from the sensor, not an
// error), but flagged with the "?" uncertainty marker - see the README's
// "Message format" case 2.
#define PLAUSIBLE_MIN_C (-5.0f)
#define PLAUSIBLE_MAX_C (45.0f)

// Boosted RX gain on the SX1262. The stock default is off, which costs real
// receive sensitivity - and sensitivity is worth more to this node than the
// small extra current, because the production build keeps the radio powered
// down between hourly cycles, so the receiver is only drawing anything at all
// for a few seconds an hour. A node at the edge of a repeater's range gains
// more from hearing that repeater than it loses from the current.
//
// This is a first-boot default, written into the persisted prefs on a fresh
// filesystem. It does NOT override a value already stored on the node: after
// changing it, either erase the filesystem or set it over the CLI with
// `set radio.rxgain on`.
//
// The four air parameters (frequency, bandwidth, SF, CR) are NOT here - they
// are build flags in platformio.ini's [arduino_base], where the EU/UK Narrow
// preset is defined and documented as a set.
#define POOLTEMP_RX_BOOSTED_GAIN 1

// Transport-code region scope, applied to every outgoing channel message and
// to the first boot advert (see examples/pooltemp/RegionScope.h and
// examples/pooltemp/README.md "Region scoping"). Repeaters that gate flood
// forwarding by region only relay packets scoped to a region they've
// explicitly allowed - an unconfigured region silently drops the packet
// instead of forwarding it. Empty string means unscoped: no transport code
// is attached, matching the plain flood/zero-hop route used before this
// feature existed.
#define POOLTEMP_REGION "se1780"

// Boot-phase time sync: how long the node stays continuously awake after a
// cold boot, hunting for a clock, before giving up and starting normal
// cycling anyway.
//
// Why this exists: the production build powers the radio down as soon as a
// send cycle resolves, which on a healthy mesh takes about two seconds. That
// left the active sync ladder roughly two seconds per hour to broadcast
// discovery, hear a repeater answer, request its clock and get a reply -
// steps that were measured taking 2-4 seconds each. The node could not sync
// at all in the field, and sent every reading with timestamp 0 forever. The
// boot phase gives the ladder uninterrupted time to finish exactly once,
// after which the clock persists and normal hourly cycling resumes.
//
// The ceiling bounds the cost: on a mesh with no reachable repeater the node
// is awake for at most this long, once, per cold boot. It is never
// permanently silent - hitting the ceiling sends the reading with timestamp 0
// and enters normal cycling, where the per-cycle retry and the passive advert
// path keep trying for free.
#define POOLTEMP_SYNC_AWAKE_MAX_SECS (60UL * 60UL)

// Boot-phase retry cadence. Replaces the per-cycle cadence, which has no
// meaning while the node is continuously awake. Kept above the repeater anon
// rate limit's window (four requests per 180 s, per repeater) with margin,
// and requests rotate between known repeaters so this node never consumes one
// repeater's allowance by itself.
#define POOLTEMP_SYNC_RETRY_SECS 150

// Time sync tuning (see examples/pooltemp/BootTimeSync.h for the full design
// and examples/pooltemp/README.md's "Time sync" section).
//
// First sync (from cold boot, clock never yet trusted): two independent
// sources (repeater clock-request replies and/or repeater advert
// timestamps) must agree within this many seconds before either is trusted.
// If only one source is ever heard, it's accepted alone once this many
// seconds' worth of retry attempts have passed without a second one
// corroborating it - see BootTimeSync's single-source fallback.
#define TIME_AGREEMENT_WINDOW_SECS 600

// First sync only: how many distinct sources (by identity) BootTimeSync
// tracks at once while hunting for an agreeing pair. Not a "how many do we
// need" number - 2 is still the minimum for agreement, this just bounds how
// many concurrently-heard candidates get remembered so a single fast/wrong
// repeater can't monopolize the only comparison slot against every other
// source that answers. Once full, the oldest candidate is evicted to make
// room for a new distinct source.
#define MAX_TIME_SYNC_CANDIDATES 6

// Once synced: the clock is trusted, but not blindly - a proposed
// adjustment (forward OR backward) is only applied if it's within this many
// seconds of the current clock. A single misbehaving/wrong-clocked repeater
// can otherwise nudge an already-good clock arbitrarily far off with one
// bad reading; this bounds the damage from any one source to a plausible
// drift-correction-sized nudge.
#define TIME_SANITY_MAX_JUMP_SECS 300

// Once synced: self-distrust escalation. TIME_SANITY_MAX_JUMP_SECS above
// protects against any one bad proposal, but if the node's OWN clock is the
// one that's actually wrong (e.g. it seeded from a bad single-source
// fallback before this repeater ever corrected itself), every legitimate
// correction from the real world keeps getting rejected as "implausible"
// forever - there's no other way out. Once this many rejected proposals
// have accumulated since the last successful sync/re-seed, AND at least two
// of the (distinct, by identity) rejecting sources agree with each other
// within TIME_AGREEMENT_WINDOW_SECS, the node concludes its own clock - not
// them - is the outlier: it drops the current time and re-seeds from that
// agreeing pair in one motion, fully logged. Deliberately requires BOTH a
// minimum count (not trigger-happy on the first couple of stray rejections)
// AND real corroboration (not just volume from one persistently-wrong
// source) - see BootTimeSync's escalation logic.
#define TIME_DISTRUST_THRESHOLD 3
