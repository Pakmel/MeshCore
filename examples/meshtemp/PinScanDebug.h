#pragma once

// TEMPORARY debug tool (MESHTEMP_PIN_DEBUG build flag only, RAK_4631_meshtemp_pindebug
// env) - not part of the normal firmware. Scans the 1-Wire bus separately on
// WB_IO1 and on the WB_IO2 pin number and logs any ROM codes found on each,
// to check empirically whether the DS18B20 answers on a different pin than
// WB_IO1 resolves to (built to diagnose an ERR(-127) case that turned out to
// be a stray wire strand shorting to the adjacent IO2 pad, not a pin-mapping
// bug - see the README's Troubleshooting section).
void pinScanDebug();
