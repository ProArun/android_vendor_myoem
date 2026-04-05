// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatora

/**
 * JNI bridge to the two vendor binder services.
 *
 * WHY JNI instead of Java AIDL stubs or raw IBinder.transact()?
 *
 * Both bmid and calculatord are NDK-backend services registered via
 * AServiceManager_addService().  Calling them from Java goes through
 * BinderProxy.transact(), which in Android 15 has a
 * BinderProxyTransactListener set by ActivityManagerService.  That
 * listener throws IllegalArgumentException when it detects a call to
 * an idle (frozen) vendor process — a cross-backend compat issue
 * specific to Android 15.
 *
 * This JNI library (libbmicalculator_jni.so) uses the pure NDK path:
 *   AServiceManager_checkService()   — no Java BinderProxy at all
 *   IBMIService::fromBinder()        — NDK AIDL generated stub
 *   ICalculatorService::fromBinder()
 *   AIBinder_transact()              — native binder call
 *
 * The C++ path has no BinderProxyTransactListener, so the
 * IllegalArgumentException cannot occur regardless of the service's
 * freeze state.
 *
 * All functions throw RuntimeException on service unavailability or
 * on a ServiceSpecificException from the server.  Callers MUST invoke
 * these on a background thread (e.g. Dispatchers.IO).
 */
object NativeBinder {

    init {
        System.loadLibrary("bmicalculator_jni")
    }

    // ── Availability probes ───────────────────────────────────────────────────
    // Called once at ViewModel init to populate the status chips.

    external fun isBmiServiceAvailable(): Boolean
    external fun isCalcServiceAvailable(): Boolean

    // ── BMI ───────────────────────────────────────────────────────────────────

    /**
     * @param height height in metres (e.g. 1.75)
     * @param weight weight in kilograms (e.g. 70.0)
     * @return BMI = weight / (height * height)
     * @throws RuntimeException if bmid is unavailable or input is invalid
     */
    external fun getBMI(height: Float, weight: Float): Float

    // ── Calculator ────────────────────────────────────────────────────────────

    /** @throws RuntimeException if calculatord is unavailable */
    external fun calcAdd(a: Int, b: Int): Int

    /** @throws RuntimeException if calculatord is unavailable */
    external fun calcSubtract(a: Int, b: Int): Int

    /** @throws RuntimeException if calculatord is unavailable */
    external fun calcMultiply(a: Int, b: Int): Int

    /**
     * @throws RuntimeException if calculatord is unavailable or b == 0
     *   (server sends ServiceSpecificException with ERROR_DIVIDE_BY_ZERO = 1)
     */
    external fun calcDivide(a: Int, b: Int): Int
}
