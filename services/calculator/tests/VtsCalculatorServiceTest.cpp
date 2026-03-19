// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

// LOG_TAG must be the very first line — before any #include — otherwise
// <log/log.h> defines it as NULL and -Werror causes a redefinition error.
#define LOG_TAG "VtsCalculatorServiceTest"

// ─────────────────────────────────────────────────────────────────────────────
// What this file tests
//
//   ICalculatorService — a vendor C++ binder service that exposes:
//     add(a, b)       → a + b
//     subtract(a, b)  → a - b
//     multiply(a, b)  → a * b
//     divide(a, b)    → a / b  (throws ERROR_DIVIDE_BY_ZERO=1 when b==0)
//
// Why VTS (not CTS)?
//   The service is vendor-only (@hide AIDL) and runs in the vendor partition.
//   VTS tests run with system privileges and can reach vendor services through
//   the system ServiceManager (/dev/binder via libbinder_ndk).
//   CTS tests run as normal app UIDs and cannot reach @hide services directly.
//
// How VTS test discovery works:
//   The Android test harness finds this binary via test_suites:["vts"] in
//   Android.bp. It pushes the binary to /data/local/tmp on the device and
//   executes it. All output is GTest XML that the harness parses.
// ─────────────────────────────────────────────────────────────────────────────

#include <atomic>     // std::atomic — for thread-safe failure counting
#include <climits>    // INT32_MAX
#include <thread>     // std::thread — for concurrency tests
#include <vector>     // std::vector<std::thread>

// GTest — the C++ unit-test framework used throughout AOSP VTS
#include <gtest/gtest.h>

// NDK binder headers (libbinder_ndk / LLNDK)
//   libbinder_ndk is the correct binder library for vendor code because it is
//   LLNDK (one copy shared by all partitions).  It always talks to the system
//   ServiceManager on /dev/binder, which is what apps query.
//
//   Do NOT use libbinder here — the vendor copy of libbinder uses /dev/vndbinder
//   and cannot reach the system ServiceManager.
#include <android/binder_manager.h>    // AServiceManager_waitForService / checkService
#include <android/binder_process.h>    // ABinderProcess_setThreadPoolMaxThreadCount

// AIDL-generated NDK proxy for ICalculatorService.
// Generated from: services/calculator/aidl/com/myoem/calculator/ICalculatorService.aidl
// The generated header lives in the NDK backend output directory.
#include <aidl/com/myoem/calculator/ICalculatorService.h>

// Bring the proxy type into the current scope for readability
using aidl::com::myoem::calculator::ICalculatorService;

// ─────────────────────────────────────────────────────────────────────────────
// Service name constant
//
// This string must exactly match (case-sensitive, no trailing slash):
//   1. kServiceName in services/calculator/src/main.cpp
//   2. The label in services/calculator/sepolicy/private/service_contexts
//
// If any of the three disagree, the service either won't register or won't
// be discoverable by clients.
// ─────────────────────────────────────────────────────────────────────────────
static constexpr const char* kServiceName = "com.myoem.calculator.ICalculatorService";

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
//
// GTest fixture lifecycle:
//   SetUpTestSuite()  — runs ONCE before all tests in this file
//   SetUp()           — runs before EACH individual test
//   TearDown()        — runs after EACH individual test
//   TearDownTestSuite() — runs ONCE after all tests
//
// We acquire the binder proxy in SetUpTestSuite so we don't pay the
// waitForService latency on every single test.
// ─────────────────────────────────────────────────────────────────────────────
class CalculatorServiceTest : public ::testing::Test {
  public:
    // Shared proxy — set once in SetUpTestSuite, reused by all test cases.
    // Declared static so all test instances share the same connection.
    static std::shared_ptr<ICalculatorService> sService;

    // Called once before all tests in this file.
    static void SetUpTestSuite() {
        // ABinderProcess_setThreadPoolMaxThreadCount(0):
        //   0 = "client only" — the test process will never receive incoming
        //   binder calls, so no worker threads are needed.
        //   The thread pool must still be started so the binder driver can
        //   deliver return values from the service back to us.
        ABinderProcess_setThreadPoolMaxThreadCount(0);
        ABinderProcess_startThreadPool();

        // AServiceManager_waitForService blocks until the service registers
        // with the ServiceManager, or 5 seconds pass (Android default timeout).
        // This is more reliable than checkService (non-blocking) in tests
        // because the device may still be booting when VTS starts.
        ndk::SpAIBinder binder(AServiceManager_waitForService(kServiceName));

        if (binder.get() != nullptr) {
            // Wrap the raw binder in the typed proxy.
            // fromBinder returns nullptr if the binder is for a different interface.
            sService = ICalculatorService::fromBinder(binder);
        }
        // If sService remains null, each individual test will call GTEST_SKIP().
    }

  protected:
    // Called before EACH test — skip cleanly if calculatord is not running.
    // GTEST_SKIP aborts the test with status "SKIPPED" (not "FAILED"), so a
    // missing service won't pollute the results with false failures.
    void SetUp() override {
        if (sService == nullptr) {
            GTEST_SKIP() << "calculatord not running — is it installed and started?";
        }
    }
};

// Static member definition — required by C++ (declaration is in the class above)
std::shared_ptr<ICalculatorService> CalculatorServiceTest::sService = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Helper macro: ASSERT_BINDER_OK(status)
//
// Asserts that a binder call returned ok(). On failure it prints the full
// human-readable status description so you know exactly which error occurred.
//
// Use ASSERT (not EXPECT) so the test aborts immediately on IPC failure —
// there is no point checking the output value if the call itself failed.
// ─────────────────────────────────────────────────────────────────────────────
#define ASSERT_BINDER_OK(status)                                               \
    ASSERT_TRUE((status).isOk())                                               \
        << "Binder call failed: " << (status).getDescription()


// =============================================================================
// Section A — Service availability
//
// These are the most fundamental tests. If they fail, nothing else will work.
// =============================================================================

// Verify the service is registered under the exact canonical name.
// AServiceManager_checkService is non-blocking — by the time SetUpTestSuite
// has returned (waitForService succeeded), the service MUST be in the registry.
// A null result here means the service name is wrong.
TEST_F(CalculatorServiceTest, ServiceRegistered) {
    ndk::SpAIBinder binder(AServiceManager_checkService(kServiceName));
    ASSERT_NE(binder.get(), nullptr)
        << "Service not found under: " << kServiceName
        << "\n  Check: kServiceName in main.cpp matches this string exactly"
        << "\n  Check: service_contexts label matches this string exactly";
}


// =============================================================================
// Section B — add(a, b) → a + b
// =============================================================================

// 10 + 3 = 13  (two positive integers — the basic case)
TEST_F(CalculatorServiceTest, Add_TwoPositives) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->add(10, 3, &result));
    EXPECT_EQ(result, 13);
}

// (-5) + (-3) = -8  (both negative — must not special-case signs)
TEST_F(CalculatorServiceTest, Add_TwoNegatives) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->add(-5, -3, &result));
    EXPECT_EQ(result, -8);
}

// (-10) + 15 = 5  (negative + positive → positive result)
TEST_F(CalculatorServiceTest, Add_PositiveAndNegative) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->add(-10, 15, &result));
    EXPECT_EQ(result, 5);
}

// 0 + 0 = 0  (edge case: both operands are the identity element)
TEST_F(CalculatorServiceTest, Add_BothZero) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->add(0, 0, &result));
    EXPECT_EQ(result, 0);
}

// 42 + 0 = 42  (zero is the identity element for addition)
TEST_F(CalculatorServiceTest, Add_WithZero) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->add(42, 0, &result));
    EXPECT_EQ(result, 42);
}

// INT32_MAX + 0 must not crash.
// The service implementation is simply a + b in C++, so this is well-defined.
TEST_F(CalculatorServiceTest, Add_MaxPlusZero) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->add(INT32_MAX, 0, &result));
    EXPECT_EQ(result, INT32_MAX);
}

// (-7) + 7 = 0  (a number plus its additive inverse must be zero)
TEST_F(CalculatorServiceTest, Add_InversesResultInZero) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->add(-7, 7, &result));
    EXPECT_EQ(result, 0);
}


// =============================================================================
// Section C — subtract(a, b) → a - b
// =============================================================================

// 10 - 3 = 7  (standard positive result)
TEST_F(CalculatorServiceTest, Subtract_PositiveResult) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->subtract(10, 3, &result));
    EXPECT_EQ(result, 7);
}

// 3 - 10 = -7  (b > a → result must be negative)
TEST_F(CalculatorServiceTest, Subtract_NegativeResult) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->subtract(3, 10, &result));
    EXPECT_EQ(result, -7);
}

// 5 - 5 = 0  (a == b → must produce exact zero)
TEST_F(CalculatorServiceTest, Subtract_SameNumbers) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->subtract(5, 5, &result));
    EXPECT_EQ(result, 0);
}

// 0 - 5 = -5  (subtracting from zero gives the negative)
TEST_F(CalculatorServiceTest, Subtract_FromZero) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->subtract(0, 5, &result));
    EXPECT_EQ(result, -5);
}

// (-3) - (-7) = 4  (subtracting a negative adds it: -3 + 7 = 4)
TEST_F(CalculatorServiceTest, Subtract_TwoNegatives) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->subtract(-3, -7, &result));
    EXPECT_EQ(result, 4);
}


// =============================================================================
// Section D — multiply(a, b) → a * b
// =============================================================================

// 4 * 5 = 20  (standard positive multiplication)
TEST_F(CalculatorServiceTest, Multiply_TwoPositives) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->multiply(4, 5, &result));
    EXPECT_EQ(result, 20);
}

// n * 0 = 0  (multiplication by zero — must produce exactly 0, not garbage)
TEST_F(CalculatorServiceTest, Multiply_ByZero) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->multiply(999, 0, &result));
    EXPECT_EQ(result, 0);
}

// (-3) * (-4) = 12  (negative × negative = positive)
TEST_F(CalculatorServiceTest, Multiply_TwoNegatives) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->multiply(-3, -4, &result));
    EXPECT_EQ(result, 12);
}

// (-3) * 4 = -12  (negative × positive = negative)
TEST_F(CalculatorServiceTest, Multiply_MixedSign) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->multiply(-3, 4, &result));
    EXPECT_EQ(result, -12);
}

// n * 1 = n  (multiplication by one is the identity operation)
TEST_F(CalculatorServiceTest, Multiply_ByOne) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->multiply(42, 1, &result));
    EXPECT_EQ(result, 42);
}


// =============================================================================
// Section E — divide(a, b) → a / b
//             Throws ServiceSpecificException(ERROR_DIVIDE_BY_ZERO=1) when b==0
// =============================================================================

// 10 / 2 = 5  (exact division with no remainder)
TEST_F(CalculatorServiceTest, Divide_ExactDivision) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->divide(10, 2, &result));
    EXPECT_EQ(result, 5);
}

// 7 / 2 = 3  (C++ integer division truncates toward zero — NOT 3.5 or 4)
// This tests that the service uses integer arithmetic as documented.
TEST_F(CalculatorServiceTest, Divide_IntegerTruncation) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->divide(7, 2, &result));
    EXPECT_EQ(result, 3);  // not 4 (round), not 3.5 (float)
}

// (-10) / 2 = -5  (negative numerator → negative result)
TEST_F(CalculatorServiceTest, Divide_NegativeByPositive) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->divide(-10, 2, &result));
    EXPECT_EQ(result, -5);
}

// (-10) / (-2) = 5  (negative / negative = positive)
TEST_F(CalculatorServiceTest, Divide_NegativeByNegative) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->divide(-10, -2, &result));
    EXPECT_EQ(result, 5);
}

// 42 / 1 = 42  (dividing by one is the identity operation)
TEST_F(CalculatorServiceTest, Divide_ByOne) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->divide(42, 1, &result));
    EXPECT_EQ(result, 42);
}

// 42 / (-1) = -42  (dividing by minus one negates the number)
TEST_F(CalculatorServiceTest, Divide_ByMinusOne) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->divide(42, -1, &result));
    EXPECT_EQ(result, -42);
}

// 0 / 5 = 0  (zero divided by any non-zero number is zero)
TEST_F(CalculatorServiceTest, Divide_ZeroByNonZero) {
    int32_t result = 0;
    ASSERT_BINDER_OK(sService->divide(0, 5, &result));
    EXPECT_EQ(result, 0);
}

// ── Division by zero ──────────────────────────────────────────────────────────
//
// This is the most critical test for divide().
// The service MUST:
//   1. NOT crash or return undefined behavior
//   2. Return a non-ok status (isOk() == false)
//   3. Use EX_SERVICE_SPECIFIC as the exception code (standard AIDL pattern)
//   4. Set the service-specific error to ERROR_DIVIDE_BY_ZERO = 1
//
// If the service crashed instead of throwing, the binder call itself would
// return a DEAD_OBJECT error, which is different from EX_SERVICE_SPECIFIC.
// ─────────────────────────────────────────────────────────────────────────────

// Step 1: verify the call fails (isOk() is false)
TEST_F(CalculatorServiceTest, Divide_ByZero_ReturnsErrorStatus) {
    int32_t result = 0;
    ndk::ScopedAStatus status = sService->divide(10, 0, &result);

    // isOk() must be false — the service must not silently succeed on 10/0
    EXPECT_FALSE(status.isOk())
        << "divide(10, 0) returned ok() — the service should have thrown "
           "ServiceSpecificException(ERROR_DIVIDE_BY_ZERO)";
}

// Step 2: verify the exception type is EX_SERVICE_SPECIFIC (not a binder crash)
// Step 3: verify the error code matches the AIDL constant exactly
TEST_F(CalculatorServiceTest, Divide_ByZero_ErrorCode_IsOne) {
    int32_t result = 0;
    ndk::ScopedAStatus status = sService->divide(10, 0, &result);

    ASSERT_FALSE(status.isOk());

    // EX_SERVICE_SPECIFIC means the service deliberately threw an application
    // error (via ndk::ScopedAStatus::fromServiceSpecificError).
    // Other codes like EX_DEAD_OBJECT would mean the service crashed.
    ASSERT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC)
        << "Expected EX_SERVICE_SPECIFIC, got: " << status.getExceptionCode()
        << " — the service may have crashed instead of throwing";

    // The service-specific error code must match the AIDL-defined constant.
    // If this fails, the service is using the wrong error code.
    EXPECT_EQ(status.getServiceSpecificError(),
              ICalculatorService::ERROR_DIVIDE_BY_ZERO)
        << "Expected ERROR_DIVIDE_BY_ZERO="
        << ICalculatorService::ERROR_DIVIDE_BY_ZERO
        << ", got: " << status.getServiceSpecificError();
}

// Protect the constant value itself: if someone accidentally changes
// ERROR_DIVIDE_BY_ZERO in the AIDL file, this test catches it and flags the
// breaking change before it reaches production.
TEST_F(CalculatorServiceTest, ErrorConstant_DivideByZero_IsOne) {
    // The value 1 is documented in the AIDL file and the TEST_PLAN.md.
    // Changing it is a breaking API change — all clients would need updating.
    EXPECT_EQ(ICalculatorService::ERROR_DIVIDE_BY_ZERO, 1);
}


// =============================================================================
// Section F — Concurrency & stability
//
// These tests verify that the service handles concurrent callers safely
// and does not degrade over many sequential calls.
// =============================================================================

// Launch 8 threads, each calling add() 100 times with distinct operands.
// A well-implemented binder service has no shared mutable state per call, so
// all results must be correct regardless of interleaving.
//
// If this test crashes or reports failures, the service has a thread-safety bug.
TEST_F(CalculatorServiceTest, ConcurrentCalls_NoDataRace) {
    static constexpr int kThreads        = 8;
    static constexpr int kCallsPerThread = 100;

    // Atomic counter — incremented by any thread that gets a wrong result.
    // Atomic because multiple threads write it simultaneously.
    std::atomic<int> failures{0};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < kCallsPerThread; ++i) {
                int32_t result = 0;
                // Each thread uses its thread-index as operand so they all
                // compute different values — we can verify correctness.
                ndk::ScopedAStatus s = sService->add(t, i, &result);
                if (!s.isOk() || result != t + i) {
                    failures++;
                }
            }
        });
    }

    // Wait for all threads to finish
    for (auto& th : threads) th.join();

    EXPECT_EQ(failures.load(), 0)
        << failures.load() << " out of "
        << (kThreads * kCallsPerThread)
        << " concurrent add() calls returned wrong results";
}

// 1000 sequential add() calls through the same binder proxy.
// Verifies there is no memory leak, connection leak, or gradual state
// corruption that would only appear after many calls.
TEST_F(CalculatorServiceTest, RepeatedCalls_ServiceStable) {
    for (int i = 0; i < 1000; ++i) {
        int32_t result = 0;
        // Use ASSERT (not EXPECT) so a binder failure at call N stops the
        // loop immediately rather than printing 1000 failure messages.
        ASSERT_BINDER_OK(sService->add(i, 1, &result));
        EXPECT_EQ(result, i + 1)
            << "Wrong result at iteration " << i;
    }
}
