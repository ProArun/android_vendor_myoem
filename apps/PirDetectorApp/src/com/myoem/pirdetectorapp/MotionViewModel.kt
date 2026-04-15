// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetectorapp

import android.app.Application
import android.os.IBinder
import android.os.ServiceManager
import android.util.Log
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.myoem.pirdetector.MotionListener
import com.myoem.pirdetector.PirDetectorManager
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * MotionViewModel — MVI ViewModel for the PIR detector screen.
 *
 * ── Responsibilities ──────────────────────────────────────────────────────────
 * 1. Connect to pirdetectord via PirDetectorManager on Connect event.
 * 2. Receive MotionListener callbacks (on main thread) → update UiState.
 * 3. Disconnect and unregister when Disconnect event is received.
 * 4. Expose uiState (StateFlow) and uiEffect (SharedFlow) to the composable.
 *
 * ── Why AndroidViewModel? ─────────────────────────────────────────────────────
 * ServiceManager is accessed via ServiceManager.checkService() which is a
 * framework API. We could inject the IBinder, but AndroidViewModel is simpler
 * here and avoids adding a custom ViewModelFactory for this AOSP-only app.
 *
 * ── Threading ─────────────────────────────────────────────────────────────────
 * ServiceManager.checkService() and PirDetectorManager calls are done on the
 * main thread inside viewModelScope.launch (which uses Dispatchers.Main by default).
 * The PirDetectorManager guarantees that MotionListener callbacks arrive on the
 * main thread — so onMotionDetected/onMotionEnded are called on the same thread
 * that runs this ViewModel. StateFlow updates are safe.
 */
class MotionViewModel(application: Application) : AndroidViewModel(application) {

    companion object {
        private const val TAG = "MotionViewModel"
    }

    // ── State and effect flows ────────────────────────────────────────────────

    private val _uiState = MutableStateFlow(MotionUiState())
    val uiState: StateFlow<MotionUiState> = _uiState.asStateFlow()

    private val _uiEffect = MutableSharedFlow<MotionUiEffect>()
    val uiEffect: SharedFlow<MotionUiEffect> = _uiEffect.asSharedFlow()

    // ── Manager reference ─────────────────────────────────────────────────────
    private var manager: PirDetectorManager? = null

    // ── MotionListener ────────────────────────────────────────────────────────
    // Called on the main thread by PirDetectorManager's Handler dispatch.
    // Directly updates StateFlow — safe because we're on the main thread.
    private val motionListener = object : MotionListener {
        override fun onMotionDetected(timestampNs: Long) {
            Log.d(TAG, "onMotionDetected ts=$timestampNs")
            _uiState.update { it.copy(motionDetected = true, lastEventTimestampNs = timestampNs) }
        }

        override fun onMotionEnded(timestampNs: Long) {
            Log.d(TAG, "onMotionEnded ts=$timestampNs")
            _uiState.update { it.copy(motionDetected = false, lastEventTimestampNs = timestampNs) }
        }
    }

    // ── Single entry point for all UI events ──────────────────────────────────
    fun onEvent(event: MotionUiEvent) {
        when (event) {
            is MotionUiEvent.Connect    -> connect()
            is MotionUiEvent.Disconnect -> disconnect()
        }
    }

    // ── Connect to pirdetectord ───────────────────────────────────────────────
    private fun connect() {
        viewModelScope.launch {
            Log.d(TAG, "connect() — looking up pirdetectord in ServiceManager")

            // ServiceManager.checkService() — @hide, available because platform_apis:true.
            // Returns null if the service is not running yet (daemon not started, crashed, etc.)
            val binder: IBinder? = ServiceManager.checkService(PirDetectorManager.SERVICE_NAME)

            if (binder == null) {
                Log.w(TAG, "pirdetectord not found in ServiceManager")
                _uiState.update { it.copy(serviceConnected = false) }
                _uiEffect.emit(
                    MotionUiEffect.ShowError("PIR service unavailable — is pirdetectord running?")
                )
                return@launch
            }

            try {
                manager = PirDetectorManager(binder)

                // Get initial state so the UI is correct before the first edge arrives
                val initialState = manager!!.getCurrentState()
                _uiState.update {
                    it.copy(
                        serviceConnected = true,
                        motionDetected = initialState == 1,
                    )
                }
                Log.d(TAG, "Initial PIR state: $initialState")

                // Register listener — PirDetectorManager delivers callbacks on main thread
                manager!!.registerListener(motionListener)
                Log.d(TAG, "Listener registered — monitoring PIR sensor")

            } catch (e: Exception) {
                Log.e(TAG, "Failed to connect to pirdetectord", e)
                _uiState.update { it.copy(serviceConnected = false) }
                _uiEffect.emit(MotionUiEffect.ShowError("Connection failed: ${e.message}"))
            }
        }
    }

    // ── Disconnect from pirdetectord ──────────────────────────────────────────
    private fun disconnect() {
        viewModelScope.launch {
            try {
                manager?.unregisterListener()
                Log.d(TAG, "Listener unregistered")
            } catch (e: Exception) {
                Log.e(TAG, "Error unregistering listener", e)
            } finally {
                manager = null
                _uiState.update { it.copy(serviceConnected = false, motionDetected = false) }
            }
        }
    }

    // ── Cleanup when ViewModel is cleared (Activity destroyed) ───────────────
    override fun onCleared() {
        super.onCleared()
        // Ensure the callback is always unregistered even if Disconnect event
        // wasn't sent (e.g., app process killed). The try/catch handles the case
        // where the service is already gone.
        try {
            manager?.unregisterListener()
        } catch (e: Exception) {
            Log.w(TAG, "onCleared: unregisterListener threw (service gone?)", e)
        }
        manager = null
        Log.d(TAG, "ViewModel cleared")
    }
}
