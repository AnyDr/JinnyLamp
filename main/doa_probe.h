#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * DOA snapshot (Direction Of Arrival) captured from XVF (AEC auto-select beam).
 *
 * - azimuth_rad: radians in range [0..2pi) typically
 * - azimuth_deg: degrees in range [0..360)
 * - age_ms: how long ago the snapshot was updated (ms)
 * - status: XVF status byte from last successful read
 * - valid: true if we have ever captured a valid azimuth (finite float)
 */
typedef struct {
    float    azimuth_rad;
    float    azimuth_deg;
    uint32_t age_ms;
    uint8_t  status;
    bool     valid;
} doa_snapshot_t;

/**
 * Start background DOA polling task (10 Hz).
 * Safe to call multiple times; subsequent calls are ignored.
 *
 * IMPORTANT: This should run regardless of CONFIG_J_DOA_DEBUG.
 * CONFIG_J_DOA_DEBUG affects only logging / UI exposure, not data availability.
 */
void doa_probe_start(void);

/**
 * Copy latest snapshot. Returns false if no valid snapshot has been captured yet.
 * out is optional.
 */
bool doa_probe_get_snapshot(doa_snapshot_t *out);

/**
 * Enable/disable DOA debug features at runtime.
 * This MUST NOT affect data acquisition; only logging / debug exposure.
 */
void doa_probe_set_debug(bool enable);
bool doa_probe_get_debug(void);


#ifdef __cplusplus
}
#endif
