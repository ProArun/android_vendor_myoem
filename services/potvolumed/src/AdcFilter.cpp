#define LOG_TAG "potvolumed"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#include "AdcFilter.h"

#include <cmath>        // std::round(), std::abs()
#include <log/log.h>    // ALOGD

int AdcFilter::update(int rawValue, bool* changed) {
    *changed = false;

    // ── First call: seed the filter with the actual reading ───────────────────
    // On startup, mEmaValue = -1 (sentinel). We don't want the filter to
    // slowly ramp up from -1 to the real value over many samples — that would
    // cause a spurious volume sweep at boot. Seed it directly instead.
    if (mEmaValue < 0.0f) {
        mEmaValue   = static_cast<float>(rawValue);
        mLastStable = rawValue;
        *changed    = true;     // Always emit on first reading so volume is set
        ALOGD("AdcFilter: seeded at raw=%d", rawValue);
        return mLastStable;
    }

    // ── Stage 1: Exponential Moving Average ──────────────────────────────────
    // EMA formula: new = α×input + (1−α)×previous
    // This is a single-pole infinite impulse response (IIR) low-pass filter.
    // It weights recent values more than old ones, with exponentially decaying
    // memory — that's why it's called "exponential" moving average.
    mEmaValue = kAlpha * static_cast<float>(rawValue)
              + (1.0f - kAlpha) * mEmaValue;

    // Round to nearest integer. Using round() (not truncation) ensures the
    // filter value is unbiased: 0.5 → 1, not 0.
    int filtered = static_cast<int>(std::round(mEmaValue));

    // ── Stage 2: Dead zone ────────────────────────────────────────────────────
    // Only emit a change notification if the filtered value has moved by at
    // least kDeadZone counts from the last stable value.
    //
    // We measure against mLastStable (the last value we told the caller about),
    // NOT against mEmaValue. This is important: if the value drifts slowly
    // across the dead zone boundary, we want exactly one notification, not
    // repeated notifications as it oscillates near the boundary.
    if (std::abs(filtered - mLastStable) >= kDeadZone) {
        ALOGD("AdcFilter: raw=%d ema=%.1f stable=%d→%d (Δ=%d)",
              rawValue, mEmaValue, mLastStable, filtered,
              filtered - mLastStable);
        mLastStable = filtered;
        *changed    = true;
    }

    return mLastStable;
}
