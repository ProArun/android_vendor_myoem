# Part 10: Building BMICalculatorA — The Direct Binder App

## Subtitle: What Happens When a Java App Tries to Talk Directly to an NDK Vendor Service in Android 15

**Series**: AOSP OEM Vendor Layer Development on Raspberry Pi 5  
**GitHub**: ProArun  
**Device**: Raspberry Pi 5 | AOSP android-15.0.0_r14  
**Prerequisites**: Parts 1–9 (Calculator service, BMI service, Manager pattern)

---

## Overview

In this part we build **BMICalculatorA** — a single-page Android app that talks directly to both our vendor binder services (`bmid` and `calculatord`) without any manager layer in between.

This is Part A of a two-part app series:

| App | Architecture |
|-----|-------------|
| **BMICalculatorA** (this article) | App → directly → vendor services |
| **BMICalculatorB** (next article) | App → BMICalManager → vendor services |

The comparison between A and B teaches the **manager pattern** — why it exists, what it protects you from, and what happens when you skip it.

Spoiler: building Part A is harder than expected. Android 15 has opinions about Java apps calling NDK vendor services directly. We hit three distinct bugs. This article documents all of them.

---

## What We're Building

A single-page Compose app with:
- Two text fields: **Value 1** (height in metres for BMI / integer A for arithmetic) and **Value 2** (weight in kg / integer B)
- Five buttons: **Calculate BMI**, **Add (A+B)**, **Subtract (A−B)**, **Multiply (A×B)**, **Divide (A÷B)**
- A result card at the bottom showing the answer or an error
- Service status chips at the top showing whether `bmid` and `calculatord` are reachable
- App name in the TopAppBar

---

## The Architecture We Intended

```
┌─────────────────────────────────┐
│      BMICalculatorA (App)       │
│                                  │
│  MainActivity                   │
│    └─ ServiceManager.checkService() ← @hide API
│         ↓ IBinder                │
│    BmiCalViewModel               │
│         ↓ IBinder.transact()     │
└──────────────┬──────────────────┘
               │ Kernel Binder
     ┌─────────┴──────────┐
     ▼                    ▼
  bmid                calculatord
(IBMIService)    (ICalculatorService)
  /vendor/bin      /vendor/bin
```

This should be straightforward. We've already proven both services work with `bmi_client` and `calculator_client` (the NDK C++ test clients). Now we write the same logic in Kotlin.

---

## App File Structure

```
vendor/myoem/apps/BMICalculatorA/
├── Android.bp
├── AndroidManifest.xml
└── src/main/
    ├── kotlin/com/myoem/bmicalculatora/
    │   ├── MainActivity.kt          ← resolves binders via ServiceManager
    │   ├── BmiCalViewModel.kt       ← all binder calls on Dispatchers.IO
    │   └── ui/
    │       ├── BmiCalScreen.kt      ← Compose single-page UI
    │       └── theme/Theme.kt
    └── res/values/
        ├── strings.xml
        └── styles.xml
```

### Android.bp Key Points

```bp
soong_namespace {
    imports: [
        "vendor/myoem/services/bmi",
        "vendor/myoem/services/calculator",
    ],
}

android_library {
    name: "BMICalculatorALib",
    static_libs: [
        "bmiservice-aidl-java",        // Java stubs for IBMIService
        "calculatorservice-aidl-java", // Java stubs for ICalculatorService
        "kotlin-stdlib",
        "kotlinx-coroutines-android",
        "androidx.compose.material3_material3",
        // ... other Compose deps
    ],
    platform_apis: true,  // needed for ServiceManager.checkService()
}

android_app {
    name: "BMICalculatorA",
    platform_apis: true,
    privileged: true,      // installs to /system/priv-app/
    certificate: "platform",
    static_libs: ["BMICalculatorALib"],
}
```

**Why `privileged: true`?** Our app calls `ServiceManager.checkService()` which is a `@hide` API. Only apps in `/system/priv-app/` (privileged apps) can call `@hide` APIs when built with `platform_apis: true`.

**Why split into `android_library` + `android_app`?** Same reason as ThermalMonitor — AOSP has no "test sourceSet inherits main" concept like Gradle. Splitting lets test modules reuse production code without R-class conflicts.

---

## Initial Implementation

### MainActivity — The Only Place We Use @hide APIs

```kotlin
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // ServiceManager.checkService() is @hide — only here, nowhere else
        val bmiBinder  = ServiceManager.checkService("com.myoem.bmi.IBMIService")
        val calcBinder = ServiceManager.checkService("com.myoem.calculator.ICalculatorService")

        val bmiService  = bmiBinder?.let  { IBMIService.Stub.asInterface(it) }
        val calcService = calcBinder?.let { ICalculatorService.Stub.asInterface(it) }

        val viewModel = ViewModelProvider(
            this,
            BmiCalViewModelFactory(bmiService, calcService)
        )[BmiCalViewModel::class.java]

        setContent {
            BMICalculatorATheme { BmiCalScreen(viewModel = viewModel) }
        }
    }
}
```

The design principle: **@hide APIs are isolated to MainActivity**. The ViewModel receives `IBMIService` and `ICalculatorService` objects — it never imports `ServiceManager` or any system-internal class.

### BmiCalViewModel — All Binder Calls on IO Thread

```kotlin
class BmiCalViewModel(
    private val bmiService: IBMIService?,
    private val calcService: ICalculatorService?,
) : ViewModel() {

    fun computeBmi(input1: String, input2: String) {
        val height = input1.toFloatOrNull() ?: return showError("Invalid height")
        val weight = input2.toFloatOrNull() ?: return showError("Invalid weight")

        viewModelScope.launch(Dispatchers.IO) {   // ← MUST be off main thread
            try {
                val bmi = bmiService!!.getBMI(height, weight)
                _uiState.update { it.copy(result = "BMI: %.2f (%s)".format(bmi, bmiCategory(bmi))) }
            } catch (e: ServiceSpecificException) {
                showError("Invalid input (code ${e.errorCode})")
            } catch (e: RemoteException) {
                showError("bmid: ${e.message}")
            }
        }
    }
}
```

**Why `Dispatchers.IO`?** Binder calls are blocking network-like operations. Calling them on the main thread causes ANR (Application Not Responding). Any binder call that crosses process boundaries must happen on a background thread.

---

## Bug #1: App Crashes on Button Press

### Symptom

```
E AndroidRuntime: FATAL EXCEPTION: DefaultDispatcher-worker-1
E AndroidRuntime: java.lang.IllegalArgumentException
E AndroidRuntime:   at android.os.BinderProxy.transactNative(Native Method)
E AndroidRuntime:   at android.os.BinderProxy.transact(BinderProxy.java:586)
E AndroidRuntime:   at com.myoem.bmi.IBMIService$Stub$Proxy.getBMI(IBMIService.java:148)
```

And this from ActivityManager:

```
W ActivityManager: pid 2036 com.myoem.bmicalculatora sent binder code 1 
                   with flags 2 to frozen apps and got error -2147483647
```

### Root Cause

`IllegalArgumentException` is **not** a subclass of `RemoteException`. Our try/catch had:

```kotlin
} catch (e: ServiceSpecificException) { ... }
} catch (e: RemoteException) { ... }
// IllegalArgumentException is RuntimeException → neither catch fires → crash
```

The exception hierarchy in Android Binder:
```
java.lang.Exception
├── java.lang.RuntimeException
│   └── java.lang.IllegalArgumentException  ← NOT caught by RemoteException
└── android.os.RemoteException
    ├── android.os.DeadObjectException
    ├── android.os.ServiceSpecificException
    └── android.os.TransactionTooLargeException
```

### Fix

Add a broad catch at the bottom of every try block:

```kotlin
} catch (e: ServiceSpecificException) { /* ... */ }
} catch (e: RemoteException) { /* ... */ }
} catch (e: Exception) {
    // Catches IllegalArgumentException and any other runtime exception
    _uiState.update { it.copy(error = "${e.javaClass.simpleName}: ${e.message}", result = "—") }
}
```

**Lesson**: In Android binder code, always add `catch (e: Exception)` as a final safety net. The binder framework can throw exceptions that are not subclasses of `RemoteException` — especially in Android 12+ where `IllegalArgumentException` is thrown for frozen-process detection.

---

## Bug #2: App No Longer Crashes But Shows IllegalArgumentException Error

After fixing the crash, the app shows the error message instead of crashing — progress. But the binder call still fails with `IllegalArgumentException`. The `bmi_client` NDK binary works fine from ADB. Why does Java fail when C++ succeeds?

### Understanding the Java Binder Stack

When a Java app calls a vendor service, the call goes through this stack:

```
Java:    bmiService.getBMI(height, weight)
            ↓
         IBMIService.Stub.Proxy.getBMI()
            ↓
         BinderProxy.transact(code=1, data, reply, flags=2)
            ↓
    ┌── BinderProxyTransactListener.onTransactStarted()  ← AMS INTERCEPTS HERE
    │        (ActivityManagerService listener)
    │        AMS checks: is the target process frozen?
    │        YES → throws IllegalArgumentException
    │        NO  → continues
    ↓
         transactNative() → kernel binder → bmid
```

Android 12 introduced `BinderProxyTransactListener`. ActivityManagerService registers a listener on ALL `BinderProxy.transact()` calls. In Android 15, this listener checks if the target process (bmid) appears frozen. If it does, it throws `IllegalArgumentException` before the call even reaches the kernel.

**Why is bmid "frozen"?** Unlike `thermalcontrold` (which is polled every 2 seconds by ThermalMonitor), `bmid` and `calculatord` sit idle between button presses. The binder freeze mechanism marks idle vendor processes as frozen. The Java listener then blocks Java calls to them.

**Why does `bmi_client` work?** The NDK binder path (`AIBinder_transact`) goes directly to `BpBinder::transact()` → kernel binder, **bypassing the Java `BinderProxy` layer entirely**. The AMS listener is only on the Java path.

```
bmi_client / JNI:              Java app:
AIBinder_transact()         BinderProxy.transact()
      ↓                              ↓
BpBinder::transact()    BinderProxyTransactListener  ← blocks here
      ↓                              ↓
 kernel binder ✅          IllegalArgumentException ❌
```

### What the `flags 2` Means

The ActivityManager log shows `flags 2`. This is `FLAG_COLLECT_NOTED_APP_OPS = 0x02`, which the AIDL-generated Java stub adds automatically. This flag signals that the Java binder framework wants to collect app ops data. The AMS listener uses this flag to identify calls from Java apps (as opposed to NDK binder calls which have no such flag).

---

## Bug #3: Raw IBinder.transact() Also Fails

### Attempted Fix

To bypass the Java AIDL stub (which adds `FLAG_COLLECT_NOTED_APP_OPS`), we switched to raw `IBinder.transact()` with `flags = 0` and wrote the parcel manually:

```kotlin
val data  = Parcel.obtain()
val reply = Parcel.obtain()
try {
    data.writeFloat(height)   // no writeInterfaceToken()
    data.writeFloat(weight)
    bmiBinder.transact(TX_GET_BMI, data, reply, 0)  // flags = 0
    when (reply.readInt()) {
        0  -> { val bmi = reply.readFloat(); showResult(bmi) }
        -8 -> { /* EX_SERVICE_SPECIFIC */ }
    }
}
```

The hypothesis was: by not using the generated stub, we avoid `FLAG_COLLECT_NOTED_APP_OPS`, and the AMS listener won't fire.

### Why It Still Failed

`IBinder.transact()` in Java still calls `BinderProxy.transact()` internally. `BinderProxy.transact()` has the `BinderProxyTransactListener` check. Setting `flags = 0` doesn't bypass the listener — the listener checks the frozen state of the target process, not the flags.

---

## The Real Diagnosis: Two Separate Binder Paths

The fundamental architectural truth:

| Path | Through Java BinderProxy? | AMS Listener? | Works for frozen vendor services? |
|------|--------------------------|---------------|----------------------------------|
| `AIBinder_transact()` (NDK) | No | No | ✅ Yes |
| `IBinder.transact()` (Java) | Yes | Yes | ❌ Blocked in Android 15 |

`ServiceManager.checkService()` returns a Java `BinderProxy` object. Every method call on a Java binder (whether via AIDL stub or raw `transact()`) goes through `BinderProxy.transact()`. There is no way to avoid this in pure Java code.

**The only way to use the NDK binder path from an Android app is via JNI.**

---

## The Solution: JNI (Java Native Interface)

JNI lets us write C++ code that runs inside the Java app process but uses NDK APIs. The C++ JNI code calls `AServiceManager_checkService()` and `AIBinder_transact()` — exactly what `bmi_client` does — and bypasses `BinderProxy.transact()` entirely.

### Architecture with JNI

```
┌──────────────────────────────────────────┐
│         BMICalculatorA (App Process)     │
│                                           │
│  Kotlin/Java Layer                        │
│    BmiCalViewModel                        │
│         ↓                                 │
│    NativeBinder.kt (companion object)     │
│         ↓  System.loadLibrary()           │
│  JNI Layer (libbmicalculator_jni.so)     │
│    BmiCalJni.cpp                          │
│         ↓  AServiceManager_checkService() │
│         ↓  AIBinder_transact()            │
└──────────────────┬───────────────────────┘
                   │ Kernel Binder (NDK path)
         ┌─────────┴──────────┐
         ▼                    ▼
      bmid               calculatord
   (IBMIService)    (ICalculatorService)
```

JNI is still "direct" — the app calls the service with zero intermediaries. The binder transaction is identical to what `bmi_client` sends.

### The JNI Library (BmiCalJni.cpp)

```cpp
#define LOG_TAG "BMICalculatorA"
#include <jni.h>
#include <android/binder_manager.h>
#include <log/log.h>
#include <aidl/com/myoem/bmi/IBMIService.h>
#include <aidl/com/myoem/calculator/ICalculatorService.h>

using namespace aidl::com::myoem::bmi;
using namespace aidl::com::myoem::calculator;

extern "C" {

JNIEXPORT jfloat JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_getBMI(
        JNIEnv* env, jclass, jfloat height, jfloat weight) {
    // AServiceManager_checkService → same as bmi_client
    ndk::SpAIBinder binder(AServiceManager_checkService("com.myoem.bmi.IBMIService"));
    auto service = IBMIService::fromBinder(binder);
    if (!service) {
        jniThrowException(env, "java/lang/RuntimeException", "bmid not available");
        return 0.0f;
    }
    float result = 0.0f;
    ndk::ScopedAStatus status = service->getBMI(height, weight, &result);
    if (!status.isOk()) {
        if (status.getServiceSpecificError() == IBMIService::ERROR_INVALID_INPUT) {
            jniThrowException(env, "java/lang/IllegalArgumentException",
                              "height and weight must be positive");
        } else {
            jniThrowException(env, "java/lang/RuntimeException", "getBMI failed");
        }
        return 0.0f;
    }
    return result;
}

JNIEXPORT jint JNICALL
Java_com_myoem_bmicalculatora_NativeBinder_divide(
        JNIEnv* env, jclass, jint a, jint b) {
    ndk::SpAIBinder binder(AServiceManager_checkService("com.myoem.calculator.ICalculatorService"));
    auto service = ICalculatorService::fromBinder(binder);
    if (!service) {
        jniThrowException(env, "java/lang/RuntimeException", "calculatord not available");
        return 0;
    }
    int32_t result = 0;
    ndk::ScopedAStatus status = service->divide(a, b, &result);
    if (!status.isOk()) {
        if (status.getServiceSpecificError() == ICalculatorService::ERROR_DIVIDE_BY_ZERO) {
            jniThrowException(env, "java/lang/ArithmeticException", "divide by zero");
        } else {
            jniThrowException(env, "java/lang/RuntimeException", "divide failed");
        }
        return 0;
    }
    return result;
}

// ... add, subtract, multiply follow the same pattern

} // extern "C"
```

### The Kotlin Wrapper (NativeBinder.kt)

```kotlin
object NativeBinder {
    init { System.loadLibrary("bmicalculator_jni") }

    @JvmStatic external fun isBmiAvailable(): Boolean
    @JvmStatic external fun isCalcAvailable(): Boolean
    @JvmStatic external fun getBMI(height: Float, weight: Float): Float
    @JvmStatic external fun add(a: Int, b: Int): Int
    @JvmStatic external fun subtract(a: Int, b: Int): Int
    @JvmStatic external fun multiply(a: Int, b: Int): Int
    @JvmStatic external fun divide(a: Int, b: Int): Int
}
```

### Updated ViewModel — Uses NativeBinder

```kotlin
class BmiCalViewModel : ViewModel() {

    // Check availability at startup via JNI (no ServiceManager needed)
    private val _uiState = MutableStateFlow(
        UiState(
            bmiServiceAvailable  = NativeBinder.isBmiAvailable(),
            calcServiceAvailable = NativeBinder.isCalcAvailable(),
        )
    )

    fun computeBmi(input1: String, input2: String) {
        val height = input1.toFloatOrNull()
        val weight = input2.toFloatOrNull()
        if (height == null || weight == null || height <= 0f || weight <= 0f) {
            _uiState.update { it.copy(error = "Enter valid height (m) and weight (kg)") }
            return
        }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val bmi = NativeBinder.getBMI(height, weight)
                _uiState.update {
                    it.copy(result = "BMI: %.2f  (%s)".format(bmi, bmiCategory(bmi)), error = null)
                }
            } catch (e: Exception) {
                _uiState.update { it.copy(error = e.message ?: e.javaClass.simpleName, result = "—") }
            }
        }
    }

    // No factory needed — ViewModel has no constructor args
}
```

### Simplified MainActivity — No More ServiceManager

```kotlin
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // No ServiceManager calls needed — JNI resolves service names internally
        val viewModel = ViewModelProvider(this)[BmiCalViewModel::class.java]
        setContent {
            BMICalculatorATheme { BmiCalScreen(viewModel = viewModel) }
        }
    }
}
```

### Updated Android.bp

```bp
soong_namespace {
    imports: [
        "vendor/myoem/services/bmi",
        "vendor/myoem/services/calculator",
    ],
}

// JNI shared library — links against NDK AIDL stubs
cc_library_shared {
    name: "libbmicalculator_jni",
    srcs: ["jni/BmiCalJni.cpp"],
    shared_libs: [
        "libbinder_ndk",  // LLNDK — available in both system and vendor
        "liblog",
    ],
    static_libs: [
        "bmiservice-aidl-ndk",
        "calculatorservice-aidl-ndk",
    ],
    stl: "c++_shared",
    header_libs: ["jni_headers"],
}

android_library {
    name: "BMICalculatorALib",
    srcs: ["src/main/kotlin/**/*.kt"],
    resource_dirs: ["src/main/res"],
    manifest: "AndroidManifest.xml",
    static_libs: [
        // No Java AIDL stubs needed — JNI handles binder calls
        "kotlin-stdlib",
        "kotlinx-coroutines-android",
        "androidx.compose.material3_material3",
        // ... other Compose deps
    ],
    platform_apis: true,
}

android_app {
    name: "BMICalculatorA",
    platform_apis: true,
    privileged: true,
    certificate: "platform",
    manifest: "AndroidManifest.xml",
    static_libs: ["BMICalculatorALib"],
    jni_libs: ["libbmicalculator_jni"],  // bundles .so into APK
    optimize: { enabled: false },
}
```

---

## Build and Install

```bash
# Build
lunch myoem_rpi5-trunk_staging-userdebug && m BMICalculatorA

# Install (system partition is read-only; remount or write full image)
adb root
adb shell mount -o remount,rw /system   # if system is separate mount
# OR:  adb shell mount -o remount,rw /  # if system-as-root
adb push out/target/product/rpi5/system/priv-app/BMICalculatorA/BMICalculatorA.apk \
         /system/priv-app/BMICalculatorA/BMICalculatorA.apk
adb reboot
```

**Note on RPi5**: There is no fastboot bootloader and no `adb remount`. For system partition changes, you either remount manually (above) or write the full system image to SD card via `dd`. Vendor partition changes use `adb shell mount -o remount,rw /vendor`.

---

## Key Lessons

### Lesson 1: IllegalArgumentException Is Not RemoteException

Always add a broad `catch (e: Exception)` in binder-calling coroutines. Android 12+ binder code can throw `IllegalArgumentException`, `IllegalStateException`, and other `RuntimeException` subclasses that are NOT caught by `RemoteException`:

```kotlin
try {
    // binder call
} catch (e: ServiceSpecificException) { /* service-level error */ }
  catch (e: RemoteException) { /* transport error */ }
  catch (e: Exception) { /* everything else — MUST have this */ }
```

### Lesson 2: Java BinderProxy and NDK Binder Are Different Paths

Android has two binder call paths:

```
Java:  BinderProxy.transact() → BinderProxyTransactListener → kernel
NDK:   AIBinder_transact()   → BpBinder::transact()         → kernel
```

In Android 12+, AMS registers a listener on the Java path. In Android 15, this listener blocks calls to frozen vendor processes. The NDK path has no such listener.

**Consequence**: A Java app cannot reliably call idle NDK vendor services directly via `IBinder.transact()` in Android 15. The fix is JNI, which uses the NDK path from within the app process.

### Lesson 3: Idle Vendor Services Get Frozen

Services that receive infrequent traffic (like `bmid` and `calculatord` which are only called on button press) can be marked as frozen by Android's binder freeze mechanism. Services with constant traffic (like `thermalcontrold` polled every 2 seconds) stay warm.

If you need a vendor service callable from Java, either:
- Use JNI to bypass the frozen-process check (for "direct" access)
- Use a manager library that polls frequently (keeps service warm)
- Use the manager pattern for event-driven calls (BMICalculatorB approach)

### Lesson 4: JNI Is Still "Direct"

JNI is not a workaround — it IS the correct mechanism for calling NDK services from Java. The app calls `NativeBinder.getBMI(height, weight)` → JNI calls `AIBinder_transact()` → kernel binder → bmid. This is identical to `bmi_client`. The binder transaction is exactly the same. JNI just packages the NDK call inside the app process.

### Lesson 5: Why libbinder_ndk (LLNDK) Works from App Space

`libbinder_ndk.so` is an **LLNDK** (Low-Level NDK) library — it's available in both system and vendor partitions. A JNI library in a system app can link against LLNDK libraries. This is how Android's camera, audio, and other HAL services are accessed: system apps → JNI → LLNDK (libbinder_ndk) → vendor services.

---

## Comparison: BMICalculatorA vs BMICalculatorB

| Aspect | BMICalculatorA | BMICalculatorB |
|--------|---------------|----------------|
| Calls services | Via JNI → NDK binder | Via Java manager library |
| MainActivity needs @hide? | No (JNI resolves names) | Yes (ServiceManager for manager) |
| Service frozen issue | Bypassed via NDK path | Managed by manager's caching |
| Code complexity | Medium (JNI C++ needed) | Low (all Kotlin/Java) |
| Learning objective | Direct NDK binder from app | Manager pattern |

---

## What's Next

In Part 11 we build **BMICalculatorB** and **BMICalManager**:
- `BMICalManager` — a Java SDK library that wraps both services
- `BMICalculatorB` — identical UI to A, but talks to `BMICalManager` instead of services directly
- We'll see how the manager pattern avoids all the JNI complexity while also providing caching, error handling, and a stable API surface

---

## Summary of Files Created

```
vendor/myoem/apps/BMICalculatorA/
├── Android.bp                                    ← JNI lib + android_app
├── AndroidManifest.xml
├── jni/
│   └── BmiCalJni.cpp                             ← C++ JNI, NDK binder calls
└── src/main/kotlin/com/myoem/bmicalculatora/
    ├── MainActivity.kt                            ← no ServiceManager needed
    ├── BmiCalViewModel.kt                         ← uses NativeBinder
    ├── NativeBinder.kt                            ← Kotlin JNI wrapper
    └── ui/
        ├── BmiCalScreen.kt                        ← Compose UI
        └── theme/Theme.kt
```

```
vendor/myoem/common/myoem_base.mk
  PRODUCT_PACKAGES    += BMICalculatorA
  PRODUCT_SOONG_NAMESPACES += vendor/myoem/apps/BMICalculatorA
```

---

*Next: Part 11 — BMICalculatorB and BMICalManager: The Manager Pattern in Practice*

---

# Part 10 — Appendix: Full Debugging Journal

This appendix documents every bug hit during the actual implementation, in the exact order they appeared, with the error, the diagnosis, the failed attempts, and the final fix. Read this before starting any similar project.

---

## Bug A: App Crashes on Button Press — `UnsatisfiedLinkError`

### Symptom
```
E AndroidRuntime: java.lang.UnsatisfiedLinkError: dlopen failed:
    library "libbmicalculator_jni.so" not found
```

### Root Cause
For system apps installed in `/system/priv-app/`, Android does NOT extract native libs from the APK into `/data/`. It expects the `.so` to be present next to the APK as a separate file. The build produces:
```
system/priv-app/BMICalculatorA/BMICalculatorA.apk
system/priv-app/BMICalculatorA/lib/arm64/libbmicalculator_jni.so  ← symlink
system/lib64/libbmicalculator_jni.so                              ← real file
```
Only the APK was pushed. The `.so` was missing on device.

### Fix
Push both the real `.so` and create the symlink:
```bash
adb root && adb shell mount -o remount,rw /
adb push out/target/product/rpi5/system/lib64/libbmicalculator_jni.so /system/lib64/
adb shell ln -sf /system/lib64/libbmicalculator_jni.so \
    /system/priv-app/BMICalculatorA/lib/arm64/libbmicalculator_jni.so
adb reboot
```

### Lesson
For system apps, the build installs JNI libs to `/system/lib64/` and creates a symlink from the priv-app dir. Both must be present on the device. For user apps (`/data/app/`) the package manager extracts the `.so` from the APK automatically — system apps work differently.

---

## Bug B: Read-Only Filesystem on Push

### Symptom
```
adb: error: failed to copy '...': remote couldn't create file: Read-only file system
```

### Root Cause
`mount -o remount,rw /` must be done in the same adb session immediately before every push. Each reboot resets the partition back to read-only.

### Fix
Always chain all commands in a single line with `&&`:
```bash
adb root && \
adb shell mount -o remount,rw / && \
adb push <file> <dest> && \
adb reboot
```
Never push in a separate terminal session after rebooting.

---

## Bug C: App Shows `IllegalArgumentException` (No Crash, But No Result)

### Symptom
App no longer crashes (after adding `catch (e: Exception)`) but buttons show:
```
bmid: IllegalArgumentException
calculatord: IllegalArgumentException
```
ActivityManager log:
```
W ActivityManager: pid 2036 com.myoem.bmicalculatora sent binder code 1
                   with flags 2 to frozen apps and got error -2147483647
```

### Root Cause
`BinderProxy.transact()` in Android 15 has a `BinderProxyTransactListener` set by ActivityManagerService. This listener detects that bmid/calculatord are **frozen** (idle vendor processes) and throws `IllegalArgumentException` — not `RemoteException`. Since `IllegalArgumentException extends RuntimeException`, not `RemoteException`, a `catch (e: RemoteException)` does not catch it.

Both `IBinder.transact()` (raw) and `IBMIService.Stub.asInterface().getBMI()` (Java AIDL stub) go through `BinderProxy.transact()` internally — there is no way to bypass this in pure Java code.

### Failed Attempt
Switching to raw `IBinder.transact()` with `flags=0` to avoid `FLAG_COLLECT_NOTED_APP_OPS`. This did NOT help — the listener checks process freeze state, not the flags.

### Fix
Use JNI to call `AServiceManager_checkService()` + NDK AIDL stubs. JNI C++ runs `AIBinder_transact()` → `BpBinder::transact()` → kernel. No `BinderProxy`, no AMS listener, no `IllegalArgumentException` possible.

### Lesson
The Java and NDK binder paths are completely separate:
```
Java:  BinderProxy.transact() → BinderProxyTransactListener → kernel
NDK:   AIBinder_transact()    → BpBinder::transact()         → kernel
```
Any Java binder call (raw or AIDL stub) goes through `BinderProxy`. JNI is the only way to use the NDK path from an Android app.

---

## Bug D: JNI `getBMI transact failed: -2147483647` (BAD_TYPE — First Occurrence)

### Symptom
```
getBMI transact failed: -2147483647
add transact failed: -2147483647
```

### Root Cause (Attempt 1 — NDK stubs without stability fix)
Using `libbinder_ndk` + `IBMIService::fromBinder()`. The NDK AIDL stubs' system-partition variant (built into `libbmicalculator_jni.so` on system partition) carries LOCAL stability. The services are registered from vendor partition → VENDOR stability. `AIBinder_transact` enforces stability: LOCAL caller cannot call VENDOR service.

### Failed Attempt 1
Switch to raw `libbinder` `BpBinder::transact`. The stability check also lives in `BpBinder::transact` for user transactions — still returned `BAD_TYPE`.

### Failed Attempt 2
Call `AIBinder_forceDowngradeToLocalStability()` on the proxy received from `AServiceManager_checkService()`. Build succeeded but crashed at runtime:
```
Abort message: 'Can only downgrade local binder'
```
This function can ONLY be called on a local (server-side) binder. Calling it on a remote proxy always aborts.

### Failed Attempt 3
Call `AIBinder_markVintfStability()` in the service's `main.cpp` before `AServiceManager_addService()`. Services crashed with:
```
E Stability: Interface being set with vintf stability but it is already marked as vendor stability
```
The NDK AIDL backend automatically marks vendor-partition binders as VENDOR stability. You cannot override to VINTF on top of that.

### Failed Attempt 4
Add VINTF manifest fragments (`bmid.xml`, `calculatord.xml`) + change service names to `<package>/<instance>` format + `AIBinder_markVintfStability()`. Same crash as Attempt 3 — the double-marking still aborts.

### Root Fix
After adding the VINTF manifest and `/default` service names, remove `AIBinder_markVintfStability()` entirely. Use raw `libbinder` with `IBinder::FLAG_PRIVATE_VENDOR` in the JNI `transact()` calls. This flag changes the required stability check in `BpBinder::transact` from LOCAL to VENDOR:

```cpp
// In BpBinder::transact source code:
Stability::Level required = privateVendor ? Stability::VENDOR
                          : Stability::getLocalLevel();  // LOCAL for system partition
if (!Stability::check(stability, required))
    return BAD_TYPE;  // VENDOR vs LOCAL → fails without the flag
                      // VENDOR vs VENDOR → passes with the flag
```

So using `FLAG_PRIVATE_VENDOR` makes `VENDOR ≥ VENDOR` check which passes.

---

## Bug E: Services Crash-Loop After Adding `AIBinder_markVintfStability`

### Symptom
Both bmid and calculatord restart every 5 seconds:
```
I bmid    : bmid starting
I bmid    : bmid starting   ← every 5s, no "registered" line
```
No SELinux denials. `service list | grep bmi` returns nothing.

### Root Cause (first crash-loop)
`AIBinder_markVintfStability()` called when no VINTF manifest fragment exists. `AServiceManager_addService` rejects the registration → process exits → init restarts it.

### Root Cause (second crash-loop)
VINTF manifest added, but `AIBinder_markVintfStability()` still called. NDK AIDL backend already sets VENDOR stability. Double-marking aborts:
```
E Stability: Interface being set with vintf stability but it is already marked as vendor stability
```

### Debug Command
```bash
adb logcat -d | grep -E "bmid|calculatord|VINTF|vintf|Stability" | tail -30
```
This is the command that revealed the actual crash message, unlike `adb logcat -s bmid` which only shows the service's own logs.

---

## Bug F: VINTF Fragment Not Installing

### Symptom
After adding `vintf_fragment` module to `Android.bp` and `PRODUCT_PACKAGES`, the xml files didn't appear in `out/target/product/rpi5/vendor/etc/vintf/manifest/`.

### Root Cause 1
`vintf_fragment` module was missing `vendor: true`. Without it, Soong doesn't know to install it to the vendor partition.

### Root Cause 2
Running `m bmid calculatord` after fixing `vendor: true` said "no work to do" because Soong considered those targets up to date. The vintf fragment is a separate Soong module.

### Fix
```bash
# Must build the fragment module by its exact name
m bmid-vintf-fragment calculatord-vintf-fragment

# Verify output
find out/target/product/rpi5/vendor/etc/vintf -name "bmid.xml" -o -name "calculatord.xml"
```

Always build vintf fragment modules by name, not as a side-effect of building the service binary.

---

## Bug G: SELinux `unknown type hwcalculatord_service`

### Symptom (unrelated to BMICalculatorA, appeared during `make systemimage`)
```
vendor/myoem/services/hwcalculator/sepolicy/private/hwcalculatord.te:14:ERROR
  'unknown type hwcalculatord_service' at token ';'
```

### Root Cause
`hwcalculatord.te` referenced `hwcalculatord_service` in `allow` rules but never declared the type. SELinux type declarations cannot be assumed — every type used in `.te` rules must be explicitly declared.

### Fix
Add to `hwcalculatord.te`:
```
type hwcalculatord_service, service_manager_type;
```

### Lesson
Every custom type used in a `.te` file must be declared in that same file (or a file that gets merged in). `service_manager_type` is the required attribute for binder service labels used in `service_contexts`.

---

## Bug H: Service Name Mismatch in `service_contexts`

### Symptom
After changing service names from `com.myoem.bmi.IBMIService` to `com.myoem.bmi.IBMIService/default`, services appeared to start (no crash-loop) but `addService` was being denied silently — services didn't appear in `service list`.

### Root Cause
`service_contexts` SELinux file still had the old name:
```
com.myoem.bmi.IBMIService    u:object_r:bmid_service:s0
```
The service manager does a string-exact lookup in `service_contexts`. The `/default` suffix was not present, so the registration was denied.

### Fix
Update all three places when changing a service name:
1. `main.cpp` — `kServiceName`
2. `service_contexts` — the label mapping
3. Client code — `AServiceManager_checkService()` string
4. VINTF manifest — `<instance>` element

```
# service_contexts
com.myoem.bmi.IBMIService/default    u:object_r:bmid_service:s0
```

---

## Final Working Architecture

```
BMICalculatorA (system/priv-app)
    │
    │  Kotlin: NativeBinder.kt
    │  System.loadLibrary("bmicalculator_jni")
    │
    ▼
libbmicalculator_jni.so (system/lib64)
    │
    │  defaultServiceManager()->checkService("com.myoem.bmi.IBMIService/default")
    │  binder->transact(TX, data, &reply, IBinder::FLAG_PRIVATE_VENDOR)
    │
    │  FLAG_PRIVATE_VENDOR:
    │    BpBinder::transact required stability = VENDOR (not LOCAL)
    │    check(VENDOR, VENDOR) = PASS ✓
    │
    ▼
bmid / calculatord (vendor/bin)
    Registered as: com.myoem.bmi.IBMIService/default
    VINTF manifest: vendor/etc/vintf/manifest/bmid.xml
    SELinux:        com.myoem.bmi.IBMIService/default → u:object_r:bmid_service:s0
```

---

## Complete Push Checklist (after any change)

| What changed | What to push |
|---|---|
| JNI C++ only | `system/lib64/libbmicalculator_jni.so` |
| Kotlin/APK only | `system/priv-app/BMICalculatorA/BMICalculatorA.apk` |
| Service binary | `vendor/bin/bmid` or `vendor/bin/calculatord` |
| VINTF fragment | `vendor/etc/vintf/manifest/bmid.xml` |
| SELinux policy | Must write full image via `dd` (policy is in boot/system image) |

Always prefix with:
```bash
adb root && adb shell mount -o remount,rw / && adb shell mount -o remount,rw /vendor
```

---

## Key Debug Commands

```bash
# Are services registered?
adb shell service list | grep -E "bmi|calc"

# Are services running (not crash-looping)?
adb shell ps -e | grep -E "bmid|calculatord"

# Crash details (tombstone)
adb logcat *:S DEBUG:I

# Service startup + stability errors
adb logcat -d | grep -E "bmid|calculatord|Stability|VINTF" | tail -30

# SELinux denials
adb logcat -d | grep "avc: denied" | grep -E "bmi|calc"

# JNI log output
adb logcat -s BmiCalJni
```
