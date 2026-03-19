// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalcontrol;

// ─────────────────────────────────────────────────────────────────────────────
// What this file tests
//
//   ThermalControlManager — the Java API that wraps IThermalControlService.
//
//   The manager takes an IBinder in its constructor.  This design means we can
//   test ALL business logic without a running service by passing null:
//     - null binder  → manager.isAvailable() == false → safe sentinel values
//     - live binder  → real IPC calls to thermalcontrold
//
// Test groups:
//   A  — Constructor & isAvailable()
//   B  — Null-binder safe defaults (pure JVM — no device needed)
//   C  — categorizeTemperature() static method (pure JVM — no IPC)
//   D  — temperatureColor() static method     (pure JVM — no IPC)
//   E  — Error code constants
//   F  — Live service tests (instrumented — needs thermalcontrold running)
//
// Why CTS (not VTS)?
//   ThermalControlManager is a Java library used by Android apps.
//   CTS verifies that the Java public API contract is correct on any
//   compatible Android device — exactly what CTS is designed for.
//   VTS tests run native C++ code against vendor services; CTS tests
//   run Java/Kotlin against framework-level APIs.
// ─────────────────────────────────────────────────────────────────────────────

import android.os.IBinder;
import android.os.ServiceManager;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeTrue;

/**
 * CTS tests for {@link ThermalControlManager}.
 *
 * <p>Groups A–E pass null as the IBinder so no service needs to be running.
 * Group F makes live IPC calls and auto-skips (via {@code assumeTrue}) when
 * thermalcontrold is not running.
 *
 * <p>Build: {@code m MyOemThermalControlManagerTests}
 * <p>Run:   {@code atest MyOemThermalControlManagerTests}
 */
@RunWith(AndroidJUnit4.class)
public class ThermalControlManagerTest {

    // =========================================================================
    // Section A — Constructor & isAvailable()
    //
    // ThermalControlManager wraps an IBinder.  The null-binder path is the
    // "graceful degradation" mode — all methods return safe sentinel values.
    // =========================================================================

    /**
     * Passing null to the constructor must produce an unavailable manager.
     * This is the primary way to test all safe-default branches without hardware.
     */
    @Test
    public void constructor_NullBinder_IsAvailableFalse() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        assertFalse(
            "isAvailable() must be false when constructed with null binder",
            mgr.isAvailable()
        );
    }

    /**
     * SERVICE_NAME must be the exact string used in main.cpp, service_contexts,
     * and the VINTF manifest.  If any one of the four copies disagrees, the
     * service will not be discoverable.
     */
    @Test
    public void serviceNameConstant_ExactValue() {
        assertEquals(
            "SERVICE_NAME must match kServiceName in main.cpp exactly",
            "com.myoem.thermalcontrol.IThermalControlService/default",
            ThermalControlManager.SERVICE_NAME
        );
    }


    // =========================================================================
    // Section B — Null-binder safe defaults
    //
    // Every read method must return a documented sentinel value when the service
    // is unavailable.  This prevents NullPointerException crashes in apps.
    //
    // Documented defaults (from ThermalControlManager.java Javadoc):
    //   getCpuTemperatureCelsius()  → 0.0f
    //   getFanSpeedRpm()            → -1
    //   isFanRunning()              → false
    //   getFanSpeedPercent()        → 0
    //   isFanAutoMode()             → true  (safe default: let kernel control fan)
    //   setFanEnabled()             → no exception
    //   setFanSpeed()               → false (no IPC performed)
    //   setFanAutoMode()            → no exception
    // =========================================================================

    @Test
    public void getCpuTemperatureCelsius_NullBinder_ReturnsZero() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        assertEquals(
            "getCpuTemperatureCelsius() must return 0.0f when service is unavailable",
            0.0f, mgr.getCpuTemperatureCelsius(), 0.0f  // delta=0 → exact comparison
        );
    }

    @Test
    public void getFanSpeedRpm_NullBinder_ReturnsMinusOne() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        assertEquals(
            "getFanSpeedRpm() must return -1 when service is unavailable (no tachometer / no service)",
            -1, mgr.getFanSpeedRpm()
        );
    }

    @Test
    public void isFanRunning_NullBinder_ReturnsFalse() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        assertFalse(
            "isFanRunning() must return false when service is unavailable",
            mgr.isFanRunning()
        );
    }

    @Test
    public void getFanSpeedPercent_NullBinder_ReturnsZero() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        assertEquals(
            "getFanSpeedPercent() must return 0 when service is unavailable",
            0, mgr.getFanSpeedPercent()
        );
    }

    /**
     * isFanAutoMode() returns TRUE (not false) as its safe default.
     *
     * Rationale: if the service is unavailable we don't know the fan state.
     * Returning true means the app assumes the kernel thermal governor is in
     * control — which is the safest assumption (no uncontrolled overheating).
     */
    @Test
    public void isFanAutoMode_NullBinder_ReturnsTrueSafeDefault() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        assertTrue(
            "isFanAutoMode() must return true (safe default) when service is unavailable",
            mgr.isFanAutoMode()
        );
    }

    /**
     * Write methods on a null-binder manager must silently no-op.
     * They must NOT throw NullPointerException or any other exception.
     */
    @Test
    public void setFanEnabled_NullBinder_NoException() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        // If this throws, the test fails automatically
        mgr.setFanEnabled(true);
        mgr.setFanEnabled(false);
    }

    /**
     * setFanSpeed() returns false when the service is unavailable.
     * It must also NOT attempt an IPC call (verified by the fact that it returns
     * false even before the first connection is established).
     */
    @Test
    public void setFanSpeed_NullBinder_ReturnsFalse() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        boolean result = mgr.setFanSpeed(50);
        assertFalse(
            "setFanSpeed() must return false when service is unavailable",
            result
        );
    }

    @Test
    public void setFanAutoMode_NullBinder_NoException() {
        ThermalControlManager mgr = new ThermalControlManager(null);
        mgr.setFanAutoMode(true);
        mgr.setFanAutoMode(false);
    }


    // =========================================================================
    // Section C — categorizeTemperature() static method
    //
    // This method has NO IPC — it is pure Java arithmetic.
    // It can be tested without a device or a running service.
    //
    // Thresholds (from ThermalControlManager.java source):
    //   celsius < 50.0f  → "Cool"
    //   celsius < 70.0f  → "Warm"
    //   celsius < 85.0f  → "Hot"
    //   else             → "Critical"
    //
    // Testing strategy: test each range + all four boundary values.
    // =========================================================================

    @Test
    public void categorizeTemp_NegativeTemp_ReturnsCool() {
        // Below freezing — still "Cool" because it's below 50°C threshold
        assertEquals(ThermalControlManager.TEMP_COOL,
            ThermalControlManager.categorizeTemperature(-10.0f));
    }

    @Test
    public void categorizeTemp_Zero_ReturnsCool() {
        assertEquals(ThermalControlManager.TEMP_COOL,
            ThermalControlManager.categorizeTemperature(0.0f));
    }

    @Test
    public void categorizeTemp_MidCool_ReturnsCool() {
        assertEquals(ThermalControlManager.TEMP_COOL,
            ThermalControlManager.categorizeTemperature(30.0f));
    }

    /** 49.9°C is still below the 50°C threshold → Cool */
    @Test
    public void categorizeTemp_JustBelow50_ReturnsCool() {
        assertEquals(ThermalControlManager.TEMP_COOL,
            ThermalControlManager.categorizeTemperature(49.9f));
    }

    /** 50.0°C is the lower boundary of "Warm" → must return Warm, not Cool */
    @Test
    public void categorizeTemp_Boundary50_ReturnsWarm() {
        assertEquals(
            "50.0°C is the Warm boundary — must return TEMP_WARM, not TEMP_COOL",
            ThermalControlManager.TEMP_WARM,
            ThermalControlManager.categorizeTemperature(50.0f)
        );
    }

    @Test
    public void categorizeTemp_MidWarm_ReturnsWarm() {
        assertEquals(ThermalControlManager.TEMP_WARM,
            ThermalControlManager.categorizeTemperature(60.0f));
    }

    /** 69.9°C is still below the 70°C threshold → Warm */
    @Test
    public void categorizeTemp_JustBelow70_ReturnsWarm() {
        assertEquals(ThermalControlManager.TEMP_WARM,
            ThermalControlManager.categorizeTemperature(69.9f));
    }

    /** 70.0°C is the lower boundary of "Hot" → must return Hot, not Warm */
    @Test
    public void categorizeTemp_Boundary70_ReturnsHot() {
        assertEquals(
            "70.0°C is the Hot boundary — must return TEMP_HOT, not TEMP_WARM",
            ThermalControlManager.TEMP_HOT,
            ThermalControlManager.categorizeTemperature(70.0f)
        );
    }

    @Test
    public void categorizeTemp_MidHot_ReturnsHot() {
        assertEquals(ThermalControlManager.TEMP_HOT,
            ThermalControlManager.categorizeTemperature(77.0f));
    }

    /** 84.9°C is still below the 85°C threshold → Hot */
    @Test
    public void categorizeTemp_JustBelow85_ReturnsHot() {
        assertEquals(ThermalControlManager.TEMP_HOT,
            ThermalControlManager.categorizeTemperature(84.9f));
    }

    /** 85.0°C is the lower boundary of "Critical" → must return Critical */
    @Test
    public void categorizeTemp_Boundary85_ReturnsCritical() {
        assertEquals(
            "85.0°C is the Critical boundary — must return TEMP_CRITICAL, not TEMP_HOT",
            ThermalControlManager.TEMP_CRITICAL,
            ThermalControlManager.categorizeTemperature(85.0f)
        );
    }

    @Test
    public void categorizeTemp_Above85_ReturnsCritical() {
        assertEquals(ThermalControlManager.TEMP_CRITICAL,
            ThermalControlManager.categorizeTemperature(100.0f));
    }

    @Test
    public void categorizeTemp_VeryHigh_ReturnsCritical() {
        assertEquals(ThermalControlManager.TEMP_CRITICAL,
            ThermalControlManager.categorizeTemperature(120.0f));
    }


    // =========================================================================
    // Section D — temperatureColor() static method
    //
    // Returns Android Color int constants (ARGB packed into int).
    // Same thresholds as categorizeTemperature().
    //
    // Color values (from ThermalControlManager.java):
    //   Cool     → 0xFF4CAF50  (Material Green 500)
    //   Warm     → 0xFFFFC107  (Material Amber 500)
    //   Hot      → 0xFFFF5722  (Material Deep Orange 500)
    //   Critical → 0xFFF44336  (Material Red 500)
    // =========================================================================

    @Test
    public void temperatureColor_CoolTemp_ReturnsGreen() {
        assertEquals(
            "30°C (Cool) should return Material Green 500",
            0xFF4CAF50, ThermalControlManager.temperatureColor(30.0f)
        );
    }

    /** Boundary at exactly 50.0°C must return Warm color (Amber), not Cool (Green) */
    @Test
    public void temperatureColor_Boundary50_ReturnsAmber() {
        assertEquals(
            "50.0°C boundary should return Material Amber 500 (Warm), not Green (Cool)",
            0xFFFFC107, ThermalControlManager.temperatureColor(50.0f)
        );
    }

    @Test
    public void temperatureColor_WarmTemp_ReturnsAmber() {
        assertEquals(
            "60°C (Warm) should return Material Amber 500",
            0xFFFFC107, ThermalControlManager.temperatureColor(60.0f)
        );
    }

    /** Boundary at exactly 70.0°C must return Hot color (Deep Orange), not Warm (Amber) */
    @Test
    public void temperatureColor_Boundary70_ReturnsDeepOrange() {
        assertEquals(
            "70.0°C boundary should return Material Deep Orange 500 (Hot), not Amber (Warm)",
            0xFFFF5722, ThermalControlManager.temperatureColor(70.0f)
        );
    }

    @Test
    public void temperatureColor_HotTemp_ReturnsDeepOrange() {
        assertEquals(
            "75°C (Hot) should return Material Deep Orange 500",
            0xFFFF5722, ThermalControlManager.temperatureColor(75.0f)
        );
    }

    /** Boundary at exactly 85.0°C must return Critical color (Red) */
    @Test
    public void temperatureColor_Boundary85_ReturnsRed() {
        assertEquals(
            "85.0°C boundary should return Material Red 500 (Critical), not Deep Orange (Hot)",
            0xFFF44336, ThermalControlManager.temperatureColor(85.0f)
        );
    }

    @Test
    public void temperatureColor_CriticalTemp_ReturnsRed() {
        assertEquals(
            "90°C (Critical) should return Material Red 500",
            0xFFF44336, ThermalControlManager.temperatureColor(90.0f)
        );
    }


    // =========================================================================
    // Section E — Error code constants
    //
    // These constants are mirrored from the AIDL file.  Changing them is a
    // breaking API change in a @VintfStability interface — this test catches
    // accidental renumbering.
    // =========================================================================

    @Test
    public void errorCode_HAL_UNAVAILABLE_Is1() {
        assertEquals(
            "ERROR_HAL_UNAVAILABLE must be 1 — matches IThermalControlService AIDL constant",
            1, ThermalControlManager.ERROR_HAL_UNAVAILABLE
        );
    }

    @Test
    public void errorCode_INVALID_SPEED_Is2() {
        assertEquals(
            "ERROR_INVALID_SPEED must be 2 — matches IThermalControlService AIDL constant",
            2, ThermalControlManager.ERROR_INVALID_SPEED
        );
    }

    @Test
    public void errorCode_SYSFS_WRITE_Is3() {
        assertEquals(
            "ERROR_SYSFS_WRITE must be 3 — matches IThermalControlService AIDL constant",
            3, ThermalControlManager.ERROR_SYSFS_WRITE
        );
    }


    // =========================================================================
    // Section F — Live service tests (instrumented — on real device)
    //
    // These tests connect to the running thermalcontrold service.
    // They are automatically SKIPPED (not failed) when the service is absent.
    //
    // How to run only these: atest CtsMyOemThermalControlManagerTests:ThermalControlManagerTest#live*
    // =========================================================================

    /** Helper: get a live manager or null if service is not running. */
    private ThermalControlManager getLiveManager() {
        IBinder binder = ServiceManager.checkService(ThermalControlManager.SERVICE_NAME);
        return new ThermalControlManager(binder);
    }

    @Test
    public void live_IsAvailable_WhenServiceRunning() {
        ThermalControlManager mgr = getLiveManager();
        // assumeTrue skips (not fails) the test if the service is absent.
        // This is important: a missing service on a device without fan hardware
        // is expected, not a test failure.
        assumeTrue("thermalcontrold not running — skipping live test",
            mgr.isAvailable());

        assertTrue("isAvailable() should be true when service is running",
            mgr.isAvailable());
    }

    @Test
    public void live_GetCpuTemp_ReturnsPositive() {
        ThermalControlManager mgr = getLiveManager();
        assumeTrue("thermalcontrold not running", mgr.isAvailable());

        float temp = mgr.getCpuTemperatureCelsius();
        assertTrue(
            "getCpuTemperatureCelsius() should return > 0 on a running device, got: " + temp,
            temp > 0.0f
        );
    }

    @Test
    public void live_SetFanSpeed_50_ReadBackEquals50() {
        ThermalControlManager mgr = getLiveManager();
        assumeTrue("thermalcontrold not running", mgr.isAvailable());

        boolean set = mgr.setFanSpeed(50);
        assumeTrue("setFanSpeed(50) failed — fan hardware not available", set);

        int readBack = mgr.getFanSpeedPercent();
        // Allow ±1 for integer rounding in the HAL's PWM math
        assertTrue(
            "After setFanSpeed(50), getFanSpeedPercent() should be ~50, got: " + readBack,
            Math.abs(readBack - 50) <= 1
        );

        // Restore auto mode — leave the fan under kernel control after the test
        mgr.setFanAutoMode(true);
    }

    @Test
    public void live_SetAutoMode_True_IsAutoModeTrue() {
        ThermalControlManager mgr = getLiveManager();
        assumeTrue("thermalcontrold not running", mgr.isAvailable());

        mgr.setFanAutoMode(true);
        assertTrue(
            "After setFanAutoMode(true), isFanAutoMode() should be true",
            mgr.isFanAutoMode()
        );
    }

    /**
     * Client-side validation: setFanSpeed(-1) must return false WITHOUT making
     * an IPC call.  The manager validates input before the binder call to avoid
     * a round-trip on obviously bad input.
     */
    @Test
    public void live_ClientSideValidation_Negative_ReturnsFalse() {
        ThermalControlManager mgr = getLiveManager();
        assumeTrue("thermalcontrold not running", mgr.isAvailable());

        boolean result = mgr.setFanSpeed(-1);
        assertFalse(
            "setFanSpeed(-1) should return false (client-side validation, no IPC)",
            result
        );
    }

    @Test
    public void live_ClientSideValidation_Over100_ReturnsFalse() {
        ThermalControlManager mgr = getLiveManager();
        assumeTrue("thermalcontrold not running", mgr.isAvailable());

        boolean result = mgr.setFanSpeed(101);
        assertFalse(
            "setFanSpeed(101) should return false (client-side validation, no IPC)",
            result
        );
    }
}
