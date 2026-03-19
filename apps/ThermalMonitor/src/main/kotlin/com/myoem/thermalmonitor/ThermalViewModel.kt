// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalmonitor

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.myoem.thermalcontrol.ThermalControlManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * UI state — single source of truth for the entire screen.
 */
data class UiState(
    val cpuTempCelsius: Float = 0f,
    val fanRpm: Int = -1,
    val fanSpeedPercent: Int = 0,
    val isFanRunning: Boolean = false,
    val isAutoMode: Boolean = true,
    val serviceAvailable: Boolean = false,
    val errorMessage: String? = null
)

// open — allows FakeThermalViewModel in tests to subclass without mockito-inline
open class ThermalViewModel(private val manager: ThermalControlManager) : ViewModel() {

    private val _uiState = MutableStateFlow(UiState())
    open val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    init {
        // Start auto-refresh loop: polls every 2 seconds on the IO dispatcher
        viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                fetchData()
                delay(2_000)
            }
        }
    }

    private fun fetchData() {
        if (!manager.isAvailable()) {
            _uiState.update { it.copy(serviceAvailable = false, errorMessage = "thermalcontrold not running") }
            return
        }
        _uiState.update {
            it.copy(
                cpuTempCelsius   = manager.getCpuTemperatureCelsius(),
                fanRpm           = manager.getFanSpeedRpm(),
                fanSpeedPercent  = manager.getFanSpeedPercent(),
                isFanRunning     = manager.isFanRunning(),
                isAutoMode       = manager.isFanAutoMode(),
                serviceAvailable = true,
                errorMessage     = null
            )
        }
    }

    open fun turnFanOn() {
        viewModelScope.launch(Dispatchers.IO) {
            manager.setFanEnabled(true)
            fetchData()
        }
    }

    open fun turnFanOff() {
        viewModelScope.launch(Dispatchers.IO) {
            manager.setFanEnabled(false)
            fetchData()
        }
    }

    open fun setFanSpeed(percent: Int) {
        viewModelScope.launch(Dispatchers.IO) {
            manager.setFanSpeed(percent)
            fetchData()
        }
    }

    open fun setAutoMode() {
        viewModelScope.launch(Dispatchers.IO) {
            manager.setFanAutoMode(true)
            fetchData()
        }
    }
}

/**
 * Factory to inject ThermalControlManager into the ViewModel.
 * The Activity creates the manager (using ServiceManager, a @hide API)
 * and passes it here so the ViewModel itself stays free of @hide imports.
 */
class ThermalViewModelFactory(
    private val manager: ThermalControlManager
) : ViewModelProvider.Factory {
    @Suppress("UNCHECKED_CAST")
    override fun <T : ViewModel> create(modelClass: Class<T>): T =
        ThermalViewModel(manager) as T
}
