// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculator

/**
 * BmiCalManager — public API for apps to call bmid and calculatord.
 *
 * Apps instantiate this class and call its methods. They never import
 * libbinder, ServiceManager, Parcel, IBinder, or any binder internals.
 * All binder complexity is hidden inside libbmicalmanager_jni.so.
 *
 * This is the key difference from BMICalculatorA, which called NativeBinder
 * directly from the app. Here, the manager owns the JNI bridge — the app
 * layer is pure Kotlin with no platform API dependency.
 *
 * All methods throw RuntimeException on service unavailability or server error.
 * Callers must invoke these on a background thread (e.g. Dispatchers.IO).
 */
class BmiCalManager {

    init {
        System.loadLibrary("bmicalmanager_jni")
    }

    // ── Service availability ──────────────────────────────────────────────────

    /** Returns true if bmid is registered and reachable. */
    external fun isBmiAvailable(): Boolean

    /** Returns true if calculatord is registered and reachable. */
    external fun isCalcAvailable(): Boolean

    // ── BMI ───────────────────────────────────────────────────────────────────

    /**
     * Calculate BMI.
     * @param height height in metres (e.g. 1.75)
     * @param weight weight in kilograms (e.g. 70.0)
     * @return BMI = weight / (height * height)
     * @throws RuntimeException if service unavailable or input invalid
     */
    fun getBMI(height: Float, weight: Float): Float = nativeGetBMI(height, weight)

    // ── Calculator ────────────────────────────────────────────────────────────

    /** @throws RuntimeException if calculatord unavailable */
    fun add(a: Int, b: Int): Int = nativeAdd(a, b)

    /** @throws RuntimeException if calculatord unavailable */
    fun subtract(a: Int, b: Int): Int = nativeSubtract(a, b)

    /** @throws RuntimeException if calculatord unavailable */
    fun multiply(a: Int, b: Int): Int = nativeMultiply(a, b)

    /** @throws RuntimeException if calculatord unavailable or b == 0 */
    fun divide(a: Int, b: Int): Int = nativeDivide(a, b)

    // ── BMI category helper ───────────────────────────────────────────────────

    fun bmiCategory(bmi: Float): String = when {
        bmi < 18.5f -> "Underweight"
        bmi < 25.0f -> "Normal"
        bmi < 30.0f -> "Overweight"
        else        -> "Obese"
    }

    // ── JNI (private — apps never call these directly) ───────────────────────

    private external fun nativeGetBMI(height: Float, weight: Float): Float
    private external fun nativeAdd(a: Int, b: Int): Int
    private external fun nativeSubtract(a: Int, b: Int): Int
    private external fun nativeMultiply(a: Int, b: Int): Int
    private external fun nativeDivide(a: Int, b: Int): Int
}
