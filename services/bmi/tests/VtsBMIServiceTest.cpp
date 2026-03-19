// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "VtsBMIServiceTest"

// ─────────────────────────────────────────────────────────────────────────────
// What this file tests
//
//   IBMIService — a vendor C++ binder service that computes BMI:
//     getBMI(height_m, weight_kg) → weight / (height * height)
//
//   Error contract: throws ServiceSpecificException(ERROR_INVALID_INPUT=1)
//     when height <= 0  OR  weight <= 0
//
// Why VTS?
//   Same reasoning as CalculatorService — vendor C++ service, @hide AIDL,
//   needs system privileges to discover and call via ServiceManager.
//
// Test structure:
//   A  — Service availability (is bmid registered?)
//   B  — Correct BMI calculations (formula verification)
//   C  — Error cases (invalid inputs must throw, not crash)
//   D  — Float precision & stability
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>    // std::isfinite, std::abs
#include <thread>
#include <vector>
#include <atomic>

#include <gtest/gtest.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>

// AIDL-generated NDK proxy for IBMIService
#include <aidl/com/myoem/bmi/IBMIService.h>

using aidl::com::myoem::bmi::IBMIService;

// Service name — must match kServiceName in services/bmi/src/main.cpp
// and the label in services/bmi/sepolicy/private/service_contexts.
static constexpr const char* kServiceName = "com.myoem.bmi.IBMIService";

// ─────────────────────────────────────────────────────────────────────────────
// Floating-point tolerance for BMI result comparisons.
// BMI uses single-precision float internally, so 0.01 is an appropriate delta.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float kBmiDelta = 0.01f;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — see CalculatorServiceTest for a full explanation of the
// SetUpTestSuite / GTEST_SKIP pattern.
// ─────────────────────────────────────────────────────────────────────────────
class BMIServiceTest : public ::testing::Test {
  public:
    static std::shared_ptr<IBMIService> sService;

    static void SetUpTestSuite() {
        ABinderProcess_setThreadPoolMaxThreadCount(0);
        ABinderProcess_startThreadPool();

        ndk::SpAIBinder binder(AServiceManager_waitForService(kServiceName));
        if (binder.get() != nullptr) {
            sService = IBMIService::fromBinder(binder);
        }
    }

  protected:
    void SetUp() override {
        if (sService == nullptr) {
            GTEST_SKIP() << "bmid not running — is it installed and started?";
        }
    }
};

std::shared_ptr<IBMIService> BMIServiceTest::sService = nullptr;

// Helper: assert that a binder call succeeded
#define ASSERT_BINDER_OK(status)                                            \
    ASSERT_TRUE((status).isOk())                                            \
        << "Binder call failed: " << (status).getDescription()

// Helper: assert that a binder call FAILED with a specific service-specific
// error code.  Used to verify error paths without duplicating the three
// assertion lines everywhere.
#define ASSERT_SERVICE_ERROR(status, expectedCode)                          \
    do {                                                                    \
        ASSERT_FALSE((status).isOk())                                       \
            << "Call should have failed with code " << (expectedCode)       \
            << " but returned ok()";                                        \
        ASSERT_EQ((status).getExceptionCode(), EX_SERVICE_SPECIFIC)         \
            << "Expected EX_SERVICE_SPECIFIC";                              \
        EXPECT_EQ((status).getServiceSpecificError(), (expectedCode))       \
            << "Wrong error code";                                          \
    } while (0)


// =============================================================================
// Section A — Service availability
// =============================================================================

// bmid must be registered in the ServiceManager under the canonical name.
// If this fails, check: main.cpp kServiceName, service_contexts label.
TEST_F(BMIServiceTest, ServiceRegistered) {
    ndk::SpAIBinder binder(AServiceManager_checkService(kServiceName));
    ASSERT_NE(binder.get(), nullptr)
        << "Service not found under: " << kServiceName;
}


// =============================================================================
// Section B — Correct BMI calculations
//
// Formula: BMI = weight / (height * height)
// All inputs are in SI units: height in metres, weight in kilograms.
// =============================================================================

// Standard adult: 70 kg at 1.75 m → BMI ≈ 22.86 (healthy range 18.5–24.9)
// This is the "golden path" test — the most common real-world usage.
TEST_F(BMIServiceTest, BMI_StandardAdult) {
    float result = 0.0f;
    ASSERT_BINDER_OK(sService->getBMI(1.75f, 70.0f, &result));
    // Expected: 70.0 / (1.75 * 1.75) = 70.0 / 3.0625 ≈ 22.857
    EXPECT_NEAR(result, 22.857f, kBmiDelta)
        << "BMI for 70kg at 1.75m should be ~22.86";
}

// Tall and light → underweight BMI
// 60 kg at 2.0 m → 60 / 4.0 = 15.0 (exact)
TEST_F(BMIServiceTest, BMI_TallAndLight) {
    float result = 0.0f;
    ASSERT_BINDER_OK(sService->getBMI(2.0f, 60.0f, &result));
    EXPECT_NEAR(result, 15.0f, kBmiDelta);
}

// Short and heavy → obese BMI
// 90 kg at 1.5 m → 90 / 2.25 = 40.0 (exact)
TEST_F(BMIServiceTest, BMI_ShortAndHeavy) {
    float result = 0.0f;
    ASSERT_BINDER_OK(sService->getBMI(1.50f, 90.0f, &result));
    EXPECT_NEAR(result, 40.0f, kBmiDelta);
}

// Verify the formula: the result must equal weight / (height * height).
// This test is independent of any specific expected numeric value —
// it verifies that the service implements the correct formula.
TEST_F(BMIServiceTest, BMI_Formula_IsWeightOverHeightSquared) {
    const float h = 1.80f;
    const float w = 80.0f;
    float result  = 0.0f;

    ASSERT_BINDER_OK(sService->getBMI(h, w, &result));

    float expected = w / (h * h);  // compute locally as reference
    EXPECT_NEAR(result, expected, kBmiDelta)
        << "Service result differs from weight/(height*height)";
}

// Edge case: very small but valid height (> 0).
// The service must not crash or error — the formula still applies.
TEST_F(BMIServiceTest, BMI_SmallButValidHeight) {
    float result = 0.0f;
    // Small values are technically out of human range but the AIDL contract
    // only requires height > 0, so this must succeed.
    ASSERT_BINDER_OK(sService->getBMI(0.1f, 0.5f, &result));
    EXPECT_TRUE(std::isfinite(result)) << "Result must be a finite float";
    EXPECT_GT(result, 0.0f);
}


// =============================================================================
// Section C — Error cases
//
// The service throws ServiceSpecificException(ERROR_INVALID_INPUT=1) for:
//   height <= 0  OR  weight <= 0
//
// We test each invalid combination independently so failures are easy to pinpoint.
// =============================================================================

// height == 0 → division by zero in the formula → must throw, not crash
TEST_F(BMIServiceTest, BMI_ZeroHeight_ThrowsError) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getBMI(0.0f, 70.0f, &result);
    ASSERT_SERVICE_ERROR(status, IBMIService::ERROR_INVALID_INPUT);
}

// height < 0 → physically impossible → must throw
TEST_F(BMIServiceTest, BMI_NegativeHeight_ThrowsError) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getBMI(-1.75f, 70.0f, &result);
    ASSERT_SERVICE_ERROR(status, IBMIService::ERROR_INVALID_INPUT);
}

// weight == 0 → BMI would be 0, but 0 kg is not a valid input per the contract
TEST_F(BMIServiceTest, BMI_ZeroWeight_ThrowsError) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getBMI(1.75f, 0.0f, &result);
    ASSERT_SERVICE_ERROR(status, IBMIService::ERROR_INVALID_INPUT);
}

// weight < 0 → physically impossible → must throw
TEST_F(BMIServiceTest, BMI_NegativeWeight_ThrowsError) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getBMI(1.75f, -70.0f, &result);
    ASSERT_SERVICE_ERROR(status, IBMIService::ERROR_INVALID_INPUT);
}

// Both zero → must throw (not crash from 0/0)
TEST_F(BMIServiceTest, BMI_BothZero_ThrowsError) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getBMI(0.0f, 0.0f, &result);
    ASSERT_SERVICE_ERROR(status, IBMIService::ERROR_INVALID_INPUT);
}

// Both negative → must throw
TEST_F(BMIServiceTest, BMI_BothNegative_ThrowsError) {
    float result = 0.0f;
    ndk::ScopedAStatus status = sService->getBMI(-1.0f, -70.0f, &result);
    ASSERT_SERVICE_ERROR(status, IBMIService::ERROR_INVALID_INPUT);
}

// Guard the constant value — changing ERROR_INVALID_INPUT is a breaking API change
TEST_F(BMIServiceTest, ErrorConstant_InvalidInput_IsOne) {
    EXPECT_EQ(IBMIService::ERROR_INVALID_INPUT, 1);
}


// =============================================================================
// Section D — Float precision & stability
// =============================================================================

// The result must be a valid IEEE 754 finite float — not NaN, not infinity.
// If the service were to compute 0/0 or 1/0 without the guard, this catches it.
TEST_F(BMIServiceTest, BMI_ReturnsFiniteFloat) {
    float result = 0.0f;
    ASSERT_BINDER_OK(sService->getBMI(1.75f, 70.0f, &result));

    EXPECT_TRUE(std::isfinite(result))
        << "Result must be finite (not NaN, not infinity), got: " << result;
    EXPECT_FALSE(std::isnan(result))
        << "Result must not be NaN";
}

// Very large values must not cause a crash or overflow to infinity.
// 3.0 m height and 500 kg weight are absurd but within float range.
TEST_F(BMIServiceTest, BMI_LargeValues_NoOverflow) {
    float result = 0.0f;
    ASSERT_BINDER_OK(sService->getBMI(3.0f, 500.0f, &result));

    EXPECT_TRUE(std::isfinite(result))
        << "Large-value BMI must still be finite, got: " << result;
    EXPECT_GT(result, 0.0f);
}

// 8 threads each call getBMI() 100 times concurrently.
// The service must have no shared mutable state that would cause a data race.
TEST_F(BMIServiceTest, ConcurrentCalls_NoDataRace) {
    static constexpr int kThreads        = 8;
    static constexpr int kCallsPerThread = 100;

    std::atomic<int> failures{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            // Each thread uses a slightly different height so results differ
            float h = 1.5f + (t * 0.05f);  // 1.50, 1.55, 1.60 … 1.85
            float w = 70.0f;
            float expected = w / (h * h);

            for (int i = 0; i < kCallsPerThread; ++i) {
                float result = 0.0f;
                ndk::ScopedAStatus s = sService->getBMI(h, w, &result);
                if (!s.isOk() || std::abs(result - expected) > kBmiDelta) {
                    failures++;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(failures.load(), 0)
        << failures.load() << " concurrent getBMI() calls failed or gave wrong results";
}
