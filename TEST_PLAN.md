# vendor/myoem — CTS & VTS Test Plan

> **Status:** Plan only — no test code exists yet. Test files will be written in future sessions.
>
> **AOSP branch:** android-15.0.0_r14 | **Target:** Raspberry Pi 5 | **Author:** ProArun

---

## Quick Reference: Which Test for Which Module?

| Module | Type | Why |
|--------|------|-----|
| `services/calculator` | **VTS** | Vendor C++ binder service, `@hide` AIDL |
| `services/bmi` | **VTS** | Vendor C++ binder service, `@hide` AIDL |
| `services/thermalcontrol` | **VTS (mandatory)** | `@VintfStability` AIDL — Android requires VTS coverage for all VINTF-declared interfaces |
| `hal/thermalcontrol` | **VTS** | Vendor C++ HAL library, direct sysfs access |
| `libs/thermalcontrol` | **CTS** | Java API consumed by apps; verifies public behavior on any compatible device |
| `apps/ThermalMonitor` | **CTS** | Android app — JVM unit tests + Compose instrumented tests |

---

## CTS vs VTS — When to Use Which

| | CTS (Compatibility Test Suite) | VTS (Vendor Test Suite) |
|---|---|---|
| **Layer** | App / Java framework | Vendor / HAL / native |
| **Runs as** | Normal app UID | System / root |
| **Language** | Java / Kotlin (JUnit4, Espresso, Compose) | C++ (GTest) |
| **Triggers** | `PRODUCT_PACKAGES` app-level modules | `@VintfStability`, HALs, vendor services |
| **Mandate** | Required for GMS / Play compatibility | Required for Treble / VINTF compliance |
| **When to choose** | Java Manager wrappers, Android apps | AIDL services, HALs, native libraries |

---

## Module 1 — CalculatorService (VTS)

**AIDL:** `com.myoem.calculator.ICalculatorService`
**Service name:** `com.myoem.calculator.ICalculatorService`
**Methods:** `add`, `subtract`, `multiply`, `divide` (all `int`)
**Error codes:** `ERROR_DIVIDE_BY_ZERO = 1`

**Planned file:** `services/calculator/tests/VtsCalculatorServiceTest.cpp`
**Build module:** `VtsCalculatorServiceTest` (`cc_test`, `vendor: true`)

### A — Service Availability

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 1 | `ServiceRegistered` | `AServiceManager_checkService("com.myoem.calculator.ICalculatorService")` returns a non-null binder |
| 2 | `ServiceName_ExactMatch` | Service is reachable only under the exact canonical name (not variant spellings) |

### B — `add()`

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 3 | `Add_TwoPositives` | `add(10, 3)` | `13` |
| 4 | `Add_TwoNegatives` | `add(-5, -3)` | `-8` |
| 5 | `Add_PositiveAndNegative` | `add(-10, 15)` | `5` |
| 6 | `Add_BothZero` | `add(0, 0)` | `0` |
| 7 | `Add_WithZero` | `add(42, 0)` | `42` |
| 8 | `Add_MaxPlusZero` | `add(INT_MAX, 0)` | `INT_MAX` (no crash) |
| 9 | `Add_ResultIsZero` | `add(-7, 7)` | `0` |

### C — `subtract()`

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 10 | `Subtract_PositiveResult` | `subtract(10, 3)` | `7` |
| 11 | `Subtract_NegativeResult` | `subtract(3, 10)` | `-7` |
| 12 | `Subtract_SameNumbers` | `subtract(5, 5)` | `0` |
| 13 | `Subtract_FromZero` | `subtract(0, 5)` | `-5` |
| 14 | `Subtract_TwoNegatives` | `subtract(-3, -7)` | `4` |

### D — `multiply()`

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 15 | `Multiply_TwoPositives` | `multiply(4, 5)` | `20` |
| 16 | `Multiply_ByZero` | `multiply(999, 0)` | `0` |
| 17 | `Multiply_TwoNegatives` | `multiply(-3, -4)` | `12` |
| 18 | `Multiply_MixedSign` | `multiply(-3, 4)` | `-12` |
| 19 | `Multiply_ByOne` | `multiply(42, 1)` | `42` |

### E — `divide()`

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 20 | `Divide_ExactDivision` | `divide(10, 2)` | `5` |
| 21 | `Divide_IntegerTruncation` | `divide(7, 2)` | `3` (not 3.5 — integer division) |
| 22 | `Divide_NegativeByPositive` | `divide(-10, 2)` | `-5` |
| 23 | `Divide_NegativeByNegative` | `divide(-10, -2)` | `5` |
| 24 | `Divide_ByOne` | `divide(42, 1)` | `42` |
| 25 | `Divide_ByMinusOne` | `divide(42, -1)` | `-42` |
| 26 | `Divide_ZeroByNonZero` | `divide(0, 5)` | `0` |
| 27 | `Divide_ByZero_ThrowsServiceSpecificException` | `divide(10, 0)` | `status.isOk() == false`, exception code == `1` |
| 28 | `Divide_ByZero_ErrorCode_IsOne` | check `ICalculatorService::ERROR_DIVIDE_BY_ZERO` | `1` |

### F — Concurrency & Stability

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 29 | `ConcurrentCalls_NoDataRace` | 8 threads each call `add/multiply/subtract` 100× concurrently — no crash, correct results |
| 30 | `RepeatedCalls_ServiceStable` | 1000 sequential calls without reconnecting — no crash |

---

## Module 2 — BMIService (VTS)

**AIDL:** `com.myoem.bmi.IBMIService`
**Service name:** `com.myoem.bmi.IBMIService`
**Method:** `getBMI(float height, float weight) → float`
**Formula:** `weight / (height * height)`
**Error codes:** `ERROR_INVALID_INPUT = 1` (when `height <= 0` or `weight <= 0`)

**Planned file:** `services/bmi/tests/VtsBMIServiceTest.cpp`
**Build module:** `VtsBMIServiceTest` (`cc_test`, `vendor: true`)

### A — Service Availability

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 1 | `ServiceRegistered` | `AServiceManager_checkService("com.myoem.bmi.IBMIService")` returns non-null |
| 2 | `ServiceName_ExactMatch` | Exact canonical name only |

### B — Correct Calculations

| # | Test name | Input | Expected (delta ±0.01) |
|---|-----------|-------|------------------------|
| 3 | `BMI_StandardAdult` | `getBMI(1.75f, 70.0f)` | `≈ 22.86f` |
| 4 | `BMI_TallAndLight` | `getBMI(2.0f, 60.0f)` | `15.0f` |
| 5 | `BMI_ShortAndHeavy` | `getBMI(1.50f, 90.0f)` | `≈ 40.0f` |
| 6 | `BMI_Formula_Correct` | any valid pair | result `≈ weight / (height * height)` |
| 7 | `BMI_SmallValidHeight` | `getBMI(0.1f, 0.5f)` | no error, valid float |

### C — Error Cases

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 8 | `BMI_ZeroHeight_ThrowsError` | `getBMI(0.0f, 70.0f)` | `ServiceSpecificException` code `1` |
| 9 | `BMI_NegativeHeight_ThrowsError` | `getBMI(-1.75f, 70.0f)` | code `1` |
| 10 | `BMI_ZeroWeight_ThrowsError` | `getBMI(1.75f, 0.0f)` | code `1` |
| 11 | `BMI_NegativeWeight_ThrowsError` | `getBMI(1.75f, -70.0f)` | code `1` |
| 12 | `BMI_BothZero_ThrowsError` | `getBMI(0.0f, 0.0f)` | code `1` |
| 13 | `BMI_BothNegative_ThrowsError` | `getBMI(-1.0f, -70.0f)` | code `1` |
| 14 | `BMI_ErrorCode_IsOne` | `IBMIService::ERROR_INVALID_INPUT` constant | `1` |

### D — Float Precision & Stability

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 15 | `BMI_ReturnsFiniteFloat` | result is not NaN, not ±inf |
| 16 | `BMI_LargeValues_NoOverflow` | `getBMI(3.0f, 500.0f)` — no crash, valid float |
| 17 | `ConcurrentCalls_NoDataRace` | 8 threads calling `getBMI()` simultaneously — no crash |

---

## Module 3 — ThermalControlService (VTS — **MANDATORY**)

> **Why mandatory?** The AIDL is annotated `@VintfStability`, which makes it part of the
> Vendor Interface (VINTF) contract. Android Treble compliance requires that every
> `@VintfStability` interface has corresponding VTS coverage.

**AIDL:** `com.myoem.thermalcontrol.IThermalControlService` (`@VintfStability`)
**Service name:** `com.myoem.thermalcontrol.IThermalControlService/default`
**Error codes:** `ERROR_HAL_UNAVAILABLE=1`, `ERROR_INVALID_SPEED=2`, `ERROR_SYSFS_WRITE=3`

**Planned file:** `services/thermalcontrol/tests/VtsThermalControlServiceTest.cpp`
**Build module:** `VtsThermalControlServiceTest` (`cc_test`, `vendor: true`)

### A — VINTF Compliance

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 1 | `VintfManifest_ServiceDeclared` | Service present in `/vendor/etc/vintf/manifest.xml` |
| 2 | `ServiceDeclared_DefaultInstance` | `AServiceManager_isDeclared("…/default")` returns `true` |
| 3 | `ServiceAvailable_WithinTimeout` | `AServiceManager_waitForService("…/default")` returns non-null within 5 s |

### B — Temperature Reads

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 4 | `GetCpuTemp_DoesNotCrash` | Call either returns `ok()` or `ERROR_HAL_UNAVAILABLE` — never crashes |
| 5 | `GetCpuTemp_WhenOk_IsFiniteFloat` | Result is not NaN, not ±inf |
| 6 | `GetCpuTemp_WhenOk_ReasonableRange` | `0.0f ≤ result ≤ 120.0f` |
| 7 | `GetCpuTemp_WhenHalUnavailable_ThrowsCode1` | If HAL absent, exception code == `ERROR_HAL_UNAVAILABLE (1)` |

### C — Fan Speed Reads

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 8 | `GetFanSpeedRpm_DoesNotCrash` | Call succeeds |
| 9 | `GetFanSpeedRpm_IsMinusOneOrPositive` | Result is `-1` (no tachometer) or `≥ 0` |
| 10 | `GetFanSpeedPercent_InRange` | `0 ≤ result ≤ 100` |
| 11 | `IsFanRunning_ReturnsBool` | No exception |
| 12 | `IsFanAutoMode_ReturnsBool` | No exception |
| 13 | `IsFanRunning_ConsistentWithPercent` | If `getFanSpeedPercent() > 0` then `isFanRunning() == true` |

### D — Fan Write Operations

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 14 | `SetFanAutoMode_True_Succeeds` | `setFanAutoMode(true)` → `ok()` |
| 15 | `SetFanAutoMode_False_Succeeds` | `setFanAutoMode(false)` → `ok()` |
| 16 | `SetFanAutoMode_True_ReflectedInGetter` | After call, `isFanAutoMode() == true` |
| 17 | `SetFanAutoMode_False_ReflectedInGetter` | After call, `isFanAutoMode() == false` |
| 18 | `SetFanEnabled_True_Succeeds` | `setFanEnabled(true)` → `ok()` |
| 19 | `SetFanEnabled_False_Succeeds` | `setFanEnabled(false)` → `ok()` |
| 20 | `SetFanEnabled_ExitsAutoMode` | `setFanAutoMode(true)` → `setFanEnabled(true)` → `isFanAutoMode() == false` |
| 21 | `SetFanSpeed_0_Succeeds` | `setFanSpeed(0)` → `ok()` |
| 22 | `SetFanSpeed_50_Succeeds` | `setFanSpeed(50)` → `ok()` |
| 23 | `SetFanSpeed_100_Succeeds` | `setFanSpeed(100)` → `ok()` |
| 24 | `SetFanSpeed_50_ReflectedInGetter` | `setFanSpeed(50)` → `getFanSpeedPercent() == 50` |
| 25 | `SetFanSpeed_ExitsAutoMode` | `setFanAutoMode(true)` → `setFanSpeed(50)` → `isFanAutoMode() == false` |

### E — Invalid Input (Error Codes)

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 26 | `SetFanSpeed_Negative_ThrowsCode2` | `setFanSpeed(-1)` | `ServiceSpecificException` code `2` |
| 27 | `SetFanSpeed_Over100_ThrowsCode2` | `setFanSpeed(101)` | code `2` |
| 28 | `SetFanSpeed_200_ThrowsCode2` | `setFanSpeed(200)` | code `2` |
| 29 | `ErrorCode_HAL_UNAVAILABLE_IsOne` | const value | `1` |
| 30 | `ErrorCode_INVALID_SPEED_IsTwo` | const value | `2` |
| 31 | `ErrorCode_SYSFS_WRITE_IsThree` | const value | `3` |

### F — Concurrency & Stability

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 32 | `ConcurrentReads_NoCrash` | 8 threads call `getCpuTemperatureCelsius()` 50× each — no crash |
| 33 | `WriteReadConsistency_Sequential` | `setFanSpeed(75)` → `getFanSpeedPercent() == 75` (on hardware) |
| 34 | `RepeatedReads_NoCrash` | 100 successive `getCpuTemp()` calls — consistent return type, no crash |

---

## Module 4 — libthermalcontrolhal (VTS)

> Tests the HAL in isolation — catches sysfs access bugs and PWM math errors
> before they surface in the service layer.

**Interface:** `IThermalControlHal` (pure virtual C++ interface)
**Key math:** `percent → PWM`: `round(255 × percent / 100)`
**Key sysfs:** `pwm1` (0–255), `pwm1_enable` (`1`=manual, `2`=auto)

**Planned file:** `hal/thermalcontrol/tests/VtsThermalControlHalTest.cpp`
**Build module:** `VtsThermalControlHalTest` (`cc_test`, `vendor: true`)

### A — Creation

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 1 | `CreateHal_ReturnsNonNull` | `createThermalControlHal() != nullptr` |
| 2 | `CreateHal_Twice_BothNonNull` | Two independent instances can be created |

### B — Temperature Read

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 3 | `GetCpuTemp_WhenHwmonMissing_Returns0` | Returns `0.0f` gracefully (no crash) |
| 4 | `GetCpuTemp_WhenHwmonPresent_IsPositive` | `result > 0.0f` (on real rpi5) |
| 5 | `GetCpuTemp_WhenHwmonPresent_InRange` | `0.0f < result < 120.0f` |
| 6 | `GetCpuTemp_IsFinite` | Result is not NaN, not ±inf |

### C — Fan Speed Read

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 7 | `GetFanSpeedPercent_WhenHwmonMissing_Returns0` | Returns `0` gracefully |
| 8 | `GetFanSpeedPercent_InRange` | `0 ≤ result ≤ 100` |
| 9 | `GetFanSpeedRpm_WhenTachometerMissing_ReturnsMinusOne` | Returns `-1` |
| 10 | `IsFanRunning_ConsistentWithPercent` | `isFanRunning() == (getFanSpeedPercent() > 0)` |
| 11 | `IsAutoMode_WhenHwmonMissing_ReturnsTrue` | Safe default — kernel is in control |

### D — Fan Write & PWM Math

| # | Test name | Input | Expected sysfs value |
|---|-----------|-------|---------------------|
| 12 | `SetFanSpeed_0Percent_WritesPwm0` | `setFanSpeed(0)` | `pwm1 = 0` |
| 13 | `SetFanSpeed_50Percent_WritesPwm127` | `setFanSpeed(50)` | `pwm1 = 127` (`round(255×50/100)`) |
| 14 | `SetFanSpeed_100Percent_WritesPwm255` | `setFanSpeed(100)` | `pwm1 = 255` |
| 15 | `SetFanEnabled_True_SetsPwm255` | `setFanEnabled(true)` | `pwm1 = 255` |
| 16 | `SetFanEnabled_False_SetsPwm0` | `setFanEnabled(false)` | `pwm1 = 0` |
| 17 | `SetAutoMode_True_WritesEnable2` | `setAutoMode(true)` | `pwm1_enable = 2` |
| 18 | `SetAutoMode_False_WritesEnable1` | `setAutoMode(false)` | `pwm1_enable = 1` |
| 19 | `SetFanSpeed_WhenHwmonMissing_ReturnsFalse` | `setFanSpeed(50)` (no hwmon) | `false`, no crash |
| 20 | `SetFanEnabled_WhenHwmonMissing_ReturnsFalse` | `setFanEnabled(true)` (no hwmon) | `false`, no crash |

### E — Boundary & Edge Cases

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 21 | `SetFanSpeed_NegativeInput_ReturnsFalse` | `setFanSpeed(-1)` | `false`, no crash |
| 22 | `SetFanSpeed_Over100_ReturnsFalse` | `setFanSpeed(101)` | `false`, no crash |
| 23 | `SetFanSpeed_Boundary_0_Succeeds` | `setFanSpeed(0)` (hwmon present) | `true` |
| 24 | `SetFanSpeed_Boundary_100_Succeeds` | `setFanSpeed(100)` (hwmon present) | `true` |

---

## Module 5 — ThermalControlManager (CTS)

> Java API wrapper for `IThermalControlService`. CTS verifies that the public
> Java contract is correct on any compatible device.

**Key thresholds from source (exact):**
- `< 50.0f` → Cool / Green (`0xFF4CAF50`)
- `50.0f – 69.9f` → Warm / Amber (`0xFFFFC107`)
- `70.0f – 84.9f` → Hot / Deep Orange (`0xFFFF5722`)
- `≥ 85.0f` → Critical / Red (`0xFFF44336`)

**Planned file:** `libs/thermalcontrol/tests/ThermalControlManagerTest.java`
**Build module:** `CtsMyOemThermalControlManagerTests` (`android_test`)

### A — Constructor & `isAvailable()`

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 1 | `Constructor_NullBinder_IsAvailableFalse` | `new ThermalControlManager(null)` → `isAvailable() == false` |
| 2 | `Constructor_ValidBinder_IsAvailableTrue` | Real binder on device → `isAvailable() == true` |
| 3 | `ServiceName_ConstantValue` | `SERVICE_NAME == "com.myoem.thermalcontrol.IThermalControlService/default"` |

### B — Null-Binder Safe Defaults (pure JVM, no device needed)

| # | Test name | Method | Expected default |
|---|-----------|--------|-----------------|
| 4 | `GetCpuTemp_NullBinder_Returns0` | `getCpuTemperatureCelsius()` | `0.0f` |
| 5 | `GetFanSpeedRpm_NullBinder_ReturnsMinusOne` | `getFanSpeedRpm()` | `-1` |
| 6 | `IsFanRunning_NullBinder_ReturnsFalse` | `isFanRunning()` | `false` |
| 7 | `GetFanSpeedPercent_NullBinder_Returns0` | `getFanSpeedPercent()` | `0` |
| 8 | `IsFanAutoMode_NullBinder_ReturnsTrue` | `isFanAutoMode()` | `true` (safe default — let kernel control fan) |
| 9 | `SetFanEnabled_NullBinder_NoException` | `setFanEnabled(true)` | no exception |
| 10 | `SetFanSpeed_NullBinder_ReturnsFalse` | `setFanSpeed(50)` | `false` |
| 11 | `SetFanAutoMode_NullBinder_NoException` | `setFanAutoMode(true)` | no exception |

### C — `categorizeTemperature()` Boundaries (pure JVM)

| # | Test name | Input | Expected |
|---|-----------|-------|----------|
| 12 | `CategorizeTemp_NegativeTemp_ReturnsCool` | `-5.0f` | `"Cool"` |
| 13 | `CategorizeTemp_ZeroTemp_ReturnsCool` | `0.0f` | `"Cool"` |
| 14 | `CategorizeTemp_MidCool_ReturnsCool` | `30.0f` | `"Cool"` |
| 15 | `CategorizeTemp_JustBelow50_ReturnsCool` | `49.9f` | `"Cool"` |
| 16 | `CategorizeTemp_Boundary50_ReturnsWarm` | `50.0f` | `"Warm"` |
| 17 | `CategorizeTemp_MidWarm_ReturnsWarm` | `60.0f` | `"Warm"` |
| 18 | `CategorizeTemp_JustBelow70_ReturnsWarm` | `69.9f` | `"Warm"` |
| 19 | `CategorizeTemp_Boundary70_ReturnsHot` | `70.0f` | `"Hot"` |
| 20 | `CategorizeTemp_MidHot_ReturnsHot` | `77.0f` | `"Hot"` |
| 21 | `CategorizeTemp_JustBelow85_ReturnsHot` | `84.9f` | `"Hot"` |
| 22 | `CategorizeTemp_Boundary85_ReturnsCritical` | `85.0f` | `"Critical"` |
| 23 | `CategorizeTemp_Above85_ReturnsCritical` | `100.0f` | `"Critical"` |

### D — `temperatureColor()` Boundaries (pure JVM)

| # | Test name | Input | Expected color |
|---|-----------|-------|---------------|
| 24 | `TempColor_Cool_ReturnsGreen` | `30.0f` | `0xFF4CAF50` |
| 25 | `TempColor_Boundary50_ReturnsAmber` | `50.0f` | `0xFFFFC107` |
| 26 | `TempColor_Warm_ReturnsAmber` | `60.0f` | `0xFFFFC107` |
| 27 | `TempColor_Boundary70_ReturnsDeepOrange` | `70.0f` | `0xFFFF5722` |
| 28 | `TempColor_Hot_ReturnsDeepOrange` | `75.0f` | `0xFFFF5722` |
| 29 | `TempColor_Boundary85_ReturnsRed` | `85.0f` | `0xFFF44336` |
| 30 | `TempColor_Critical_ReturnsRed` | `90.0f` | `0xFFF44336` |

### E — Error Code Constants

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 31 | `ErrorCode_HAL_UNAVAILABLE_Is1` | `ERROR_HAL_UNAVAILABLE == 1` |
| 32 | `ErrorCode_INVALID_SPEED_Is2` | `ERROR_INVALID_SPEED == 2` |
| 33 | `ErrorCode_SYSFS_WRITE_Is3` | `ERROR_SYSFS_WRITE == 3` |

### F — Live Service Tests (instrumented, on real device)

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 34 | `LiveService_IsAvailable` | `isAvailable() == true` when `thermalcontrold` is running |
| 35 | `LiveService_GetCpuTemp_ReturnsPositive` | `getCpuTemperatureCelsius() > 0.0f` |
| 36 | `LiveService_SetFanSpeed_50_ReadBackEquals50` | `setFanSpeed(50)` → `getFanSpeedPercent() == 50` |
| 37 | `LiveService_SetAutoMode_True_StateReflected` | `setFanAutoMode(true)` → `isFanAutoMode() == true` |
| 38 | `LiveService_ClientSideValidation_Negative_ReturnsFalse` | `setFanSpeed(-1)` → `false` (blocked client-side, no IPC) |
| 39 | `LiveService_ClientSideValidation_Over100_ReturnsFalse` | `setFanSpeed(101)` → `false` |

---

## Module 6 — ThermalMonitor App (CTS)

Split into two sub-suites:

---

### 6a — ViewModel JVM Unit Tests (no device needed)

**Source:** `ThermalViewModel.kt` + `UiState` data class
**Planned file:** `apps/ThermalMonitor/src/test/kotlin/com/myoem/thermalmonitor/ThermalViewModelTest.kt`
**Build module:** `ThermalMonitorUnitTests` (`android_test`, runs on host JVM with Robolectric)

#### Initial State

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 1 | `InitialState_ServiceUnavailable_ServiceAvailableFalse` | `manager.isAvailable() == false` → `uiState.serviceAvailable == false` |
| 2 | `InitialState_CpuTempIsZero` | `uiState.cpuTempCelsius == 0f` |
| 3 | `InitialState_FanRpmIsMinusOne` | `uiState.fanRpm == -1` |
| 4 | `InitialState_FanSpeedPercentIsZero` | `uiState.fanSpeedPercent == 0` |
| 5 | `InitialState_IsAutoModeTrue` | `uiState.isAutoMode == true` |
| 6 | `InitialState_ErrorMessage_IsSet` | `uiState.errorMessage == "thermalcontrold not running"` |

#### `fetchData()` with mocked manager

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 7 | `FetchData_WhenAvailable_UpdatesCpuTemp` | Mock returns `42.5f` → `uiState.cpuTempCelsius == 42.5f` |
| 8 | `FetchData_WhenAvailable_UpdatesFanRpm` | Mock returns `1200` → `uiState.fanRpm == 1200` |
| 9 | `FetchData_WhenAvailable_UpdatesFanPercent` | Mock returns `50` → `uiState.fanSpeedPercent == 50` |
| 10 | `FetchData_WhenAvailable_SetsServiceAvailableTrue` | `uiState.serviceAvailable == true` |
| 11 | `FetchData_WhenAvailable_ClearsErrorMessage` | `uiState.errorMessage == null` |
| 12 | `FetchData_WhenUnavailable_SetsErrorMessage` | `uiState.errorMessage == "thermalcontrold not running"` |
| 13 | `FetchData_WhenUnavailable_SetsServiceAvailableFalse` | `uiState.serviceAvailable == false` |

#### Fan control actions

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 14 | `TurnFanOn_CallsManager_SetFanEnabled_True` | Mock captures `setFanEnabled(true)` call |
| 15 | `TurnFanOff_CallsManager_SetFanEnabled_False` | Mock captures `setFanEnabled(false)` |
| 16 | `SetFanSpeed_50_CallsManagerWithPercent50` | Mock captures `setFanSpeed(50)` |
| 17 | `SetFanSpeed_0_CallsManagerWithPercent0` | Mock captures `setFanSpeed(0)` |
| 18 | `SetAutoMode_CallsManager_SetFanAutoMode_True` | Mock captures `setFanAutoMode(true)` |
| 19 | `TurnFanOn_ThenFetchData_StateUpdated` | After `turnFanOn()`, state reflects new mock data |

#### Auto-refresh loop

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 20 | `AutoRefresh_PollingInterval_Is2000ms` | Two fetch cycles with `2_000` ms `delay()` between them |
| 21 | `AutoRefresh_CalledRepeatedly_StateUpdatesEachCycle` | State changes on each mock data change |

---

### 6b — Compose Instrumented Tests (on device)

**Source:** `ThermalScreen.kt`
**Planned file:** `apps/ThermalMonitor/src/androidTest/kotlin/com/myoem/thermalmonitor/ThermalScreenTest.kt`
**Build module:** `ThermalMonitorInstrumentedTests` (`android_test`, on device)

#### Service unavailable UI

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 22 | `WhenServiceUnavailable_ShowsErrorMessage` | `"thermalcontrold not running"` text is displayed |
| 23 | `WhenServiceUnavailable_ErrorText_IsNotEmpty` | Error node has non-empty content |

#### Service available UI

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 24 | `WhenServiceAvailable_ShowsTemperatureValue` | CPU temperature value is displayed |
| 25 | `WhenServiceAvailable_ShowsFanSpeedPercent` | Fan speed percentage is displayed |
| 26 | `WhenServiceAvailable_ShowsFanRpm` | Fan RPM value is displayed |

#### Fan control widgets

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 27 | `TurnFanOnButton_IsDisplayed` | Button visible on screen |
| 28 | `TurnFanOnButton_WhenClicked_CallsViewModel` | `viewModel.turnFanOn()` invoked |
| 29 | `TurnFanOffButton_IsDisplayed` | Button visible |
| 30 | `TurnFanOffButton_WhenClicked_CallsViewModel` | `viewModel.turnFanOff()` invoked |
| 31 | `FanSpeedSlider_IsDisplayed` | Slider visible |
| 32 | `FanSpeedSlider_WhenMoved_UpdatesDisplayedPercent` | Moving slider changes the displayed value |
| 33 | `SetSpeedButton_WhenClicked_CallsViewModelWithSliderValue` | `viewModel.setFanSpeed(sliderValue)` invoked |

#### Auto mode

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 34 | `AutoModeButton_IsDisplayed` | Auto mode button/toggle visible |
| 35 | `AutoModeButton_WhenClicked_CallsSetAutoMode` | `viewModel.setAutoMode()` invoked |

#### Temperature color (visual)

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 36 | `CoolTemp_ShowsGreenColor` | State with temp `30f` → green color indicator |
| 37 | `WarmTemp_ShowsAmberColor` | State with temp `60f` → amber |
| 38 | `HotTemp_ShowsOrangeColor` | State with temp `75f` → deep orange |
| 39 | `CriticalTemp_ShowsRedColor` | State with temp `90f` → red |

#### State update propagation

| # | Test name | What it verifies |
|---|-----------|-----------------|
| 40 | `WhenStateUpdates_UIReflectsNewTemp` | Emit new state → UI shows updated temperature |
| 41 | `WhenAutoModeEnabled_ManualControlsReflectState` | `isAutoMode == true` reflected in UI state |

---

## Planned Directory Layout (to be created when writing tests)

```
vendor/myoem/
├── services/
│   ├── calculator/
│   │   └── tests/
│   │       ├── Android.bp                   ← VtsCalculatorServiceTest
│   │       └── VtsCalculatorServiceTest.cpp
│   ├── bmi/
│   │   └── tests/
│   │       ├── Android.bp                   ← VtsBMIServiceTest
│   │       └── VtsBMIServiceTest.cpp
│   └── thermalcontrol/
│       └── tests/
│           ├── Android.bp                   ← VtsThermalControlServiceTest
│           └── VtsThermalControlServiceTest.cpp
├── hal/
│   └── thermalcontrol/
│       └── tests/
│           ├── Android.bp                   ← VtsThermalControlHalTest
│           └── VtsThermalControlHalTest.cpp
├── libs/
│   └── thermalcontrol/
│       └── tests/
│           ├── Android.bp                   ← CtsMyOemThermalControlManagerTests
│           └── ThermalControlManagerTest.java
└── apps/
    └── ThermalMonitor/
        └── src/
            ├── test/kotlin/com/myoem/thermalmonitor/
            │   └── ThermalViewModelTest.kt  ← JVM unit tests (Robolectric)
            └── androidTest/kotlin/com/myoem/thermalmonitor/
                └── ThermalScreenTest.kt     ← Compose instrumented tests
```

---

## Test Count Summary

| Module | Test Suite | # Tests |
|--------|-----------|---------|
| CalculatorService | VTS | 30 |
| BMIService | VTS | 17 |
| ThermalControlService | VTS | 34 |
| libthermalcontrolhal | VTS | 24 |
| ThermalControlManager | CTS | 39 |
| ThermalMonitor ViewModel | CTS (JVM) | 21 |
| ThermalMonitor UI | CTS (instrumented) | 20 |
| **Total** | | **185** |

---

*Last updated: 2026-03-19 | AOSP android-15.0.0_r14 | Target: rpi5*
