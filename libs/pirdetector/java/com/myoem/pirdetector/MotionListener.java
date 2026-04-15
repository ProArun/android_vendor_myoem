// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetector;

/**
 * MotionListener — app-facing callback interface for PIR motion events.
 *
 * Implement this interface in your Activity, Fragment, or ViewModel to receive
 * real-time notifications from the PIR sensor.
 *
 * ── Threading guarantee ───────────────────────────────────────────────────────
 * Both methods are ALWAYS called on the Android main thread (Looper.getMainLooper()).
 * PirDetectorManager handles the Binder thread → main thread dispatch internally.
 * You can safely update UI directly inside these callbacks.
 *
 * ── Lifecycle ─────────────────────────────────────────────────────────────────
 * Register in onStart() or onResume():
 *   manager.registerListener(this);
 *
 * Unregister in onStop() or onDestroy():
 *   manager.unregisterListener();
 *
 * Failing to unregister causes a memory leak in pirdetectord — the service holds
 * a reference to the callback Binder until unregistration or client process death.
 */
public interface MotionListener {

    /**
     * Called when the PIR sensor detects motion (GPIO rising edge: 0V → 3.3V).
     *
     * The HC-SR501 asserts its OUT pin HIGH when it detects infrared radiation
     * from a moving warm body. This callback fires at the leading edge of that
     * signal — i.e., the moment motion starts.
     *
     * @param timestampNs  monotonic kernel timestamp of the GPIO interrupt, in
     *                     nanoseconds. From the interrupt handler — more accurate
     *                     than the time this callback is called. Use for latency
     *                     measurement or multi-sensor correlation.
     */
    void onMotionDetected(long timestampNs);

    /**
     * Called when motion stops (GPIO falling edge: 3.3V → 0V).
     *
     * The HC-SR501 releases its OUT pin LOW after the configured delay (Tx knob)
     * when it no longer detects motion. With the jumper in "repeating trigger" (H)
     * position, the OUT stays HIGH as long as motion continues — so this callback
     * fires when motion truly ends, not just after a fixed timeout.
     *
     * @param timestampNs  monotonic kernel timestamp of the GPIO interrupt, in
     *                     nanoseconds.
     */
    void onMotionEnded(long timestampNs);
}
