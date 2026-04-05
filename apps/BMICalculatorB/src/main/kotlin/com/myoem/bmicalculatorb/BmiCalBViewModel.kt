// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatorb

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.myoem.bmicalculator.BmiCalManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

/**
 * UI state — identical shape to BMICalculatorA's UiState.
 */
data class UiState(
    val result: String = "—",
    val error: String? = null,
    val bmiServiceAvailable: Boolean = false,
    val calcServiceAvailable: Boolean = false,
)

/**
 * ViewModel for BMICalculatorB.
 *
 * Compare with BMICalculatorA's BmiCalViewModel:
 *   A: holds NativeBinder (app-level JNI object), calls NativeBinder.getBMI() etc.
 *   B: holds BmiCalManager (library object), calls manager.getBMI() etc.
 *      The manager owns the JNI internally — this ViewModel has zero binder imports.
 *
 * The binder complexity is encapsulated one level deeper.
 * From this ViewModel's perspective, BmiCalManager is just a regular Kotlin class.
 */
class BmiCalBViewModel : ViewModel() {

    private val manager = BmiCalManager()

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.update {
                it.copy(
                    bmiServiceAvailable  = manager.isBmiAvailable(),
                    calcServiceAvailable = manager.isCalcAvailable(),
                )
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
                val bmi = manager.getBMI(height, weight)
                _uiState.update {
                    it.copy(
                        result = "BMI: %.2f  (%s)".format(bmi, manager.bmiCategory(bmi)),
                        error  = null,
                    )
                }
            } catch (e: Exception) {
                _uiState.update { it.copy(error = e.message ?: "bmid error", result = "—") }
            }
        }
    }

    // ── Calculator ────────────────────────────────────────────────────────────

    fun computeAdd(input1: String, input2: String)      = calcOp("Add", input1, input2) { a, b -> manager.add(a, b) }
    fun computeSubtract(input1: String, input2: String) = calcOp("Sub", input1, input2) { a, b -> manager.subtract(a, b) }
    fun computeMultiply(input1: String, input2: String) = calcOp("Mul", input1, input2) { a, b -> manager.multiply(a, b) }
    fun computeDivide(input1: String, input2: String)   = calcOp("Div", input1, input2) { a, b -> manager.divide(a, b) }

    private fun calcOp(opName: String, input1: String, input2: String, op: (Int, Int) -> Int) {
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
}
