# Testing a Full-Stack AOSP Vendor Module — From First Principles to `atest` Green
## A Complete Engineering Journal of Writing, Breaking, and Fixing Every Test

**A 15-Part Article Series**

---

> **About this series**: This is the exact, step-by-step story of writing tests
> for the `vendor/myoem` ThermalControl stack on Raspberry Pi 5 / Android 15
> (android-15.0.0_r14). Every build error, every wrong assumption, and every
> fix is documented in the order it actually happened. By the end you'll have
> a mental model of AOSP testing that no blog post currently gives you in one place.

---

## Table of Contents

1. [The Big Picture — What We Were Testing](#part-1)
2. [The Decision Matrix — Which Module Needs Which Test?](#part-2)
3. [Industry Standard Test Placement in AOSP](#part-3)
4. [VTS for C++ Vendor Services — Calculator and BMI](#part-4)
5. [VTS for the Thermal Control Service — VINTF Awareness](#part-5)
6. [VTS for the HAL Library — Direct C++ Testing](#part-6)
7. [Java Instrumented Tests for ThermalControlManager](#part-7)
8. [Build Crisis #1 — CTS Module Cannot Use Platform APIs](#part-8)
9. [Build Crisis #2 — android_test Panics Without a Manifest](#part-9)
10. [Build Crisis #3 — JUnit Libraries Are Not Pre-installed](#part-10)
11. [Kotlin ViewModel Tests — ThermalViewModelTest](#part-11)
12. [Kotlin Compose UI Tests — ThermalScreenTest](#part-12)
13. [Build Crisis #4 — Wiring Up Kotlin Tests in AOSP](#part-13)
14. [How to Run Every Test — The Complete Execution Guide](#part-14)
15. [Google CTS/VTS Certification, GMS, and MADA Explained](#part-15)

---

<a name="part-1"></a>
# Part 1: The Big Picture — What We Were Testing

## The Stack

Before we can talk about tests, we need a clear picture of what exists. The
`vendor/myoem/` ThermalControl project is a full vertical stack:

```
┌──────────────────────────────────────────────────────┐
│ ThermalMonitor.apk (Kotlin / Jetpack Compose)        │  Layer 4 — App
│ vendor/myoem/apps/ThermalMonitor/                    │
└──────────────────────┬───────────────────────────────┘
                       │ Java API
┌──────────────────────▼───────────────────────────────┐
│ ThermalControlManager.java                           │  Layer 3 — Manager
│ vendor/myoem/libs/thermalcontrol/                    │
└──────────────────────┬───────────────────────────────┘
                       │ AIDL Binder IPC
┌──────────────────────▼───────────────────────────────┐
│ thermalcontrold (C++ daemon)                         │  Layer 2 — Service
│ vendor/myoem/services/thermalcontrol/                │
└──────────────────────┬───────────────────────────────┘
                       │ C++ function calls
┌──────────────────────▼───────────────────────────────┐
│ libthermalcontrolhal.so                              │  Layer 1 — HAL
│ vendor/myoem/hal/thermalcontrol/                     │
└──────────────────────┬───────────────────────────────┘
                       │ sysfs file I/O
┌──────────────────────▼───────────────────────────────┐
│ Linux kernel (pwm-fan driver)                        │
│ /sys/class/hwmon/hwmon*/                             │
└──────────────────────────────────────────────────────┘
```

Plus two simpler services at the same binder level:
- `vendor/myoem/services/calculator/` — a basic arithmetic binder service
- `vendor/myoem/services/bmi/` — a BMI calculation binder service

**Every layer needs a different kind of test.** The question is: which kind?

## Why This Question Is Hard

In a standard Android Studio project you write JUnit tests and Espresso tests
and move on. In AOSP, the question of "which test type" has build-system
consequences. The wrong choice produces confusing build errors that look like
dependency failures when really they're policy failures.

The answer depends on three things:
1. What **partition** does the code live on?
2. What **APIs** does the test need to call?
3. Is the interface **declared in the VINTF manifest**?

---

<a name="part-2"></a>
# Part 2: The Decision Matrix — Which Module Needs Which Test?

## The Two Test Universes

AOSP has two top-level test frameworks:

| Framework | Abbrev | For | Build type | Runner |
|-----------|--------|-----|-----------|--------|
| Vendor Test Suite | VTS | Vendor C++ code, HALs, native binder services | `cc_test` | gtest on device |
| Compatibility Test Suite | CTS | Java/Kotlin app-layer public API code | `android_test` | JUnit on device |

The critical rule:

> **CTS modules cannot use `platform_apis: true`.**
> VTS (and `general-tests`) modules can.

If you call any `@hide` API, you are **automatically disqualified from CTS**.

## Applying the Matrix

| Module | Layer | Language | `@hide` APIs needed? | VINTF-declared? | Test type |
|--------|-------|----------|---------------------|----------------|-----------|
| libthermalcontrolhal | HAL lib | C++ | No (sysfs only) | No | `cc_test` → VTS |
| thermalcontrold | Binder service | C++ | `AServiceManager_*` | Yes (`@VintfStability`) | `cc_test` → VTS |
| calculatord | Binder service | C++ | `AServiceManager_*` | No (`unstable`) | `cc_test` → VTS |
| bmid | Binder service | C++ | `AServiceManager_*` | No (`unstable`) | `cc_test` → VTS |
| ThermalControlManager | Java library | Java | `ServiceManager.checkService()` is `@hide` in tests | — | `android_test` → `general-tests` |
| ThermalMonitor | Compose app | Kotlin | — | — | `android_test` → `general-tests` |

## The VTS Rule for `@VintfStability`

If an AIDL interface is annotated `@VintfStability` and the service is declared
in the VINTF manifest, the VTS test has an extra responsibility: it must verify
the VINTF declaration itself. This is not just a nice-to-have — **VINTF
interfaces are part of Android's Treble contract** and their presence in the
manifest must be tested.

```cpp
// Section A of VtsThermalControlServiceTest.cpp
TEST_F(ThermalControlServiceTest, VintfManifest_ServiceDeclared) {
    ASSERT_TRUE(AServiceManager_isDeclared(kServiceName))
        << "Service not declared in VINTF manifest — "
           "is thermalcontrol-vintf-fragment installed?";
}
```

This test calls `AServiceManager_isDeclared()` which reads the VINTF manifest
at runtime. If the `vintf_fragment` was not built and flashed to the device,
this test fails.

## Why NOT CTS for ThermalControlManager?

The manager's live service tests (Group F) call:

```java
IBinder b = ServiceManager.checkService(ThermalControlManager.SERVICE_NAME);
assumeTrue("thermalcontrold not running", b != null);
```

`ServiceManager.checkService()` is `@hide`. Using it requires `platform_apis: true`.
CTS modules are **forbidden** from using `platform_apis: true` — the build
enforces this with an explicit error:

```
error: CtsMyOemThermalControlManagerTests cannot depend on framework
  because the module is in CTS test_suites.
```

There is no way to put a module in both `test_suites: ["cts"]` and use
`platform_apis: true`. If you need hidden APIs, you are in `general-tests`.

---

<a name="part-3"></a>
# Part 3: Industry Standard Test Placement in AOSP

## The Co-located `tests/` Pattern

Before writing a single test file, the question of *where to put it* matters.
The AOSP standard, used by Google's own modules, is **co-location**:

```
vendor/myoem/
├── services/calculator/
│   ├── Android.bp           ← production build
│   ├── src/
│   │   └── main.cpp
│   └── tests/               ← test lives HERE, next to what it tests
│       ├── Android.bp
│       └── VtsCalculatorServiceTest.cpp
├── hal/thermalcontrol/
│   ├── Android.bp
│   ├── src/
│   └── tests/
│       ├── Android.bp
│       └── VtsThermalControlHalTest.cpp
└── libs/thermalcontrol/
    ├── Android.bp
    ├── java/
    └── tests/
        ├── Android.bp
        ├── AndroidManifest.xml
        └── java/com/myoem/thermalcontrol/
            └── ThermalControlManagerTest.java
```

## Why Co-located, Not a Separate `tests/` Root?

Real AOSP examples (`frameworks/base/`, `hardware/interfaces/`) all put tests
next to the module they test. The reason is **navigability**: when you're
reading `Android.bp` for a module and want to understand its tests, they're
in the next directory over, not somewhere else in the tree.

This is different from typical Java project structure where you might have:
```
src/main/java/...
src/test/java/...
```
That's a Gradle convention. AOSP/Soong uses the co-located `<module>/tests/`
convention. Kotlin app tests (which do use Gradle-style paths for the source
files) still use the same principle — the test **modules** are in
`apps/ThermalMonitor/Android.bp`, co-located with the app.

## The Complete Test File Inventory

```
vendor/myoem/
├── services/
│   ├── calculator/tests/
│   │   ├── Android.bp                       ← VtsCalculatorServiceTest
│   │   └── VtsCalculatorServiceTest.cpp
│   ├── bmi/tests/
│   │   ├── Android.bp                       ← VtsBMIServiceTest
│   │   └── VtsBMIServiceTest.cpp
│   └── thermalcontrol/tests/
│       ├── Android.bp                       ← VtsThermalControlServiceTest
│       └── VtsThermalControlServiceTest.cpp
├── hal/thermalcontrol/tests/
│   ├── Android.bp                           ← VtsThermalControlHalTest
│   └── VtsThermalControlHalTest.cpp
├── libs/thermalcontrol/tests/
│   ├── Android.bp                           ← MyOemThermalControlManagerTests
│   ├── AndroidManifest.xml
│   └── java/com/myoem/thermalcontrol/
│       └── ThermalControlManagerTest.java
└── apps/ThermalMonitor/
    ├── Android.bp                           ← ThermalMonitorLib + app + 2 test modules
    ├── src/test/
    │   ├── AndroidManifest.xml
    │   └── kotlin/com/myoem/thermalmonitor/
    │       └── ThermalViewModelTest.kt       ← ThermalMonitorUnitTests
    └── src/androidTest/
        ├── AndroidManifest.xml
        └── kotlin/com/myoem/thermalmonitor/
            └── ThermalScreenTest.kt          ← ThermalMonitorInstrumentedTests
```

---

<a name="part-4"></a>
# Part 4: VTS for C++ Vendor Services — Calculator and BMI

## The `cc_test` Android.bp

```
// vendor/myoem/services/calculator/tests/Android.bp
cc_test {
    name: "VtsCalculatorServiceTest",
    vendor: true,              // installs to vendor partition
    srcs: ["VtsCalculatorServiceTest.cpp"],
    shared_libs: ["libbinder_ndk", "liblog"],
    static_libs: ["calculatorservice-aidl-ndk"],  // AIDL NDK stubs
    test_suites: ["vts"],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

Four things to understand about this file:

1. **`vendor: true`** — the test binary must live on the vendor partition because
   it calls the vendor service. A system-partition binary cannot call a
   vendor-local binder service.

2. **`shared_libs: ["libbinder_ndk"]`** — LLNDK binder library. Never use
   `libbinder` (VNDK) in vendor test code. `libbinder_ndk` is the stable
   cross-partition path.

3. **`static_libs: ["calculatorservice-aidl-ndk"]`** — AIDL-generated stubs are
   always statically linked. There is no shared version. If you put them in
   `shared_libs`, the build fails.

4. **`test_suites: ["vts"]`** — this is what makes `atest` and the Android
   test harness pick it up as a VTS module.

## The Five-Pattern NDK Binder Test

Every VTS test for an NDK binder service follows this pattern:

```cpp
// 1. Constant for the service name
static constexpr const char* kServiceName =
    "com.myoem.calculator.ICalculatorService";

// 2. Static pointer — shared across all tests in the suite
static std::shared_ptr<ICalculatorService> gService;

// 3. SetUpTestSuite — runs once, gets the binder
static void SetUpTestSuite() {
    // waitForService blocks until the service appears or timeout
    AIBinder* binder = AServiceManager_waitForService(kServiceName);
    if (binder == nullptr) {
        // Don't fail here — let each TEST skip individually
        return;
    }
    gService = ICalculatorService::fromBinder(ndk::SpAIBinder(binder));
}

// 4. GTEST_SKIP() guard in each test
TEST_F(CalculatorServiceTest, Add_TwoPositives) {
    if (!gService) GTEST_SKIP() << "calculatord not running";
    // ... actual test ...
}

// 5. ASSERT_BINDER_OK macro for clean error messages
#define ASSERT_BINDER_OK(status)                            \
    ASSERT_TRUE((status).isOk())                           \
        << "Binder call failed: " << (status).getDescription()
```

**Why `GTEST_SKIP()` instead of `ASSERT_NE(gService, nullptr)`?**

`GTEST_SKIP()` marks the test as SKIPPED (not FAILED). This matters because:
- CI pipelines that treat SKIP and PASS the same will not break when the
  device doesn't have the service running
- The test output is honest: `[  SKIPPED ]` is different from `[  FAILED ]`
- Hardware-optional tests can be skipped without polluting the failure count

## The Divide-By-Zero Error Code Test

```cpp
TEST_F(CalculatorServiceTest, Divide_ByZero_ErrorCode_IsOne) {
    if (!gService) GTEST_SKIP() << "calculatord not running";

    int32_t result = 0;
    auto status = gService->divide(10, 0, &result);

    // The service must throw ServiceSpecificException, not crash
    ASSERT_FALSE(status.isOk());
    ASSERT_EQ(status.getExceptionCode(), EX_SERVICE_SPECIFIC);
    ASSERT_EQ(status.getServiceSpecificError(), 1);  // ERROR_DIVIDE_BY_ZERO = 1
}
```

This test verifies two things simultaneously: that the service doesn't crash
on divide-by-zero, AND that it returns the correct error code constant. If
the service returns `EX_TRANSACTION_FAILED` instead, it crashed — different
failure, different fix.

## The Concurrency Test

```cpp
TEST_F(CalculatorServiceTest, ConcurrentCalls_NoDataRace) {
    if (!gService) GTEST_SKIP() << "calculatord not running";

    constexpr int kThreads = 8;
    constexpr int kCallsPerThread = 100;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < kCallsPerThread; ++i) {
                int32_t result = 0;
                auto s = gService->add(i, i, &result);
                if (!s.isOk() || result != i + i) failures++;
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(0, failures.load())
        << failures.load() << " calls failed under concurrency";
}
```

This test catches thread-safety bugs in the service. An AIDL service runs in
a thread pool — if it has unprotected shared state (like a cached HAL object),
concurrent calls will produce wrong results or crash. The `std::atomic<int>`
counter avoids any data race in the test itself.

## BMI Service — Floating Point Testing

The BMI service requires `EXPECT_NEAR` instead of `EXPECT_EQ` because floating
point arithmetic across IPC is not exactly reproducible:

```cpp
TEST_F(BmiServiceTest, Calculate_NormalAdultMale) {
    if (!gService) GTEST_SKIP() << "bmid not running";

    // BMI = weight(kg) / height(m)^2 = 70 / (1.75 * 1.75) = 22.857...
    float result = 0.0f;
    ASSERT_BINDER_OK(gService->calculateBmi(70.0f, 1.75f, &result));
    EXPECT_NEAR(22.857f, result, 0.01f);  // delta = 0.01 = acceptable float error
}
```

`EXPECT_NEAR(expected, actual, delta)` passes if `|expected - actual| < delta`.
Always use this for floating point binder calls. `EXPECT_EQ` will randomly fail
due to floating point representation differences between the test process and
the service process.

---

<a name="part-5"></a>
# Part 5: VTS for the Thermal Control Service — VINTF Awareness

## Why This Test Is Different

The `IThermalControlService` is `@VintfStability`. This makes its VTS test
fundamentally different from the calculator and BMI tests in three ways:

1. **The service name format changes**: `package.Interface/instance` not just `package.Interface`
2. **The VINTF manifest must be checked**, not just the service availability
3. **The test module needs both `"vts"` and `"general-tests"`** in `test_suites`

```
// vendor/myoem/services/thermalcontrol/tests/Android.bp
cc_test {
    name: "VtsThermalControlServiceTest",
    vendor: true,
    srcs: ["VtsThermalControlServiceTest.cpp"],
    shared_libs: ["libbinder_ndk", "liblog"],
    static_libs: ["thermalcontrolservice-aidl-V1-ndk"],  // note V1 suffix
    test_suites: ["vts", "general-tests"],               // both suites
    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

**Why `"general-tests"` in addition to `"vts"`?**

The `atest` tool and the Android test harness use test suite membership to
route tests. `"vts"` is the official Treble compliance suite. `"general-tests"`
is a broader bucket that `atest` can also pick up. Including both ensures the
test is runnable with `atest` during development and also appears in formal
VTS runs.

**Why `thermalcontrolservice-aidl-V1-ndk` with the V1 suffix?**

This is caused by the `stability: "vintf"` declaration in the AIDL module. When
`unstable: true`, the module generates:
- `thermalcontrolservice-aidl-ndk`
- `thermalcontrolservice-aidl-java`

When `stability: "vintf"` with `versions_with_info: [{ version: "1", ... }]`,
it generates:
- `thermalcontrolservice-aidl-V1-ndk`
- `thermalcontrolservice-aidl-V1-java`

This is a common confusion point — if you upgrade from `unstable` to `vintf`,
every `Android.bp` that references the AIDL module must be updated.

## The Service Name Format

```cpp
// For unstable AIDL:
static constexpr const char* kServiceName =
    "com.myoem.thermalcontrol.IThermalControlService";

// For VINTF AIDL — notice the /default suffix
static constexpr const char* kServiceName =
    "com.myoem.thermalcontrol.IThermalControlService/default";
```

The `/default` is the instance name. VINTF AIDL service names always use the
format `<package>.<Interface>/<instance>`. This must match:
- What `thermalcontrold` passes to `AServiceManager_addService()`
- What the test passes to `AServiceManager_waitForService()`
- The `service_contexts` SELinux file
- The `SERVICE_NAME` constant in `ThermalControlManager.java`

## Section A: VINTF Manifest Test

```cpp
// This test checks whether the VINTF manifest fragment is installed
TEST_F(ThermalControlServiceTest, VintfManifest_ServiceDeclared) {
    ASSERT_TRUE(AServiceManager_isDeclared(kServiceName))
        << "Service not declared in VINTF manifest.\n"
        << "Did you build and flash thermalcontrol-vintf-fragment?\n"
        << "Check: adb shell cat /vendor/etc/vintf/manifest/thermalcontrol.xml";
}
```

`AServiceManager_isDeclared()` reads the device's VINTF manifest (which
aggregates `/vendor/etc/vintf/manifest/*.xml`) and returns true if the service
appears there. This test FAILS if:
- The `vintf_fragment` module wasn't included in `PRODUCT_PACKAGES`
- The vendor image hasn't been reflashed since the fragment was added
- The XML is malformed

## Hardware-Dependent Tests — Graceful Degradation

The fan write tests need actual hwmon hardware:

```cpp
TEST_F(ThermalControlServiceTest, SetFanSpeed_50_ReadsBack50) {
    if (!gService) GTEST_SKIP() << "Service not running";

    auto status = gService->setFanSpeed(50);

    // If hardware is absent, the service returns ERROR_SYSFS_WRITE
    if (status.getExceptionCode() == EX_SERVICE_SPECIFIC &&
        status.getServiceSpecificError() ==
            IThermalControlService::ERROR_SYSFS_WRITE) {
        GTEST_SKIP() << "Fan hardware not available on this device";
    }

    ASSERT_BINDER_OK(status);

    int32_t readBack = 0;
    ASSERT_BINDER_OK(gService->getFanSpeedPercent(&readBack));
    EXPECT_NEAR(50, readBack, 2);  // ±2% tolerance for PWM rounding
}
```

This is the "graceful degradation" pattern:
- First check if the service ran at all
- Then check if the specific operation failed due to absent hardware
- Only skip on expected hardware-absence errors, not on unexpected failures

---

<a name="part-6"></a>
# Part 6: VTS for the HAL Library — Direct C++ Testing

## Why Test the HAL Separately?

The HAL (`libthermalcontrolhal.so`) can be tested without the binder service.
This is important because:
- It isolates HAL bugs from service bugs
- It can run without the service being registered
- It tests the PWM math directly

## The Android.bp

```
// vendor/myoem/hal/thermalcontrol/tests/Android.bp
cc_test {
    name: "VtsThermalControlHalTest",
    vendor: true,
    srcs: ["VtsThermalControlHalTest.cpp"],
    shared_libs: ["libthermalcontrolhal", "liblog"],
    // No AIDL stubs needed — we call the C++ HAL directly
    test_suites: ["vts", "general-tests"],
}
```

Note: `libthermalcontrolhal` is in `shared_libs`, not `static_libs`. The HAL
is a shared library (`cc_library_shared`), so it's linked dynamically.

## TearDown — Restoring Safe State

```cpp
class ThermalControlHalTest : public ::testing::Test {
protected:
    std::unique_ptr<IThermalControlHal> mHal;

    void SetUp() override {
        mHal = createThermalControlHal();
        ASSERT_NE(mHal, nullptr);
    }

    void TearDown() override {
        // CRITICAL: always restore auto mode after each test.
        // If a test sets manual mode and then fails, subsequent tests
        // and the kernel thermal governor would be stuck in manual mode.
        if (mHal) {
            mHal->setAutoMode(true);
        }
    }
};
```

**Why is `TearDown()` critical here?** Without it, if `SetFanSpeed_50_Manual`
writes `pwm1_enable = 1` (manual) and then fails midway, the kernel thermal
governor is permanently disabled for that boot. The fan won't respond to
temperature changes. Every subsequent test that reads fan state gets stale
data. The `TearDown()` is a safety net that ensures each test starts from a
known state.

## The PWM Math Test

```cpp
TEST_F(ThermalControlHalTest, SetFanSpeed_50_ReadBackApprox50) {
    // If hardware is absent, setFanSpeed returns false
    if (!mHal->setFanSpeed(50)) {
        GTEST_SKIP() << "Fan hardware not available";
    }

    int32_t readBack = mHal->getFanSpeedPercent();

    // PWM math: write 50% → pwm = (50*255)/100 = 127
    // Read back: percent = (127*100+254)/255 = 49.8 → rounds to 50
    // ±1 tolerance for ceiling/floor rounding in integer arithmetic
    EXPECT_NEAR(50, readBack, 1)
        << "PWM math error: wrote 50%, read back " << readBack << "%";
}
```

The ±1 tolerance accounts for integer truncation. The PWM conversion is:
- Write: `pwm = (percent × 255) / 100` → 50% → 127 PWM
- Read: `percent = (pwm × 100 + 254) / 255` → 127 PWM → 50%

The ceiling division `(pwm × 100 + 254) / 255` is deliberate — it prevents
reporting 0% when the fan is actually spinning at PWM=1.

## Hardware Detection Pattern

```cpp
TEST_F(ThermalControlHalTest, SetFanEnabled_TurnOn) {
    bool success = mHal->setFanEnabled(true);

    if (!success) {
        // setFanEnabled returns false when hwmon is absent
        GTEST_SKIP() << "Fan hardware not available (hwmon path not found)";
    }

    // If we get here, hardware is present — verify the state changed
    EXPECT_TRUE(mHal->isFanRunning());
}
```

The HAL's `setFanEnabled()` returns `false` specifically when the sysfs write
fails, which happens when `/sys/class/hwmon/hwmon*/pwm1` doesn't exist. We
use this return value to detect hardware absence without any magic constants.

---

<a name="part-7"></a>
# Part 7: Java Instrumented Tests for ThermalControlManager

## What ThermalControlManagerTest Covers

The Java test covers 6 groups of tests across 39 test methods:

```
Group A — Constructor and isAvailable()          (3 tests)
Group B — Null-binder safe defaults             (8 tests)
Group C — categorizeTemperature() boundaries    (8 tests)
Group D — temperatureColor() boundaries         (5 tests)
Group E — Error code constants                  (3 tests)
Group F — Live service tests (auto-skip)        (12 tests)
```

Groups A–E need **no device connection** — they test pure Java logic with a
`null` binder. Group F needs `thermalcontrold` running.

## Group B: Null-Binder Safe Defaults

```java
// Groups A–E: pure Java logic, no device needed
// ThermalControlManager(null) creates a manager in the "unavailable" state
// Every data method must return a safe sentinel, never throw NullPointerException

@Test
public void nullBinder_getCpuTemperatureCelsius_ReturnsZero() {
    ThermalControlManager manager = new ThermalControlManager(null);
    assertEquals("getCpuTemperatureCelsius() must return 0.0f when service unavailable",
        0.0f, manager.getCpuTemperatureCelsius(), 0.0f);
}

@Test
public void nullBinder_getFanSpeedRpm_ReturnsMinusOne() {
    ThermalControlManager manager = new ThermalControlManager(null);
    assertEquals("getFanSpeedRpm() must return -1 sentinel when service unavailable",
        -1, manager.getFanSpeedRpm());
}

@Test
public void nullBinder_isFanAutoMode_ReturnsTrue() {
    // Safe default: report auto mode = true so UI shows "system is in control"
    ThermalControlManager manager = new ThermalControlManager(null);
    assertTrue("isFanAutoMode() must return true (safe default)",
        manager.isFanAutoMode());
}
```

Why test these? Because `ThermalControlManager` is a manager library — apps
depend on it not throwing exceptions when the service is down. If any method
throws `NullPointerException` instead of returning a sentinel, the whole app
crashes. These tests prevent that regression.

## Group C: Temperature Boundary Tests

```java
// The exact boundary at 50.0°C — is it Cool or Warm?
// This is the most common off-by-one bug in range checks.
@Test
public void categorizeTemp_Boundary50_ReturnsWarm() {
    assertEquals("Warm", ThermalControlManager.categorizeTemperature(50.0f));
}

@Test
public void categorizeTemp_JustBelow50_ReturnsCool() {
    assertEquals("Cool", ThermalControlManager.categorizeTemperature(49.9f));
}

@Test
public void categorizeTemp_Boundary70_ReturnsHot() {
    assertEquals("Hot", ThermalControlManager.categorizeTemperature(70.0f));
}

@Test
public void categorizeTemp_Boundary85_ReturnsCritical() {
    assertEquals("Critical", ThermalControlManager.categorizeTemperature(85.0f));
}
```

The boundary values (`50.0f`, `70.0f`, `85.0f`) are the only values that can
reveal an off-by-one error. Testing `42.5f` and `75.0f` is useful but testing
`50.0f` is *critical*.

## Group F: Live Service Tests with `assumeTrue`

```java
@Test
public void live_getCpuTemperatureCelsius_ReturnsPositiveValue() {
    // assumeTrue() silently SKIPS the test (JUnit Assume) if condition is false.
    // This is different from assertTrue() which FAILS the test.
    assumeTrue("thermalcontrold not running — skipping live test",
        isServiceRunning());

    float temp = manager.getCpuTemperatureCelsius();
    assertTrue("Live temperature must be > 0°C", temp > 0.0f);
    assertTrue("Live temperature must be < 150°C (sanity check)", temp < 150.0f);
}

private boolean isServiceRunning() {
    // Uses @hide ServiceManager.checkService() — allowed because
    // this test module uses platform_apis: true
    return android.os.ServiceManager.checkService(
        ThermalControlManager.SERVICE_NAME) != null;
}
```

**`assumeTrue()` vs `assertTrue()`**: This distinction is important.
- `assertTrue(condition)` FAILS the test if condition is false
- `assumeTrue(condition)` SKIPS the test if condition is false

For tests that need a running service, `assumeTrue` is the right choice. The
test infrastructure reports a skip, not a failure. CI pipelines should not
break when a service happens to not be running on the test device.

---

<a name="part-8"></a>
# Part 8: Build Crisis #1 — CTS Module Cannot Use Platform APIs

## The Error

After creating the first version of `Android.bp` for the ThermalControlManager
tests, the build produced this error:

```
error: vendor/myoem/libs/thermalcontrol/tests/Android.bp:24:1:
"CtsMyOemThermalControlManagerTests" depends on forbidden libs:
platform_apis is true for a module in "cts" test_suites.
```

The original `Android.bp` had:
```
android_test {
    name: "CtsMyOemThermalControlManagerTests",
    platform_apis: true,
    test_suites: ["cts"],        // ← the problem
}
```

## The Root Cause

CTS (Compatibility Test Suite) is Google's public certification suite. By
design, CTS modules **must use only public Android APIs** — no hidden framework
internals. This is enforced at build time.

Our test needed `ServiceManager.checkService()` which is `@hide`. Using any
`@hide` API requires `platform_apis: true`. These two things are mutually
exclusive:

```
platform_apis: true  →  test_suites: ["general-tests"]   (vendor OEM tests)
sdk_version: "..."   →  test_suites: ["cts"]              (public-API-only tests)
```

There is NO way to have both. The build system enforces this rule with a hard
error. There is no flag to bypass it. This is intentional — if CTS allowed
`@hide` APIs, CTS certification would become meaningless.

## The Fix

Change the module name and test suite:

```
android_test {
    name: "MyOemThermalControlManagerTests",   // removed "Cts" prefix
    platform_apis: true,
    test_suites: ["general-tests"],             // changed from "cts"
}
```

**The naming convention**: Google's naming is:
- `CtsXxxTest` → for real CTS modules (public API only)
- `VtsXxxTest` → for VTS modules (vendor C++ / HAL)
- `XxxTests` or `MyOemXxxTests` → for `general-tests` modules

We renamed to `MyOemThermalControlManagerTests` to make it clear this is an
OEM general-test, not a Google CTS test.

## The Key Rule to Memorize

```
IF you call @hide APIs in tests:
    → platform_apis: true
    → test_suites: ["general-tests"]
    → Module name prefix: whatever you want (not "Cts")

IF you only call public APIs in tests:
    → sdk_version: "test_current" or "current"
    → test_suites: ["cts"]
    → Module name prefix: "Cts" (by convention)
```

---

<a name="part-9"></a>
# Part 9: Build Crisis #2 — android_test Panics Without a Manifest

## The Error

After fixing the CTS error, a new one appeared:

```
FAILED: out/soong/.intermediates/.../ThermalControlManagerTests/.../aapt2
error: vendor/myoem/libs/thermalcontrol/tests/Android.bp:24:1:
module source path AndroidManifest.xml does not exist for module
"MyOemThermalControlManagerTests"

fatal error: nil pointer dereference
    Certificate.AndroidMkString()
    ...soong/java/app.go:...
```

A `nil pointer dereference` in the Soong build system itself — not in our code,
but in Soong's APK builder.

## Why This Happens

There is a fundamental difference between `cc_test` and `android_test`:

| Module type | What it builds | Needs manifest? |
|-------------|---------------|-----------------|
| `cc_test` | ELF binary | No |
| `android_test` | Real APK (`.apk` file) | **Yes, mandatory** |

`android_test` calls AAPT2 (the Android Asset Packaging Tool) to create an APK.
AAPT2 requires an `AndroidManifest.xml` to know the package name, SDK version,
and instrumentation runner. Without it, AAPT2 returns null, and Soong's APK
signing code panics on the null `Certificate`.

This was not immediately obvious because `cc_test` modules don't need manifests
at all. The error message was misleading — it looked like a source path error,
but the real crash was in Soong's signing code.

## The Fix: Create the Manifest and Declare It

**Step 1**: Create `vendor/myoem/libs/thermalcontrol/tests/AndroidManifest.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<manifest
    xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.myoem.thermalcontrol.test">

    <uses-sdk
        android:minSdkVersion="21"
        android:targetSdkVersion="35" />

    <!--
        android:targetPackage must match this manifest's own package because
        the test APK is self-contained — no separate app is being instrumented.
    -->
    <instrumentation
        android:name="androidx.test.runner.AndroidJUnitRunner"
        android:targetPackage="com.myoem.thermalcontrol.test"
        android:label="ThermalControlManager Tests" />

    <application android:label="ThermalControlManager Tests" />

</manifest>
```

**Step 2**: Declare it explicitly in `Android.bp`:

```
android_test {
    name: "MyOemThermalControlManagerTests",
    manifest: "AndroidManifest.xml",   // ← explicit path required
    vendor: true,
    srcs: ["java/**/*.java"],
    platform_apis: true,
    test_suites: ["general-tests"],
}
```

## Three Things the Manifest Must Get Right

1. **`package`**: unique package name for the test APK. Different from the app
   package. If two APKs with the same package are installed simultaneously,
   one will overwrite the other.

2. **`android:targetPackage`**: for a self-contained test APK (no separate app
   under test), this must match `package`. If it points to a different app,
   that app must be installed and the classloader will look there for test
   classes.

3. **`android:targetSdkVersion`**: must match the AOSP branch. Android 15 is
   API 35. Setting the wrong targetSdkVersion can cause behaviour differences
   in the test runtime.

---

<a name="part-10"></a>
# Part 10: Build Crisis #3 — JUnit Libraries Are Not Pre-installed

## The Error

After creating the manifest, the build failed on the Java compilation:

```
vendor/myoem/libs/thermalcontrol/tests/java/.../ThermalControlManagerTest.java:
error: package org.junit does not exist
error: package org.junit.runner does not exist
error: package androidx.test.ext.junit.runners does not exist
```

## The Root Cause

In a standard Android Gradle project, JUnit is provided by the test framework
automatically. In AOSP, nothing is automatic. Every library must be declared
as a dependency in `Android.bp`.

But there's a subtler issue: **how** you declare the dependency matters.

```
// WRONG — libs are compile-time only, not bundled into the APK
libs: ["junit", "androidx.test.runner"],

// RIGHT — static_libs bundles the .dex code into the APK
static_libs: ["junit", "androidx.test.runner", "androidx.test.ext.junit"],
```

**`libs` vs `static_libs` for test dependencies:**

| Property | Effect | When to use |
|----------|--------|------------|
| `libs` | Compile-time classpath only — not in APK | For framework classes already on device (e.g., `android.jar`) |
| `static_libs` | Bundled into the APK's dex | For libraries that aren't pre-installed on device |

JUnit (`org.junit.*`) is not pre-installed on Android devices. The
`androidx.test.*` libraries are not pre-installed on Android devices. If you
put them in `libs`, the APK compiles but at runtime you get:
```
java.lang.NoClassDefFoundError: org.junit.Test
```

They must be `static_libs`.

## The Fix

```
android_test {
    name: "MyOemThermalControlManagerTests",
    vendor: true,
    manifest: "AndroidManifest.xml",
    srcs: ["java/**/*.java"],

    libs: [
        // The library under test (pre-installed on vendor partition)
        "thermalcontrol-manager",
        // AIDL Java stubs (pre-installed on vendor partition)
        "thermalcontrolservice-aidl-V1-java",
    ],

    // Test framework libraries — NOT pre-installed, must be bundled in APK
    static_libs: [
        "junit",                      // org.junit.Test, Assert, Assume
        "androidx.test.runner",       // AndroidJUnitRunner
        "androidx.test.ext.junit",    // @RunWith(AndroidJUnit4.class)
    ],

    platform_apis: true,
    test_suites: ["general-tests"],
}
```

**Why is `thermalcontrol-manager` in `libs` not `static_libs`?**

`thermalcontrol-manager` is installed on the vendor partition as a Java library
(the `java_library` module builds a JAR that gets installed). When the test APK
runs on the device, the class is already loaded — we just need it on the
compile-time classpath, not bundled again inside the test APK.

Bundling it via `static_libs` would work too, but would produce duplicate classes
warnings and a larger APK. `libs` is the correct choice for libraries that are
guaranteed to be present on the target device.

## After All Three Fixes: Build Success

```bash
m MyOemThermalControlManagerTests

# Output:
[100%] Install: out/target/product/rpi5/testcases/\
  MyOemThermalControlManagerTests/arm64/MyOemThermalControlManagerTests.apk
#### build completed successfully ####
```

---

<a name="part-11"></a>
# Part 11: Kotlin ViewModel Tests — ThermalViewModelTest

## What It Tests

`ThermalViewModelTest` tests `ThermalViewModel` — the Compose ViewModel that
polls the thermal service and exposes `StateFlow<UiState>`. There are 4 sections:

```
Section A — Initial state with service unavailable     (6 tests)
Section B — fetchData() with a mocked available manager (6 tests)
Section C — Fan control action methods                  (6 tests)
Section D — Auto-refresh polling loop                   (1 test)
```

Total: 19 tests, all JVM-style (isolated, no device needed for sections A–C).

## Why Not a `java_test_host`?

The first instinct was to use `java_test_host` for host-JVM tests. This would
let tests run without a device. But there's a problem:

`ThermalControlManager` takes an `android.os.IBinder` in its constructor.
`android.os.IBinder` is an Android framework class — it doesn't exist on the
host JVM. Even `ThermalControlManager(null)` references the type `IBinder` at
the class level.

**The verdict**: Any test that references `ThermalControlManager` must run on
device as `android_test`. We call them "unit tests" in spirit (isolated,
Mockito-controlled), but they execute on the Android runtime.

## The Standard TestDispatcher Pattern

```kotlin
@OptIn(ExperimentalCoroutinesApi::class)
class ThermalViewModelTest {

    private val testDispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        // Replace the Main dispatcher globally so all ViewModel coroutines
        // use testDispatcher instead of the real Main dispatcher.
        // This lets us control coroutine execution with advanceUntilIdle().
        Dispatchers.setMain(testDispatcher)
    }

    @After
    fun tearDown() {
        // ALWAYS reset after each test — not resetting leaks dispatcher state
        // into subsequent tests and causes flaky failures.
        Dispatchers.resetMain()
    }
```

**Why `StandardTestDispatcher` instead of `UnconfinedTestDispatcher`?**

- `UnconfinedTestDispatcher`: coroutines run eagerly, immediately when launched.
  Good for simple tests, but makes coroutine ordering non-deterministic.
- `StandardTestDispatcher`: coroutines are queued and only run when you call
  `advanceUntilIdle()` or `advanceTimeBy()`. This gives deterministic control.

For `ThermalViewModel` which has a `while(true) { delay(2_000) }` polling loop,
`StandardTestDispatcher` is essential — `UnconfinedTestDispatcher` would cause
the loop to run forever.

## The Mockito Helper

```kotlin
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
    whenever(mock.setFanSpeed(anyInt())).thenReturn(true)
    return mock
}
```

This factory creates a fully stubbed `ThermalControlManager` that returns
whatever values you want. Using a factory with default parameters means most
tests only need to override one value:
```kotlin
val manager = mockAvailableManager(temp = 90.0f)  // only override temp
```

**Important**: This uses `mockito-target`, not `mockito-core`. In AOSP:
- `mockito-core` is for host JVM tests (`java_test_host`)
- `mockito-target` is for device tests (`android_test`) — uses dexmaker
  to generate mock classes at runtime on the Dalvik/ART bytecode level

## The Polling Loop Test

```kotlin
@Test
fun autoRefresh_After2000ms_StateUpdatesAgain() = runTest {
    val manager = mockAvailableManager(temp = 40.0f)
    val viewModel = ThermalViewModel(manager)

    // First poll fires immediately
    testDispatcher.scheduler.advanceUntilIdle()
    assertEquals(40.0f, viewModel.uiState.value.cpuTempCelsius, 0.01f)

    // Change what the mock returns for the second poll
    whenever(manager.getCpuTemperatureCelsius()).thenReturn(55.0f)

    // Jump the virtual clock forward 2001ms — triggers the 2-second delay
    advanceTimeBy(2001L)
    testDispatcher.scheduler.advanceUntilIdle()

    assertEquals(55.0f, viewModel.uiState.value.cpuTempCelsius, 0.01f)
}
```

This test is elegant because:
- No real `Thread.sleep(2000)` — tests run in milliseconds
- The `while(true) { fetchData(); delay(2_000) }` loop is controlled by
  virtual time — `advanceTimeBy(2001L)` fires the delay without real waiting
- We can verify the polling behavior in a deterministic, fast way

## The `open class` Requirement

One issue that surfaced during the AOSP build (not during Gradle/Android Studio
builds): `ThermalViewModel` was declared as a regular Kotlin `class`. In Kotlin,
all classes are `final` by default. The `ThermalViewModelTest` depends on
Mockito being able to create a subclass of `ThermalControlManager`. In Android
Studio with Gradle, `mockito-inline` is included automatically — it handles
final classes via bytecode instrumentation. In AOSP's `mockito-target`, this
support is not enabled by default.

The fix: make `ThermalControlManager` mockable by making key parts `open`.
(More on this in Part 13.)

---

<a name="part-12"></a>
# Part 12: Kotlin Compose UI Tests — ThermalScreenTest

## What It Tests

`ThermalScreenTest` tests `ThermalScreen` — the top-level Compose function.
There are 6 sections across 26 tests:

```
Section A — Service unavailable UI                    (2 tests)
Section B — Service available data display            (6 tests)
Section C — Fan control buttons (3 buttons)           (6 tests)
Section D — Apply button and text field               (2 tests)
Section E — AUTO badge in app bar                     (2 tests)
Section F — State update propagation (StateFlow)      (2 tests)
```

## Why Not Mockito for the ViewModel?

The test uses a `FakeThermalViewModel` (a hand-written test double), not a
Mockito mock. This is intentional:

```kotlin
private class FakeThermalViewModel : ThermalViewModel(
    ThermalControlManager(null)
) {
    // Override with a controllable MutableStateFlow
    private val _fakeState = MutableStateFlow(UiState())
    override val uiState get() = _fakeState

    // Expose setter so tests can push any state
    fun setState(state: UiState) { _fakeState.value = state }

    // Track method calls for assertion
    var turnFanOnCalled = false
    var turnFanOffCalled = false
    var setAutoModeCalled = false
    var lastSetFanSpeed: Int? = null

    override fun turnFanOn()             { turnFanOnCalled = true }
    override fun turnFanOff()            { turnFanOffCalled = true }
    override fun setFanSpeed(pct: Int)   { lastSetFanSpeed = pct }
    override fun setAutoMode()           { setAutoModeCalled = true }
}
```

**Why a fake instead of a Mockito mock?**

Compose observes `StateFlow` directly via `collectAsState()`. If you stub
`uiState` with Mockito to return a `StateFlow`, Mockito can return the value
but it won't trigger Compose recomposition when the flow emits. The fake
overrides `uiState` as an actual `MutableStateFlow`, which Compose can observe
properly. When the test calls `viewModel.setState(newState)`, Compose
recomposes the UI exactly as it would in production.

## The ComposeTestRule Pattern

```kotlin
@get:Rule
val composeTestRule = createComposeRule()

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
```

`createComposeRule()` starts a minimal Activity inside the test APK. There is
no need to install the production `ThermalMonitor.apk` first — the Compose
runtime is bundled in the test APK via `ui-test-manifest` and `ui-test-junit4`.

## Button Click Tests

```kotlin
@Test
fun fanControlCard_TurnOnButton_WhenClicked_CallsViewModel() {
    val viewModel = launchScreen(UiState(serviceAvailable = true))

    composeTestRule
        .onNodeWithText("Turn On", substring = true)
        .performClick()

    assertTrue("turnFanOn() should have been called after clicking Turn On",
        viewModel.turnFanOnCalled)
}
```

This test uses `performClick()` to simulate a real user tap. The `substring = true`
means the button text just needs to *contain* "Turn On", not be exactly "Turn On"
— useful when string resources add padding or prefixes.

## State Propagation Test

```kotlin
@Test
fun stateUpdate_ServiceBecomesAvailable_ErrorCardHides() {
    val viewModel = FakeThermalViewModel()
    composeTestRule.setContent {
        ThermalMonitorTheme { ThermalScreen(viewModel = viewModel) }
    }

    // Start: service unavailable
    viewModel.setState(UiState(serviceAvailable = false,
        errorMessage = "thermalcontrold not running"))
    composeTestRule.waitForIdle()

    composeTestRule
        .onNodeWithText("Service Unavailable", substring = true)
        .assertIsDisplayed()

    // Simulate service coming online
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
```

`composeTestRule.waitForIdle()` is essential after `setState()`. It waits for
all pending Compose recompositions to finish. Without it, the assertions run
before Compose has redrawn the UI and you get false failures.

---

<a name="part-13"></a>
# Part 13: Build Crisis #4 — Wiring Up Kotlin Tests in AOSP

This was the most complex build debugging session of the entire test series.
Four separate layers of problems had to be solved in sequence.

## Problem 1: `kotlinx-coroutines-test` Not Found

**Error:**
```
error: vendor/myoem/apps/ThermalMonitor/Android.bp:70:1:
"ThermalMonitorUnitTests" depends on undefined module "kotlinx-coroutines-test".
Or did you mean ["kotlinx_coroutines_test"]?
```

**Root cause**: AOSP module names use underscores for some libraries, not
hyphens. The Soong build system is case- and separator-sensitive.

**Rule**: Always verify AOSP module names with `grep` before writing Android.bp:
```bash
grep -r '"kotlinx' prebuilts/misc/common/ 2>/dev/null | grep -i coroutine
# Shows: "kotlinx_coroutines_test"
```

**Fix**: `"kotlinx-coroutines-test"` → `"kotlinx_coroutines_test"`

## Problem 2: `ThermalViewModel` Not Found in Test Sources

**Error:**
```
error: unresolved reference: ThermalViewModel
```

**Root cause**: AOSP's Soong build system does not have Gradle's concept of
"test sourceSet inherits main sourceSet". In Gradle:
- `src/main/kotlin/` is automatically on the classpath when compiling
  `src/test/kotlin/`
- You declare test sources separately but get production classes for free

In Soong/AOSP, **there is no such inheritance**. Each `android_test` module is
completely independent. The test module's `srcs` must explicitly include
everything it needs.

**First attempt**: Add `"src/main/kotlin/**/*.kt"` to the test module's `srcs`.
This compiled the production classes inline. But then the next problem appeared.

## Problem 3: R.string.* References Fail (The R Class Package Mismatch)

**Error:**
```
error: unresolved reference: service_unavailable
        text = stringResource(R.string.service_unavailable)
                              ^
```

**Root cause**: When you include `resource_dirs: ["src/main/res"]` in a test
module with package name `com.myoem.thermalmonitor.test.unit`, AAPT2 generates
an `R` class under `com.myoem.thermalmonitor.test.unit.R`. But the production
Kotlin code in `ThermalScreen.kt` references `R.string.service_unavailable`
which resolves to `com.myoem.thermalmonitor.R`. The packages don't match.

In Gradle, this is handled cleanly: `applicationId` (the APK package) is
separate from `namespace` (the R class package). You can have:
- APK package: `com.myoem.thermalmonitor.test.unit`
- R class: `com.myoem.thermalmonitor`

Soong doesn't expose this split cleanly. The R class is generated from the
manifest's `package` attribute.

**The correct architectural fix**: Extract production code into an `android_library`.

## The `android_library` Solution

```
// vendor/myoem/apps/ThermalMonitor/Android.bp

android_library {
    name: "ThermalMonitorLib",
    srcs: ["src/main/kotlin/**/*.kt"],
    resource_dirs: ["src/main/res"],
    manifest: "AndroidManifest.xml",    // ← key: uses the APP's manifest
    static_libs: [
        // all production deps...
        "thermalcontrol-manager",
        "thermalcontrolservice-aidl-V1-java",
        "kotlin-stdlib",
        "kotlinx-coroutines-android",
        "androidx.core_core-ktx",
        "androidx.activity_activity-compose",
        "androidx.compose.runtime_runtime",
        "androidx.compose.ui_ui",
        "androidx.compose.foundation_foundation",
        "androidx.compose.material3_material3",
        "androidx.lifecycle_lifecycle-viewmodel-ktx",
        "androidx.lifecycle_lifecycle-viewmodel-compose",
        "androidx.lifecycle_lifecycle-runtime-compose",
    ],
    platform_apis: true,
}

android_app {
    name: "ThermalMonitor",
    platform_apis: true,
    privileged: true,
    certificate: "platform",
    manifest: "AndroidManifest.xml",
    static_libs: ["ThermalMonitorLib"],   // ← depends on library
    optimize: { enabled: false },
}

android_test {
    name: "ThermalMonitorUnitTests",
    vendor: true,
    manifest: "src/test/AndroidManifest.xml",
    srcs: ["src/test/kotlin/**/*.kt"],
    static_libs: [
        "ThermalMonitorLib",             // ← library brings all production classes + R
        "kotlinx_coroutines_test",
        "mockito-target",
        "junit",
        "androidx.test.runner",
        "androidx.test.ext.junit",
    ],
    platform_apis: true,
    test_suites: ["general-tests"],
}
```

**Why this works**: `ThermalMonitorLib` uses `AndroidManifest.xml` (the app's
manifest, with `package="com.myoem.thermalmonitor"`). AAPT2 generates the R
class under `com.myoem.thermalmonitor.R`. When the test module depends on
`ThermalMonitorLib`, it gets that R class on its classpath. The production
Kotlin code's `R.string.*` references resolve correctly.

## Problem 4: Manifest `application@label` Merge Conflict

**Error:**
```
Error: Attribute application@label value=(ThermalMonitor Unit Tests)
is also present at AndroidManifest.xml:10:18-50 value=(@string/app_name).
Suggestion: add 'tools:replace="android:label"' to <application> element
```

**Root cause**: The manifest merger merges the test manifest with all
dependency manifests (including `ThermalMonitorLib`'s manifest, which has
`android:label="@string/app_name"`). Two different `android:label` values
for `<application>` is a merge conflict.

**Fix**: Tell the merger that the test manifest's label wins:

```xml
<!-- src/test/AndroidManifest.xml -->
<manifest
    xmlns:android="http://schemas.android.com/apk/res/android"
    xmlns:tools="http://schemas.android.com/tools"   <!-- ← add tools namespace -->
    package="com.myoem.thermalmonitor.test.unit">

    ...
    <application
        android:label="ThermalMonitor Unit Tests"
        tools:replace="android:label" />     <!-- ← explicit override -->
</manifest>
```

`tools:replace="android:label"` is a manifest merger instruction: "use this
value from my manifest, ignore the same attribute from merged-in libraries".

## Problem 5: `ThermalViewModel` Is Not `open`

**Error:**
```
error: type mismatch: inferred type is FakeThermalViewModel
  but ThermalViewModel was expected
```

**Root cause**: In Kotlin, all classes are `final` by default. When
`FakeThermalViewModel` tried to extend `ThermalViewModel`, the Kotlin compiler
rejected it — you cannot subclass a `final` class.

In Android Studio with Gradle, `mockito-inline` (enabled by default) handles
this transparently via bytecode manipulation — it can mock and subclass even
`final` classes. In AOSP's `mockito-target`, this capability is not enabled
by default.

**The lesson**: AOSP's test toolchain is stricter than Android Studio's.
When you write code that needs to be testable via inheritance in AOSP, declare
it `open`.

**Fix** in `ThermalViewModel.kt`:
```kotlin
// Before:
class ThermalViewModel(private val manager: ThermalControlManager) : ViewModel() {
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()
    fun turnFanOn() { ... }
    fun turnFanOff() { ... }
    fun setFanSpeed(percent: Int) { ... }
    fun setAutoMode() { ... }

// After:
open class ThermalViewModel(private val manager: ThermalControlManager) : ViewModel() {
    open val uiState: StateFlow<UiState> = _uiState.asStateFlow()
    open fun turnFanOn() { ... }
    open fun turnFanOff() { ... }
    open fun setFanSpeed(percent: Int) { ... }
    open fun setAutoMode() { ... }
```

Each method and property that `FakeThermalViewModel` overrides must be `open`.

## Problem 6: Missing `assertTrue`/`assertEquals` Imports

**Error:**
```
error: unresolved reference: assertTrue
error: unresolved reference: assertEquals
```

In `ThermalScreenTest.kt`, `assertTrue` and `assertEquals` were called without
imports. In Android Studio's Gradle project, these resolve automatically
because Android Studio indexes the JUnit JAR. In Soong, every import must be
explicit.

**Fix**: Add to `ThermalScreenTest.kt`:
```kotlin
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
```

## Problem 7: Java Annotation Syntax in Kotlin File

**Error:**
```
error: Expecting ')'
@RunWith(AndroidJUnit4.class)
                       ^
```

The original test file had:
```kotlin
@RunWith(AndroidJUnit4.class)  // Java syntax
```

In Kotlin, the class literal syntax is different:
```kotlin
@RunWith(AndroidJUnit4::class)  // Kotlin syntax
```

This is a Java → Kotlin migration mistake. `.class` is Java. `::class` is Kotlin.

## Problem 8: `assertDoesNotExist` Not in AOSP Prebuilt

**Error:**
```
error: unresolved reference: assertDoesNotExist
import androidx.compose.ui.test.assertDoesNotExist
                                ^
```

`assertDoesNotExist()` is a standard Compose test API — available in Maven
Central from `androidx.compose.ui:ui-test:1.1.0+`. But the AOSP prebuilt
`ui-test-android-1.8.0-alpha02.aar` bundled in
`prebuilts/sdk/current/androidx/m2repository/` does not include it as a
top-level extension function.

**How to verify**: Unpack the AAR and check the class file:
```bash
unzip -p prebuilts/sdk/current/androidx/m2repository/\
  androidx/compose/ui/ui-test-android/1.8.0-alpha02/\
  ui-test-android-1.8.0-alpha02.aar classes.jar > /tmp/test.jar

python3 -c "
import zipfile
with zipfile.ZipFile('/tmp/test.jar') as z:
    print([e for e in z.namelist() if 'AssertionsKt' in e])
"
# AssertionsKt.class exists, but no doesNotExist in it
```

**The alternative**: `onAllNodesWithText(...).assertCountEquals(0)` achieves
the same semantics — if zero nodes match the query, the assertion passes.

```kotlin
// Before (doesn't compile in AOSP):
composeTestRule
    .onNodeWithText("AUTO")
    .assertDoesNotExist()

// After (works in AOSP):
composeTestRule
    .onAllNodesWithText("AUTO")
    .assertCountEquals(0)
```

## Final Build Success

After all 8 fixes:

```bash
m ThermalMonitorUnitTests ThermalMonitorInstrumentedTests

# Output:
[ 68% 13/19] Install: out/target/product/rpi5/testcases/\
  ThermalMonitorUnitTests/arm64/ThermalMonitorUnitTests.apk
[100% 19/19] Install: out/target/product/rpi5/testcases/\
  ThermalMonitorInstrumentedTests/arm64/ThermalMonitorInstrumentedTests.apk
#### build completed successfully (16 seconds) ####
```

## Complete Error-to-Fix Summary Table

| # | Error Message | Root Cause | Fix |
|---|--------------|------------|-----|
| 1 | `CtsXxx cannot depend on framework` | `test_suites:["cts"]` + `platform_apis:true` | Remove `cts`, use `general-tests` |
| 2 | `AndroidManifest.xml does not exist` + nil panic | `android_test` builds APK, needs manifest | Create manifest + add `manifest:` to bp |
| 3 | `package org.junit does not exist` | JUnit not pre-installed on device | Add `junit`, `androidx.test.*` to `static_libs` |
| 4 | `kotlinx-coroutines-test` undefined | AOSP uses underscores | `kotlinx_coroutines_test` |
| 5 | `unresolved reference: ThermalViewModel` | No sourceSet inheritance in Soong | Refactor to `android_library` |
| 6 | `R.string.* unresolved` | Test APK has different package → wrong R class | `android_library` generates R under correct package |
| 7 | `application@label` merge conflict | Library and test both set `android:label` | `tools:replace="android:label"` in test manifest |
| 8 | `type mismatch: FakeThermalViewModel` | Kotlin classes are `final`, AOSP lacks mockito-inline | Add `open` to class + methods |
| 9 | `unresolved reference: assertTrue` | Missing JUnit Assert imports in Kotlin file | `import org.junit.Assert.assertTrue` etc. |
| 10 | `@RunWith(AndroidJUnit4.class)` syntax error | Java class literal syntax in Kotlin file | `@RunWith(AndroidJUnit4::class)` |
| 11 | `unresolved reference: assertDoesNotExist` | Not in AOSP prebuilt AAR | Use `onAllNodesWithText().assertCountEquals(0)` |

---

<a name="part-14"></a>
# Part 14: How to Run Every Test — The Complete Execution Guide

## Prerequisites

```bash
# 1. Source the AOSP build environment
source build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug

# 2. Connect the device
adb devices
# Should show: 192.168.x.x:5555  device  (or USB serial)

# 3. Build all test modules
m VtsCalculatorServiceTest \
  VtsBMIServiceTest \
  VtsThermalControlServiceTest \
  VtsThermalControlHalTest \
  MyOemThermalControlManagerTests \
  ThermalMonitorUnitTests \
  ThermalMonitorInstrumentedTests
```

## Running with `atest`

`atest` is the recommended approach. It automatically:
- Finds the test APK/binary in the build output
- Pushes it to the device
- Executes it with the right runner
- Pulls results back
- Shows pass/fail/skip summary with colors

```bash
# Run a single module
atest VtsCalculatorServiceTest

# Run a specific test within a module
atest VtsCalculatorServiceTest:CalculatorServiceTest#Add_TwoPositives

# Run all divide tests (gtest filter)
atest VtsCalculatorServiceTest -- --gtest_filter="*Divide*"

# Run all vendor/myoem tests at once
atest VtsCalculatorServiceTest VtsBMIServiceTest \
      VtsThermalControlServiceTest VtsThermalControlHalTest \
      MyOemThermalControlManagerTests \
      ThermalMonitorUnitTests ThermalMonitorInstrumentedTests

# Run only manager tests that don't need a running service
atest MyOemThermalControlManagerTests -- \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#constructor*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#categorize*"
```

## Reading atest Output

```
# VTS test passing:
[ RUN      ] ThermalControlServiceTest.VintfManifest_ServiceDeclared
[       OK ] ThermalControlServiceTest.VintfManifest_ServiceDeclared (3 ms)

# VTS test skipping (hardware absent — correct behaviour):
[ RUN      ] ThermalControlHalTest.SetFanSpeed_50_ReadBackApprox50
[  SKIPPED ] ThermalControlHalTest.SetFanSpeed_50_ReadBackApprox50 (0 ms)
  Reason: Fan hardware not available

# Java test failing:
[ RUN      ] ThermalControlManagerTest#categorizeTemp_Boundary50_ReturnsWarm
[  FAILED  ] categorizeTemp_Boundary50_ReturnsWarm
  Expected: Warm
  Actual  : Cool
```

**`SKIPPED` is not a failure.** Hardware-dependent tests are designed to skip
when the hardware isn't available. Only `FAILED` is a failure.

## Manual Execution (Without atest)

Sometimes `atest` isn't available (e.g., first-time setup, restricted CI).

### VTS C++ tests manually:
```bash
# Push binary
adb push out/target/product/rpi5/vendor/bin/VtsCalculatorServiceTest \
         /data/local/tmp/

# Make executable and run as root
adb shell chmod +x /data/local/tmp/VtsCalculatorServiceTest
adb shell "su root /data/local/tmp/VtsCalculatorServiceTest"

# With gtest filter
adb shell "su root /data/local/tmp/VtsCalculatorServiceTest \
           --gtest_filter=*Divide*"

# With XML output for CI
adb shell "su root /data/local/tmp/VtsCalculatorServiceTest \
           --gtest_output=xml:/data/local/tmp/results.xml"
adb pull /data/local/tmp/results.xml .
```

### Java/Kotlin tests manually:
```bash
# Install the APK
adb install -r out/target/product/rpi5/testcases/\
  MyOemThermalControlManagerTests/arm64/MyOemThermalControlManagerTests.apk

# Run all tests
adb shell am instrument -w \
  com.myoem.thermalcontrol.test/androidx.test.runner.AndroidJUnitRunner

# Run a specific method
adb shell am instrument -w \
  -e class "com.myoem.thermalcontrol.ThermalControlManagerTest\
#categorizeTemp_Boundary50_ReturnsWarm" \
  com.myoem.thermalcontrol.test/androidx.test.runner.AndroidJUnitRunner
```

## Pre-test Verification Checklist

Before running any test, verify the environment:

```bash
# 1. Check services are running
adb shell service list | grep -E "calculator|bmi|thermalcontrol"

# 2. Check VINTF manifest (for thermal service tests)
adb shell cat /vendor/etc/vintf/manifest/thermalcontrol.xml

# 3. Check fan hardware (for HAL and service write tests)
adb shell ls /sys/class/hwmon/
adb shell cat /sys/class/hwmon/hwmon0/name  # look for "pwm-fan"

# 4. Check sysfs permissions (for fan write tests)
adb shell ls -la /sys/class/hwmon/hwmon2/pwm1
# Should be: -rw-rw-r-- root system ...

# 5. Verify service domain (for SELinux correctness)
adb shell ps -eZ | grep thermalcontrold
# Should be: u:r:thermalcontrold:s0  system  <pid>
```

## CI Integration Script

```bash
#!/bin/bash
set -e  # exit on first failure

echo "=== Building test modules ==="
m VtsCalculatorServiceTest VtsBMIServiceTest \
  VtsThermalControlServiceTest VtsThermalControlHalTest \
  MyOemThermalControlManagerTests \
  ThermalMonitorUnitTests ThermalMonitorInstrumentedTests

echo "=== Running VTS tests ==="
atest VtsCalculatorServiceTest VtsBMIServiceTest \
      VtsThermalControlServiceTest VtsThermalControlHalTest

echo "=== Running manager tests ==="
atest MyOemThermalControlManagerTests

echo "=== Running Kotlin app tests ==="
atest ThermalMonitorUnitTests ThermalMonitorInstrumentedTests

echo "=== All tests passed ==="
```

---

<a name="part-15"></a>
# Part 15: Google CTS/VTS Certification, GMS, and MADA Explained

## What Are CTS and VTS, Officially?

Google maintains two official test suites as part of the Android Compatibility
Program:

### CTS — Compatibility Test Suite

CTS tests that an Android build correctly implements the Android API surface
as documented. It tests:
- Java API behaviour (does `String.format()` work correctly?)
- System behaviour (does the Activity lifecycle fire in the right order?)
- Security requirements (are dangerous permissions gated correctly?)
- UI/hardware requirements (does the touch screen support multi-touch?)

CTS runs are what determine whether a device is "Android compatible". **You
must pass CTS to be allowed to call your device "Android"** (technically, to
use the Android trademark and logo).

### VTS — Vendor Test Suite

VTS tests the vendor partition's compliance with the Treble contract. It tests:
- HAL interface contracts (does your camera HAL implement all required methods?)
- AIDL/HIDL stability (are your VINTF-declared interfaces backward compatible?)
- SELinux policy correctness for vendor code

VTS is particularly important because Treble's whole purpose is to allow Android
system updates without updating the vendor partition. VTS verifies that the
vendor partition honors that contract.

## How Does Google Know Your Build Passed?

You run CTS yourself. The process is:

```
1. Download the CTS test package from source.android.com
   → https://source.android.com/docs/compatibility/cts/downloads

2. Run CTS against your device:
   java -jar cts-tradefed.jar run cts --plan CTS

3. CTS generates an XML results report

4. You upload the report to the Android Partner Portal
   (requires MADA — see below)

5. Google's automated systems validate the report

6. If all required tests pass, your device is certified
```

For learning and development on RPi5, you can download and run CTS without
any registration:
```bash
# Download from source.android.com, then:
java -jar android-cts/tools/cts-tradefed.jar run cts \
  --plan CTS-instant
```

Google does not have a "secret" ability to run tests on your device remotely.
The test runs are self-reported but the XML format is standardized and
verifiable. Google's engineers may also manually inspect submitted reports.

## The GMS Licensing Path

GMS = Google Mobile Services. This includes Google Play Store, Google Maps,
Gmail, YouTube, etc. — the apps most users think of as "Android".

**GMS is NOT open source and NOT included in AOSP.** AOSP is the Android
Open Source Project — it's Android without Google's proprietary apps.

The path to getting GMS on a commercial device:

```
Step 1: Build your device on AOSP
         ↓
Step 2: Pass CTS (Compatibility Test Suite)
         ↓
Step 3: Pass GTS (Google Test Suite)
         ← Google's private test suite
         ← Only accessible after signing MADA
         ← Tests GMS-specific requirements beyond CTS
         ↓
Step 4: Sign MADA (Mobile Application Distribution Agreement)
         ← Legal contract with Google
         ← Only available to commercial device manufacturers
         ← Requires CTS pass first
         ↓
Step 5: Submit via Android Partner Portal
         ← portal.android.com (requires MADA credentials)
         ← Submit device specs + CTS results + GTS results
         ↓
Step 6: Google approval
         ← Review process (weeks to months)
         ← Physical device inspection may be required for new OEM
         ↓
Step 7: Receive GMS Distribution Agreement
         ← Authorized to pre-install GMS APKs
         ← Receive signing keys for Google apps
```

## What GTS Is

GTS (Google Test Suite) is Google's private test suite that specifically tests
GMS integration requirements. It tests things like:
- Is the Google Play Store correctly installed and functional?
- Are Google's background services (GMS Core, Play Services) properly set up?
- Does the device report correct device IDs to Google's servers?

You cannot download GTS from source.android.com. You cannot run GTS without
MADA credentials. GTS is only accessible through the Android Partner Portal
after signing MADA.

## MADA — The Legal Framework

MADA (Mobile Application Distribution Agreement) is the contract between
Google and a device manufacturer that grants:
- The right to pre-install Google's proprietary apps
- Access to the Android Partner Portal
- Access to GTS
- Access to Google's OTA infrastructure (if desired)

MADA requirements:
1. **You must be a commercial device manufacturer** — not a hobbyist project,
   not a one-off device
2. **Your device must pass CTS** — Google verifies this before signing
3. **You agree to Google's app placement and UI requirements** — where icons
   appear on the home screen, what's in the app drawer, etc.
4. **You agree to anti-fragmentation provisions** — you cannot fork Android
   in ways incompatible with future updates

## Can an OEM Pre-install ONLY Google Search and Skip the Rest?

This is a question about MADA's "required app list". The short answer: **No.**

Under MADA, Google defines a minimum set of apps that must be pre-installed
on a device. The required set varies by device type:

| Device Type | Core Required Apps |
|-------------|-------------------|
| Smartphone / Tablet | Google Search, Google Play Store, Chrome, Gmail, Google Maps, YouTube, Google Drive, Google Assistant, Play Services, + others |
| Android TV | Google Play Store, YouTube, Google Assistant + TV-specific subset |
| Android Automotive | Google Maps, Google Assistant, Play Store + automotive subset |
| Android Go (low RAM) | Lighter set, Google Go apps instead of full versions |

**Google Search and Google Assistant are non-negotiable on all device types.**
You cannot sign MADA and then skip those.

The reasoning: MADA is a revenue agreement as much as a distribution agreement.
Google Search is Google's core revenue product. Pre-installing it on every
Android device is part of the value exchange: you get Google's apps and
ecosystem, Google gets distribution of Search.

**What flexibility exists:**
- Some apps can be placed in the app drawer instead of the home screen
- Some apps can be "not pre-installed but available in Play Store"
- Low-RAM (Android Go) devices have a lighter required set with Go-edition apps
- Some regional variations exist in the required list (e.g., Google Maps vs
  local map alternatives in China)

**What flexibility does NOT exist:**
- You cannot remove Google Search/Assistant
- You cannot remove Play Store
- You cannot remove Play Services (the background framework)
- You cannot ship a "Google-branded" Android with no Google apps

This is why AOSP exists as a separate thing from "Android with GMS". Companies
like Huawei (post-sanctions), Amazon (Fire tablets), and many Chinese OEMs
that don't want MADA constraints use AOSP without GMS and build their own app
stores and services.

## Practical Reality for RPi5 Builds

For our `myoem_rpi5` build:

- **CTS**: You can download and run it for free. Great for learning and verifying
  your AOSP customizations don't break standard behaviour.

- **VTS**: You can download and run it for free. Important for verifying that
  your `@VintfStability` HAL interfaces are correctly declared.

- **GMS/Google Play**: Not available. The RPi5 is not a commercial consumer
  device. MADA is not available for hobby/learning/research devices.

- **What you CAN have**: A fully working AOSP system with your custom vendor
  stack, testable with CTS/VTS. Just no Google apps.

The skills you develop writing and passing CTS/VTS tests on RPi5 are the same
skills needed for a commercial Android device. The difference is paperwork
(MADA) and money (hardware volume), not engineering.

---

## Final Summary: The Complete Test Architecture for vendor/myoem

```
vendor/myoem/
│
├── 4 VTS C++ tests (gtest on device, vendor partition)
│   ├── VtsCalculatorServiceTest           ← unstable AIDL binder service
│   ├── VtsBMIServiceTest                  ← unstable AIDL binder service
│   ├── VtsThermalControlServiceTest       ← @VintfStability + VINTF check
│   └── VtsThermalControlHalTest           ← direct C++ HAL, no binder
│
├── 1 Java instrumented test (android_test on device, platform_apis)
│   └── MyOemThermalControlManagerTests    ← general-tests (NOT cts)
│       └── 39 test methods across 6 groups
│
└── 2 Kotlin instrumented tests (android_test on device)
    ├── ThermalMonitorUnitTests            ← ViewModel + Mockito + coroutines-test
    │   └── 19 test methods
    └── ThermalMonitorInstrumentedTests    ← Compose UI + FakeThermalViewModel
        └── 26 test methods
```

**Total: 7 test modules, 120+ test methods.**

The design principle across all of them:
- Tests that need hardware gracefully skip, not fail
- Tests that need a running service use `assumeTrue()` / `GTEST_SKIP()`
- Pure logic tests run without any device state
- Each layer is independently testable

---

*End of Series*

**What this series taught:**
1. Test type (VTS vs CTS vs general-tests) is determined by APIs used, not by
   what you're testing
2. Soong's build system has no Gradle inheritance — every dependency is explicit
3. `android_test` is a real APK — it needs a manifest, bundled JUnit, a package
4. Kotlin classes are `final` by default — design for testability by using `open`
5. AOSP prebuilt AARs may not contain all APIs from Maven Central
6. The path from `atest` green to Google Play is long — but the engineering is
   what matters, and the engineering you've done is the same engineering Google's
   OEM partners do

*Last updated: 2026-03-19 | AOSP android-15.0.0_r14 | Target: myoem_rpi5*

---

# Quick Reference: Build & Run After Every Code Change

> Bookmark this section. Every time you modify source code or add a new test,
> use the matching command from the table below — no need to rebuild everything.

---

## Step 0 — One-time environment setup (new terminal)

```bash
source build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug
```

---

## Step 1 — Build only what changed

| What you modified | Build command |
|-------------------|--------------|
| HAL C++ source (`libthermalcontrolhal`) | `m libthermalcontrolhal` |
| Service C++ source (`thermalcontrold`) | `m thermalcontrold` |
| Service AIDL file (`IThermalControlService.aidl`) | `m thermalcontrolservice-aidl` |
| VINTF manifest fragment | `m thermalcontrol-vintf-fragment` |
| Java manager (`ThermalControlManager.java`) | `m thermalcontrol-manager` |
| Kotlin app source (`ThermalMonitor`) | `m ThermalMonitor` |
| SELinux policy | `make vendorimage -j$(nproc)` ← full image required |
| **HAL VTS test** | `m VtsThermalControlHalTest` |
| **Service VTS test (Calculator)** | `m VtsCalculatorServiceTest` |
| **Service VTS test (BMI)** | `m VtsBMIServiceTest` |
| **Service VTS test (ThermalControl)** | `m VtsThermalControlServiceTest` |
| **Java instrumented test** | `m MyOemThermalControlManagerTests` |
| **Kotlin ViewModel test** | `m ThermalMonitorUnitTests` |
| **Kotlin Compose UI test** | `m ThermalMonitorInstrumentedTests` |
| Everything at once | See "Build all" block below |

```bash
# Build all test modules at once
m VtsCalculatorServiceTest \
  VtsBMIServiceTest \
  VtsThermalControlServiceTest \
  VtsThermalControlHalTest \
  MyOemThermalControlManagerTests \
  ThermalMonitorUnitTests \
  ThermalMonitorInstrumentedTests
```

---

## Step 2 — Push changed binaries to device (no reflash needed)

> Only needed for production binary/library changes, not for test-only changes.
> `atest` pushes test APKs/binaries automatically.

```bash
adb root
adb shell mount -o remount,rw /vendor

# HAL library
adb push out/target/product/rpi5/vendor/lib64/libthermalcontrolhal.so \
         /vendor/lib64/

# Service binary
adb push out/target/product/rpi5/vendor/bin/thermalcontrold /vendor/bin/
adb shell stop thermalcontrold && adb shell start thermalcontrold

# VINTF manifest (verify with: adb shell cat /vendor/etc/vintf/manifest/thermalcontrol.xml)
adb push out/target/product/rpi5/vendor/etc/vintf/manifest/thermalcontrol.xml \
         /vendor/etc/vintf/manifest/thermalcontrol.xml

# Kotlin app (needs system partition)
adb shell mount -o remount,rw /
adb push out/target/product/rpi5/system/priv-app/ThermalMonitor/ThermalMonitor.apk \
         /system/priv-app/ThermalMonitor/ThermalMonitor.apk
adb reboot
```

---

## Step 3 — Run tests

### Run a single module
```bash
atest VtsCalculatorServiceTest
atest VtsBMIServiceTest
atest VtsThermalControlServiceTest
atest VtsThermalControlHalTest
atest MyOemThermalControlManagerTests
atest ThermalMonitorUnitTests
atest ThermalMonitorInstrumentedTests
```

### Run a single test method
```bash
# C++ gtest: ModuleName:SuiteName#TestName
atest VtsCalculatorServiceTest:CalculatorServiceTest#Add_TwoPositives
atest VtsThermalControlServiceTest:ThermalControlServiceTest#VintfManifest_ServiceDeclared

# Java/Kotlin: ModuleName:FullClassName#methodName
atest MyOemThermalControlManagerTests:\
com.myoem.thermalcontrol.ThermalControlManagerTest#categorizeTemp_Boundary50_ReturnsWarm

atest ThermalMonitorUnitTests:\
com.myoem.thermalmonitor.ThermalViewModelTest#autoRefresh_After2000ms_StateUpdatesAgain

atest ThermalMonitorInstrumentedTests:\
com.myoem.thermalmonitor.ThermalScreenTest#whenServiceAvailable_ShowsTemperatureValue
```

### Run by test name pattern (gtest filter)
```bash
# All divide tests
atest VtsCalculatorServiceTest -- --gtest_filter="*Divide*"

# All error code tests
atest VtsThermalControlServiceTest -- --gtest_filter="*ErrorCode*"

# All fan-related tests
atest VtsThermalControlServiceTest -- --gtest_filter="*Fan*"
```

### Run tests that don't need a device (Java pure logic groups)
```bash
# Groups A–E of manager tests (no thermalcontrold needed)
atest MyOemThermalControlManagerTests -- \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#constructor*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#nullBinder*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#categorize*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#temperatureColor*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#errorCode*"
```

### Run all vendor/myoem tests at once
```bash
atest VtsCalculatorServiceTest VtsBMIServiceTest \
      VtsThermalControlServiceTest VtsThermalControlHalTest \
      MyOemThermalControlManagerTests \
      ThermalMonitorUnitTests ThermalMonitorInstrumentedTests
```

---

## Step 4 — When a test fails: quick diagnosis

```bash
# Service not found / test skips unexpectedly
adb shell service list | grep -E "calculator|bmi|thermalcontrol"
adb shell ps -e | grep -E "calculatord|bmid|thermalcontrold"

# SELinux blocking something
adb shell dmesg | grep "avc: denied" | grep thermalcontrold

# VINTF manifest missing
adb shell cat /vendor/etc/vintf/manifest/thermalcontrol.xml

# Fan hardware absent (expected skip on non-fan RPi5)
adb shell ls /sys/class/hwmon/
adb shell grep -r "pwm-fan" /sys/class/hwmon/*/name 2>/dev/null

# sysfs permission wrong (fan write fails)
adb shell ls -la /sys/class/hwmon/hwmon2/pwm1
# Must be: -rw-rw-r-- root system ...

# Service crashed (check tombstone)
adb shell ls /data/tombstones/
adb logcat -s thermalcontrold | grep -E "crash|signal|FATAL"

# Java ClassNotFound at runtime
# → a library is in libs: instead of static_libs: in Android.bp

# Wrong test result after code change
# → did you rebuild? run: m <TestModuleName> first
```

---

## Step 5 — Adding a new test: checklist

When you write a new test method, verify each item before running:

```
[ ] Test method name follows the convention:
      C++  : TestSuiteName.WhatItTests_Condition_ExpectedResult
      Java : camelCase#whatItTests_Condition_expectedResult

[ ] For C++ service tests:
      → Guard with: if (!gService) GTEST_SKIP() << "service not running";
      → Use ASSERT_BINDER_OK(status) for binder call results
      → Use EXPECT_NEAR() for float comparisons, never EXPECT_EQ()

[ ] For Java tests that need the service:
      → Guard with: assumeTrue("reason", isServiceRunning());
      → (assumeTrue SKIPS, assertTrue FAILS — choose correctly)

[ ] For hardware-dependent tests:
      → Check the return value / error code first
      → GTEST_SKIP() / Assume.assumeTrue() on ERROR_SYSFS_WRITE or false return

[ ] For Kotlin tests:
      → ThermalViewModel and its methods are declared open
      → FakeThermalViewModel overrides only the methods it needs
      → Call composeTestRule.waitForIdle() after setState()
      → Use onAllNodesWithText().assertCountEquals(0) instead of assertDoesNotExist()

[ ] Build the test module: m <ModuleName>
[ ] Run with atest: atest <ModuleName>:<ClassName>#<methodName>
[ ] Check output for PASSED / SKIPPED (not FAILED)
```

---

## One-page Cheat Sheet

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     vendor/myoem TEST CHEAT SHEET                           │
├─────────────────┬───────────────────────────────┬───────────────────────────┤
│ Module          │ Build                         │ Run                       │
├─────────────────┼───────────────────────────────┼───────────────────────────┤
│ Calculator VTS  │ m VtsCalculatorServiceTest    │ atest VtsCalculator...    │
│ BMI VTS         │ m VtsBMIServiceTest           │ atest VtsBMI...           │
│ Thermal Svc VTS │ m VtsThermalControlServiceTest│ atest VtsThermalCtrl...   │
│ Thermal HAL VTS │ m VtsThermalControlHalTest    │ atest VtsThermalCtrlHal.. │
│ Manager Tests   │ m MyOemThermalControl...Tests │ atest MyOemThermal...     │
│ ViewModel Tests │ m ThermalMonitorUnitTests     │ atest ThermalMonitorUnit..│
│ Compose UI Tests│ m ThermalMonitorInstrumented..│ atest ThermalMonitorInst..│
├─────────────────┼───────────────────────────────┼───────────────────────────┤
│ ALL             │ m Vts* MyOem* ThermalMonitor* │ atest Vts* MyOem* Therm.. │
└─────────────────┴───────────────────────────────┴───────────────────────────┘

Decision: which test_suites?
  @hide APIs used?  YES → platform_apis:true, test_suites:["general-tests"]
                    NO  → sdk_version:"...",  test_suites:["cts"]
  C++ vendor code?       → cc_test, vendor:true, test_suites:["vts"]

android_test checklist:
  ✓ AndroidManifest.xml exists + manifest: declared in Android.bp
  ✓ junit + androidx.test.* in static_libs (NOT libs)
  ✓ ThermalViewModel (and overridden members) declared open
  ✓ No assertDoesNotExist() → use onAllNodes().assertCountEquals(0)
  ✓ @RunWith(AndroidJUnit4::class) not .class
```
