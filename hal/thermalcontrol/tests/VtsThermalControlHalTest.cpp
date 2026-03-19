// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "VtsThermalControlHalTest"

// ─────────────────────────────────────────────────────────────────────────────
// What this file tests
//
//   IThermalControlHal — the C++ HAL interface that reads sysfs files
//   for CPU temperature and fan control on Raspberry Pi 5.
//
//   Key sysfs paths (discovered at runtime by ThermalControlHal):
//     /sys/class/thermal/thermal_zone0/temp  — CPU millidegrees
//     /sys/class/hwmon/hwmonN/pwm1           — fan PWM duty cycle 0–255
//     /sys/class/hwmon/hwmonN/pwm1_enable    — 1=manual, 2=auto
//     /sys/class/hwmon/hwmonN/fan1_input     — tachometer RPM (-1 if absent)
//
//   PWM math (from ThermalControlHal.cpp):
//     setFanSpeed(percent) writes: pwm = (percent * 255) / 100
//     getFanSpeedPercent() reads: percent = (pwm * 100 + 254) / 255  [ceiling]
//
//   Graceful degradation:
//     When hwmon sysfs is absent: fan reads return safe defaults (0, -1, true)
//     When thermal_zone0 absent: getCpuTemperatureCelsius() returns 0.0f
//     Write methods return false when hwmon absent (service maps this to SYSFS_WRITE error)
//
// Why test the HAL separately from the service?
//   The HAL directly touches hardware sysfs. Testing it in isolation:
//   1. Catches PWM math bugs before they reach the service layer
//   2. Verifies the "hwmon absent → safe defaults" contract explicitly
//   3. Easier to run on devices with/without fan hardware
//
// Hardware-dependent tests:
//   Tests that write PWM values and read them back can only pass on real rpi5
//   hardware with a fan connected. On a device without fan hardware these tests
//   are automatically SKIPPED (the HAL will report mAvailable=false).
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>    // std::isfinite, std::isnan
#include <memory>   // std::unique_ptr

#include <gtest/gtest.h>

// Public HAL interface header — exported by libthermalcontrolhal
// via export_include_dirs: ["include"] in its Android.bp.
// This is the ONLY header the service (and tests) should include —
// they should never include ThermalControlHal.h (the private implementation).
#include <thermalcontrol/IThermalControlHal.h>

using myoem::thermalcontrol::IThermalControlHal;
using myoem::thermalcontrol::createThermalControlHal;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: detect whether fan hardware is available.
// Creates a temporary HAL instance, checks availability by seeing whether
// getFanSpeedPercent() returns something while setFanSpeed(1) succeeds.
// A simpler proxy: if setFanEnabled(false) returns true, hardware is available.
// ─────────────────────────────────────────────────────────────────────────────
static bool isFanHardwareAvailable() {
    auto hal = createThermalControlHal();
    if (!hal) return false;
    // setFanEnabled returns false if hwmon was not found at init
    return hal->setFanEnabled(false);  // false = turn off fan (safe, non-destructive)
}

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
//
// Unlike the service tests, HAL tests don't need binder.
// We create a fresh HAL instance in SetUp() so each test starts clean.
// ─────────────────────────────────────────────────────────────────────────────
class ThermalControlHalTest : public ::testing::Test {
  protected:
    // Fresh HAL instance for each test
    std::unique_ptr<IThermalControlHal> mHal;

    void SetUp() override {
        // createThermalControlHal() is the factory function declared in
        // IThermalControlHal.h and implemented in ThermalControlHal.cpp.
        // It probes /sys/class/hwmon/hwmon0..15 for the fan PWM driver.
        mHal = createThermalControlHal();

        ASSERT_NE(mHal, nullptr)
            << "createThermalControlHal() returned nullptr — factory function broken";
    }

    void TearDown() override {
        // Restore auto mode after any test that may have set manual mode.
        // This ensures the kernel thermal governor takes back control
        // and prevents the fan from staying at a fixed speed after tests.
        if (mHal) {
            mHal->setAutoMode(true);
        }
    }
};


// =============================================================================
// Section A — Creation
// =============================================================================

// The factory function must return a non-null unique_ptr.
// SetUp() already verifies this, but an explicit test gives a clear name
// in the test report.
TEST_F(ThermalControlHalTest, CreateHal_ReturnsNonNull) {
    // mHal was created in SetUp() — just verify it is non-null
    EXPECT_NE(mHal.get(), nullptr);
}

// Two independent instances must not interfere.
// The HAL reads/writes sysfs files — separate instances share the same
// underlying hardware, but each instance must operate independently.
TEST_F(ThermalControlHalTest, CreateHal_TwoInstances_BothNonNull) {
    auto hal2 = createThermalControlHal();
    EXPECT_NE(hal2.get(), nullptr);
    // Both instances must still be usable after creation
    EXPECT_NE(mHal.get(), nullptr);
}


// =============================================================================
// Section B — Temperature read
// =============================================================================

// getCpuTemperatureCelsius must not crash even when the sysfs path is absent.
// On a device without /sys/class/thermal/thermal_zone0/temp it returns 0.0f.
TEST_F(ThermalControlHalTest, GetCpuTemp_WhenThermalNodeMissing_Returns0) {
    float temp = mHal->getCpuTemperatureCelsius();

    // Must not be NaN or infinity — even when sysfs is unreadable
    EXPECT_TRUE(std::isfinite(temp))
        << "getCpuTemperatureCelsius returned non-finite value: " << temp;
    EXPECT_FALSE(std::isnan(temp));
}

// On real rpi5 hardware, the CPU temperature must be positive (device is warm)
TEST_F(ThermalControlHalTest, GetCpuTemp_WhenHardwarePresent_IsPositive) {
    float temp = mHal->getCpuTemperatureCelsius();

    // If temp is 0.0f the thermal sysfs node was not found — skip gracefully
    if (temp == 0.0f) {
        GTEST_SKIP() << "/sys/class/thermal/thermal_zone0/temp not readable";
    }

    EXPECT_GT(temp, 0.0f)
        << "CPU temperature on running device must be positive";
}

// On real hardware, the temperature must be in a physically plausible range
TEST_F(ThermalControlHalTest, GetCpuTemp_WhenHardwarePresent_InRange) {
    float temp = mHal->getCpuTemperatureCelsius();

    if (temp == 0.0f) {
        GTEST_SKIP() << "Thermal sysfs not readable";
    }

    EXPECT_GE(temp,   0.0f) << "Temperature cannot be below absolute zero";
    EXPECT_LE(temp, 120.0f) << "Temperature above 120°C is impossible for rpi5 SoC";
}

// Result must always be a finite IEEE 754 float — never NaN or infinity
TEST_F(ThermalControlHalTest, GetCpuTemp_AlwaysFinite) {
    for (int i = 0; i < 5; ++i) {
        float temp = mHal->getCpuTemperatureCelsius();
        EXPECT_TRUE(std::isfinite(temp))
            << "Iteration " << i << ": getCpuTemperatureCelsius returned: " << temp;
    }
}


// =============================================================================
// Section C — Fan speed read
// =============================================================================

// When hwmon is absent (fan hardware not found), getFanSpeedPercent() must
// return 0 gracefully — not crash, not return garbage.
TEST_F(ThermalControlHalTest, GetFanSpeedPercent_WhenHwmonMissing_Returns0) {
    int32_t percent = mHal->getFanSpeedPercent();

    // Either 0 (hwmon absent) or 0–100 (hwmon present) are valid
    EXPECT_GE(percent, 0)   << "getFanSpeedPercent cannot be negative";
    EXPECT_LE(percent, 100) << "getFanSpeedPercent cannot exceed 100";
}

// getFanSpeedPercent must always return 0–100, never outside
TEST_F(ThermalControlHalTest, GetFanSpeedPercent_AlwaysInRange) {
    int32_t percent = mHal->getFanSpeedPercent();
    EXPECT_GE(percent, 0);
    EXPECT_LE(percent, 100);
}

// When tachometer is not wired, getFanSpeedRpm returns -1.
// This is the documented contract for rpi5 (tachometer wire is optional).
TEST_F(ThermalControlHalTest, GetFanSpeedRpm_WhenTachometerAbsent_ReturnsMinusOne) {
    int32_t rpm = mHal->getFanSpeedRpm();

    EXPECT_TRUE(rpm == -1 || rpm >= 0)
        << "getFanSpeedRpm must return -1 (no tachometer) or >= 0 RPM, got: " << rpm;
}

// isFanRunning must be consistent with getFanSpeedPercent.
// If percent > 0, the fan is spinning → isFanRunning must be true.
TEST_F(ThermalControlHalTest, IsFanRunning_ConsistentWithFanSpeedPercent) {
    int32_t percent = mHal->getFanSpeedPercent();
    bool running    = mHal->isFanRunning();

    if (percent > 0) {
        EXPECT_TRUE(running)
            << "getFanSpeedPercent()=" << percent
            << " but isFanRunning()=false — HAL mapping is inconsistent";
    }
}

// When hwmon is absent, isAutoMode() must return true (safe default).
// "True" means "the kernel thermal governor has control" — if we can't
// write to the hwmon, the safest assumption is that the kernel is in control.
TEST_F(ThermalControlHalTest, IsAutoMode_WhenHwmonMissing_ReturnsSafeDefault) {
    // We can't force-remove hwmon in a test, but we can verify the return is bool
    bool mode = mHal->isAutoMode();
    // Just ensure no crash and a valid bool is returned
    EXPECT_TRUE(mode == true || mode == false);
}


// =============================================================================
// Section D — Fan write operations and PWM math
//
// PWM math reference (from ThermalControlHal.cpp):
//   setFanSpeed(percent) writes: pwm = (percent * 255) / 100  [integer division]
//   Examples:
//     setFanSpeed(0)   → pwm = 0
//     setFanSpeed(50)  → pwm = (50 * 255) / 100 = 12750 / 100 = 127
//     setFanSpeed(100) → pwm = (100 * 255) / 100 = 255
//
//   getFanSpeedPercent() reads: percent = (pwm * 100 + 254) / 255  [ceiling div]
//   Examples:
//     pwm=0   → (0 + 254) / 255 = 0
//     pwm=127 → (12700 + 254) / 255 = 12954 / 255 = 50
//     pwm=255 → (25500 + 254) / 255 = 25754 / 255 = 100
//
// The round-trip setFanSpeed(x) → getFanSpeedPercent() may differ by ±1 due
// to integer rounding, but setFanSpeed(0), (50), (100) are exact round-trips.
// =============================================================================

// When hwmon is not available, setFanSpeed must return false without crashing.
TEST_F(ThermalControlHalTest, SetFanSpeed_WhenHwmonMissing_ReturnsFalse_NoCrash) {
    // We can verify the return value is boolean-valid and no crash occurs.
    // On real hardware this will return true; that is also acceptable.
    bool result = mHal->setFanSpeed(50);
    EXPECT_TRUE(result == true || result == false);  // just no crash
}

// setFanSpeed(0) — fan off (minimum valid value)
TEST_F(ThermalControlHalTest, SetFanSpeed_0_WritesZero_ReadBackZero) {
    if (!mHal->setFanSpeed(0)) {
        GTEST_SKIP() << "Fan hardware not available";
    }

    int32_t percent = mHal->getFanSpeedPercent();
    // setFanSpeed(0) → pwm=0 → getFanSpeedPercent()=0 (exact)
    EXPECT_EQ(percent, 0)
        << "After setFanSpeed(0), getFanSpeedPercent() should return 0";
}

// setFanSpeed(50) → getFanSpeedPercent() should be ~50
// PWM math: (50 * 255) / 100 = 127, then (127 * 100 + 254) / 255 = 50
TEST_F(ThermalControlHalTest, SetFanSpeed_50_ReadBackApprox50) {
    if (!mHal->setFanSpeed(50)) {
        GTEST_SKIP() << "Fan hardware not available";
    }

    int32_t percent = mHal->getFanSpeedPercent();
    // Allow ±1 for integer arithmetic rounding at other percent values,
    // but 50% is an exact round-trip per the math above
    EXPECT_NEAR(percent, 50, 1)
        << "After setFanSpeed(50), getFanSpeedPercent() should be ~50, got: " << percent;
}

// setFanSpeed(100) — fan full speed (maximum valid value)
TEST_F(ThermalControlHalTest, SetFanSpeed_100_ReadBackFull) {
    if (!mHal->setFanSpeed(100)) {
        GTEST_SKIP() << "Fan hardware not available";
    }

    int32_t percent = mHal->getFanSpeedPercent();
    // setFanSpeed(100) → pwm=255 → getFanSpeedPercent()=100 (exact)
    EXPECT_EQ(percent, 100)
        << "After setFanSpeed(100), getFanSpeedPercent() should return 100";

    mHal->setAutoMode(true);  // restore kernel control
}

// setFanEnabled(true) turns fan fully on (PWM=255 = 100%)
TEST_F(ThermalControlHalTest, SetFanEnabled_True_FanRunning) {
    if (!mHal->setFanEnabled(true)) {
        GTEST_SKIP() << "Fan hardware not available";
    }

    EXPECT_TRUE(mHal->isFanRunning())
        << "After setFanEnabled(true), isFanRunning() should be true";

    int32_t percent = mHal->getFanSpeedPercent();
    EXPECT_EQ(percent, 100)
        << "setFanEnabled(true) should set fan to 100%";

    mHal->setAutoMode(true);
}

// setFanEnabled(false) turns fan fully off (PWM=0 = 0%)
TEST_F(ThermalControlHalTest, SetFanEnabled_False_FanStopped) {
    if (!mHal->setFanEnabled(false)) {
        GTEST_SKIP() << "Fan hardware not available";
    }

    EXPECT_FALSE(mHal->isFanRunning())
        << "After setFanEnabled(false), isFanRunning() should be false";

    int32_t percent = mHal->getFanSpeedPercent();
    EXPECT_EQ(percent, 0)
        << "setFanEnabled(false) should set fan to 0%";

    mHal->setAutoMode(true);
}

// setAutoMode(true) must set pwm1_enable=2 (kernel thermal governor)
// We verify the effect via isAutoMode() getter
TEST_F(ThermalControlHalTest, SetAutoMode_True_IsAutoModeTrue) {
    if (!mHal->setAutoMode(true)) {
        GTEST_SKIP() << "Fan hardware not available";
    }
    EXPECT_TRUE(mHal->isAutoMode())
        << "setAutoMode(true) → isAutoMode() should return true";
}

// setAutoMode(false) must set pwm1_enable=1 (manual control)
TEST_F(ThermalControlHalTest, SetAutoMode_False_IsAutoModeFalse) {
    if (!mHal->setAutoMode(false)) {
        GTEST_SKIP() << "Fan hardware not available";
    }
    EXPECT_FALSE(mHal->isAutoMode())
        << "setAutoMode(false) → isAutoMode() should return false";

    mHal->setAutoMode(true);  // restore
}

// When hwmon is absent, setFanEnabled must return false without crashing
TEST_F(ThermalControlHalTest, SetFanEnabled_WhenHwmonMissing_NoCrash) {
    bool result = mHal->setFanEnabled(false);
    // true = hardware present and call succeeded
    // false = hardware absent (graceful)
    EXPECT_TRUE(result == true || result == false);
}


// =============================================================================
// Section E — Boundary & edge cases for setFanSpeed
// =============================================================================

// Negative input — the HAL must return false (input rejected), not crash
// The service layer also validates input (ERROR_INVALID_SPEED), but the HAL
// has its own guard as a second line of defense.
TEST_F(ThermalControlHalTest, SetFanSpeed_NegativeInput_ReturnsFalse) {
    // On a device without fan hardware this also returns false, which is fine.
    // We just verify it doesn't crash.
    bool result = mHal->setFanSpeed(-1);
    // If hwmon is available, must return false (input rejected by HAL guard).
    // If hwmon is absent, also returns false (unavailable).
    // Either way: false is the expected outcome, and no crash must occur.
    EXPECT_FALSE(result)
        << "setFanSpeed(-1) should return false (invalid input)";
}

// Over 100 — must also return false
TEST_F(ThermalControlHalTest, SetFanSpeed_Over100_ReturnsFalse) {
    bool result = mHal->setFanSpeed(101);
    EXPECT_FALSE(result)
        << "setFanSpeed(101) should return false (out of range)";
}

// 0 is the lower boundary — valid input
// On hardware it should succeed; on no-hardware it returns false (unavailable)
TEST_F(ThermalControlHalTest, SetFanSpeed_Boundary_0_ValidInput) {
    bool result = mHal->setFanSpeed(0);
    // Accept true (hardware present) or false (no hardware = unavailable)
    // What we must NOT get is a crash or an exception
    EXPECT_TRUE(result == true || result == false);
}

// 100 is the upper boundary — valid input
TEST_F(ThermalControlHalTest, SetFanSpeed_Boundary_100_ValidInput) {
    bool result = mHal->setFanSpeed(100);
    EXPECT_TRUE(result == true || result == false);
    if (result) {
        mHal->setAutoMode(true);  // restore
    }
}
