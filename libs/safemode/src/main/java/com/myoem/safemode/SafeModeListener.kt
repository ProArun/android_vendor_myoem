// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemode

/**
 * SafeModeListener — callback interface for apps using safemode_library.
 *
 * Implement this interface in your Activity, Fragment, ViewModel, or any
 * other component that needs to react to vehicle state changes.
 *
 * ── Threading ──────────────────────────────────────────────────────────────
 * [onSafeModeChanged] is called on the *main thread* (Looper.getMainLooper()).
 * You can safely update UI directly inside this callback — no Handler.post()
 * or runOnUiThread() needed.
 *
 * ── Lifecycle ──────────────────────────────────────────────────────────────
 * Always pair [SafeModeManager.attach] / [SafeModeManager.dispose] with the
 * Activity or Fragment lifecycle to avoid leaks:
 *
 * ```kotlin
 * // In Activity:
 * override fun onStart()  { safeModeManager.attach(this, listener) }
 * override fun onStop()   { safeModeManager.dispose(this) }
 *
 * // In Fragment:
 * override fun onViewCreated(...) { safeModeManager.attach(this, listener) }
 * override fun onDestroyView()    { safeModeManager.dispose(this) }
 * ```
 *
 * ── Example ────────────────────────────────────────────────────────────────
 * ```kotlin
 * val listener = object : SafeModeListener {
 *     override fun onSafeModeChanged(state: SafeModeState, info: VehicleInfo) {
 *         when (state) {
 *             SafeModeState.NO_SAFE_MODE    -> showFullUi()
 *             SafeModeState.NORMAL_SAFE_MODE -> simplifyUi()
 *             SafeModeState.HARD_SAFE_MODE   -> showMinimalUi()
 *         }
 *     }
 * }
 * ```
 */
fun interface SafeModeListener {

    /**
     * Called whenever the vehicle's SafeMode state changes.
     *
     * Note: This is a *state-change* callback, not a raw VHAL event callback.
     * If speed oscillates between 14 km/h and 16 km/h, you may receive rapid
     * state changes. Implement debounce in your UI if needed.
     *
     * @param state The new [SafeModeState] derived from [info].
     * @param info  The raw vehicle data that triggered the state change.
     *              Includes speed, gear, and fuel level for any supplementary
     *              display your app might want to show.
     */
    fun onSafeModeChanged(state: SafeModeState, info: VehicleInfo)
}
