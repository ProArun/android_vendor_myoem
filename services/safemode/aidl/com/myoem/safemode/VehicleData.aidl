// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemode;

/**
 * VehicleData — raw snapshot of vehicle state, as read from VHAL.
 *
 * DESIGN RULE: All values are in RAW VHAL units — no conversion happens inside
 * the service. Unit conversion (m/s → km/h, ml → %) is the library's job.
 *
 *   speedMs     — PERF_VEHICLE_SPEED   — float, metres per second
 *   gear        — CURRENT_GEAR         — int32, VehicleGear enum constants
 *   fuelLevelMl — FUEL_LEVEL           — float, millilitres
 *
 * This is a Parcelable — it is serialised/deserialised automatically by the
 * AIDL framework when passed across the Binder IPC boundary.
 *
 * Default values ensure the struct is safe to read before the first VHAL event
 * arrives (speed=0, gear=PARK=4, fuel=0).
 */
@VintfStability
parcelable VehicleData {

    /**
     * Current vehicle speed in metres per second.
     * VHAL property: PERF_VEHICLE_SPEED (0x11600207)
     * Type: CONTINUOUS — arrives at ~1 Hz when subscribed.
     * Default: 0.0 (stationary)
     */
    float speedMs = 0.0f;

    /**
     * Current gear position, encoded as a VehicleGear enum integer.
     * VHAL property: CURRENT_GEAR (0x11400400)
     * Type: ON_CHANGE — only pushed when driver shifts gear.
     *
     * Common values (from android.car.VehicleGear):
     *   0x0001 = NEUTRAL  0x0002 = REVERSE
     *   0x0004 = PARK     0x0008 = DRIVE
     *   0x0010 = GEAR_1 … 0x1000 = GEAR_9
     *
     * Default: 4 (PARK) — safe assumption at startup.
     */
    int gear = 4;

    /**
     * Current fuel level in millilitres.
     * VHAL property: FUEL_LEVEL (0x45100004)
     * Type: ON_CHANGE — only pushed when fuel level changes measurably.
     * Default: 0.0
     *
     * To convert to percentage: (fuelLevelMl / tankCapacityMl) * 100
     * The tank capacity is NOT part of this struct — it is configured
     * in the library (SafeModeManager.configure()) per vehicle variant.
     */
    float fuelLevelMl = 0.0f;
}
