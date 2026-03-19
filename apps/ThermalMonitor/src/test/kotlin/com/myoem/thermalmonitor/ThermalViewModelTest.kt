// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalmonitor

// ─────────────────────────────────────────────────────────────────────────────
// What this file tests
//
//   ThermalViewModel — the Compose ViewModel that:
//     - Polls ThermalControlManager every 2 seconds (auto-refresh loop)
//     - Exposes UiState as a StateFlow
//     - Handles turnFanOn/Off, setFanSpeed, setAutoMode actions
//
//   UiState fields:
//     cpuTempCelsius, fanRpm, fanSpeedPercent, isFanRunning,
//     isAutoMode, serviceAvailable, errorMessage
//
// Why unit tests (not instrumented)?
//   ThermalViewModel only depends on ThermalControlManager — not on any
//   Android framework.  By constructing ThermalControlManager(null) we get
//   a real manager that returns safe defaults, letting us test every state
//   transition without a device, an emulator, or a running service.
//
// Testing strategy:
//   - Use ThermalControlManager(null) for the "service unavailable" path
//   - For the "service available" path: Use Mockito to mock the binder calls
//     so we control what the manager returns (avoids IPC in unit tests)
//   - For coroutines: use kotlinx-coroutines-test (UnconfinedTestDispatcher)
//     so coroutines run synchronously inside the test
// ─────────────────────────────────────────────────────────────────────────────

import com.myoem.thermalcontrol.ThermalControlManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mockito.Mockito.mock
import org.mockito.Mockito.verify
import org.mockito.Mockito.`when` as whenever

/**
 * JVM unit tests for [ThermalViewModel].
 *
 * Dependencies:
 *   - kotlinx-coroutines-test   — controls coroutine time without sleeping
 *   - mockito-core              — mock ThermalControlManager for controlled responses
 *
 * These tests run on the host JVM (no emulator/device needed).
 */
@OptIn(ExperimentalCoroutinesApi::class)
class ThermalViewModelTest {

    // TestDispatcher controls coroutine time — we advance it manually
    // instead of waiting for real clock time (2 s between polls would be slow)
    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        // Replace the Main dispatcher so ViewModel coroutines use testDispatcher
        Dispatchers.setMain(testDispatcher)
    }

    @After
    fun tearDown() {
        // Restore the real Main dispatcher after each test
        Dispatchers.resetMain()
    }

    // =========================================================================
    // Section A — Initial state
    //
    // When the service is unavailable (null binder), the ViewModel must emit
    // an initial UiState with safe defaults and an error message.
    // =========================================================================

    /**
     * When ThermalControlManager is built with a null IBinder, isAvailable() is
     * false.  The ViewModel should reflect this immediately after creation.
     */
    @Test
    fun initialState_ServiceUnavailable_ServiceAvailableFalse() = runTest {
        val manager = ThermalControlManager(null)  // null binder → unavailable
        val viewModel = ThermalViewModel(manager)

        // Advance past the first fetchData() call so the state is populated
        testDispatcher.scheduler.advanceUntilIdle()

        assertFalse(
            "When service is unavailable, serviceAvailable must be false",
            viewModel.uiState.value.serviceAvailable
        )
    }

    /** CPU temperature must start at 0f when service is unavailable */
    @Test
    fun initialState_ServiceUnavailable_CpuTempIsZero() = runTest {
        val manager = ThermalControlManager(null)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            "cpuTempCelsius must be 0f when service is unavailable",
            0f, viewModel.uiState.value.cpuTempCelsius, 0f
        )
    }

    /** Fan RPM must be -1 (the "not available" sentinel) when service is unavailable */
    @Test
    fun initialState_ServiceUnavailable_FanRpmIsMinusOne() = runTest {
        val manager = ThermalControlManager(null)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            "fanRpm must be -1 when service is unavailable",
            -1, viewModel.uiState.value.fanRpm
        )
    }

    /** Fan speed percent must be 0 when service is unavailable */
    @Test
    fun initialState_ServiceUnavailable_FanSpeedPercentIsZero() = runTest {
        val manager = ThermalControlManager(null)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            "fanSpeedPercent must be 0 when service is unavailable",
            0, viewModel.uiState.value.fanSpeedPercent
        )
    }

    /**
     * isAutoMode must default to true.
     * The ViewModel reads isFanAutoMode() from the manager.
     * With null binder, manager.isFanAutoMode() returns true (safe default).
     */
    @Test
    fun initialState_ServiceUnavailable_IsAutoModeTrue() = runTest {
        val manager = ThermalControlManager(null)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertTrue(
            "isAutoMode must be true (safe default) when service is unavailable",
            viewModel.uiState.value.isAutoMode
        )
    }

    /** The ViewModel must set an error message when the service is not running */
    @Test
    fun initialState_ServiceUnavailable_ErrorMessageIsSet() = runTest {
        val manager = ThermalControlManager(null)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        val errorMessage = viewModel.uiState.value.errorMessage
        assertNotNull(
            "errorMessage must not be null when service is unavailable",
            errorMessage
        )
        assertTrue(
            "errorMessage must be non-empty",
            errorMessage!!.isNotEmpty()
        )
    }


    // =========================================================================
    // Section B — fetchData() with a mocked available manager
    //
    // We use Mockito to stub ThermalControlManager methods so we can control
    // exactly what the ViewModel "receives" from the service without IPC.
    // =========================================================================

    /** Create a Mockito mock of ThermalControlManager that returns live data */
    private fun mockAvailableManager(
        temp: Float = 42.5f,
        rpm: Int = 1200,
        percent: Int = 50,
        isRunning: Boolean = true,
        isAuto: Boolean = false
    ): ThermalControlManager {
        val mock = mock(ThermalControlManager::class.java)
        whenever(mock.isAvailable()).thenReturn(true)
        whenever(mock.getCpuTemperatureCelsius()).thenReturn(temp)
        whenever(mock.getFanSpeedRpm()).thenReturn(rpm)
        whenever(mock.getFanSpeedPercent()).thenReturn(percent)
        whenever(mock.isFanRunning()).thenReturn(isRunning)
        whenever(mock.isFanAutoMode()).thenReturn(isAuto)
        whenever(mock.setFanSpeed(org.mockito.ArgumentMatchers.anyInt())).thenReturn(true)
        return mock
    }

    @Test
    fun fetchData_WhenAvailable_UpdatesCpuTemp() = runTest {
        val manager = mockAvailableManager(temp = 42.5f)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            "cpuTempCelsius should match what the manager returned",
            42.5f, viewModel.uiState.value.cpuTempCelsius, 0.01f
        )
    }

    @Test
    fun fetchData_WhenAvailable_UpdatesFanRpm() = runTest {
        val manager = mockAvailableManager(rpm = 1200)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals("fanRpm should match manager's value",
            1200, viewModel.uiState.value.fanRpm)
    }

    @Test
    fun fetchData_WhenAvailable_UpdatesFanSpeedPercent() = runTest {
        val manager = mockAvailableManager(percent = 75)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals("fanSpeedPercent should match manager's value",
            75, viewModel.uiState.value.fanSpeedPercent)
    }

    @Test
    fun fetchData_WhenAvailable_SetsServiceAvailableTrue() = runTest {
        val manager = mockAvailableManager()
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertTrue("serviceAvailable must be true when manager is available",
            viewModel.uiState.value.serviceAvailable)
    }

    /** When service is available, errorMessage must be null (no error) */
    @Test
    fun fetchData_WhenAvailable_ClearsErrorMessage() = runTest {
        val manager = mockAvailableManager()
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertNull("errorMessage must be null when service is available",
            viewModel.uiState.value.errorMessage)
    }

    /** When service is unavailable, errorMessage must be populated */
    @Test
    fun fetchData_WhenUnavailable_SetsErrorMessage() = runTest {
        val manager = ThermalControlManager(null)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertNotNull("errorMessage must be set when service is unavailable",
            viewModel.uiState.value.errorMessage)
    }


    // =========================================================================
    // Section C — Fan control actions
    //
    // Each action calls a method on ThermalControlManager and then fetches
    // fresh data.  We use Mockito.verify() to confirm the correct manager
    // method was called with the correct argument.
    // =========================================================================

    /** turnFanOn() must call manager.setFanEnabled(true) */
    @Test
    fun turnFanOn_CallsManagerSetFanEnabled_True() = runTest {
        val manager = mockAvailableManager()
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()  // flush init fetch

        viewModel.turnFanOn()
        testDispatcher.scheduler.advanceUntilIdle()  // flush action coroutine

        verify(manager).setFanEnabled(true)
    }

    /** turnFanOff() must call manager.setFanEnabled(false) */
    @Test
    fun turnFanOff_CallsManagerSetFanEnabled_False() = runTest {
        val manager = mockAvailableManager()
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        viewModel.turnFanOff()
        testDispatcher.scheduler.advanceUntilIdle()

        verify(manager).setFanEnabled(false)
    }

    /** setFanSpeed(50) must call manager.setFanSpeed(50) */
    @Test
    fun setFanSpeed_50_CallsManagerWithPercent50() = runTest {
        val manager = mockAvailableManager()
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        viewModel.setFanSpeed(50)
        testDispatcher.scheduler.advanceUntilIdle()

        verify(manager).setFanSpeed(50)
    }

    /** setFanSpeed(0) must call manager.setFanSpeed(0) */
    @Test
    fun setFanSpeed_0_CallsManagerWithPercent0() = runTest {
        val manager = mockAvailableManager()
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        viewModel.setFanSpeed(0)
        testDispatcher.scheduler.advanceUntilIdle()

        verify(manager).setFanSpeed(0)
    }

    /** setAutoMode() must call manager.setFanAutoMode(true) */
    @Test
    fun setAutoMode_CallsManagerSetFanAutoMode_True() = runTest {
        val manager = mockAvailableManager()
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        viewModel.setAutoMode()
        testDispatcher.scheduler.advanceUntilIdle()

        verify(manager).setFanAutoMode(true)
    }

    /**
     * After an action, the ViewModel should call fetchData() and the new state
     * should reflect what the manager now returns.
     * Simulates: fan was off → turnFanOn() → state shows fan running.
     */
    @Test
    fun turnFanOn_ThenFetchData_StateUpdated() = runTest {
        // Start: fan is off
        val manager = mockAvailableManager(percent = 0, isRunning = false)
        val viewModel = ThermalViewModel(manager)
        testDispatcher.scheduler.advanceUntilIdle()

        assertFalse("Initial state: fan not running",
            viewModel.uiState.value.isFanRunning)

        // Simulate: after setFanEnabled(true), manager now reports fan as running
        whenever(manager.getFanSpeedPercent()).thenReturn(100)
        whenever(manager.isFanRunning()).thenReturn(true)

        viewModel.turnFanOn()
        testDispatcher.scheduler.advanceUntilIdle()

        assertTrue("After turnFanOn() + fetchData(), isFanRunning should be true",
            viewModel.uiState.value.isFanRunning)
    }


    // =========================================================================
    // Section D — Auto-refresh loop
    //
    // The ViewModel polls every 2000 ms.  Using advanceTimeBy() we can move
    // time forward without actually sleeping, verifying the polling behavior.
    // =========================================================================

    /**
     * After 2000 ms, the ViewModel should have fetched data a second time.
     * We verify this by changing what the mock returns after the first fetch
     * and checking that the state updated after the second poll.
     */
    @Test
    fun autoRefresh_After2000ms_StateUpdatesAgain() = runTest {
        val manager = mockAvailableManager(temp = 40.0f)
        val viewModel = ThermalViewModel(manager)

        // First poll
        testDispatcher.scheduler.advanceUntilIdle()
        assertEquals(40.0f, viewModel.uiState.value.cpuTempCelsius, 0.01f)

        // Change what the mock returns for the second poll
        whenever(manager.getCpuTemperatureCelsius()).thenReturn(55.0f)

        // Advance time by 2001 ms to trigger the second poll
        advanceTimeBy(2001L)
        testDispatcher.scheduler.advanceUntilIdle()

        assertEquals(
            "After 2 s, the ViewModel should have polled again and updated the temperature",
            55.0f, viewModel.uiState.value.cpuTempCelsius, 0.01f
        )
    }
}
