// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmicalculatorc

import android.os.ServiceManager
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.myoem.bmiapp.IBmiAppService
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class UiState(
    val result: String = "—",
    val error: String? = null,
    val bmiServiceAvailable: Boolean = false,
    val calcServiceAvailable: Boolean = false,
)

/**
 * ViewModel for BMICalculatorC.
 *
 * The entire binder interaction with bmid and calculatord is reduced to:
 *   1. ServiceManager.getService("bmi_system_service")   ← @hide, but that's it
 *   2. IBmiAppService.Stub.asInterface(binder)           ← generated AIDL
 *   3. service.getBMI(height, weight)                    ← plain interface call
 *
 * No JNI. No Parcel. No FLAG_PRIVATE_VENDOR. No binder transaction codes.
 * No stability knowledge. This is how Android's own framework apps work —
 * AudioManager.setVolume(), LocationManager.requestUpdates(), etc. all follow
 * exactly this pattern.
 *
 * Compare:
 *   BmiCalAViewModel (App A) — calls NativeBinder.getBMI() [JNI in the app]
 *   BmiCalBViewModel (App B) — calls manager.getBMI()  [JNI in the manager lib]
 *   BmiCalCViewModel (App C) — calls service.getBMI()  [NO JNI anywhere in app]
 */
class BmiCalCViewModel : ViewModel() {

    // ServiceManager.getService() is the standard way to look up a named binder
    // service registered via ServiceManager.addService(). It's @hide because
    // regular third-party apps should use Context.getSystemService() instead.
    // For a system app talking to an OEM service, this is the correct API.
    private val service: IBmiAppService? = run {
        val binder = ServiceManager.getService("bmi_system_service")
        if (binder != null) IBmiAppService.Stub.asInterface(binder) else null
    }

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.update {
                it.copy(
                    bmiServiceAvailable  = service?.isBmiAvailable()  ?: false,
                    calcServiceAvailable = service?.isCalcAvailable() ?: false,
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
                val bmi = service?.getBMI(height, weight)
                    ?: throw RuntimeException("BmiSystemService not available")
                _uiState.update {
                    it.copy(result = "BMI: %.2f  (%s)".format(bmi, bmiCategory(bmi)), error = null)
                }
            } catch (e: Exception) {
                _uiState.update { it.copy(error = e.message ?: "bmid error", result = "—") }
            }
        }
    }

    // ── Calculator ────────────────────────────────────────────────────────────

    fun computeAdd(input1: String, input2: String)      = calcOp("Add",      input1, input2) { a, b -> service!!.add(a, b) }
    fun computeSubtract(input1: String, input2: String) = calcOp("Subtract", input1, input2) { a, b -> service!!.subtract(a, b) }
    fun computeMultiply(input1: String, input2: String) = calcOp("Multiply", input1, input2) { a, b -> service!!.multiply(a, b) }
    fun computeDivide(input1: String, input2: String)   = calcOp("Divide",   input1, input2) { a, b -> service!!.divide(a, b) }

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

    // ── Helpers ───────────────────────────────────────────────────────────────

    private fun bmiCategory(bmi: Float): String = when {
        bmi < 18.5f -> "Underweight"
        bmi < 25.0f -> "Normal"
        bmi < 30.0f -> "Overweight"
        else        -> "Obese"
    }
}
