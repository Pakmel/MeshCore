#pragma once

// Compile-time constants for MeshBuoy. No remote administration: changing
// any of these requires a USB reflash. See CLAUDE.md "Architecture
// decisions" and "Radio".

// Hashtag channel name, including the '#'. The channel key is derived at
// boot as the first 16 bytes of SHA256 of this exact string.
#define MESHBUOY_CHANNEL_NAME "#watertemp"

// Push interval: once per hour.
#define MESHBUOY_SEND_INTERVAL_SECS (60UL * 60UL)

// How long to stay in RX after TX, listening for a repeater to bounce our
// own packet, before giving up and sending exactly one retry.
#define RETRY_WINDOW_S 30
