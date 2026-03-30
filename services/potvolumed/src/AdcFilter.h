// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
#pragma once

// AdcFilter — smooths noisy ADC readings and suppresses tiny fluctuations.
//
// WHY FILTERING IS NEEDED:
//   A real potentiometer is an analog, mechanical component. Even when you
//   hold the knob perfectly still, the ADC reading fluctuates by ±2–5 counts
//   due to:
//     1. Electrical noise on the wires (coupled from RPi5 switching power supply)
//     2. Resistive contact noise inside the pot (especially near wiper endpoints)
//     3. Quantization: the 10-bit ADC represents a continuous voltage as
//        integer steps, so tiny voltage wobbles toggle between two adjacent values
//
//   Without filtering, the daemon would call setStreamVolumeIndex() hundreds
//   of times per second even when you're not touching the knob. Each call is
//   a Binder IPC to AudioPolicyService — expensive and annoying (UI flicker).
//
// TWO-STAGE APPROACH:
//
//   Stage 1 — Exponential Moving Average (EMA):
//     Smooths rapid high-frequency noise. The EMA is a weighted average that
//     "remembers" recent values:
//
//       EMA_new = α × raw + (1 − α) × EMA_old
//
//     α = 0.2 means each new sample contributes 20% and history 80%.
//     A small α = heavy smoothing (slow to react). A large α = fast but noisy.
//     0.2 is a good balance for a volume knob (reacts in ~5 samples = 250ms).
//
//   Stage 2 — Dead zone:
//     Even after EMA, the filtered value may oscillate by ±1 near the true
//     value. The dead zone suppresses changes smaller than kDeadZone counts.
//     Only when the value moves by at least kDeadZone from the last *stable*
//     (emitted) value do we notify the caller that something changed.
//
//     With kDeadZone = 8 and ADC range 0–1023, this gives us about 128
//     distinct stable regions — more than enough for 16 volume steps.

class AdcFilter {
public:
    // Update the filter with a new raw ADC reading.
    //
    // rawValue: latest 10-bit value from SpiReader::read() (0–1023)
    // changed:  set to true if the stable output changed (caller should act)
    //
    // Returns the current stable filtered value (same as last call if unchanged).
    int update(int rawValue, bool* changed);

private:
    // EMA smoothing factor. 0.2 → each new sample is 20% of the output.
    static constexpr float kAlpha    = 0.2f;

    // Dead zone width in ADC counts. Changes smaller than this are suppressed.
    // 8 counts out of 1023 ≈ 0.8% of full scale ≈ sub-one-degree knob movement.
    static constexpr int   kDeadZone = 8;

    // mEmaValue: running EMA accumulator. Stored as float to avoid rounding
    // errors that would accumulate over thousands of integer operations.
    // Initialized to -1.0f as a sentinel meaning "not yet seeded".
    float mEmaValue{-1.0f};

    // mLastStable: the most recently emitted (changed=true) value.
    // The dead zone is measured relative to this, not relative to mEmaValue.
    int   mLastStable{-1};
};
