#pragma once

// Compile-time constants for MeshTemp. No remote administration: changing
// any of these requires a USB reflash. See CLAUDE.md "Architecture
// decisions" and "Radio".

// Hashtag channel name, including the '#'. The channel key is derived at
// boot as the first 16 bytes of SHA256 of this exact string.
#define MESHTEMP_CHANNEL_NAME "#meshtemp"

// Push interval: once per hour.
#define MESHTEMP_SEND_INTERVAL_SECS (60UL * 60UL)

// How long to stay in RX after TX, listening for a repeater to bounce our
// own packet, before giving up and sending exactly one retry.
#define RETRY_WINDOW_S 30

// Plausibility bounds for a water temperature reading. Outside this range
// the reading is still sent (it's a real value from the sensor, not an
// error), but flagged with the "?" uncertainty marker - see CLAUDE.md
// "Message format" case 2.
#define PLAUSIBLE_MIN_C (-5.0f)
#define PLAUSIBLE_MAX_C (45.0f)

// Transport-code region scope, applied to every outgoing channel message and
// to the first boot advert (see examples/meshtemp/RegionScope.h and
// examples/meshtemp/README.md "Region scoping"). Repeaters that gate flood
// forwarding by region only relay packets scoped to a region they've
// explicitly allowed - an unconfigured region silently drops the packet
// instead of forwarding it. Empty string means unscoped: no transport code
// is attached, matching the plain flood/zero-hop route used before this
// feature existed.
#define MESHTEMP_REGION "se17"
