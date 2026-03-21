// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemode;

import com.myoem.safemode.ISafeModeCallback;
import com.myoem.safemode.VehicleData;

/**
 * ISafeModeService — the contract between SafeModeService and safemode_library.
 *
 * Service name (registered in ServiceManager):
 *   "com.myoem.safemode.ISafeModeService"
 *
 * ─── DESIGN RULE: This service is a RAW DATA PIPE ────────────────────────────
 * The service exposes raw VHAL values (speed in m/s, gear as integer, fuel in
 * ml). It does NOT:
 *   - Compute SafeModeState (NO / NORMAL / HARD) — that is the library's job.
 *   - Apply any thresholds or business logic.
 *   - Know anything about UI restrictions.
 *
 * This separation allows the library to evolve its business logic (e.g., change
 * speed thresholds) without touching the service or requiring an OTA update to
 * the vendor partition.
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Usage flow:
 *   1. Library calls getCurrentData() to get the initial snapshot immediately.
 *   2. Library calls registerCallback(cb) to subscribe to future changes.
 *   3. Service calls cb.onVehicleDataChanged() on every VHAL property event.
 *   4. Library calls unregisterCallback(cb) in onDestroy() to avoid leak.
 */
interface ISafeModeService {

    /**
     * Returns the most recent vehicle data snapshot, synchronously.
     *
     * This is called once when the library first connects, so the client
     * immediately has current data without waiting for the next VHAL event.
     *
     * If VHAL hasn't sent any events yet (device just booted), the returned
     * VehicleData will contain the AIDL default values (speed=0, gear=PARK).
     *
     * @return  current snapshot of speed, gear, and fuel
     */
    VehicleData getCurrentData();

    /**
     * Subscribe to receive real-time vehicle data updates via callback.
     *
     * The callback is invoked on EVERY VHAL property change event for any of:
     * speed, gear, or fuel. The full VehicleData snapshot is sent each time.
     *
     * A client MUST call unregisterCallback() when it no longer needs updates
     * (e.g., in Activity.onDestroy()). Failing to do so leaks memory on the
     * service side because the service holds a strong reference to the callback
     * Binder object.
     *
     * Registering the same callback twice is a no-op (idempotent).
     *
     * @param callback  the client's ISafeModeCallback implementation
     */
    void registerCallback(ISafeModeCallback callback);

    /**
     * Unsubscribe a previously registered callback.
     *
     * Safe to call even if the callback was never registered (no-op).
     * Called automatically by SafeModeManager.dispose() in the library.
     *
     * @param callback  the same callback object passed to registerCallback()
     */
    void unregisterCallback(ISafeModeCallback callback);

    /**
     * Returns the service API version.
     *
     * The safemode_library checks this on connect to ensure compatibility.
     * Current version: 1.
     *
     * If the library requires a minimum version (e.g., version 2 adds a new
     * feature), it can fall back gracefully when the service is older.
     */
    int getVersion();

    /**
     * [TEST ONLY] Directly inject vehicle data into the service.
     *
     * This bypasses VHAL entirely and is used for testing on devices that do
     * not have a running Vehicle HAL (e.g., Raspberry Pi 5 running plain AOSP).
     *
     * Usage via adb shell:
     *   adb shell /vendor/bin/safemode_client inject <speedMs> <gear> <fuelMl>
     *
     * Example — simulate 50 km/h driving in DRIVE gear with half a tank:
     *   adb shell /vendor/bin/safemode_client inject 13.89 8 25000.0
     *
     * This method dispatches the injected data to all registered callbacks
     * exactly as if it had arrived from VHAL — the library and app cannot tell
     * the difference. Useful for full-stack end-to-end testing without AAOS.
     */
    void injectTestData(in VehicleData data);
}
