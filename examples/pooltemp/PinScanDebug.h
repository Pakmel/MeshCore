#pragma once

// TEMPORARY debug tool (POOLTEMP_PIN_DEBUG build flag only, RAK_4631_pooltemp_pindebug
// env) - not part of the normal firmware. Scans the 1-Wire bus separately on
// WB_IO1 and on the WB_IO2 pin number and logs any ROM codes found on each,
// to check empirically whether the DS18B20 answers on a different pin than
// WB_IO1 resolves to (built to diagnose an ERR(-127) case that turned out to
// be a stray wire strand shorting to the adjacent IO2 pad, not a pin-mapping
// bug - see the README's Troubleshooting section).
//
// NOTE for PoolTemp: this tool is carried over from MeshTemp unchanged and
// still scans WB_IO1/WB_IO2, which are NOT PoolTemp's data pin - PoolTemp
// reads the probe on TX0 (P0.20, PIN_SERIAL2_TX, see main.cpp). Deliberately
// left as-is rather than repointed, so the shipped firmware carries exactly
// one behavioural change from MeshTemp. Neither shipped variant compiles this
// in; if you need a bus scan on the new pin, change the pins in
// PinScanDebug.cpp's scanPin() calls first.
void pinScanDebug();
