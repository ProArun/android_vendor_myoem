// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetectorapp

/**
 * MVI contract for the PIR motion detector screen.
 *
 * ── MVI pattern ───────────────────────────────────────────────────────────────
 *   Model  = MotionUiState    — immutable snapshot of what the UI should show
 *   Intent = MotionUiEvent    — user actions / lifecycle triggers sent to ViewModel
 *   Effect = MotionUiEffect   — one-time events (Snackbar, Toast) that don't belong in state
 */

// ── UiState ───────────────────────────────────────────────────────────────────
/**
 * Complete UI state for the motion detector screen.
 *
 * @param motionDetected        true when PIR sensor is currently detecting motion.
 *                              Drives the colour and label of the indicator.
 * @param lastEventTimestampNs  kernel monotonic timestamp of the last GPIO edge, in ns.
 *                              Shown in the UI as elapsed time or raw value.
 * @param serviceConnected      true when the Binder connection to pirdetectord is live.
 *                              false = show "Service unavailable" overlay.
 * @param error                 non-null when a recoverable error occurred (shown in Snackbar).
 */
data class MotionUiState(
    val motionDetected: Boolean = false,
    val lastEventTimestampNs: Long = 0L,
    val serviceConnected: Boolean = false,
    val error: String? = null,
)

// ── UiEvent ───────────────────────────────────────────────────────────────────
/**
 * User-driven or lifecycle-driven actions sent to the ViewModel.
 *
 * All UI interactions go through onEvent() — a single entry point.
 * The ViewModel decides what to do with each event.
 */
sealed class MotionUiEvent {
    /** Sent from LaunchedEffect(Unit) in the composable on first composition. */
    data object Connect : MotionUiEvent()

    /** Sent from DisposableEffect onDispose — Activity going to background. */
    data object Disconnect : MotionUiEvent()
}

// ── UiEffect ──────────────────────────────────────────────────────────────────
/**
 * One-time side effects that should not live in UiState (they fire once then disappear).
 *
 * Collected in the composable via LaunchedEffect + SharedFlow.
 */
sealed class MotionUiEffect {
    /** Show a Snackbar or Toast with the given message. */
    data class ShowError(val message: String) : MotionUiEffect()
}
