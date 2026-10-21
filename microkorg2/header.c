/*
 *  File: header.c
 *
 *  Dusty Pads - microKORG 2 oscillator unit header
 *  Parameters keep the same order as the NTS-1 mkII version.
 *  Names: max 8 characters on the microKORG 2.
 */

#include "unit.h"
#include "runtime.h"

// The load-meter build (tools/mk2-meter) reuses this header with its own
// name and unit ID, so both can be loaded side by side.
#ifndef DPADS_UNIT_NAME
#define DPADS_UNIT_NAME "DustyPad"
#endif
#ifndef DPADS_UNIT_ID
#define DPADS_UNIT_ID 0x4U  // 1 = VectorFM, 2 = Formant Mutator, 3 = Rollers, 4 = Dusty Pads
#endif

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x5477476EU,  // 'TwGn' (TwistedGenetics) - same as the NTS-1 mkII build
    .unit_id = DPADS_UNIT_ID,
    .version = 0x00010000U,  // 1.0.0
    .name = DPADS_UNIT_NAME,
    .num_presets = 0,
    .num_params = 8,
    .params = {
        // min, max, center, default, type, frac, frac mode, reserved, name

        // Page 1
        {0, 1023, 0, 180, k_unit_param_type_none, 0, 0, 0, {"Timbre"}},
        {0, 1023, 0, 300, k_unit_param_type_none, 0, 0, 0, {"Grit"}},
        {0, 100, 0, 60, k_unit_param_type_percent, 0, 0, 0, {"Ensemble"}},

        // Page 2
        {0, 100, 0, 35, k_unit_param_type_percent, 0, 0, 0, {"Drift"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"Tone"}},
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"Resample"}},
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"KeyTrack"}},
        {0, 100, 0, 15, k_unit_param_type_percent, 0, 0, 0, {"Hiss"}},

        // Page 3 (unused)
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}},
        {0, 0, 0, 0, k_unit_param_type_none, 0, 0, 0, {""}}}};
