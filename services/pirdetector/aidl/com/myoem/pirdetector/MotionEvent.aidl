// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetector;

/**
 * MotionEvent — data bundle sent to clients on every GPIO edge interrupt.
 *
 * This parcelable is the payload of IPirDetectorCallback.onMotionEvent().
 * It carries the raw hardware state at the moment of the interrupt.
 *
 * ── Design: raw hardware data only ───────────────────────────────────────────
 * The service reports raw GPIO edge information. It does NOT:
 *   - Debounce the signal
 *   - Apply any business logic or thresholds
 *   - Classify the motion (person / object / vibration)
 *
 * All logic (e.g., "show detected for at least 2 seconds") belongs in the
 * Java manager or the app, so it can evolve without an OTA vendor update.
 *
 * ── motionState values ────────────────────────────────────────────────────────
 *   1 = MOTION_DETECTED — rising edge: PIR OUT went 0V → 3.3V (motion started)
 *   0 = MOTION_ENDED    — falling edge: PIR OUT went 3.3V → 0V (motion stopped)
 *
 * ── timestampNs ───────────────────────────────────────────────────────────────
 * Monotonic kernel timestamp in nanoseconds, captured inside the interrupt
 * handler by the GPIO driver. This is the most accurate timestamp available —
 * it is NOT the time the userspace daemon woke up (which adds scheduling latency).
 * Use it for:
 *   - Measuring end-to-end detection latency
 *   - Ordering events if multiple arrive in a burst
 *   - Correlating with other sensor timestamps
 */
@VintfStability
parcelable MotionEvent {
    /** GPIO state: 1 = motion detected (rising edge), 0 = motion ended (falling edge). */
    int motionState;

    /**
     * Kernel monotonic timestamp of the GPIO interrupt, in nanoseconds.
     * From CLOCK_MONOTONIC — not wall clock. Zero if unavailable.
     */
    long timestampNs;
}
