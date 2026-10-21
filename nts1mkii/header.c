/*
 *  File: header.c
 *
 *  Dusty Pads - NTS-1 mkII oscillator unit header
 */

#include "unit_osc.h"

const __unit_header unit_header_t unit_header = {
    .header_size = sizeof(unit_header_t),
    .target = UNIT_TARGET_PLATFORM | k_unit_module_osc,
    .api = UNIT_API_VERSION,
    .dev_id = 0x5477476EU,  // 'TwGn' (TwistedGenetics), same as VectorFM / Formant Mutator / Rollers
    .unit_id = 0x4U,        // 1 = VectorFM, 2 = Formant Mutator, 3 = Rollers, 4 = Dusty Pads
    .version = 0x00010000U,  // 1.0.0
    .name = "Dusty Pads",
    .num_params = 8,
    .params = {
        // min, max, center, default, type, frac, frac mode, reserved, name
        // Knob A / Knob B
        {0, 1023, 0, 180, k_unit_param_type_none, 0, 0, 0, {"TIMBRE"}},
        {0, 1023, 0, 300, k_unit_param_type_none, 0, 0, 0, {"GRIT"}},
        // Edit 1-6
        {0, 100, 0, 60, k_unit_param_type_percent, 0, 0, 0, {"ENSEMBLE"}},
        {0, 100, 0, 35, k_unit_param_type_percent, 0, 0, 0, {"DRIFT"}},
        {-100, 100, 0, 0, k_unit_param_type_none, 0, 0, 0, {"TONE"}},
        {0, 2, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"RESAMPLE"}},
        {0, 1, 0, 0, k_unit_param_type_strings, 0, 0, 0, {"KEY TRACK"}},
        {0, 100, 0, 15, k_unit_param_type_percent, 0, 0, 0, {"HISS"}}},
};
