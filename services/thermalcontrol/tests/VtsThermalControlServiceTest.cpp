// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "VtsThermalControlServiceTest"

// ─────────────────────────────────────────────────────────────────────────────
// What this file tests
//
//   IThermalControlService — a @VintfStability vendor binder service that
//   reads CPU temperature and controls the fan on Raspberry Pi 5.
//
//   Read methods  : getCpuTemperatureCelsius, getFanSpeedRpm, isFanRunning,
//                   getFanSpeedPercent, isFanAutoMode
//   Write methods : setFanEnabled, setFanSpeed, setFanAutoMode
//
//   Error codes:
//     ERROR_HAL_UNAVAILABLE = 1  — sysfs path not found at HAL init
//     ERROR_INVALID_SPEED   = 2  — speedPercent not in 0–100
//     ERROR_SYSFS_WRITE     = 3  — write to sysfs failed
//
// Why VTS (MANDATORY)?
//   @VintfStability is part of Android Treble's VINTF contract.
//   Any interface annotated @VintfStability MUST have VTS tests.
//   The test harness checks the VINTF manifest and verifies the service
//   matches what was declared.  Failing these tests → not Treble-compliant.
//
// Test structure:
//   A  — VINTF compliance (manifest declaration, service availability)
//   B  — Temperature reads
//   C  — Fan speed reads
//   D  — Fan write operations + getter verification
//   E  — Invalid input error codes
//   F  — Concurrency & stability
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>    // std::isfinite, std::isnan
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#include <gtest/gtest.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>

// AIDL-generated NDK proxy — V1 suffix because this is a vintf-stable interface.
// The "V1" version was frozen by running:  m thermalcontrolservice-aidl-freeze
#include <aidl/com/myoem/thermalcontrol/IThermalControlService.h>

using aidl::com::myoem::thermalcontrol::IThermalControlService;

// ─────────────────────────────────────────────────────────────────────────────
// Service name constant
//
// Format for @VintfStability interfaces: "<package>/<instance>"
// The "/default" instance name is the AOSP convention for a single instance.
// This string must match:
//   1. kServiceName in services/thermalcontrol/src/main.cpp
//   2. The instance in services/thermalcontrol/vintf/thermalcontrol.xml
//   3. The label in services/thermalcontrol/sepolicy/private/service_contexts
// ─────────────────────────────────────────────────────────────────────────────
static constexpr const char* kServiceName =
    "com.myoem.thermalcontrol.IThermalControlService/default";

// Reasonable temperature range for a Raspberry Pi 5 SoC
static constexpr float kMinReasonableTemp =   0.0f;  // °C
static constexpr float kMaxReasonableTemp = 120.0f;  // °C (throttle limit is ~85°C)

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────
class ThermalControlServiceTest : public ::testing::Test {
  public:
    static std::shared_ptr<IThermalControlService> sService;

    static void SetUpTestSuite() {
        ABinderProcess_setThreadPoolMaxThreadCount(0);
        ABinderProcess_startThreadPool();

        // waitForService is especially important here: thermalcontrold starts
        // after the HAL discovers the hwmon sysfs path, which can take a moment.
        ndk::SpAIBinder binder(AServiceManager_waitForService(kServiceName));
        if (binder.get() != nullptr) {
            sService = IThermalControlService::fromBinder(binder);
        }
    }

  protected:
    void SetUp() override {
        if (sService == nullptr) {
            GTEST_SKIP() << "thermalcontrold not running — is it installed and started?";
        }
    }
};

std::shared_ptr<IThermalControlService> ThermalControlServiceTest::sService = nullptr;

#define ASSERT_BINDER_OK(status)                                               \
    ASSERT_TRUE((status).isOk())                                               \
        << "Binder call failed: " << (status).getDescription()

#define ASSERT_SERVICE_ERROR(status, expectedCode)                             \
    do {                                                                       \
        ASSERT_FALSE((status).isOk())                                          \
            << "Expected error code " << (expectedCode)                        \
            << " but call returned ok()";                                      \
        ASSERT_EQ((status).getExceptionCode(), EX_SERVICE_SPECIFIC)            \
            << "Expected EX_SERVICE_SPECIFIC, got: "                           \
            << (status).getExceptionCode();                                    \
        EXPECT_EQ((status).getServiceSpecificError(), (expectedCode))          \
            << "Wrong service-specific error code";                            \
    } while (0)


// =============================================================================
// Section A — VINTF compliance
//
// These tests verify the Treble VINTF contract before testing service behavior.
// Failing any of these means the device is not Treble-compliant for this HAL.
// =============================================================================

// AServiceManager_isDeclared checks whether the service is declared in the
// VINTF manifest (vendor/etc/vintf/manifest.xml or a vintf_fragment).
// This is DIFFERENT from checkService (which checks if it's currently running).
// A service can be declared but not yet started — declaration is static,
// registration is dynamic.
TEST_F(ThermalControlServiceTest, VintfManifest_ServiceDeclared) {
    bool declared = AServiceManager_isDeclared(kServiceName);
    ASSERT_TRUE(declared)
        << "Service not declared in VINTF manifest: " << kServiceName
        << "\n  Check: services/thermalcontrol/vintf/thermalcontrol.xml"
        << "\n  Check: vintf_fragment module in Android.bp is built and installed";
}

// The service must be registered in the ServiceManager (actually running).
// By the time SetUpTestSuite succeeded, this must be true.
TEST_F(ThermalControlServiceTest, ServiceAvailable_Running) {
    ndk::SpAIBinder binder(AServiceManager_checkService(kServiceName));
    ASSERT_NE(binder.get(), nullptr)
        << "Service declared but not running: " << kServiceName;
}

// The service proxy must be a valid IThermalControlService, not some other interface
TEST_F(ThermalControlServiceTest, ServiceProxy_ValidInterface) {
    ASSERT_NE(sService, nullptr)
        << "fromBinder() returned null — binder is for a different interface";
}


// =============================================================================
// Section B — Temperature reads
// =============================================================================

// getCpuTemperatureCelsius must not crash.
// It can either:
//   - Return ok() with a valid temperature (hwmon/thermal_zone0 present)
//   - Return ERROR_HAL_UNAVAILABLE (sysfs not found at init)
// It must NEVER return EX_TRANSACTION_FAILED (crash) or leave result undefined.
TEST_F(ThermalControlServiceTest, GetCpuTemp_DoesNotCrash) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getCpuTemperatureCelsius(&result);

    // Accept either ok() or ERROR_HAL_UNAVAILABLE — both are valid responses.
    // Any other exception code means something went wrong with binder itself.
    bool acceptable = status.isOk() ||
        (status.getExceptionCode() == EX_SERVICE_SPECIFIC &&
         status.getServiceSpecificError() == IThermalControlService::ERROR_HAL_UNAVAILABLE);

    EXPECT_TRUE(acceptable)
        << "getCpuTemperatureCelsius returned unexpected status: "
        << status.getDescription();
}

// When the call succeeds, the result must be a valid IEEE 754 finite float.
// NaN or infinity would indicate a sysfs parsing bug.
TEST_F(ThermalControlServiceTest, GetCpuTemp_WhenOk_IsFiniteFloat) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getCpuTemperatureCelsius(&result);

    if (!status.isOk()) {
        GTEST_SKIP() << "HAL unavailable — skipping precision check";
    }

    EXPECT_TRUE(std::isfinite(result))
        << "Temperature must be finite, got: " << result;
    EXPECT_FALSE(std::isnan(result))
        << "Temperature must not be NaN";
}

// When the call succeeds, the temperature must be in a physically reasonable range.
// On rpi5 at room temperature: typically 40–60°C.
// Acceptable range: 0–120°C (above 120 is impossible for this SoC to survive).
TEST_F(ThermalControlServiceTest, GetCpuTemp_WhenOk_ReasonableRange) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getCpuTemperatureCelsius(&result);

    if (!status.isOk()) {
        GTEST_SKIP() << "HAL unavailable — skipping range check";
    }

    EXPECT_GE(result, kMinReasonableTemp)
        << "Temperature below 0°C is physically impossible for this SoC";
    EXPECT_LE(result, kMaxReasonableTemp)
        << "Temperature above 120°C is impossible — likely a sysfs parsing bug";
}

// If the HAL is unavailable, the error code must be exactly ERROR_HAL_UNAVAILABLE=1
TEST_F(ThermalControlServiceTest, GetCpuTemp_WhenHalUnavailable_ErrorCode_Is1) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getCpuTemperatureCelsius(&result);

    if (status.isOk()) {
        GTEST_SKIP() << "HAL is available — skipping unavailable error code check";
    }

    ASSERT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
    EXPECT_EQ(status.getServiceSpecificError(),
              IThermalControlService::ERROR_HAL_UNAVAILABLE)
        << "When HAL is unavailable, must return ERROR_HAL_UNAVAILABLE=1";
}


// =============================================================================
// Section C — Fan speed reads
// =============================================================================

// getFanSpeedRpm must return -1 (no tachometer) or >= 0 (RPM reading)
TEST_F(ThermalControlServiceTest, GetFanSpeedRpm_IsMinusOneOrPositive) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->getFanSpeedRpm(&result));

    EXPECT_TRUE(result == -1 || result >= 0)
        << "getFanSpeedRpm must return -1 (no tachometer) or >= 0, got: " << result;
}

// getFanSpeedPercent must return 0–100 (the range of a percentage)
TEST_F(ThermalControlServiceTest, GetFanSpeedPercent_InRange_0To100) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->getFanSpeedPercent(&result));

    EXPECT_GE(result, 0)   << "Fan speed percent must be >= 0";
    EXPECT_LE(result, 100) << "Fan speed percent must be <= 100";
}

// isFanRunning must return without exception (boolean result is always valid)
TEST_F(ThermalControlServiceTest, IsFanRunning_ReturnsBool) {
    bool result = false;
    ASSERT_BINDER_OK(sService->isFanRunning(&result));
    // No range check needed — bool is always valid
}

// isFanAutoMode must return without exception
TEST_F(ThermalControlServiceTest, IsFanAutoMode_ReturnsBool) {
    bool result = false;
    ASSERT_BINDER_OK(sService->isFanAutoMode(&result));
}

// Consistency: if fan speed percent > 0, the fan must be reported as running.
// A fan that is spinning but reported as "not running" is a bug in the HAL
// mapping between PWM value and isFanRunning.
TEST_F(ThermalControlServiceTest, IsFanRunning_ConsistentWithFanSpeedPercent) {
    int32_t percent = 0;
    bool running    = false;

    ASSERT_BINDER_OK(sService->getFanSpeedPercent(&percent));
    ASSERT_BINDER_OK(sService->isFanRunning(&running));

    if (percent > 0) {
        EXPECT_TRUE(running)
            << "getFanSpeedPercent()=" << percent
            << " but isFanRunning()=false — these must be consistent";
    }
    // Note: if percent == 0 and running == false, that is consistent (fan off).
    // If percent == 0 and running == true, that is a bug in the HAL, but
    // hardware timing may cause a transient mismatch, so we only check one direction.
}


// =============================================================================
// Section D — Fan write operations
//
// Note: these tests call write methods and then verify with getters.
// On real hardware the sysfs write happens immediately.
// If the HAL is unavailable (no fan hardware), write methods return
// ERROR_SYSFS_WRITE — we skip the write-then-read tests in that case.
// =============================================================================

// setFanAutoMode(true) must succeed (or fail with ERROR_SYSFS_WRITE on no hardware)
TEST_F(ThermalControlServiceTest, SetFanAutoMode_True_Succeeds) {
    ndk::ScopedAStatus status = sService->setFanAutoMode(true);

    bool acceptable = status.isOk() ||
        (status.getExceptionCode() == EX_SERVICE_SPECIFIC &&
         status.getServiceSpecificError() == IThermalControlService::ERROR_SYSFS_WRITE);

    EXPECT_TRUE(acceptable)
        << "setFanAutoMode(true) returned unexpected: " << status.getDescription();
}

// setFanAutoMode(false) must succeed (or ERROR_SYSFS_WRITE on no hardware)
TEST_F(ThermalControlServiceTest, SetFanAutoMode_False_Succeeds) {
    ndk::ScopedAStatus status = sService->setFanAutoMode(false);

    bool acceptable = status.isOk() ||
        (status.getExceptionCode() == EX_SERVICE_SPECIFIC &&
         status.getServiceSpecificError() == IThermalControlService::ERROR_SYSFS_WRITE);

    EXPECT_TRUE(acceptable)
        << "setFanAutoMode(false) returned unexpected: " << status.getDescription();
}

// After setFanAutoMode(true), isFanAutoMode() must return true.
// This is the full read-back-after-write test.
TEST_F(ThermalControlServiceTest, SetFanAutoMode_True_ReflectedInGetter) {
    ndk::ScopedAStatus writeStatus = sService->setFanAutoMode(true);
    if (!writeStatus.isOk()) {
        GTEST_SKIP() << "Hardware not available — skipping read-back test";
    }

    bool autoMode = false;
    ASSERT_BINDER_OK(sService->isFanAutoMode(&autoMode));
    EXPECT_TRUE(autoMode)
        << "setFanAutoMode(true) succeeded but isFanAutoMode() returned false";
}

// After setFanAutoMode(false), isFanAutoMode() must return false
TEST_F(ThermalControlServiceTest, SetFanAutoMode_False_ReflectedInGetter) {
    ndk::ScopedAStatus writeStatus = sService->setFanAutoMode(false);
    if (!writeStatus.isOk()) {
        GTEST_SKIP() << "Hardware not available — skipping read-back test";
    }

    bool autoMode = true;  // start true so we can verify it changes
    ASSERT_BINDER_OK(sService->isFanAutoMode(&autoMode));
    EXPECT_FALSE(autoMode)
        << "setFanAutoMode(false) succeeded but isFanAutoMode() returned true";

    // Restore auto mode so we leave the fan under kernel control
    sService->setFanAutoMode(true);
}

// setFanEnabled(true) must set fan to 100% AND exit auto mode.
// After the call, isFanAutoMode() must be false.
TEST_F(ThermalControlServiceTest, SetFanEnabled_True_ExitsAutoMode) {
    // First put the fan in auto mode
    ndk::ScopedAStatus autoStatus = sService->setFanAutoMode(true);
    if (!autoStatus.isOk()) {
        GTEST_SKIP() << "Hardware not available";
    }

    // Now call setFanEnabled — this must exit auto mode
    ndk::ScopedAStatus enableStatus = sService->setFanEnabled(true);
    if (!enableStatus.isOk()) {
        GTEST_SKIP() << "setFanEnabled failed — hardware write issue";
    }

    bool autoMode = true;
    ASSERT_BINDER_OK(sService->isFanAutoMode(&autoMode));
    EXPECT_FALSE(autoMode)
        << "setFanEnabled(true) must exit auto mode — kernel would override manual PWM otherwise";

    // Restore: leave in auto mode
    sService->setFanAutoMode(true);
}

// setFanSpeed(0) must succeed for valid input
TEST_F(ThermalControlServiceTest, SetFanSpeed_0_Succeeds) {
    ndk::ScopedAStatus status = sService->setFanSpeed(0);
    bool acceptable = status.isOk() ||
        (status.getExceptionCode() == EX_SERVICE_SPECIFIC &&
         status.getServiceSpecificError() == IThermalControlService::ERROR_SYSFS_WRITE);
    EXPECT_TRUE(acceptable)
        << "setFanSpeed(0) returned: " << status.getDescription();
}

// setFanSpeed(50) must succeed and set fan speed to 50%
TEST_F(ThermalControlServiceTest, SetFanSpeed_50_Succeeds) {
    ndk::ScopedAStatus status = sService->setFanSpeed(50);
    bool acceptable = status.isOk() ||
        (status.getExceptionCode() == EX_SERVICE_SPECIFIC &&
         status.getServiceSpecificError() == IThermalControlService::ERROR_SYSFS_WRITE);
    EXPECT_TRUE(acceptable)
        << "setFanSpeed(50) returned: " << status.getDescription();
}

// setFanSpeed(100) must succeed for the maximum valid value
TEST_F(ThermalControlServiceTest, SetFanSpeed_100_Succeeds) {
    ndk::ScopedAStatus status = sService->setFanSpeed(100);
    bool acceptable = status.isOk() ||
        (status.getExceptionCode() == EX_SERVICE_SPECIFIC &&
         status.getServiceSpecificError() == IThermalControlService::ERROR_SYSFS_WRITE);
    EXPECT_TRUE(acceptable)
        << "setFanSpeed(100) returned: " << status.getDescription();

    // Restore kernel auto mode
    sService->setFanAutoMode(true);
}

// setFanSpeed(50) → getFanSpeedPercent() == 50
// This is the most important write/read consistency test.
TEST_F(ThermalControlServiceTest, SetFanSpeed_50_ReflectedInGetter) {
    ndk::ScopedAStatus writeStatus = sService->setFanSpeed(50);
    if (!writeStatus.isOk()) {
        GTEST_SKIP() << "Hardware not available — skipping read-back";
    }

    int32_t percent = -1;
    ASSERT_BINDER_OK(sService->getFanSpeedPercent(&percent));
    // The HAL maps percent → PWM via integer arithmetic, then reads back.
    // Allow ±1 for rounding in the HAL's ceiling-division formula.
    EXPECT_NEAR(percent, 50, 1)
        << "setFanSpeed(50) should result in getFanSpeedPercent() ≈ 50";

    sService->setFanAutoMode(true);
}

// After setFanSpeed(), isFanAutoMode() must be false (manual mode)
TEST_F(ThermalControlServiceTest, SetFanSpeed_ExitsAutoMode) {
    ndk::ScopedAStatus autoStatus = sService->setFanAutoMode(true);
    if (!autoStatus.isOk()) {
        GTEST_SKIP() << "Hardware not available";
    }

    ndk::ScopedAStatus speedStatus = sService->setFanSpeed(50);
    if (!speedStatus.isOk()) {
        GTEST_SKIP() << "setFanSpeed failed";
    }

    bool autoMode = true;
    ASSERT_BINDER_OK(sService->isFanAutoMode(&autoMode));
    EXPECT_FALSE(autoMode)
        << "setFanSpeed must exit auto mode so the kernel does not override the value";

    sService->setFanAutoMode(true);
}


// =============================================================================
// Section E — Invalid input / error codes
// =============================================================================

// setFanSpeed(-1) must throw ERROR_INVALID_SPEED=2.
// The service must validate input BEFORE attempting any sysfs write.
TEST_F(ThermalControlServiceTest, SetFanSpeed_Negative_ThrowsCode2) {
    ndk::ScopedAStatus status = sService->setFanSpeed(-1);
    ASSERT_SERVICE_ERROR(status, IThermalControlService::ERROR_INVALID_SPEED);
}

// setFanSpeed(101) must throw ERROR_INVALID_SPEED=2
TEST_F(ThermalControlServiceTest, SetFanSpeed_Over100_ThrowsCode2) {
    ndk::ScopedAStatus status = sService->setFanSpeed(101);
    ASSERT_SERVICE_ERROR(status, IThermalControlService::ERROR_INVALID_SPEED);
}

// setFanSpeed(200) — far out of range — must still return ERROR_INVALID_SPEED=2
// (not crash, not ERROR_SYSFS_WRITE)
TEST_F(ThermalControlServiceTest, SetFanSpeed_200_ThrowsCode2) {
    ndk::ScopedAStatus status = sService->setFanSpeed(200);
    ASSERT_SERVICE_ERROR(status, IThermalControlService::ERROR_INVALID_SPEED);
}

// Guard all three error code constant values.
// Changing these is a breaking change in a @VintfStability interface —
// versioning rules require freezing a new version instead.
TEST_F(ThermalControlServiceTest, ErrorCode_HAL_UNAVAILABLE_Is1) {
    EXPECT_EQ(IThermalControlService::ERROR_HAL_UNAVAILABLE, 1);
}

TEST_F(ThermalControlServiceTest, ErrorCode_INVALID_SPEED_Is2) {
    EXPECT_EQ(IThermalControlService::ERROR_INVALID_SPEED, 2);
}

TEST_F(ThermalControlServiceTest, ErrorCode_SYSFS_WRITE_Is3) {
    EXPECT_EQ(IThermalControlService::ERROR_SYSFS_WRITE, 3);
}


// =============================================================================
// Section F — Concurrency & stability
// =============================================================================

// 8 threads each call getCpuTemperatureCelsius() 50 times concurrently.
// The service must not crash and every call must return a consistent type.
TEST_F(ThermalControlServiceTest, ConcurrentReads_NoCrash) {
    static constexpr int kThreads        = 8;
    static constexpr int kCallsPerThread = 50;

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kCallsPerThread; ++i) {
                float temp = 0.0f;
                ndk::ScopedAStatus s = sService->getCpuTemperatureCelsius(&temp);
                // Accept both ok() and ERROR_HAL_UNAVAILABLE
                bool ok = s.isOk() ||
                    (s.getExceptionCode() == EX_SERVICE_SPECIFIC &&
                     s.getServiceSpecificError() ==
                         IThermalControlService::ERROR_HAL_UNAVAILABLE);
                if (!ok) failures++;
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(failures.load(), 0)
        << failures.load() << " concurrent getCpuTemperatureCelsius() calls failed";
}

// 100 sequential temperature reads — verifies no state corruption over time
TEST_F(ThermalControlServiceTest, RepeatedReads_NoCrash) {
    for (int i = 0; i < 100; ++i) {
        float temp = 0.0f;
        ndk::ScopedAStatus s = sService->getCpuTemperatureCelsius(&temp);

        bool acceptable = s.isOk() ||
            (s.getExceptionCode() == EX_SERVICE_SPECIFIC &&
             s.getServiceSpecificError() ==
                 IThermalControlService::ERROR_HAL_UNAVAILABLE);

        ASSERT_TRUE(acceptable)
            << "Call " << i << " failed unexpectedly: " << s.getDescription();
    }
}
