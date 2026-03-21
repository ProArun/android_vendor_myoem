// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemode

/**
 * SafeModeState — the three operational states of the vehicle safety system.
 *
 * The service ([ISafeModeService]) publishes raw sensor data (speed in m/s,
 * gear as an integer, fuel in ml). The *library* — not the service — is
 * responsible for interpreting those raw values into a SafeModeState.
 *
 * This separation is intentional:
 *   - The service lives in the vendor partition and changes only via OTA.
 *   - The library is an AAR that app developers version independently.
 *     Threshold changes (e.g., 5 km/h → 8 km/h) ship as a library update,
 *     not a full OTA.
 *
 * ── Thresholds ────────────────────────────────────────────────────────────
 *  Speed < 5 km/h  (1.39 m/s) → NO_SAFE_MODE    — normal UI, all features on
 *  5 ≤ speed < 15  (4.17 m/s) → NORMAL_SAFE_MODE — reduce UI complexity
 *  speed ≥ 15 km/h (4.17 m/s) → HARD_SAFE_MODE   — minimal UI, block distractions
 */
enum class SafeModeState {

    /**
     * Vehicle is stationary or moving very slowly (< 5 km/h).
     * No UI restrictions apply. Full app functionality is available.
     */
    NO_SAFE_MODE,

    /**
     * Vehicle is moving at low urban speed (5–15 km/h).
     * Apply moderate UI restrictions:
     *   - Reduce the number of interactive elements
     *   - Collapse complex menus
     *   - Prefer voice input over touch
     */
    NORMAL_SAFE_MODE,

    /**
     * Vehicle is moving at highway speed (≥ 15 km/h).
     * Apply strict UI restrictions:
     *   - Show only essential information (navigation, media controls)
     *   - Block text input
     *   - Dismiss non-critical dialogs automatically
     */
    HARD_SAFE_MODE;

    companion object {
        // ── Speed thresholds in km/h ────────────────────────────────────────
        // Stored as km/h for readability; converted from m/s in fromVehicleInfo().
        private const val NORMAL_KMH = 5.0f
        private const val HARD_KMH   = 15.0f

        /**
         * Derive the SafeModeState from raw vehicle data.
         *
         * Called by [SafeModeManager] every time the service delivers a new
         * [VehicleInfo]. The result is what gets delivered to [SafeModeListener].
         *
         * @param info Raw vehicle data from [ISafeModeService.getCurrentData()]
         *             or an [ISafeModeCallback.onVehicleDataChanged()] event.
         */
        @JvmStatic
        fun fromVehicleInfo(info: VehicleInfo): SafeModeState {
            // Convert m/s to km/h for threshold comparison.
            // VHAL always delivers PERF_VEHICLE_SPEED in m/s.
            val kmh = info.speedMs * 3.6f

            return when {
                kmh >= HARD_KMH   -> HARD_SAFE_MODE
                kmh >= NORMAL_KMH -> NORMAL_SAFE_MODE
                else              -> NO_SAFE_MODE
            }
        }
    }
}
