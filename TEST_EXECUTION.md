# Test Execution Guide — vendor/myoem

> **Prerequisites:** Device flashed with `myoem_rpi5-trunk_staging-userdebug`,
> `adb` connected, AOSP build environment sourced (`source build/envsetup.sh && lunch myoem_rpi5-trunk_staging-userdebug`).

---

## Quick Reference

| Command | What it does |
|---------|-------------|
| `atest VtsCalculatorServiceTest` | Run Calculator VTS |
| `atest VtsBMIServiceTest` | Run BMI VTS |
| `atest VtsThermalControlServiceTest` | Run ThermalControl Service VTS |
| `atest VtsThermalControlHalTest` | Run ThermalControl HAL VTS |
| `atest MyOemThermalControlManagerTests` | Run Manager CTS |
| `atest ThermalMonitorUnitTests` | Run ViewModel JVM tests |
| `atest ThermalMonitorInstrumentedTests` | Run Compose UI tests |
| `atest VtsCalculatorServiceTest VtsBMIServiceTest VtsThermalControlServiceTest VtsThermalControlHalTest MyOemThermalControlManagerTests ThermalMonitorUnitTests ThermalMonitorInstrumentedTests` | Run all at once |

---

## Step 1 — Build the test binaries

Always build before running. You only need to rebuild if source files changed.

```bash
# Build all test modules in one command
m VtsCalculatorServiceTest \
  VtsBMIServiceTest \
  VtsThermalControlServiceTest \
  VtsThermalControlHalTest \
  MyOemThermalControlManagerTests \
  ThermalMonitorUnitTests \
  ThermalMonitorInstrumentedTests
```

---

## Step 2 — Run tests with `atest`

`atest` is the recommended way to run AOSP tests. It automatically:
- Pushes the binary to the device
- Executes it
- Pulls results back
- Shows a color-coded pass/fail summary

### Run a single module
```bash
atest VtsCalculatorServiceTest
```

### Run a single test case within a module
```bash
# Format: ModuleName:ClassName#testMethodName
atest VtsCalculatorServiceTest:CalculatorServiceTest#Add_TwoPositives
atest VtsCalculatorServiceTest:CalculatorServiceTest#Divide_ByZero_ErrorCode_IsOne
```

### Run all tests in a section (regex filter)
```bash
# Run all divide() tests
atest VtsCalculatorServiceTest -- --gtest_filter="*Divide*"

# Run all error code tests across modules
atest VtsThermalControlServiceTest -- --gtest_filter="*ErrorCode*"
```

### Run all vendor/myoem tests at once
```bash
atest VtsCalculatorServiceTest VtsBMIServiceTest \
      VtsThermalControlServiceTest VtsThermalControlHalTest \
      MyOemThermalControlManagerTests \
      ThermalMonitorUnitTests ThermalMonitorInstrumentedTests
```

---

## Step 3 — Reading test output

### Passing run
```
[==========] Running 30 tests from 6 test suites.
[----------] 7 tests from CalculatorServiceTest/Add
[ RUN      ] CalculatorServiceTest.Add_TwoPositives
[       OK ] CalculatorServiceTest.Add_TwoPositives (12 ms)
...
[  PASSED  ] 30 tests.
```

### Failing test
```
[ RUN      ] CalculatorServiceTest.Divide_ByZero_ErrorCode_IsOne
[  FAILED  ] CalculatorServiceTest.Divide_ByZero_ErrorCode_IsOne (8 ms)
VtsCalculatorServiceTest.cpp:201: Failure
Expected ERROR_DIVIDE_BY_ZERO=1, got: 2
```
The file path and line number tell you exactly which assertion failed.

### Skipped test
```
[ RUN      ] ThermalControlHalTest.SetFanSpeed_50_ReadBackApprox50
[  SKIPPED ] ThermalControlHalTest.SetFanSpeed_50_ReadBackApprox50 (0 ms)
  Reason: Fan hardware not available
```
`SKIPPED` is not a failure — it means the hardware needed for that test was absent.

---

## Module-by-module execution details

---

### Module 1 — VtsCalculatorServiceTest

**What it needs:** `calculatord` running on device.

**Verify service is running first:**
```bash
adb shell service list | grep calculator
# Expected: com.myoem.calculator.ICalculatorService: [calculatord]
```

**Run:**
```bash
atest VtsCalculatorServiceTest
```

**If tests skip:** Service is not running. Check:
```bash
adb shell ps -e | grep calculatord            # is the process alive?
adb logcat -s calculatord | grep addService   # did it register?
adb logcat | grep "avc: denied" | grep calc   # SELinux blocking?
```

---

### Module 2 — VtsBMIServiceTest

**What it needs:** `bmid` running on device.

```bash
adb shell service list | grep bmi
atest VtsBMIServiceTest
```

---

### Module 3 — VtsThermalControlServiceTest

**What it needs:** `thermalcontrold` running + VINTF manifest installed.

```bash
# Check VINTF manifest is installed
adb shell cat /vendor/etc/vintf/manifest.xml | grep thermalcontrol

# Check service is running
adb shell service list | grep thermalcontrol

atest VtsThermalControlServiceTest
```

**Important:** This test checks `AServiceManager_isDeclared()` which reads the
VINTF manifest. If the `vintf_fragment` module was not built and flashed, the
first test (`VintfManifest_ServiceDeclared`) will fail.

**Hardware-dependent tests:** Tests that call `setFanSpeed()` and read back the
result will SKIP automatically if the rpi5 fan hwmon is not present.

---

### Module 4 — VtsThermalControlHalTest

**What it needs:** Can run even without fan hardware (sysfs-absent path tested).
On real rpi5 with fan: the write/read-back tests will also execute.

```bash
atest VtsThermalControlHalTest
```

**Check if fan hardware is present:**
```bash
adb shell ls /sys/class/hwmon/
adb shell cat /sys/class/hwmon/hwmon0/name   # should contain "pwmfan" or similar
```

**After test:** Verify the fan is back in auto mode:
```bash
adb shell cat /sys/class/hwmon/hwmon0/pwm1_enable   # should be 2 (auto)
```

---

### Module 5 — MyOemThermalControlManagerTests

**What it needs:** No service needed for groups A–E (pure JVM).
Group F (live tests) needs `thermalcontrold` running.

```bash
atest MyOemThermalControlManagerTests
```

**Run only the pure JVM tests (no service needed):**
```bash
atest MyOemThermalControlManagerTests -- \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#constructor*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#categorize*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#temperatureColor*" \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#errorCode*"
```

**Run only live service tests:**
```bash
atest MyOemThermalControlManagerTests -- \
  --include-filter "com.myoem.thermalcontrol.ThermalControlManagerTest#live*"
```

---

### Module 6 — ThermalMonitorUnitTests (ViewModel tests)

Tests ThermalViewModel in isolation using Mockito + coroutines-test.
Runs on device (android_test) because ThermalControlManager uses Android IBinder APIs.

```bash
atest ThermalMonitorUnitTests
```

**Key dependencies in Android.bp:**
- `mockito-target` — Mockito for Android (dexmaker-based)
- `kotlinx-coroutines-test` — StandardTestDispatcher for virtual time control

---

### Module 7 — ThermalMonitorInstrumentedTests (Compose UI tests)

**What it needs:** Device with Compose runtime. The test APK is self-contained
(uses `createComposeRule()` to start its own Activity — no production APK needed).

```bash
atest ThermalMonitorInstrumentedTests
```

**Run a single UI test:**
```bash
atest ThermalMonitorInstrumentedTests:ThermalScreenTest#whenServiceAvailable_ShowsTemperatureValue
```

---

## Manual test execution (without atest)

Sometimes `atest` is unavailable (e.g., first-time setup or CI environment).
Use these manual commands instead.

### VTS C++ tests (manual)
```bash
# Step 1: Push the binary to the device
adb push out/target/product/rpi5/vendor/bin/VtsCalculatorServiceTest \
         /data/local/tmp/

# Step 2: Make it executable
adb shell chmod +x /data/local/tmp/VtsCalculatorServiceTest

# Step 3: Run it (as root so it can reach vendor services)
adb shell "su root /data/local/tmp/VtsCalculatorServiceTest"

# Step 4: Run with GTest filters
adb shell "su root /data/local/tmp/VtsCalculatorServiceTest \
           --gtest_filter=*Divide*"

# Step 5: Run with XML output (for CI systems)
adb shell "su root /data/local/tmp/VtsCalculatorServiceTest \
           --gtest_output=xml:/data/local/tmp/calc_results.xml"
adb pull /data/local/tmp/calc_results.xml .
```

### CTS Java tests (manual)
```bash
# Step 1: Install the test APK
adb install -r out/target/product/rpi5/testcases/MyOemThermalControlManagerTests/*.apk

# Step 2: Run with am instrument
adb shell am instrument -w \
  com.myoem.thermalcontrol.test/androidx.test.runner.AndroidJUnitRunner

# Step 3: Run a specific test method
adb shell am instrument -w \
  -e class com.myoem.thermalcontrol.ThermalControlManagerTest#categorizeTemp_Boundary50_ReturnsWarm \
  com.myoem.thermalcontrol.test/androidx.test.runner.AndroidJUnitRunner
```

---

## Interpreting failures

### "calculatord not running — is it installed and started?"
The service process is not in the ServiceManager. Run:
```bash
adb shell ps -e | grep calculatord
adb logcat -s calculatord
```

### "Service not found under: com.myoem.calculator.ICalculatorService"
The service name in the test doesn't match what the daemon registered.
```bash
adb shell service list | grep -i calc   # see the actual registered name
```

### "EX_TRANSACTION_FAILED" in a divide-by-zero test
The service crashed instead of returning `ServiceSpecificException`.
```bash
adb logcat -s calculatord | grep -E "crash|signal|tombstone"
adb shell ls /data/tombstones/
```

### VINTF test fails: "Service not declared in VINTF manifest"
The vintf_fragment was not built or installed.
```bash
adb shell cat /vendor/etc/vintf/manifest.xml   # does thermalcontrol appear?
m thermalcontrol-vintf-fragment                 # rebuild
fastboot flash vendor out/target/product/rpi5/vendor.img
```

### Fan hardware tests skip: "Fan hardware not available"
This is expected if you are running on a rpi5 without a PWM fan connected.
Only tests marked `(hardware)` in the test name require real fan hardware.
All other tests (safe-defaults, error codes, concurrency) run without it.

### Java test: "ClassNotFoundException" or "NoClassDefFoundError"
A library dependency is missing in Android.bp. Check that all `libs` and
`static_libs` entries match their actual Soong module names.

---

## CI integration (recommended workflow)

```bash
#!/bin/bash
set -e   # exit on first failure

echo "=== Building test modules ==="
m VtsCalculatorServiceTest VtsBMIServiceTest \
  VtsThermalControlServiceTest VtsThermalControlHalTest \
  MyOemThermalControlManagerTests

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

## Test file locations (all created)

```
vendor/myoem/
├── services/
│   ├── calculator/tests/
│   │   ├── Android.bp
│   │   └── VtsCalculatorServiceTest.cpp
│   ├── bmi/tests/
│   │   ├── Android.bp
│   │   └── VtsBMIServiceTest.cpp
│   └── thermalcontrol/tests/
│       ├── Android.bp
│       └── VtsThermalControlServiceTest.cpp
├── hal/thermalcontrol/tests/
│   ├── Android.bp
│   └── VtsThermalControlHalTest.cpp
├── libs/thermalcontrol/tests/
│   ├── Android.bp
│   └── java/com/myoem/thermalcontrol/
│       └── ThermalControlManagerTest.java
└── apps/ThermalMonitor/
    ├── Android.bp                           ← app + ThermalMonitorUnitTests + ThermalMonitorInstrumentedTests
    ├── src/test/
    │   ├── AndroidManifest.xml              ← package: com.myoem.thermalmonitor.test.unit
    │   └── kotlin/com/myoem/thermalmonitor/
    │       └── ThermalViewModelTest.kt      ← ViewModel unit-style tests
    └── src/androidTest/
        ├── AndroidManifest.xml             ← package: com.myoem.thermalmonitor.test.ui
        └── kotlin/com/myoem/thermalmonitor/
            └── ThermalScreenTest.kt        ← Compose instrumented tests
```

---

*Last updated: 2026-03-19 | AOSP android-15.0.0_r14 | Target: rpi5*
