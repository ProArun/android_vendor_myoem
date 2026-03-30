// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
#pragma once

// VolumeMapper — converts a 10-bit ADC value (0–1023) to an Android
// AUDIO_STREAM_MUSIC volume index (0–15).
//
// ANDROID VOLUME MODEL:
//   Android does not use a 0–100% percentage for volume. Instead, it uses
//   discrete integer "index" values per stream type. The maximum index varies
//   per stream:
//     AUDIO_STREAM_RING    = 0–7   (8 steps)
//     AUDIO_STREAM_MUSIC   = 0–15  (16 steps)  ← we control this one
//     AUDIO_STREAM_ALARM   = 0–7
//   Index 0 = muted (silent), index 15 = maximum loudness.
//
// LINEAR MAPPING:
//   We map the ADC range linearly to the volume range:
//     volume_index = round( (adc_value / 1023.0) × 15 )
//
//   Examples:
//     adc=0    → volume=0   (knob fully left, 0V → mute)
//     adc=512  → volume=8   (knob at midpoint → 50% volume)
//     adc=1023 → volume=15  (knob fully right, 3.3V → max)
//
//   Using round() instead of truncation (integer division) makes the mapping
//   symmetric and unbiased. Each of the 16 volume steps covers ~64 ADC counts.

class VolumeMapper {
public:
    // Convert a filtered ADC value (0–1023) to a volume index (0–15).
    // The result is clamped so out-of-range inputs never produce invalid indices.
    int toVolumeIndex(int adcValue) const;

private:
    static constexpr int kAdcMax      = 1023;   // MCP3008 10-bit maximum
    static constexpr int kVolumeMax   = 15;     // AUDIO_STREAM_MUSIC max index
};
