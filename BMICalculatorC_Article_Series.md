# Part 12: Building BMICalculatorC — The Industry-Standard Way

## Subtitle: Why Apps Should Never Call Vendor Services Directly — and How to Fix It

**Series**: AOSP OEM Vendor Layer Development on Raspberry Pi 5
**GitHub**: ProArun
**Device**: Raspberry Pi 5 | AOSP android-15.0.0_r14
**Prerequisites**: Parts 10–11 (BMICalculatorA, BMICalculatorB, binder stability, manager pattern)

---

## The Question That Started This

After building BMICalculatorB, a natural question arose:

> "You created `BmiCalManagerJni.cpp` as middleware to talk to both binder services — can't the Java manager class directly talk to both binder services? What is the industry-standard way?"

The answer reveals something important about Android's architecture.

---

## What's Wrong with A and B

BMICalculatorA (direct JNI in the app) and BMICalculatorB (JNI in a manager library) both share a structural problem: the app process is directly calling vendor binder services — whether via JNI in the app itself, or JNI one layer down in a library.

```
BMICalculatorA / BMICalculatorB (the problem):

┌─────────────────────────────────────────┐
│         App Process                      │
│  (system/priv-app — can be idle)        │
│                                          │
│  NativeBinder (A) / BmiCalManager (B)   │
│         ↓  FLAG_PRIVATE_VENDOR          │   ← cross-partition
└─────────────────┬───────────────────────┘
                  │ Binder
        ┌─────────┴──────────┐
        ▼                    ▼
     bmid               calculatord
  (VENDOR process)    (VENDOR process)
```

Two problems with this design:

### Problem 1: The Freeze Problem

Android 15's ActivityManagerService has a `BinderProxyTransactListener` that monitors every Java `BinderProxy.transact()` call. If the target process is in a frozen state (Android 15 aggressively freezes idle processes), AMS throws `IllegalArgumentException`. 

Vendor processes like `bmid` and `calculatord` are idle most of the time — they only do work when a button is pressed. Android will freeze them.

BMICalculatorA/B work around this by using JNI. JNI calls go directly to the kernel binder driver, bypassing `BinderProxy` entirely — so the listener never fires. But this is a **workaround**, not a solution. We're fighting the framework instead of working with it.

### Problem 2: The Stability Problem

System-partition code cannot call VENDOR-stability binder services via standard means. Every system app that directly calls vendor services needs `FLAG_PRIVATE_VENDOR` in its binder calls. This flag is an escape hatch, not a design pattern.

---

## The Industry-Standard Solution

The correct Android architecture is:

```
App → Framework System Service → Vendor HAL
```

Every major Android subsystem uses this pattern:
- **AudioManager.setVolume()** → AudioService (system_server) → Audio HAL
- **LocationManager.requestUpdates()** → LocationManagerService (system_server) → GNSS HAL
- **CameraManager.openCamera()** → CameraService (system_server) → Camera HAL

The "Framework System Service" layer is the key. It:
1. Is never frozen (system_server is never frozen; privileged persistent system apps are treated similarly)
2. Can be called by apps via standard Java AIDL — no JNI, no stability tricks
3. Handles all cross-partition complexity internally, invisible to apps

Applied to our BMI stack:

```
┌──────────────────────────────────────────────────────────┐
│           BMICalculatorC (App Process)                    │
│                                                           │
│  Kotlin — zero binder knowledge                          │
│    IBmiAppService.Stub.asInterface(                       │
│        ServiceManager.getService("bmi_system_service"))   │
│    service.getBMI(height, weight)  ← plain AIDL call     │
└───────────────────────────┬───────────────────────────────┘
                            │ LOCAL-stability Binder
                            ▼
┌──────────────────────────────────────────────────────────┐
│           BmiSystemService (Persistent System App)        │
│                                                           │
│  IBmiAppService.Stub implementation (Java)               │
│  ServiceManager.addService("bmi_system_service", binder) │
│         ↓  libbmiappsvc_jni.so                           │
│  JNI  (FLAG_PRIVATE_VENDOR)  ← complexity lives HERE    │
└──────────┬──────────────────────────┬────────────────────┘
           │ Kernel Binder            │ Kernel Binder
           ▼                          ▼
        bmid                     calculatord
     (IBMIService)            (ICalculatorService)
     /vendor/bin               /vendor/bin
```

The app makes one normal AIDL call. The system service handles the cross-partition binder internally. The app is completely decoupled from vendor service details.

---

## What We're Building

```
vendor/myoem/
├── services/bmiapp/                     ← NEW: the system service
│   ├── Android.bp
│   ├── AndroidManifest.xml
│   ├── aidl/com/myoem/bmiapp/
│   │   └── IBmiAppService.aidl          ← app ↔ svc interface
│   ├── jni/
│   │   └── BmiAppServiceJni.cpp         ← FLAG_PRIVATE_VENDOR (here only)
│   └── java/com/myoem/bmiapp/
│       ├── BmiAppService.java           ← IBmiAppService.Stub
│       └── BmiAppApplication.java       ← starts service at boot
│
└── apps/BMICalculatorC/                 ← NEW: the client app
    ├── Android.bp                       ← NO jni_libs
    ├── AndroidManifest.xml
    └── src/main/kotlin/com/myoem/bmicalculatorc/
        ├── MainActivity.kt              ← identical to B
        ├── BmiCalCViewModel.kt          ← 3 lines of binder, then plain calls
        └── ui/BmiCalScreen.kt           ← identical to A and B
```

---

## Step 1: The AIDL Interface

### `vendor/myoem/services/bmiapp/aidl/com/myoem/bmiapp/IBmiAppService.aidl`

```aidl
package com.myoem.bmiapp;

interface IBmiAppService {
    boolean isBmiAvailable();
    boolean isCalcAvailable();

    float getBMI(float height, float weight);

    int add(int a, int b);
    int subtract(int a, int b);
    int multiply(int a, int b);
    int divide(int a, int b);
}
```

This is a **LOCAL-stability** interface. Both the system service (implementor) and the app (caller) live on the system partition. No `@VintfStability`, no manifest fragment, no VINTF registration needed. Standard Java AIDL.

Compare this to `IBMIService.aidl` (for bmid) which needs VINTF stability, a manifest XML file, and `service_contexts` entries. `IBmiAppService` needs none of that.

### Android.bp for the AIDL module

```bp
aidl_interface {
    name: "bmiapp-aidl",
    srcs: ["aidl/**/*.aidl"],
    local_include_dir: "aidl",
    unstable: true,
    backend: {
        java: { enabled: true },
        cpp: { enabled: false },
        ndk: { enabled: false },
        rust: { enabled: false },
    },
}
```

This generates `bmiapp-aidl-java` — the Java stubs both sides use.

---

## Step 2: BmiSystemService

### BmiAppService.java

```java
public class BmiAppService extends Service {

    static final String SERVICE_NAME = "bmi_system_service";

    private final IBmiAppService.Stub mBinder = new IBmiAppService.Stub() {

        @Override public boolean isBmiAvailable()  { return nativeIsBmiAvailable(); }
        @Override public boolean isCalcAvailable() { return nativeIsCalcAvailable(); }

        @Override public float getBMI(float height, float weight) {
            return nativeGetBMI(height, weight);
        }
        @Override public int add(int a, int b)      { return nativeAdd(a, b); }
        @Override public int subtract(int a, int b) { return nativeSubtract(a, b); }
        @Override public int multiply(int a, int b) { return nativeMultiply(a, b); }
        @Override public int divide(int a, int b)   { return nativeDivide(a, b); }
    };

    @Override
    public void onCreate() {
        super.onCreate();
        System.loadLibrary("bmiappsvc_jni");
        // Register with ServiceManager — requires platform certificate + privileged
        ServiceManager.addService(SERVICE_NAME, mBinder);
    }

    @Override public IBinder onBind(Intent intent) { return mBinder; }
    @Override public int onStartCommand(Intent i, int f, int id) { return START_STICKY; }

    // Native methods — apps never see these
    private native boolean nativeIsBmiAvailable();
    private native boolean nativeIsCalcAvailable();
    private native float   nativeGetBMI(float height, float weight);
    private native int     nativeAdd(int a, int b);
    private native int     nativeSubtract(int a, int b);
    private native int     nativeMultiply(int a, int b);
    private native int     nativeDivide(int a, int b);
}
```

### BmiAppApplication.java

```java
public class BmiAppApplication extends Application {
    @Override
    public void onCreate() {
        super.onCreate();
        startService(new Intent(this, BmiAppService.class));
    }
}
```

### AndroidManifest.xml

```xml
<manifest package="com.myoem.bmiapp">
    <application
        android:name=".BmiAppApplication"
        android:persistent="true">   <!-- ← never killed/frozen by AMS -->
        <service
            android:name=".BmiAppService"
            android:exported="false" />
    </application>
</manifest>
```

`android:persistent="true"` is the key: ActivityManager starts this process at boot and keeps it alive. Unlike vendor processes (`bmid`, `calculatord`), a persistent privileged system app is not subject to the freeze mechanism. It is always available.

### BmiAppServiceJni.cpp

Identical technique to BMICalculatorA/B — `libbinder` + `FLAG_PRIVATE_VENDOR`. The JNI symbol prefix is now:
```
Java_com_myoem_bmiapp_BmiAppService_nativeGetBMI
```

### Android.bp for BmiSystemService

```bp
cc_library_shared {
    name: "libbmiappsvc_jni",
    srcs: ["jni/BmiAppServiceJni.cpp"],
    header_libs: ["jni_headers"],
    shared_libs: ["libbinder", "libutils", "liblog"],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}

android_app {
    name: "BmiSystemService",
    platform_apis: true,
    privileged: true,
    certificate: "platform",
    srcs: ["java/**/*.java"],
    static_libs: ["bmiapp-aidl-java"],
    jni_libs: ["libbmiappsvc_jni"],
    optimize: { enabled: false },
}
```

---

## Step 3: BMICalculatorC App

### BmiCalCViewModel.kt — The Payoff

```kotlin
class BmiCalCViewModel : ViewModel() {

    // 3 lines of "binder" code — this is the ENTIRE system service interaction
    private val service: IBmiAppService? = run {
        val binder = ServiceManager.getService("bmi_system_service")
        if (binder != null) IBmiAppService.Stub.asInterface(binder) else null
    }

    fun computeBmi(input1: String, input2: String) {
        // ...
        viewModelScope.launch(Dispatchers.IO) {
            val bmi = service?.getBMI(height, weight)
                ?: throw RuntimeException("BmiSystemService not available")
            // That's it. One line. No Parcel, no transaction codes.
        }
    }

    fun computeAdd(input1: String, input2: String) =
        calcOp("Add", input1, input2) { a, b -> service!!.add(a, b) }
}
```

Three lines to get the service proxy. Then every call is a plain Kotlin interface method call. No Parcel. No transaction codes. No exception code parsing. No stability flags. This is standard Android.

### Android.bp

```bp
soong_namespace {
    imports: ["vendor/myoem/services/bmiapp"],
}

android_app {
    name: "BMICalculatorC",
    platform_apis: true,   // needed for ServiceManager.getService() only
    privileged: true,
    certificate: "platform",
    static_libs: [
        "bmiapp-aidl-java",  // ← ONLY binder dependency
        "kotlin-stdlib",
        "kotlinx-coroutines-android",
        // ... Compose deps
    ],
    // No jni_libs — this app has ZERO native code
    optimize: { enabled: false },
}
```

The absence of `jni_libs` is significant. BMICalculatorA and B both needed a `.so`. BMICalculatorC needs nothing.

---

## Step 4: myoem_base.mk

```makefile
PRODUCT_SOONG_NAMESPACES += \
    vendor/myoem/services/bmiapp \
    vendor/myoem/apps/BMICalculatorC

PRODUCT_PACKAGES += \
    bmiapp-aidl-java \
    libbmiappsvc_jni \
    BmiSystemService \
    BMICalculatorC
```

---

## Build and Install

```bash
# Build both the system service and the app
m BmiSystemService BMICalculatorC

# Verify: system service APK
find out/target/product/rpi5/system/priv-app/BmiSystemService -name "*.apk"

# Verify: NO native .so in the app
find out/target/product/rpi5/system/priv-app/BMICalculatorC -name "*.so"
# → Should print nothing. BMICalculatorC has no JNI.

# Verify: .so in system service
find out/target/product/rpi5/system/lib64 -name "libbmiappsvc_jni.so"

# Deploy
adb root && adb shell mount -o remount,rw / && \
adb push out/target/product/rpi5/system/lib64/libbmiappsvc_jni.so \
         /system/lib64/ && \
adb push out/target/product/rpi5/system/priv-app/BmiSystemService/BmiSystemService.apk \
         /system/priv-app/BmiSystemService/BmiSystemService.apk && \
adb shell mkdir -p /system/priv-app/BmiSystemService/lib/arm64 && \
adb shell ln -sf /system/lib64/libbmiappsvc_jni.so \
         /system/priv-app/BmiSystemService/lib/arm64/libbmiappsvc_jni.so && \
adb push out/target/product/rpi5/system/priv-app/BMICalculatorC/BMICalculatorC.apk \
         /system/priv-app/BMICalculatorC/BMICalculatorC.apk && \
adb reboot
```

### Verify BmiSystemService started

```bash
# Check it's running
adb shell ps -A | grep bmiapp
# Should show: u:r:system_app:s0  <pid>  ... com.myoem.bmiapp

# Check service is registered
adb shell service list | grep bmi_system_service
# Should show: bmi_system_service: [com.myoem.bmiapp.IBmiAppService]

# Check logs
adb logcat -s BmiAppService
# Should show: BmiSystemService: bmi_system_service registered
```

---

## The Complete Three-Way Comparison

| Aspect | App A | App B | App C |
|--------|-------|-------|-------|
| Who has JNI | App itself | Manager library | System service (separate APK) |
| App has `.so` | Yes | Yes (via manager) | **No** |
| App calls vendor binder | Directly (via JNI) | Directly (via manager JNI) | **Never** |
| App's binder API | NativeBinder (custom JNI object) | BmiCalManager (library object) | **IBmiAppService (AIDL stub)** |
| FLAG_PRIVATE_VENDOR in app process | Yes | Yes | **No** |
| Freeze vulnerability | Worked around with JNI | Worked around with JNI | **Not applicable** |
| App imports binder internals | No (moved to JNI) | No (moved to JNI) | **No** |
| App needs `jni_libs` | Yes | Yes | **No** |
| Comparable to Android's own API | No | Closer | **Yes** |
| Lines of "binder" code in app ViewModel | ~50 | 0 | **3** |

The 3 lines in App C are: `ServiceManager.getService()`, `IBmiAppService.Stub.asInterface()`, and the `?:` null check. These are unavoidable because we're a system app (third-party apps use `Context.getSystemService()` instead).

---

## Why This Is the Industry Standard

### What Android's own services look like

```java
// AudioManager.java (simplified)
public class AudioManager {
    private final IAudioService mService;

    public AudioManager(Context context) {
        IBinder b = ServiceManager.getService(Context.AUDIO_SERVICE);
        mService = IAudioService.Stub.asInterface(b);
    }

    public void setStreamVolume(int streamType, int index, int flags) {
        mService.setStreamVolume(streamType, index, flags, ...);  // plain AIDL call
    }
}
```

`BmiCalCViewModel` follows exactly this pattern. `ServiceManager.getService()` → `Stub.asInterface()` → plain method calls.

### Why system_server is special

`system_server` hosts Android's core services (`ActivityManagerService`, `WindowManagerService`, `PackageManagerService`, etc.). It runs with UID `system`, is never frozen, and is the first Java process started by the zygote. Its services are always reachable.

We can't add to `system_server` without modifying `frameworks/` (out of scope for an OEM vendor layer). But a **persistent privileged system app** (`android:persistent="true"`, system/priv-app, platform certificate) is the closest equivalent that stays in `vendor/myoem/`. ActivityManager treats it with high priority and does not apply the idle freeze policy to it.

### The binder stability chain

```
App (LOCAL)  ──→  BmiSystemService (LOCAL)  ──→  bmid/calculatord (VENDOR)
              ↑ standard Java AIDL            ↑ libbinder + FLAG_PRIVATE_VENDOR
              no stability tricks             (hidden inside system service JNI)
```

The stability complexity is isolated to the system service. Apps are completely decoupled from it. If tomorrow we replaced bmid with a VINTF HAL or a socket-based service, only `BmiAppServiceJni.cpp` changes — `IBmiAppService.aidl` and `BMICalculatorC` are unchanged.

---

## Key Lessons

### Lesson 1: The Freeze Problem Is Architectural, Not a Bug to Work Around

BMICalculatorA and B use JNI to bypass `BinderProxyTransactListener`. This works, but it's fighting the framework. The correct fix is to put the vendor binder calls in a process that is never frozen — a persistent system service. The freeze mechanism exists for a reason; design around it, not through it.

### Lesson 2: FLAG_PRIVATE_VENDOR Should Be Invisible to Apps

`IBinder::FLAG_PRIVATE_VENDOR` is an escape hatch for code that must cross partition boundaries on the system side. It should never appear in app code, or even in a library that apps depend on. In BMICalculatorC, it lives exclusively in `BmiAppServiceJni.cpp`, one file in one package that app developers never touch.

### Lesson 3: LOCAL vs VENDOR Stability Determines Which Side Needs the Flag

| Caller | Callee | Stability required | Flag needed? |
|--------|--------|--------------------|-------------|
| App (system) | BmiSystemService (system) | LOCAL ↔ LOCAL | No |
| BmiSystemService (system) | bmid (vendor) | LOCAL → VENDOR | **Yes** |
| App (system) | bmid (vendor) | LOCAL → VENDOR | Yes (A & B workaround) |

By adding the system service layer, the app never encounters the LOCAL→VENDOR crossing. The system service handles it internally.

### Lesson 4: android:persistent="true" Is the OEM Alternative to system_server

Real production OEM services live in `system_server` (they subclass `SystemService` and are listed in `SystemServiceManager.startBootstrapServices()`). Since that requires `frameworks/` changes, OEM vendor layer services use `android:persistent="true"` in a privileged system app as a practical equivalent. ActivityManager starts it at boot and restarts it if it crashes.

### Lesson 5: The AIDL Interface Is the Contract

`IBmiAppService.aidl` is the only thing both sides agree on. The system service implementation can change (C++, Kotlin, different vendor services), the client app can change (different UI, different ViewModel), but as long as both sides honour the AIDL contract, they're independent. This is exactly how Android's own framework/app separation works.

---

## Summary: The Evolution Across Three Apps

```
BMICalculatorA: App ──(JNI, FLAG_PRIVATE_VENDOR)──→ vendor services
                Problem: JNI in the app, freeze workaround, stability knowledge in app

BMICalculatorB: App ──(library call)──→ BmiCalManager ──(JNI, FLAG_PRIVATE_VENDOR)──→ vendor services
                Better: app has no JNI. Problem: vendor calls still from app process (freeze risk)

BMICalculatorC: App ──(Java AIDL)──→ BmiSystemService ──(JNI, FLAG_PRIVATE_VENDOR)──→ vendor services
                Industry standard: app has no JNI, no stability knowledge, no freeze risk
                FLAG_PRIVATE_VENDOR is in one place, in a process that's never frozen
```

Each iteration moved the binder complexity one level further from the app. BMICalculatorC reaches the correct final position: the app knows nothing about binder stability, vendor partitions, or process freeze. It just calls an AIDL interface.

---

*Part 12 completes the BMI Calculator series. We went from raw JNI in the app (A), to a manager library (B), to the correct industry-standard architecture (C). The progression mirrors how Android's own audio, location, and camera subsystems are structured.*

---

## Bug Encountered: SecurityException — App UIDs Cannot Add Services

### Symptom

After installing and rebooting, the app showed "BmiSystemService not available" for every operation. Logs showed the process was starting but immediately crashing:

```
E AndroidRuntime: java.lang.RuntimeException: Unable to create service
    com.myoem.bmiapp.BmiAppService:
    java.lang.SecurityException: App UIDs cannot add services.
E AndroidRuntime:   at com.myoem.bmiapp.BmiAppService.onCreate(BmiAppService.java:87)
```

### Root Cause

`ServiceManager.addService()` is gated by the kernel binder driver — only processes running as UID 1000 (`android.uid.system`) are permitted to register new services. Our `BmiSystemService` was running as a regular app UID (`u0a106`) even though it was a privileged system app. `privileged: true` and `certificate: "platform"` give the app extra permissions, but do **not** change its UID.

### Fix

Add `android:sharedUserId="android.uid.system"` to the manifest:

```xml
<manifest xmlns:android="http://schemas.android.com/apk/res/android"
    package="com.myoem.bmiapp"
    android:sharedUserId="android.uid.system">
```

This causes the process to run with UID 1000 (same as `system_server`), which is allowed to call `ServiceManager.addService()`. Combined with `certificate: "platform"` (which verifies the APK has the right signature to use this shared UID), the security check passes.

### Why This Is Required

| What you have | What you get | Allowed to addService? |
|---|---|---|
| `privileged: true` | Access to priv-app permissions | No |
| `certificate: "platform"` | Platform signature trust | No |
| `android:sharedUserId="android.uid.system"` | UID 1000 (system) | **Yes** |

All three are needed together. `sharedUserId` alone won't work without the platform certificate, and the certificate alone doesn't change the UID.

Note: `android:sharedUserId` is marked deprecated in API 29+ for third-party apps. For system services on the platform, it remains the correct mechanism.

---

## Quick Reference: Build and Run Commands

```bash
# 1. Build
lunch myoem_rpi5-trunk_staging-userdebug && m BmiSystemService BMICalculatorC

# 2. Deploy
adb root && adb shell mount -o remount,rw / && \
adb push out/target/product/rpi5/system/lib64/libbmiappsvc_jni.so \
         /system/lib64/ && \
adb shell mkdir -p /system/priv-app/BmiSystemService && \
adb push out/target/product/rpi5/system/priv-app/BmiSystemService/BmiSystemService.apk \
         /system/priv-app/BmiSystemService/BmiSystemService.apk && \
adb shell mkdir -p /system/priv-app/BmiSystemService/lib/arm64 && \
adb shell ln -sf /system/lib64/libbmiappsvc_jni.so \
         /system/priv-app/BmiSystemService/lib/arm64/libbmiappsvc_jni.so && \
adb shell mkdir -p /system/priv-app/BMICalculatorC && \
adb push out/target/product/rpi5/system/priv-app/BMICalculatorC/BMICalculatorC.apk \
         /system/priv-app/BMICalculatorC/BMICalculatorC.apk && \
adb reboot

# 3. Verify after reboot
adb shell service list | grep bmi_system_service
adb logcat -s BmiAppService
```

---

## Final Comparison: BMICalculatorA vs B vs C

### Architecture

```
App A:  BMICalculatorA
        ├── NativeBinder.kt         (JNI object, lives in app)
        ├── libbmicalculator_jni.so (FLAG_PRIVATE_VENDOR, in app process)
        └── ──────────────────────────────→ bmid / calculatord

App B:  BMICalculatorB
        ├── BmiCalManager           (library object, no JNI visible to app)
        │     └── libbmicalmanager_jni.so (FLAG_PRIVATE_VENDOR, still app process)
        └── ──────────────────────────────→ bmid / calculatord

App C:  BMICalculatorC
        ├── IBmiAppService          (AIDL stub, plain Java interface call)
        │     └── BmiSystemService  (separate persistent system process, UID=system)
        │           └── libbmiappsvc_jni.so (FLAG_PRIVATE_VENDOR, system process)
        └── ──────────────────────────────→ bmid / calculatord
```

### Feature Comparison

| | BMICalculatorA | BMICalculatorB | BMICalculatorC |
|---|---|---|---|
| **App has JNI** | Yes (`libbmicalculator_jni`) | Yes (via manager) | **No** |
| **App has `.so`** | Yes | Yes | **No** |
| **Binder API in app** | `NativeBinder` (custom JNI object) | `BmiCalManager` (library class) | `IBmiAppService` (AIDL stub) |
| **FLAG_PRIVATE_VENDOR location** | App's JNI | Manager library JNI | System service JNI |
| **Who calls vendor binder** | App process | App process | System service process |
| **Freeze risk** | Worked around (JNI bypasses BinderProxy) | Worked around (JNI bypasses BinderProxy) | **None** (system UID never frozen) |
| **App process UID** | u0a (app) | u0a (app) | u0a (app) |
| **Vendor caller UID** | u0a (app) | u0a (app) | **1000 (system)** |
| **App needs `platform_apis`** | Yes (ServiceManager) | No | Yes (ServiceManager.getService) |
| **App binder code lines** | ~50 (NativeBinder.kt) | 0 | **3** |
| **New process required** | No | No | Yes (BmiSystemService) |
| **`android:sharedUserId` needed** | No | No | **Yes** (system service only) |
| **Comparable to Android framework** | No | Closer | **Yes** |

### When to Use Each

| Pattern | Use when |
|---|---|
| **A — Direct JNI** | Learning / one-off experiments; you want to understand raw binder mechanics |
| **B — Manager library** | Multiple apps share the same vendor service; you want binder hidden from apps but no new process |
| **C — System service** | Production code; multiple apps; vendor service may be frozen; proper architectural separation required |

### What Each Approach Teaches

- **A** teaches you what binder actually is: Parcel, transaction codes, stability flags, service lookup
- **B** teaches encapsulation: how to hide complexity behind a library so app developers don't need to know binder
- **C** teaches Android's real architecture: the three-tier model (app → framework service → vendor HAL) that every subsystem in AOSP uses
