// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatora

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * UI state — single source of truth for the entire screen.
 */
data class UiState(
    val result: String = "—",
    val error: String? = null,
    val bmiServiceAvailable: Boolean = false,
    val calcServiceAvailable: Boolean = false,
)

/**
 * ViewModel for BMICalculatorA.
 *
 * All binder calls are delegated to [NativeBinder], a JNI object that uses
 * the NDK path (AServiceManager_checkService → AIBinder_transact) rather
 * than Java's BinderProxy.  This completely avoids the
 * BinderProxyTransactListener that caused IllegalArgumentException in the
 * earlier pure-Java implementation.
 *
 * Service availability is probed once at init (background thread) so the
 * green/red status chips can reflect real state without blocking the UI.
 */
class BmiCalViewModel : ViewModel() {

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    init {
        // Probe service availability on a background thread so the status
        // chips update as soon as the ViewModel is created.
        viewModelScope.launch(Dispatchers.IO) {
            val bmiAvail  = NativeBinder.isBmiServiceAvailable()
            val calcAvail = NativeBinder.isCalcServiceAvailable()
            _uiState.update {
                it.copy(bmiServiceAvailable = bmiAvail, calcServiceAvailable = calcAvail)
            }
        }
    }

    // ── BMI ───────────────────────────────────────────────────────────────────

    fun computeBmi(input1: String, input2: String) {
        val height = input1.toFloatOrNull()
        val weight = input2.toFloatOrNull()
        if (height == null || weight == null || height <= 0f || weight <= 0f) {
            _uiState.update { it.copy(error = "Enter valid height (m) and weight (kg)", result = "—") }
            return
        }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bmi = NativeBinder.getBMI(height, weight)
                _uiState.update {
                    it.copy(
                        result = "BMI: %.2f  (%s)".format(bmi, bmiCategory(bmi)),
                        error  = null,
                    )
                }
            } catch (e: Exception) {
                _uiState.update { it.copy(error = e.message ?: "bmid error", result = "—") }
            }
        }
    }

    // ── Calculator ────────────────────────────────────────────────────────────

    fun computeAdd(input1: String, input2: String)      = calcOp("Add", input1, input2) { a, b -> NativeBinder.calcAdd(a, b) }
    fun computeSubtract(input1: String, input2: String) = calcOp("Sub", input1, input2) { a, b -> NativeBinder.calcSubtract(a, b) }
    fun computeMultiply(input1: String, input2: String) = calcOp("Mul", input1, input2) { a, b -> NativeBinder.calcMultiply(a, b) }
    fun computeDivide(input1: String, input2: String)   = calcOp("Div", input1, input2) { a, b -> NativeBinder.calcDivide(a, b) }

    private fun calcOp(
        opName: String,
        input1: String,
        input2: String,
        op: (Int, Int) -> Int,
    ) {
        val a = input1.toIntOrNull()
        val b = input2.toIntOrNull()
        if (a == null || b == null) {
            _uiState.update { it.copy(error = "$opName requires whole numbers", result = "—") }
            return
        }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val result = op(a, b)
                _uiState.update { it.copy(result = "$opName: $result", error = null) }
            } catch (e: Exception) {
                _uiState.update { it.copy(error = e.message ?: "$opName error", result = "—") }
            }
        }
    }

    private fun bmiCategory(bmi: Float) = when {
        bmi < 18.5f -> "Underweight"
        bmi < 25.0f -> "Normal"
        bmi < 30.0f -> "Overweight"
        else        -> "Obese"
    }
}
