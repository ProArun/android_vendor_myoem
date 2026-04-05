# Part 11: Building BMICalculatorB — The Manager Pattern

## Subtitle: Hiding Binder Complexity Behind a Library So Apps Stay Clean

**Series**: AOSP OEM Vendor Layer Development on Raspberry Pi 5
**GitHub**: ProArun
**Device**: Raspberry Pi 5 | AOSP android-15.0.0_r14
**Prerequisites**: Part 10 (BMICalculatorA, JNI, binder stability, VINTF)

---

## Overview

In Part 10 we built **BMICalculatorA** — an app that calls vendor binder services directly via JNI. It works, but the app has to know about:
- `NativeBinder.kt` (JNI bridge)
- `libbmicalculator_jni.so` (C++ binder code)
- `FLAG_PRIVATE_VENDOR` (binder stability trick)
- `/default` service name format

That's a lot of binder internals leaking into an app. In Part 11 we build **BMICalculatorB**, which has the same UI but talks to a `BmiCalManager` library instead. The app becomes completely clean.

| Aspect | BMICalculatorA | BMICalculatorB |
|--------|---------------|----------------|
| Binder knowledge in app | Yes (NativeBinder, JNI) | No |
| ServiceManager in app | No (moved to JNI) | No |
| IBinder in app | No | No |
| App imports | NativeBinder (JNI object) | BmiCalManager (plain Kotlin class) |
| Where JNI lives | App's own JNI | Manager library's JNI |
| App complexity | Medium | Low |

---

## What We're Building

```
vendor/myoem/
├── libs/bmicalculator/          ← BmiCalManager library
│   ├── Android.bp
│   ├── jni/BmiCalManagerJni.cpp ← C++ binder bridge (FLAG_PRIVATE_VENDOR)
│   └── java/com/myoem/bmicalculator/
│       └── BmiCalManager.kt     ← Public Kotlin API
│
└── apps/BMICalculatorB/         ← App
    ├── Android.bp
    ├── AndroidManifest.xml
    └── src/main/kotlin/com/myoem/bmicalculatorb/
        ├── MainActivity.kt      ← 4 lines, no binder code
        ├── BmiCalBViewModel.kt  ← uses BmiCalManager like any library
        └── ui/BmiCalScreen.kt   ← identical to App A's UI
```

---

## Architecture

```
┌──────────────────────────────────────────────┐
│         BMICalculatorB (App Process)         │
│                                              │
│  Kotlin — zero binder imports               │
│    BmiCalBViewModel                          │
│         ↓  manager.getBMI(height, weight)    │
│    BmiCalManager  (bmicalculator-manager)    │
│         ↓  System.loadLibrary(...)           │
│  JNI — libbmicalmanager_jni.so              │
│    BmiCalManagerJni.cpp                      │
│         ↓  defaultServiceManager()           │
│         ↓  binder->transact(FLAG_PRIVATE_VENDOR)
└─────────────────┬────────────────────────────┘
                  │ Kernel Binder
        ┌─────────┴──────────┐
        ▼                    ▼
     bmid               calculatord
  (IBMIService)    (ICalculatorService)
  /vendor/bin        /vendor/bin
```

Compare this to BMICalculatorA where `NativeBinder` was inside the app. Here `BmiCalManager` is a separate library — the app only imports it, it doesn't own the JNI.

---

## Step 1: BmiCalManager Library

### Android.bp (`vendor/myoem/libs/bmicalculator/Android.bp`)

```bp
soong_namespace {}

cc_library_shared {
    name: "libbmicalmanager_jni",
    srcs: ["jni/BmiCalManagerJni.cpp"],
    header_libs: ["jni_headers"],
    shared_libs: ["libbinder", "libutils", "liblog"],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}

java_library {
    name: "bmicalculator-manager",
    srcs: ["java/**/*.kt"],
    libs: ["kotlin-stdlib"],
    sdk_version: "current",
}
```

**Key design decision — `sdk_version: "current"` not `platform_apis: true`:**

ThermalControlManager uses `sdk_version: "system_current"` because it takes an `IBinder` in its constructor — the app calls `ServiceManager.checkService()` and passes the result in. BmiCalManager uses `sdk_version: "current"` because `ServiceManager` is called inside the JNI C++ code via `defaultServiceManager()`. The Kotlin manager never touches any `@hide` API.

This means **the manager is a more complete abstraction** than ThermalControlManager — even the service lookup is hidden inside the JNI.

### BmiCalManagerJni.cpp

The JNI uses the exact same technique discovered in BMICalculatorA:

```cpp
#define LOG_TAG "BmiCalManagerJni"

#include <jni.h>
#include <binder/IServiceManager.h>
#include <binder/IBinder.h>
#include <binder/Parcel.h>
#include <log/log.h>

static const char* kBmiSvc  = "com.myoem.bmi.IBMIService/default";
static const char* kCalcSvc = "com.myoem.calculator.ICalculatorService/default";
static const char* kBmiDesc  = "com.myoem.bmi.IBMIService";
static const char* kCalcDesc = "com.myoem.calculator.ICalculatorService";

// Transaction codes (FIRST_CALL_TRANSACTION = 1)
static const uint32_t TX_GET_BMI  = IBinder::FIRST_CALL_TRANSACTION;
static const uint32_t TX_ADD      = IBinder::FIRST_CALL_TRANSACTION;
static const uint32_t TX_SUBTRACT = IBinder::FIRST_CALL_TRANSACTION + 1;
static const uint32_t TX_MULTIPLY = IBinder::FIRST_CALL_TRANSACTION + 2;
static const uint32_t TX_DIVIDE   = IBinder::FIRST_CALL_TRANSACTION + 3;

extern "C" JNIEXPORT jfloat JNICALL
Java_com_myoem_bmicalculator_BmiCalManager_nativeGetBMI(
        JNIEnv* env, jobject, jfloat height, jfloat weight) {
    sp<IBinder> binder = defaultServiceManager()
                             ->checkService(String16(kBmiSvc));
    if (!binder) { /* throwRE */ return 0.0f; }

    Parcel data, reply;
    data.writeInterfaceToken(String16(kBmiDesc));
    data.writeFloat((float)height);
    data.writeFloat((float)weight);

    // FLAG_PRIVATE_VENDOR: lowers required stability in BpBinder::transact
    // from LOCAL (system partition) to VENDOR — allows system JNI to call
    // VENDOR-stability binder services. Same trick as BMICalculatorA.
    binder->transact(TX_GET_BMI, data, &reply, IBinder::FLAG_PRIVATE_VENDOR);

    int32_t ex = reply.readInt32();
    if (ex == 0) return reply.readFloat();  // EX_NONE
    // handle EX_SERVICE_SPECIFIC (-8) ...
    return 0.0f;
}
```

The only difference from BMICalculatorA's JNI is the JNI symbol prefix:
- BMICalculatorA: `Java_com_myoem_bmicalculatora_NativeBinder_getBMI`
- BMICalculatorB: `Java_com_myoem_bmicalculator_BmiCalManager_nativeGetBMI`

### BmiCalManager.kt

```kotlin
class BmiCalManager {
    init { System.loadLibrary("bmicalmanager_jni") }

    // Availability probes
    external fun isBmiAvailable(): Boolean
    external fun isCalcAvailable(): Boolean

    // Public API — what the app calls
    fun getBMI(height: Float, weight: Float): Float = nativeGetBMI(height, weight)
    fun add(a: Int, b: Int): Int      = nativeAdd(a, b)
    fun subtract(a: Int, b: Int): Int = nativeSubtract(a, b)
    fun multiply(a: Int, b: Int): Int = nativeMultiply(a, b)
    fun divide(a: Int, b: Int): Int   = nativeDivide(a, b)

    fun bmiCategory(bmi: Float): String = when {
        bmi < 18.5f -> "Underweight"
        bmi < 25.0f -> "Normal"
        bmi < 30.0f -> "Overweight"
        else        -> "Obese"
    }

    // JNI — private, never visible to app code
    private external fun nativeGetBMI(height: Float, weight: Float): Float
    private external fun nativeAdd(a: Int, b: Int): Int
    private external fun nativeSubtract(a: Int, b: Int): Int
    private external fun nativeMultiply(a: Int, b: Int): Int
    private external fun nativeDivide(a: Int, b: Int): Int
}
```

The `private external` pattern is the key design: the app calls `manager.getBMI()` (a public Kotlin function). The `nativeGetBMI` JNI function is private — app code cannot call it directly. This is what makes the manager a true abstraction layer.

---

## Step 2: BMICalculatorB App

### Android.bp (`vendor/myoem/apps/BMICalculatorB/Android.bp`)

```bp
soong_namespace {
    imports: ["vendor/myoem/libs/bmicalculator"],
}

android_library {
    name: "BMICalculatorBLib",
    static_libs: [
        "bmicalculator-manager",  // ← ONLY binder-related dependency
        "kotlin-stdlib",
        "kotlinx-coroutines-android",
        "androidx.compose.material3_material3",
        // ... other Compose deps
    ],
    platform_apis: true,
}

android_app {
    name: "BMICalculatorB",
    platform_apis: true,
    privileged: true,
    certificate: "platform",
    static_libs: ["BMICalculatorBLib"],
    jni_libs: ["libbmicalmanager_jni"],  // packages .so from manager lib
    optimize: { enabled: false },
}
```

The app imports `bmicalculator-manager` as a `static_lib`. The `jni_libs` entry causes Soong to package `libbmicalmanager_jni.so` inside the APK and install it to `/system/lib64/`.

### MainActivity.kt — The Payoff

```kotlin
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // No ServiceManager. No IBinder. No JNI imports. Just ViewModel.
        val viewModel = ViewModelProvider(this)[BmiCalBViewModel::class.java]
        setContent {
            BMICalculatorBTheme { BmiCalScreen(viewModel = viewModel) }
        }
    }
}
```

BMICalculatorA's MainActivity called `ServiceManager.checkService()` twice and passed raw `IBinder` objects to the ViewModel factory. BMICalculatorB's MainActivity has **zero binder code**. This is the manager pattern in action.

### BmiCalBViewModel.kt

```kotlin
class BmiCalBViewModel : ViewModel() {
    private val manager = BmiCalManager()  // ← just instantiate the library

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    init {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.update {
                it.copy(
                    bmiServiceAvailable  = manager.isBmiAvailable(),
                    calcServiceAvailable = manager.isCalcAvailable(),
                )
            }
        }
    }

    fun computeBmi(input1: String, input2: String) {
        val height = input1.toFloatOrNull()
        val weight = input2.toFloatOrNull()
        if (height == null || weight == null || height <= 0f || weight <= 0f) {
            _uiState.update { it.copy(error = "Enter valid height (m) and weight (kg)") }
            return
        }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bmi = manager.getBMI(height, weight)  // ← looks like a normal call
                _uiState.update {
                    it.copy(result = "BMI: %.2f  (%s)".format(bmi, manager.bmiCategory(bmi)))
                }
            } catch (e: Exception) {
                _uiState.update { it.copy(error = e.message ?: "bmid error") }
            }
        }
    }

    fun computeAdd(input1: String, input2: String) = calcOp("Add", input1, input2) { a, b -> manager.add(a, b) }
    // subtract, multiply, divide follow same pattern...
}
```

No `IBinder`, no `Parcel`, no `NativeBinder`. From this ViewModel's perspective, `BmiCalManager` is an ordinary Kotlin class. The binder complexity is completely encapsulated.

---

## Step 3: myoem_base.mk

```makefile
PRODUCT_SOONG_NAMESPACES += \
    vendor/myoem/libs/bmicalculator \
    vendor/myoem/apps/BMICalculatorB

PRODUCT_PACKAGES += \
    bmicalculator-manager \
    libbmicalmanager_jni \
    BMICalculatorB
```

---

## Build and Install

```bash
# Build
m BMICalculatorB

# Verify .so was produced
find out/target/product/rpi5/system/lib64 -name "libbmicalmanager_jni.so"

# Install
adb root && adb shell mount -o remount,rw / && \
adb push out/target/product/rpi5/system/lib64/libbmicalmanager_jni.so \
         /system/lib64/ && \
adb push out/target/product/rpi5/system/priv-app/BMICalculatorB/BMICalculatorB.apk \
         /system/priv-app/BMICalculatorB/BMICalculatorB.apk && \
adb shell mkdir -p /system/priv-app/BMICalculatorB/lib/arm64 && \
adb shell ln -sf /system/lib64/libbmicalmanager_jni.so \
         /system/priv-app/BMICalculatorB/lib/arm64/libbmicalmanager_jni.so && \
adb reboot
```

**Important:** Push the APK to the full file path (`/BMICalculatorB.apk`), not just the directory. Pushing to the directory silently succeeds but the app won't install because the PackageManager won't find the APK.

---

## Why the Manager Pattern Matters

### For app developers

BMICalculatorA developer must know:
- JNI exists and how to load `.so` files
- `libbinder` vs `libbinder_ndk` and why the difference matters
- `FLAG_PRIVATE_VENDOR` and the binder stability system
- VINTF manifest format and `/default` service name convention
- Parcel format: `writeInterfaceToken`, exception codes `EX_NONE`/`EX_SERVICE_SPECIFIC`

BMICalculatorB developer must know:
- `BmiCalManager` exists
- Call its methods on a background thread (`Dispatchers.IO`)

That's the whole point: **the binder transport becomes an implementation detail**.

### For AOSP architecture

This mirrors how Android's own framework works:
- Apps call `AudioManager.setVolume()`, `LocationManager.getLastLocation()`
- These managers talk to system services via binder internally
- Apps never see a single `IBinder` or `Parcel`

Our `BmiCalManager` follows the same pattern at the OEM vendor layer. If the underlying service changes (service name, AIDL version, transport mechanism), only the manager needs to change — apps are unaffected.

### When to use A vs B

| Use BMICalculatorA pattern when | Use BMICalculatorB pattern when |
|---|---|
| One-off experiment / learning | Production code |
| Only one app will ever call the service | Multiple apps need the service |
| Service API is stable and won't change | Service might change |
| You want to understand binder internals | You want app developers insulated from binder |

---

## Complete Comparison: A vs B

| | BMICalculatorA | BMICalculatorB |
|---|---|---|
| `MainActivity` binder code | `ServiceManager.checkService()` × 2 | None (0 lines) |
| ViewModel binder imports | `NativeBinder` (JNI object in app) | `BmiCalManager` (external library) |
| Who owns the JNI | The app | The manager library |
| App `Android.bp` namespace imports | bmi + calculator services | bmicalculator library |
| `jni_libs` in app | `libbmicalculator_jni` | `libbmicalmanager_jni` |
| Lines of binder code in app | ~50 (NativeBinder.kt) | 0 |
| Reusability | App-specific | Any app can use BmiCalManager |
| Testing | Must test JNI in app context | Can unit-test BmiCalManager separately |
| `sdk_version` of manager | N/A (JNI in app) | `current` (no @hide needed) |

---

## File Summary

```
vendor/myoem/libs/bmicalculator/
├── Android.bp                              ← cc_library_shared + java_library
├── jni/
│   └── BmiCalManagerJni.cpp               ← C++ JNI, FLAG_PRIVATE_VENDOR
└── java/com/myoem/bmicalculator/
    └── BmiCalManager.kt                   ← public Kotlin API, private JNI

vendor/myoem/apps/BMICalculatorB/
├── Android.bp                              ← imports bmicalculator namespace
├── AndroidManifest.xml                    ← package com.myoem.bmicalculatorb
└── src/main/kotlin/com/myoem/bmicalculatorb/
    ├── MainActivity.kt                    ← 4 lines, zero binder code
    ├── BmiCalBViewModel.kt                ← uses BmiCalManager like any library
    └── ui/
        ├── BmiCalScreen.kt                ← identical UI to App A
        └── theme/Theme.kt

vendor/myoem/common/myoem_base.mk
    PRODUCT_SOONG_NAMESPACES += vendor/myoem/libs/bmicalculator
    PRODUCT_SOONG_NAMESPACES += vendor/myoem/apps/BMICalculatorB
    PRODUCT_PACKAGES         += bmicalculator-manager libbmicalmanager_jni BMICalculatorB
```

---

## Key Lessons

### Lesson 1: The Manager Pattern Is About Encapsulation
The manager pattern isn't about adding a layer for its own sake. It's about deciding who bears the responsibility for binder complexity. In A, the app bears it. In B, the library bears it. B is the right answer for any code that will be reused or maintained by others.

### Lesson 2: JNI Symbol Names Must Match Exactly
The JNI function name is derived from the fully-qualified Java/Kotlin class name:
```
Java_<package_underscores>_<ClassName>_<methodName>
com.myoem.bmicalculator.BmiCalManager.nativeGetBMI
→ Java_com_myoem_bmicalculator_BmiCalManager_nativeGetBMI
```
If the package or class name changes, all JNI symbols must be updated.

### Lesson 3: `sdk_version: "current"` Is Possible When JNI Does the System Work
If your manager library uses `@hide` APIs (like `ServiceManager.checkService()`), it must use `platform_apis: true` or `sdk_version: "system_current"`. But if all `@hide` work is done in C++ JNI (`defaultServiceManager()` is available in libbinder which is always linked), the Kotlin/Java layer can use `sdk_version: "current"` — keeping the library more portable.

### Lesson 4: Push APK to Full File Path, Not Directory
```bash
# WRONG — APK lands incorrectly
adb push BMICalculatorB.apk /system/priv-app/BMICalculatorB/

# CORRECT — explicit filename
adb push BMICalculatorB.apk /system/priv-app/BMICalculatorB/BMICalculatorB.apk
```
When pushing to a directory that already exists, adb places the file inside it with the source filename. But if the directory already exists with a different structure from a previous install, the file may not end up where PackageManager expects it.

---

*This completes the BMI Calculator two-app series. Part 10 showed the direct approach (A). Part 11 showed the manager pattern (B). Together they demonstrate the full spectrum of how Android apps can call OEM vendor binder services.*
