// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalmonitor

// ─────────────────────────────────────────────────────────────────────────────
// What this file tests
//
//   ThermalScreen — the top-level Compose UI function.
//
//   When service is unavailable:
//     → Shows a ServiceUnavailableCard with the error message text
//
//   When service is available:
//     → Shows TemperatureCard  (temperature value + category label)
//     → Shows FanStatusCard    (percent, RPM, running status)
//     → Shows FanControlCard   (Turn On / Turn Off / Auto buttons + slider)
//
//   These are instrumented tests — they run on a real device or emulator,
//   render the actual Compose UI, and interact with it via ComposeTestRule.
//
// Why instrumented (not pure JVM)?
//   Compose UI can only be rendered in an Android runtime.  ComposeTestRule
//   launches an Activity, inflates the Compose tree, and lets us find nodes
//   by text/semantics and simulate user interactions (clicks, swipes).
//
// Key Compose test APIs used:
//   composeTestRule.setContent { }     — inflate a composable in the test
//   onNodeWithText("foo")              — find a node by visible text
//   onNodeWithContentDescription("x") — find a node by accessibility label
//   assertIsDisplayed()               — verify node is on screen
//   performClick()                    — simulate a click
//   assertTextContains("x")           — check text content
// ─────────────────────────────────────────────────────────────────────────────

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import com.myoem.thermalcontrol.ThermalControlManager
import com.myoem.thermalmonitor.ui.ThermalScreen
import com.myoem.thermalmonitor.ui.theme.ThermalMonitorTheme
import kotlinx.coroutines.flow.MutableStateFlow
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.mock
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when` as whenever

/**
 * Compose instrumented tests for [ThermalScreen].
 *
 * These tests render the composable with a controlled [UiState] and verify
 * that the correct UI elements appear and respond to user interactions.
 *
 * Testing strategy:
 *   We use a [FakeThermalViewModel] (a test double) instead of the real
 *   ThermalViewModel.  This gives us full control of the UiState without
 *   needing a real service, real coroutines, or real hardware.
 *
 *   FakeThermalViewModel exposes a mutable StateFlow so we can push any
 *   UiState we want and verify the UI reacts correctly.
 */
@RunWith(AndroidJUnit4::class)
class ThermalScreenTest {

    // ComposeTestRule starts a minimal Compose Activity for each test
    @get:Rule
    val composeTestRule = createComposeRule()

    // ─────────────────────────────────────────────────────────────────────────
    // Test double: FakeThermalViewModel
    //
    // We subclass ThermalViewModel with a controllable StateFlow.
    // Using a fake (not a mock) is cleaner for Compose tests because Compose
    // observes the StateFlow directly — Mockito stubs won't trigger recomposition.
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * A minimal ViewModel for tests.  Exposes [_state] so tests can push
     * any UiState and verify the UI reacts.  Action methods record the last
     * call so tests can verify interactions.
     */
    private class FakeThermalViewModel : ThermalViewModel(
        // ThermalViewModel requires a ThermalControlManager — pass null-binder version
        ThermalControlManager(null)
    ) {
        // Override the backing StateFlow with a controllable one
        private val _fakeState = MutableStateFlow(UiState())

        // We shadow uiState from the parent by re-declaring as val
        // The parent's uiState will be ignored because ThermalScreen
        // receives this instance and reads uiState from it.
        override val uiState get() = _fakeState

        // Expose the mutable flow so tests can push states
        fun setState(state: UiState) { _fakeState.value = state }

        // Track method calls for verify-style assertions
        var lastSetFanSpeed: Int? = null
        var turnFanOnCalled = false
        var turnFanOffCalled = false
        var setAutoModeCalled = false

        override fun turnFanOn() { turnFanOnCalled = true }
        override fun turnFanOff() { turnFanOffCalled = true }
        override fun setFanSpeed(percent: Int) { lastSetFanSpeed = percent }
        override fun setAutoMode() { setAutoModeCalled = true }
    }

    // Helper: launch ThermalScreen with a controlled state
    private fun launchScreen(state: UiState): FakeThermalViewModel {
        val viewModel = FakeThermalViewModel()
        viewModel.setState(state)

        composeTestRule.setContent {
            ThermalMonitorTheme {
                ThermalScreen(viewModel = viewModel)
            }
        }
        return viewModel
    }


    // =========================================================================
    // Section A — Service unavailable UI
    //
    // When serviceAvailable == false, ThermalScreen shows ServiceUnavailableCard.
    // The TemperatureCard, FanStatusCard, and FanControlCard must NOT appear.
    // =========================================================================

    /**
     * The "Service Unavailable" title must be displayed when the service is down.
     * This text comes from R.string.service_unavailable.
     */
    @Test
    fun whenServiceUnavailable_ShowsServiceUnavailableTitle() {
        launchScreen(UiState(serviceAvailable = false))

        // The string "Service Unavailable" is defined in res/values/strings.xml
        composeTestRule
            .onNodeWithText("Service Unavailable", substring = true)
            .assertIsDisplayed()
    }

    /**
     * The error message detail text must be displayed below the title.
     * In ThermalViewModel, errorMessage = "thermalcontrold not running"
     * when manager.isAvailable() == false.
     */
    @Test
    fun whenServiceUnavailable_ShowsErrorMessageDetail() {
        launchScreen(UiState(
            serviceAvailable = false,
            errorMessage = "thermalcontrold not running"
        ))

        composeTestRule
            .onNodeWithText("thermalcontrold not running", substring = true)
            .assertIsDisplayed()
    }


    // =========================================================================
    // Section B — Service available UI
    //
    // When serviceAvailable == true, all three cards must be shown.
    // =========================================================================

    /**
     * The CPU temperature value must be displayed in the TemperatureCard.
     * The format string in ThermalScreen is "%.1f °C".format(temp).
     */
    @Test
    fun whenServiceAvailable_ShowsTemperatureValue() {
        launchScreen(UiState(serviceAvailable = true, cpuTempCelsius = 42.5f))

        composeTestRule
            .onNodeWithText("42.5 °C", substring = true)
            .assertIsDisplayed()
    }

    /**
     * The temperature category label must be shown below the numeric value.
     * 42.5°C is below 50°C → "Cool"
     */
    @Test
    fun whenServiceAvailable_ShowsTemperatureCategory_Cool() {
        launchScreen(UiState(serviceAvailable = true, cpuTempCelsius = 42.5f))

        // The category dot text is "● Cool" (from ThermalScreen.kt TemperatureCard)
        composeTestRule
            .onNodeWithText("Cool", substring = true)
            .assertIsDisplayed()
    }

    /** 75°C is in the Hot range (70–84.9°C) → category "Hot" */
    @Test
    fun whenServiceAvailable_ShowsTemperatureCategory_Hot() {
        launchScreen(UiState(serviceAvailable = true, cpuTempCelsius = 75.0f))

        composeTestRule
            .onNodeWithText("Hot", substring = true)
            .assertIsDisplayed()
    }

    /** Fan speed percentage must appear in FanStatusCard */
    @Test
    fun whenServiceAvailable_ShowsFanSpeedPercent() {
        launchScreen(UiState(serviceAvailable = true, fanSpeedPercent = 50))

        composeTestRule
            .onNodeWithText("50%", substring = true)
            .assertIsDisplayed()
    }

    /** Fan RPM must appear in FanStatusCard */
    @Test
    fun whenServiceAvailable_ShowsFanRpm() {
        launchScreen(UiState(serviceAvailable = true, fanRpm = 1200))

        composeTestRule
            .onNodeWithText("1200 RPM", substring = true)
            .assertIsDisplayed()
    }

    /** When fanRpm == -1 (no tachometer), show "N/A" instead of "-1 RPM" */
    @Test
    fun whenServiceAvailable_FanRpmUnavailable_ShowsNA() {
        launchScreen(UiState(serviceAvailable = true, fanRpm = -1))

        composeTestRule
            .onNodeWithText("N/A", substring = true)
            .assertIsDisplayed()
    }

    /** isFanRunning == true → display "Yes" */
    @Test
    fun whenFanRunning_ShowsYes() {
        launchScreen(UiState(serviceAvailable = true, isFanRunning = true))

        composeTestRule
            .onNodeWithText("Yes", substring = true)
            .assertIsDisplayed()
    }

    /** isFanRunning == false → display "No" */
    @Test
    fun whenFanNotRunning_ShowsNo() {
        launchScreen(UiState(serviceAvailable = true, isFanRunning = false))

        composeTestRule
            .onNodeWithText("No", substring = true)
            .assertIsDisplayed()
    }


    // =========================================================================
    // Section C — Fan control buttons
    //
    // FanControlCard has three buttons: Turn On, Turn Off, Auto Mode.
    // =========================================================================

    /** The "Turn On" button must be visible in the FanControlCard */
    @Test
    fun fanControlCard_TurnOnButton_IsDisplayed() {
        launchScreen(UiState(serviceAvailable = true))

        composeTestRule
            .onNodeWithText("Turn On", substring = true)
            .assertIsDisplayed()
    }

    /** Clicking "Turn On" must call viewModel.turnFanOn() */
    @Test
    fun fanControlCard_TurnOnButton_WhenClicked_CallsViewModel() {
        val viewModel = launchScreen(UiState(serviceAvailable = true))

        composeTestRule
            .onNodeWithText("Turn On", substring = true)
            .performClick()

        assertTrue("turnFanOn() should have been called after clicking Turn On",
            viewModel.turnFanOnCalled)
    }

    /** The "Turn Off" button must be visible */
    @Test
    fun fanControlCard_TurnOffButton_IsDisplayed() {
        launchScreen(UiState(serviceAvailable = true))

        composeTestRule
            .onNodeWithText("Turn Off", substring = true)
            .assertIsDisplayed()
    }

    /** Clicking "Turn Off" must call viewModel.turnFanOff() */
    @Test
    fun fanControlCard_TurnOffButton_WhenClicked_CallsViewModel() {
        val viewModel = launchScreen(UiState(serviceAvailable = true))

        composeTestRule
            .onNodeWithText("Turn Off", substring = true)
            .performClick()

        assertTrue("turnFanOff() should have been called after clicking Turn Off",
            viewModel.turnFanOffCalled)
    }

    /** The "Auto" button must be visible */
    @Test
    fun fanControlCard_AutoButton_IsDisplayed() {
        launchScreen(UiState(serviceAvailable = true))

        composeTestRule
            .onNodeWithText("Auto", substring = true)
            .assertIsDisplayed()
    }

    /** Clicking "Auto" must call viewModel.setAutoMode() */
    @Test
    fun fanControlCard_AutoButton_WhenClicked_CallsViewModel() {
        val viewModel = launchScreen(UiState(serviceAvailable = true))

        composeTestRule
            .onNodeWithText("Auto", substring = true)
            .performClick()

        assertTrue("setAutoMode() should have been called after clicking Auto",
            viewModel.setAutoModeCalled)
    }


    // =========================================================================
    // Section D — Apply button (text field + speed set)
    //
    // The FanControlCard has an OutlinedTextField + "Apply" button.
    // Clicking Apply calls viewModel.setFanSpeed(textFieldValue).
    // =========================================================================

    /** The "Apply" button must be visible in the FanControlCard */
    @Test
    fun fanControlCard_ApplyButton_IsDisplayed() {
        launchScreen(UiState(serviceAvailable = true))

        composeTestRule
            .onNodeWithText("Apply", substring = true)
            .assertIsDisplayed()
    }

    /**
     * Clicking "Apply" should call viewModel.setFanSpeed() with the current
     * text field value.  The text field is initialized from fanSpeedPercent,
     * so if state has percent=50 the button click should call setFanSpeed(50).
     */
    @Test
    fun fanControlCard_ApplyButton_WhenClicked_CallsViewModelWithCurrentPercent() {
        val viewModel = launchScreen(UiState(serviceAvailable = true, fanSpeedPercent = 50))

        composeTestRule
            .onNodeWithText("Apply", substring = true)
            .performClick()

        // The text field was initialised to "50" from state.fanSpeedPercent
        assertEquals(
            "Apply with initial percent=50 should call setFanSpeed(50)",
            50, viewModel.lastSetFanSpeed
        )
    }


    // =========================================================================
    // Section E — AUTO badge in top bar
    //
    // When isAutoMode == true, an "AUTO" badge appears in the TopAppBar.
    // =========================================================================

    /** When isAutoMode is true, the "AUTO" badge must be displayed in the app bar */
    @Test
    fun autoModeBadge_WhenAutoModeTrue_IsDisplayed() {
        launchScreen(UiState(serviceAvailable = true, isAutoMode = true))

        composeTestRule
            .onNodeWithText("AUTO")
            .assertIsDisplayed()
    }

    /** When isAutoMode is false, the "AUTO" badge must NOT be displayed */
    @Test
    fun autoModeBadge_WhenAutoModeFalse_IsNotDisplayed() {
        launchScreen(UiState(serviceAvailable = true, isAutoMode = false))

        // assertDoesNotExist() is not in this Compose test prebuilt;
        // onAllNodes + assertCountEquals(0) is the equivalent check.
        composeTestRule
            .onAllNodesWithText("AUTO")
            .assertCountEquals(0)
    }


    // =========================================================================
    // Section F — State update propagation
    //
    // Verify that the UI reacts to new UiState emissions from the StateFlow.
    // =========================================================================

    /**
     * When the state changes, the UI must recompose and show the new values.
     * This tests that ThermalScreen correctly observes the StateFlow.
     */
    @Test
    fun stateUpdate_NewTemp_UIRefreshes() {
        val viewModel = FakeThermalViewModel()

        composeTestRule.setContent {
            ThermalMonitorTheme {
                ThermalScreen(viewModel = viewModel)
            }
        }

        // Initial state: service available at 40°C
        viewModel.setState(UiState(serviceAvailable = true, cpuTempCelsius = 40.0f))
        composeTestRule.waitForIdle()

        composeTestRule
            .onNodeWithText("40.0 °C", substring = true)
            .assertIsDisplayed()

        // State update: temperature rises to 90°C (Critical)
        viewModel.setState(UiState(serviceAvailable = true, cpuTempCelsius = 90.0f))
        composeTestRule.waitForIdle()

        // Old value should be gone, new value should appear
        composeTestRule
            .onNodeWithText("90.0 °C", substring = true)
            .assertIsDisplayed()

        // Category must also update to "Critical" (90°C ≥ 85°C threshold)
        composeTestRule
            .onNodeWithText("Critical", substring = true)
            .assertIsDisplayed()
    }

    /**
     * Transition from unavailable → available.
     * The error card should disappear and the data cards should appear.
     */
    @Test
    fun stateUpdate_ServiceBecomesAvailable_ErrorCardHides() {
        val viewModel = FakeThermalViewModel()

        composeTestRule.setContent {
            ThermalMonitorTheme {
                ThermalScreen(viewModel = viewModel)
            }
        }

        // Start: service not running
        viewModel.setState(UiState(serviceAvailable = false,
            errorMessage = "thermalcontrold not running"))
        composeTestRule.waitForIdle()

        composeTestRule
            .onNodeWithText("Service Unavailable", substring = true)
            .assertIsDisplayed()

        // Service comes online
        viewModel.setState(UiState(serviceAvailable = true, cpuTempCelsius = 45.0f))
        composeTestRule.waitForIdle()

        // Error card must be gone
        composeTestRule
            .onAllNodesWithText("Service Unavailable", substring = true)
            .assertCountEquals(0)

        // Data must be visible
        composeTestRule
            .onNodeWithText("45.0 °C", substring = true)
            .assertIsDisplayed()
    }
}
