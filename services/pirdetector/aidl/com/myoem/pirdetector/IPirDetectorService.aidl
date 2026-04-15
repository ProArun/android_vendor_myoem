// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetector;

import com.myoem.pirdetector.IPirDetectorCallback;

/**
 * IPirDetectorService — vendor daemon ↔ Java manager contract.
 *
 * Service name registered in ServiceManager:
 *   "com.myoem.pirdetector.IPirDetectorService"
 *
 * ── Design: raw hardware data pipe ───────────────────────────────────────────
 * This service is a thin layer over the GPIO hardware. It exposes:
 *   - The current GPIO line state (getCurrentState)
 *   - A subscription mechanism for real-time edge events (registerCallback)
 *
 * It does NOT apply debouncing, thresholds, or any business logic.
 * That separation allows the Java manager or app to evolve its logic without
 * touching the vendor partition (no OTA required for logic changes).
 *
 * ── Callback lifecycle ────────────────────────────────────────────────────────
 * 1. App binds → PirDetectorManager calls registerCallback(cb).
 * 2. On each GPIO edge: service calls cb.onMotionEvent(event) [oneway].
 * 3. App unbinds → PirDetectorManager calls unregisterCallback(cb).
 *
 * If the client process crashes without unregistering, the service detects
 * the dead binder via AIBinder_DeathRecipient and removes the callback
 * automatically (no manual cleanup needed from the client side).
 *
 * ── VINTF stability ────────────────────────────────────────────────────────
 * @VintfStability is required because:
 *   - pirdetectord is a vendor process (vendor: true in Android.bp)
 *   - PirDetectorApp is a system app (vendor: true, platform_apis: true)
 *   - Without VINTF stability, libbinder rejects cross-partition Binder calls
 *     (BAD_TYPE error — partition stability mismatch).
 *   - VINTF stability upgrades the service to cross-partition level so that
 *     system apps can call it. The AIDL compiler emits the required
 *     AIBinder_markVintfStability() call automatically.
 *   - The service name must also be declared in the VINTF manifest (vintf/*.xml).
 */
@VintfStability
interface IPirDetectorService {

    /**
     * Returns the current GPIO line state, synchronously.
     *
     * Call once immediately after connecting to get the initial state without
     * waiting for the next edge event. This prevents a stale UI on connect
     * (e.g., sensor is already HIGH when the app starts).
     *
     * @return  1 if the PIR sensor is currently detecting motion (GPIO HIGH),
     *          0 if no motion is detected (GPIO LOW).
     */
    int getCurrentState();

    /**
     * Subscribe to real-time motion events.
     *
     * The callback's onMotionEvent() is invoked on every GPIO edge interrupt —
     * both rising (motion detected) and falling (motion ended). The callback
     * is oneway, so the EventThread never blocks waiting for clients.
     *
     * Registering the same callback object twice is a no-op (idempotent).
     *
     * Call this in Activity.onStart() or onResume(). Always pair with
     * unregisterCallback() in onStop() or onDestroy() to avoid memory leaks.
     *
     * @param callback  the client's IPirDetectorCallback implementation
     */
    void registerCallback(IPirDetectorCallback callback);

    /**
     * Unsubscribe a previously registered callback.
     *
     * The service will stop sending events to this callback. Any pending
     * oneway calls already in flight will still be delivered.
     *
     * Safe to call even if the callback was never registered (no-op).
     * Called automatically by PirDetectorManager.dispose().
     *
     * @param callback  same object that was passed to registerCallback()
     */
    void unregisterCallback(IPirDetectorCallback callback);

    /**
     * Returns the service API version.
     *
     * Current version: 1.
     * The manager can use this to detect older service versions and fall
     * back gracefully if a new API method is not available.
     */
    int getVersion();
}
