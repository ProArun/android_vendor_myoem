// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemode

/**
 * VehicleInfo — the library's public vehicle data model.
 *
 * This is a *library-layer* data class, distinct from the AIDL-generated
 * [VehicleData] parcelable. Keeping them separate gives us:
 *   1. A stable public API — the library can add helper properties (e.g.,
 *      [speedKmh], [gearName]) without modifying the AIDL interface.
 *   2. No Binder dependency in the library's public surface — callers
 *      don't need to import AIDL stubs directly.
 *   3. Freedom to evolve AIDL and library independently.
 *
 * [SafeModeManager] maps AIDL [VehicleData] → [VehicleInfo] internally
 * before delivering to [SafeModeListener].
 *
 * @property speedMs     Raw vehicle speed in metres per second (m/s).
 *                       Source: VHAL PERF_VEHICLE_SPEED (0x11600207).
 * @property gear        Current gear as a [VehicleGear] int bitmask.
 *                       Source: VHAL CURRENT_GEAR (0x11400400).
 * @property fuelLevelMl Fuel level in millilitres.
 *                       Source: VHAL FUEL_LEVEL (0x45100004).
 */
data class VehicleInfo(
    val speedMs:     Float = 0f,
    val gear:        Int   = VehicleGear.PARK,
    val fuelLevelMl: Float = 0f,
) {
    /** Speed converted to km/h for display or threshold checks. */
    val speedKmh: Float get() = speedMs * 3.6f

    /** Fuel level converted to litres. */
    val fuelLevelL: Float get() = fuelLevelMl / 1000f

    /** Human-readable gear name from [VehicleGear]. */
    val gearName: String get() = VehicleGear.name(gear)
}

/**
 * VehicleGear — constants mirroring android.hardware.automotive.vehicle.VehicleGear.
 *
 * The VHAL CURRENT_GEAR property uses a bitmask-style int. We mirror the
 * relevant subset here so the library has no dependency on hidden AIDL types.
 * Values must stay in sync with VehicleGear.aidl in the VHAL HAL package.
 */
object VehicleGear {
    const val NEUTRAL = 0x0001
    const val REVERSE = 0x0002
    const val PARK    = 0x0004
    const val DRIVE   = 0x0008
    const val GEAR_1  = 0x0010
    const val GEAR_2  = 0x0020
    const val GEAR_3  = 0x0040
    const val GEAR_4  = 0x0080
    const val GEAR_5  = 0x0100
    const val GEAR_6  = 0x0200
    const val GEAR_7  = 0x0400
    const val GEAR_8  = 0x0800
    const val GEAR_9  = 0x1000

    private val names = mapOf(
        NEUTRAL to "NEUTRAL",
        REVERSE to "REVERSE",
        PARK    to "PARK",
        DRIVE   to "DRIVE",
        GEAR_1  to "GEAR 1",
        GEAR_2  to "GEAR 2",
        GEAR_3  to "GEAR 3",
        GEAR_4  to "GEAR 4",
        GEAR_5  to "GEAR 5",
        GEAR_6  to "GEAR 6",
        GEAR_7  to "GEAR 7",
        GEAR_8  to "GEAR 8",
        GEAR_9  to "GEAR 9",
    )

    /** Returns a human-readable name for a gear bitmask value. */
    fun name(gear: Int): String = names[gear] ?: "UNKNOWN(0x${gear.toString(16)})"
}
