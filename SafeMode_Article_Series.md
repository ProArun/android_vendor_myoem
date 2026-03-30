# Building a Full-Stack AOSP Safe Mode System on Raspberry Pi 5 AAOS
## From VHAL Subscription to Jetpack Compose Dashboard

**A Complete Engineering Journal**

---

> **About this article**: This is a real engineering journal of building a complete
> vendor-only AOSP automotive stack — VHAL → Native Binder Service → Kotlin Library → Compose App —
> on a Raspberry Pi 5 running Android Automotive OS (AAOS) with Android 15 (`android-15.0.0_r14`).
> Every failed approach, every error message, and every debugging command is documented
> exactly as it happened. **The full stack is working end-to-end** as of this version of the article.

---

## Table of Contents

1. [Project Motivation — What Is Safe Mode and Why Build It?](#part-1)
2. [Architecture — Planning a Full AAOS Stack in vendor/myoem/](#part-2)
3. [Phase 1 — AIDL Interface Design (The Contract)](#part-3)
4. [Phase 2 — The Native Service (`safemoded`)](#part-4)
5. [Phase 2 — SELinux Policy](#part-5)
6. [Phase 3 — The Kotlin Library (`safemode_library`)](#part-6)
7. [Phase 4 — The Jetpack Compose App (`SafeModeDemo`)](#part-7)
8. [Build Error Deep Dive — Every Error and Its Fix](#part-8)
9. [Runtime Debugging — Layer by Layer](#part-9)
10. [Lessons Learned — AAOS Development on RPi5](#part-10)
11. [Complete Testing Guide — Bottom-Up](#part-11)
12. [Problems Faced and How They Were Fixed (Post-Build Runtime Issues)](#part-12)

---

<a name="part-1"></a>
# Part 1: Project Motivation — What Is Safe Mode and Why Build It?

## The Problem

Modern in-car infotainment systems (AAOS — Android Automotive OS) display complex UI:
maps, media controls, climate settings, third-party apps. While the car is stationary,
this is fine. While the driver is doing 100 km/h on a highway, it is dangerous.

The concept of **Safe Mode** is simple: the system monitors vehicle speed and applies
progressively stricter UI restrictions as speed increases. At low speed (< 5 km/h),
everything works normally. At medium speed (5–15 km/h), complexity is reduced. At
highway speed (≥ 15 km/h), only essential information (navigation, media controls)
is shown and input is minimised.

This is not a theoretical idea — Mercedes-Benz and other OEMs implement variants of this.
The architectural approach for implementing it in AOSP (as an OEM vendor layer, not
modifying the framework) is what this project explores.

## The Learning Goal

The project builds on the previous ThermalControl project (which read sysfs directly)
but steps into a more complex domain:

- Reading data from **VHAL** (Vehicle Hardware Abstraction Layer) — the automotive-specific
  HAL in AAOS that abstracts all vehicle signals
- Using the **AIDL V3 API** for VHAL (which changed significantly from V2)
- Building a **multi-process subscription system** (service → library → app) with Binder IPC
- Doing all of this entirely in `vendor/myoem/` — zero changes to framework, system, or device code

## What VHAL Is

VHAL is the HAL that bridges Android and the car's CAN bus signals. In production,
VHAL talks to a CAN gateway that reads real sensors. On our RPi5 dev board, VHAL is
an **emulator** — it generates synthetic vehicle data.

The VHAL AIDL interface is at:
```
hardware/interfaces/automotive/vehicle/aidl/android/hardware/automotive/vehicle/IVehicle.aidl
```

Key properties we care about:
| Property | ID | Type | Description |
|---|---|---|---|
| `PERF_VEHICLE_SPEED` | `0x11600207` | FLOAT, CONTINUOUS | Speed in m/s |
| `CURRENT_GEAR` | `0x11400400` | INT32, ON_CHANGE | Gear bitmask |
| `FUEL_LEVEL` | `0x45100004` | FLOAT, ON_CHANGE | Fuel in ml |

---

<a name="part-2"></a>
# Part 2: Architecture — Planning a Full AAOS Stack in vendor/myoem/

## Design Principles

Before writing any code, we established three design rules:

**Rule 1: The service is a raw data pipe.**
`safemoded` reads VHAL and delivers raw values (speed in m/s, gear as integer).
It does NOT compute SafeMode states. The library does that. Why? Because
business logic thresholds (5 km/h, 15 km/h) might change. If the service computed
them, every threshold change would require an OTA. With the library doing it,
thresholds change via an app/library update.

**Rule 2: Everything in `vendor/myoem/` — zero framework changes.**
We use only stable, published AOSP interfaces: Binder NDK, VHAL AIDL, ServiceManager.
No modifications to `frameworks/`, `device/brcm/`, or `system/`.

**Rule 3: Use `libbinder_ndk` everywhere in vendor code.**
Vendor binaries must use the NDK Binder surface (`libbinder_ndk`) not the
platform surface (`libbinder`). This is required by Treble compliance.

## The Full Stack

```
┌─────────────────────────────────────────────────────────────┐
│  LAYER 4: SafeModeDemo (Android App)                        │
│  vendor/myoem/apps/SafeModeDemo/                            │
│  Kotlin + Jetpack Compose — shows Speed/Gear/Fuel cards     │
│  + big animated SafeMode state card (green/yellow/red)      │
└──────────────────┬──────────────────────────────────────────┘
                   │ SafeModeManager.attach() / dispose()
                   │ SafeModeListener.onSafeModeChanged()
┌──────────────────▼──────────────────────────────────────────┐
│  LAYER 3: safemode_library (Kotlin Android Library)         │
│  vendor/myoem/libs/safemode/                                │
│  SafeModeManager singleton — polls service every 200ms      │
│  Converts VehicleData → VehicleInfo → SafeModeState         │
│  Delivers updates on main thread via Handler.postDelayed()  │
└──────────────────┬──────────────────────────────────────────┘
                   │ Binder IPC: getCurrentData() poll every 200ms
                   │ System → Vendor direction (always allowed)
┌──────────────────▼──────────────────────────────────────────┐
│  LAYER 2: safemoded (C++ Native Service)                    │
│  vendor/myoem/services/safemode/                            │
│  Implements ISafeModeService AIDL                           │
│  RPi5 dev: reads /data/local/tmp/safemode_sim.txt (SIM_MODE)│
│  Production: subscribes to VHAL, stores latest VehicleData  │
└──────────────────┬──────────────────────────────────────────┘
     [PRODUCTION]  │ Binder IPC (VHAL AIDL V3)
                   │ IVehicle::subscribe() / onPropertyEvent()
┌──────────────────▼──────────────────────────────────────────┐
│  LAYER 1: VHAL (android.hardware.automotive.vehicle)        │
│  hal_vehicle_default — the VHAL emulator on RPi5 AAOS      │
│  Provides PERF_VEHICLE_SPEED, CURRENT_GEAR, FUEL_LEVEL      │
└─────────────────────────────────────────────────────────────┘

RPi5 Dev data path (SAFEMODE_SIM_MODE):

┌──────────────────────────────────────────────────────────────┐
│  Python Simulator (simulate_all.py)                          │
│  vendor/myoem/tools/safemode-simulator/                      │
│  Cycles 20 data points (speed/gear/fuel) every 5s           │
│  Writes: "speed_ms gear_int fuel_ml" to safemode_sim.txt    │
└──────────────────┬───────────────────────────────────────────┘
                   │ adb shell write to /data/local/tmp/safemode_sim.txt
┌──────────────────▼───────────────────────────────────────────┐
│  safemoded simulator thread (runSimulatorThread)             │
│  Polls safemode_sim.txt every 500ms                          │
│  Calls same dispatchToCallbacks() path as real VHAL          │
└──────────────────────────────────────────────────────────────┘
```

## File Structure

```
vendor/myoem/
├── services/safemode/          ← Phase 2: native service
│   ├── Android.bp              ← aidl_interface + cc_binary safemoded
│   ├── safemoded.rc            ← init service descriptor
│   ├── aidl/com/myoem/safemode/
│   │   ├── VehicleData.aidl    ← parcelable data bundle
│   │   ├── ISafeModeCallback.aidl ← oneway client callback
│   │   └── ISafeModeService.aidl  ← main service interface
│   ├── include/
│   │   └── SafeModeService.h   ← VhalEventCallback + SafeModeService classes
│   ├── src/
│   │   ├── SafeModeService.cpp ← VHAL subscription + dispatch logic
│   │   └── main.cpp            ← Binder thread pool + ServiceManager registration
│   ├── test/
│   │   └── safemode_client.cpp ← CLI test client (subscribe / getCurrentData)
│
├── tools/safemode-simulator/   ← RPi5 dev data injection
│   ├── simulate_all.py         ← cycles 20 data points, writes to safemode_sim.txt
│   └── data/
│       ├── speed_data.txt      ← 20 speed values in m/s (0→50→0 km/h drive cycle)
│       ├── gear_data.txt       ← 20 gear values (PARK→NEUTRAL→REVERSE→DRIVE→PARK)
│       └── fuel_data.txt       ← 20 fuel values in ml (50000→31000, 1L per step)
│   └── sepolicy/private/       ← 4 SELinux files
│       ├── safemoded.te        ← domain policy
│       ├── service.te          ← service label declaration
│       ├── service_contexts    ← name→label mapping
│       └── file_contexts       ← binary label
│
├── libs/safemode/              ← Phase 3: Kotlin library
│   ├── Android.bp              ← android_library: safemode_library
│   └── src/main/
│       ├── aidl/com/myoem/safemode/   ← bundled AIDL copies
│       └── java/com/myoem/safemode/
│           ├── SafeModeManager.kt     ← singleton, Binder connection
│           ├── SafeModeState.kt       ← NO/NORMAL/HARD state + thresholds
│           ├── VehicleInfo.kt         ← public data model + VehicleGear
│           └── SafeModeListener.kt    ← fun interface for callbacks
│
└── apps/SafeModeDemo/          ← Phase 4: Compose app
    ├── Android.bp              ← android_app: SafeModeDemo
    ├── AndroidManifest.xml
    └── src/main/java/com/myoem/safemodedemo/
        └── MainActivity.kt     ← single Compose screen
```

## The Lunch Target Problem

This was discovered early: the plain RPi5 product (`myoem_rpi5-trunk_staging-userdebug`)
does NOT include VHAL. VHAL requires the AAOS car build:

```bash
# WRONG — no VHAL, no CarService
lunch myoem_rpi5-trunk_staging-userdebug

# CORRECT — includes VHAL emulator + CarService
lunch myoem_rpi5_car-trunk_staging-userdebug
```

Verify VHAL is running:
```bash
adb shell service list | grep "android.hardware.automotive.vehicle"
# Expected: android.hardware.automotive.vehicle.IVehicle/default: [...]
```

---

<a name="part-3"></a>
# Part 3: Phase 1 — AIDL Interface Design (The Contract)

## Why AIDL First?

In AOSP, the AIDL files ARE the API contract. Both the C++ service and the Kotlin library
depend on the same AIDL definition. Getting the design right before writing implementation
code saves painful refactors later.

## VehicleData.aidl — The Parcelable

The service sends raw sensor values. One parcelable bundles them:

```aidl
// vendor/myoem/services/safemode/aidl/com/myoem/safemode/VehicleData.aidl

parcelable VehicleData {
    // PERF_VEHICLE_SPEED — metres per second
    float speedMs = 0.0f;

    // CURRENT_GEAR — VehicleGear enum integer
    // 0x0004 = PARK (default at startup)
    int gear = 4;

    // FUEL_LEVEL — millilitres
    float fuelLevelMl = 0.0f;
}
```

**Design decisions:**
- Raw VHAL units — no km/h conversion here. The library does that.
- Default values match safe startup assumptions (stopped, in park, no fuel data).
- Only 3 fields — keeps the IPC payload tiny. Every VHAL event triggers one of these crossing the Binder boundary.

## ISafeModeCallback.aidl — The `oneway` Callback

```aidl
// vendor/myoem/services/safemode/aidl/com/myoem/safemode/ISafeModeCallback.aidl

oneway interface ISafeModeCallback {
    void onVehicleDataChanged(in VehicleData data);
}
```

**Why `oneway`?** Without it, every time the service sends a speed update (at 1 Hz),
it would BLOCK waiting for the client to return from `onVehicleDataChanged()`. If the
client is doing UI work or is slow, the service hangs. With `oneway`, the service
fire-and-forgets. The Binder driver queues the call in the client's thread pool and
the service continues immediately.

**Restriction:** `oneway` methods cannot return values. That's why we have a separate
callback interface instead of a return value on `ISafeModeService`.

## ISafeModeService.aidl — The Main Interface

```aidl
// vendor/myoem/services/safemode/aidl/com/myoem/safemode/ISafeModeService.aidl

interface ISafeModeService {
    VehicleData getCurrentData();               // synchronous snapshot
    void registerCallback(ISafeModeCallback cb); // subscribe to updates
    void unregisterCallback(ISafeModeCallback cb); // unsubscribe
    int getVersion();                            // API version check
}
```

**Why `getCurrentData()`?** When an app starts and calls `registerCallback()`, the next
VHAL event could be up to 1 second away (SPEED is sampled at 1 Hz). The app would show
stale/default data for up to 1 second. `getCurrentData()` solves this: the library calls
it immediately on connect and the app has correct data from frame 1.

## AIDL in Android.bp

The service's `Android.bp` compiles the AIDL files to both NDK (for C++) and Java (for Kotlin):

```bp
aidl_interface {
    name: "safemodeservice-aidl",
    vendor_available: true,
    unstable: true,
    srcs: [
        "aidl/com/myoem/safemode/VehicleData.aidl",
        "aidl/com/myoem/safemode/ISafeModeCallback.aidl",
        "aidl/com/myoem/safemode/ISafeModeService.aidl",
    ],
    local_include_dir: "aidl",
    backend: {
        cpp: { enabled: false },  // NEVER for vendor — wrong binder library
        ndk: { enabled: true  },  // For safemoded (C++)
        java: { enabled: true, platform_apis: true },  // For safemode_library
        rust: { enabled: false },
    },
}
```

**Key point: cpp backend is disabled.** The `cpp` backend links against `libbinder`
(a VNDK library). Vendor binaries using `libbinder` break Treble compliance. The `ndk`
backend links against `libbinder_ndk` (LLNDK) — the stable surface designed for vendor code.

---

<a name="part-4"></a>
# Part 4: Phase 2 — The Native Service (`safemoded`)

## The Two-Class Design

The service consists of two C++ classes:

**`VhalEventCallback`** — inner helper that receives raw VHAL push events.
Inherits from `BnVehicleCallback` (the VHAL AIDL generated base class).
Registered with `IVehicle::subscribe()`. When VHAL sends a property event,
`onPropertyEvent()` is called on a Binder thread.

**`SafeModeService`** — implements `ISafeModeService` AIDL interface.
Manages the callback list, the current data snapshot, and thread safety.

## main.cpp — The Binder Bootstrap

The entry point follows a strict sequence that every AIDL service in AOSP must follow:

```cpp
int main() {
    // 1. Configure thread pool — before ANY Binder calls
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    // 2. Create service — choose data source at compile time
    auto service = ::ndk::SharedRefBase::make<SafeModeService>();

#ifdef SAFEMODE_SIM_MODE
    // RPi5 dev mode: reads /data/local/tmp/safemode_sim.txt every 500ms.
    // Python simulate_all.py writes "<speed_ms> <gear_int> <fuel_ml>" to that file.
    // Remove -DSAFEMODE_SIM_MODE from Android.bp cflags for production.
    if (!service->startSimulator()) {
        ALOGE("startSimulator() failed — exiting");
        return 1;
    }
#else
    // Production: subscribe to real VHAL properties
    if (!service->connectToVhal()) {
        ALOGE("connectToVhal() failed — exiting");
        return 1;
    }
#endif

    // 3. Register with ServiceManager — VINTF format: package/instance
    // Must match SERVICE_NAME in SafeModeManager.kt and service_contexts
    AServiceManager_addService(service->asBinder().get(),
                               "com.myoem.safemode.ISafeModeService/default");

    // 4. Block the main thread in the thread pool
    ABinderProcess_joinThreadPool();
    return 0;  // unreachable
}
```

**Why `ndk::SharedRefBase::make<T>()` and not `new T()`?** The NDK AIDL base class
`BnSafeModeService` requires `shared_ptr` semantics. The `SharedRefBase::make<>()` factory
creates a `shared_ptr<T>` with the correct NDK reference counting. Using `new` directly
would corrupt the Binder reference count and cause use-after-free crashes.

## connectToVhal() — Connecting to VHAL

```cpp
bool SafeModeService::connectToVhal() {
    // Step 1: Block until VHAL registers in ServiceManager
    ::ndk::SpAIBinder binder(AServiceManager_waitForService(
        "android.hardware.automotive.vehicle.IVehicle/default"));

    // Step 2: Cast raw Binder → typed IVehicle
    mVhal = IVehicle::fromBinder(binder);

    // Step 3: Create our callback object
    mVhalCallback = ::ndk::SharedRefBase::make<VhalEventCallback>(this);

    // Step 4: Build subscription options
    SubscribeOptions speedOpts;
    speedOpts.propId     = 0x11600207;  // PERF_VEHICLE_SPEED
    speedOpts.areaIds    = {0};         // GLOBAL area
    speedOpts.sampleRate = 1.0f;        // 1 Hz

    // ... gear and fuel opts similarly ...

    // Step 5: Subscribe one property at a time (critical — see bug below)
    for (auto& p : props) {
        auto status = mVhal->subscribe(mVhalCallback, {p.opts}, 0);
        if (!status.isOk() && p.required) return false;
    }

    // Step 6: Request initial values (async in VHAL V3)
    mVhal->getValues(mVhalCallback, getRequests);
    return true;
}
```

**Why raw hex property IDs?** `VehicleProperty.h` is generated from a very large AIDL file
and is not always present in the NDK include path for vendor binaries. Using raw hex values
(`0x11600207`) is equivalent and avoids the fragile generated-header dependency.

## VhalEventCallback — Receiving VHAL Events

### The VHAL V3 API Change

This was the biggest source of build errors in the project. VHAL V3 (AOSP 14/15) changed
the callback interfaces significantly from V2:

| Method | V2 (old) | V3 (AOSP 14/15) |
|--------|----------|------------------|
| `onPropertyEvent` | `(vector<VehiclePropValue>&, int64_t timestamp)` | `(VehiclePropValues&, int32_t sharedMemFileCount)` |
| `onPropertySetError` | `(vector<VehiclePropError>&)` | `(VehiclePropErrors&)` |
| `onGetValues` | did not exist | `(GetValueResults&)` — NEW, mandatory |
| `onSetValues` | did not exist | `(SetValueResults&)` — NEW, mandatory |
| `getValues()` | synchronous, returned results directly | async, results via `onGetValues()` |

The key insight: **V3 wraps everything in parcelable structs**, not raw vectors.
`VehiclePropValues` is NOT a `vector<VehiclePropValue>`. It is:
```cpp
parcelable VehiclePropValues {
    VehiclePropValue[] payloads;  // <-- the actual data is in .payloads
}
```

So the correct iteration:
```cpp
::ndk::ScopedAStatus VhalEventCallback::onPropertyEvent(
        const VehiclePropValues& propValues,
        int32_t /* sharedMemoryFileCount */) {
    for (const auto& propValue : propValues.payloads) {  // NOTE: .payloads
        mParent->onVhalEvent(propValue);
    }
    return ::ndk::ScopedAStatus::ok();
}
```

### The Async getValues()

In V3, `getValues()` is fully asynchronous. Results don't return from the call — they
arrive later via `onGetValues()`. We reuse `mVhalCallback` for this so the result flows
through the same `onVhalEvent()` path as live subscription events:

```cpp
::ndk::ScopedAStatus VhalEventCallback::onGetValues(const GetValueResults& results) {
    for (const auto& result : results.payloads) {
        if (result.status != StatusCode::OK) continue;
        if (!result.prop.has_value()) continue;
        mParent->onVhalEvent(result.prop.value());
    }
    return ::ndk::ScopedAStatus::ok();
}
```

## Thread Safety in SafeModeService

The service has two data structures that are accessed from multiple threads:
- `mCurrentData` — updated by VHAL Binder threads, read by client Binder threads
- `mCallbacks` — modified by client Binder threads (register/unregister), iterated
  during dispatch

`mMutex` protects both. **Critical rule: never hold the mutex while dispatching callbacks.**
If you call `cb->onVehicleDataChanged(data)` while holding the mutex, and the client's
callback handler calls `registerCallback()` back into the service, you deadlock.

The solution is to copy the snapshot and the callback list under the lock, then release
the lock before dispatching:

```cpp
void SafeModeService::dispatchToCallbacks(const VehicleData& data) {
    // Copy callback list under lock
    std::vector<std::shared_ptr<ISafeModeCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        callbacks = mCallbacks;  // cheap shared_ptr copy
    }
    // Dispatch OUTSIDE the lock
    for (const auto& cb : callbacks) {
        auto status = cb->onVehicleDataChanged(data);
        if (!status.isOk()) { /* mark dead, remove after loop */ }
    }
}
```

## The RC File

```
# vendor/myoem/services/safemode/safemoded.rc

service safemoded /vendor/bin/safemoded
    class main
    user system
    group system
```

**`class main`** — init starts this service when it reaches `class_start main` during
normal boot. No manual `start` command needed in any init.rc.

**`user system`** — runs as UID 1000. Sufficient for Binder IPC with VHAL and ServiceManager.
Unlike ThermalControl (which wrote sysfs files as root), SafeMode only does Binder IPC —
no filesystem permission setup needed in the RC file.

---

<a name="part-5"></a>
# Part 5: Phase 2 — SELinux Policy

## The Four SELinux Files

Every vendor service needs exactly four SELinux files:

### 1. `safemoded.te` — Domain Policy

```te
type safemoded,      domain;
type safemoded_exec, exec_type, vendor_file_type, file_type;

init_daemon_domain(safemoded)   # domain transition + basic daemon perms
binder_use(safemoded)           # open /dev/binder, make/receive transactions
binder_service(safemoded)       # register with ServiceManager, receive calls
add_service(safemoded, safemoded_service)  # register our service name
binder_call(safemoded, hal_vehicle_server) # call into VHAL
```

### 2. `service.te` — Service Label Declaration

```te
type safemoded_service, app_api_service, service_manager_type;
```

**`app_api_service`** — allows regular Android apps to find this service in ServiceManager.
Without this, only system/vendor processes could look up the service — our app wouldn't
be able to connect.

### 3. `service_contexts` — Name-to-Label Mapping

```
com.myoem.safemode.ISafeModeService    u:object_r:safemoded_service:s0
```

This maps the service registration name to the SELinux label. The name here must match
**exactly** what is passed to `AServiceManager_addService()` in `main.cpp` and
`ServiceManager.getService()` in `SafeModeManager.kt`.

### 4. `file_contexts` — Binary Label

```
/vendor/bin/safemoded    u:object_r:safemoded_exec:s0
```

## Build Error: Wrong VHAL Domain Name

**Error:**
```
out/...safemoded.te:67: error: unknown type hal_automotive_vehicle_server
```

**Wrong assumption:** The VHAL process domain must be `hal_automotive_vehicle_server`.

**How to find the correct name:**
```bash
grep -r "hal_vehicle" system/sepolicy/vendor/
# Found: system/sepolicy/vendor/hal_vehicle_default.te
# Contents: hal_server_domain(hal_vehicle_default, hal_vehicle)
# Meaning: the domain attribute is "hal_vehicle_server"
```

**Fix:** `binder_call(safemoded, hal_vehicle_server)` — NOT `hal_automotive_vehicle_server`.

**Lesson:** Never assume SELinux type names. Always grep `system/sepolicy/` for the
actual type definition.

## Build Error: `log_device` Does Not Exist

**Error:**
```
out/...safemoded.te:71: error: unknown type log_device
```

**Wrong assumption:** Logging requires explicit SELinux permission for `/dev/log/*`.

**Reality:** In Android 13+, the character device `/dev/log/*` and its SELinux type
`log_device` were removed. Logging now goes through `logd` via a Unix socket.
The necessary permissions are already included in `init_daemon_domain()`.

**Fix:** Remove the line `allow safemoded log_device:chr_file rw_file_perms;` entirely.

---

<a name="part-6"></a>
# Part 6: Phase 3 — The Kotlin Library (`safemode_library`)

## Library Architecture

The library is an `android_library` (AAR equivalent) in `vendor/myoem/libs/safemode/`.
It provides three public types to app developers:

| Class | Purpose |
|---|---|
| `SafeModeManager` | Singleton. Manages Binder connection, delivers updates on main thread |
| `SafeModeState` | Enum: `NO_SAFE_MODE`, `NORMAL_SAFE_MODE`, `HARD_SAFE_MODE` |
| `VehicleInfo` | Data class with `speedKmh`, `gearName`, `fuelLevelL` computed properties |
| `SafeModeListener` | `fun interface` for receiving updates |

## SafeModeState — Business Logic Lives Here

```kotlin
enum class SafeModeState {
    NO_SAFE_MODE, NORMAL_SAFE_MODE, HARD_SAFE_MODE;

    companion object {
        private const val NORMAL_KMH = 5.0f
        private const val HARD_KMH   = 15.0f

        fun fromVehicleInfo(info: VehicleInfo): SafeModeState {
            val kmh = info.speedMs * 3.6f
            return when {
                kmh >= HARD_KMH   -> HARD_SAFE_MODE
                kmh >= NORMAL_KMH -> NORMAL_SAFE_MODE
                else              -> NO_SAFE_MODE
            }
        }
    }
}
```

This is the key architectural boundary: the service delivers raw m/s, the library
applies the threshold logic. Changing thresholds is a library update, not a vendor OTA.

## VehicleInfo — Public Model vs AIDL Model

There are two vehicle data types in this project:

| Type | Where | Purpose |
|---|---|---|
| `VehicleData` (AIDL) | `ISafeModeService.aidl` | Binder IPC transport — raw VHAL units |
| `VehicleInfo` (Kotlin) | `safemode_library` | Public API — adds computed properties |

The library maps one to the other internally. App developers never see `VehicleData`.
This separation means the AIDL interface can evolve independently of the public library API.

```kotlin
data class VehicleInfo(
    val speedMs:     Float = 0f,
    val gear:        Int   = VehicleGear.PARK,
    val fuelLevelMl: Float = 0f,
) {
    val speedKmh:   Float  get() = speedMs * 3.6f
    val fuelLevelL: Float  get() = fuelLevelMl / 1000f
    val gearName:   String get() = VehicleGear.name(gear)
}
```

## SafeModeManager — The Singleton

The manager uses double-checked locking for the singleton:

```kotlin
companion object {
    @Volatile private var instance: SafeModeManager? = null

    fun getInstance(context: Context): SafeModeManager =
        instance ?: synchronized(this) {
            instance ?: SafeModeManager(context.applicationContext).also { instance = it }
        }
}
```

**Why `applicationContext`?** Passing an Activity context to a singleton would leak
the Activity (it can't be garbage collected as long as the singleton holds a reference).
`applicationContext` lives as long as the process — safe for a singleton.

## Why Polling Instead of Push Callbacks? — The Binder Stability Problem

The original design registered a Java `ISafeModeCallback.Stub()` with `safemoded`
so the vendor service could push updates directly to the library. This worked for
C++ clients but **fails at runtime for Java clients** with this error:

```
Cannot do a user transaction on a system stability binder
(com.myoem.safemode.ISafeModeCallback) in a vendor stability context
```

**Root cause:** Even though the AIDL is declared `@VintfStability`, the Java AIDL
compiler does NOT emit `AIBinder_markVintfStability()` for Java `Stub` objects.
A Java `ISafeModeCallback.Stub()` created in the system partition has **SYSTEM stability**.
Vendor processes (`safemoded`) cannot call into SYSTEM stability binders — the NDK
rejects any such cross-stability transaction.

The binder stability model:
```
System process (app) → Vendor process (safemoded): ALLOWED (polling direction)
Vendor process (safemoded) → System process (app): BLOCKED (callback direction)
```

**The fix: polling.** SafeModeManager calls `getCurrentData()` on safemoded every
200ms on the main thread. This is system→vendor (always allowed). No callback binder
is ever sent from the vendor side into the system partition.

This is identical to how `ThermalControlManager` works in our ThermalControl project.

## getOrConnect() — Connecting to safemoded on First Poll

The library connects to `safemoded` using the hidden `android.os.ServiceManager` API
via reflection (works in both the AOSP platform build and standalone Gradle builds):

```kotlin
private fun getOrConnect(): ISafeModeService? {
    mService?.let { return it }  // already connected

    return try {
        // Note the VINTF format: "package.Interface/instance"
        val binder = getServiceBinder("com.myoem.safemode.ISafeModeService/default")
        if (binder == null) {
            Log.e(TAG, "Service not found — is safemoded running?")
            return null
        }
        binder.linkToDeath(mDeathRecipient, 0)  // detect service crash
        val svc = ISafeModeService.Stub.asInterface(binder)
        mService = svc
        Log.i(TAG, "Connected to SafeModeService (version=${safeGetVersion(svc)})")
        svc
    } catch (e: Exception) {
        Log.e(TAG, "Connect failed [${e.javaClass.simpleName}]: ${e.message}")
        null
    }
}
```

**Why not `Context.bindService()`?** `bindService()` works with services registered
through `PackageManager`. `safemoded` is a native Binder service registered directly
with ServiceManager — it has no `ServiceConnection` or intent filter. It must be found
via `ServiceManager.getService()`.

**Service name includes `/default`** — VINTF-stable services must be registered and
looked up as `"package.Interface/instance"`. Omitting `/default` causes the
connection to silently fail (the name doesn't match the `service_contexts` mapping).

**Logging `e.javaClass.simpleName`** — A critical debugging lesson: when `e.message`
is null (as with `DeadObjectException`), just logging `e.message` produces
`"Failed to connect: null"` — completely uninformative. Always log the exception class.

## The Binder Death Recipient

```kotlin
private val mDeathRecipient = IBinder.DeathRecipient {
    Log.w(TAG, "SafeModeService died — will reconnect on next poll")
    mService = null
}
```

If `safemoded` crashes, the Binder dies and this fires on a Binder thread.
We clear `mService` so the next poll cycle will reconnect automatically. This makes
the library resilient to service restarts without requiring app restarts.

## The Polling Loop

```kotlin
/** Runs on the main thread every POLL_INTERVAL_MS (200ms). */
private val pollRunnable = object : Runnable {
    override fun run() {
        if (!mPolling) return

        try {
            val svc = getOrConnect()
            if (svc != null) {
                val data = svc.getCurrentData()  // system→vendor: always allowed
                if (data != null) processData(data)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Poll error [${e.javaClass.simpleName}]: ${e.message}")
            mService = null   // force reconnect on next poll
        }

        if (mPolling) mainHandler.postDelayed(this, POLL_INTERVAL_MS)
    }
}
```

**Why 200ms?** The Python simulator updates data every 5 seconds. At 200ms poll
interval, the app sees new data within 200ms of the simulator writing to the file.
In production (real VHAL), speed events arrive at ~1 Hz — 200ms is still well under
the VHAL event rate, so no update is ever missed. The overhead of one `getCurrentData()`
Binder call every 200ms is negligible.

## processData() — Converting and Notifying on Main Thread

```kotlin
private fun processData(data: VehicleData) {
    // Always on the main thread (pollRunnable runs on mainHandler)
    val info     = VehicleInfo(speedMs=data.speedMs, gear=data.gear, fuelLevelMl=data.fuelLevelMl)
    val newState = SafeModeState.fromVehicleInfo(info)

    mCurrentState = newState
    mCurrentInfo  = info

    // Notify all listeners — already on main thread, so no post() needed
    listeners.values.forEach { it.onSafeModeChanged(newState, info) }
}
```

Because `pollRunnable` runs entirely on `mainHandler` (the main thread), all listener
calls are already on the correct thread. Compose `mutableStateOf` writes happen
directly without needing `post()` or `runOnUiThread()`.

## attach() / dispose() — Lifecycle Management

```kotlin
fun attach(owner: Any, listener: SafeModeListener) {
    listeners[owner] = listener
    // Deliver cached state immediately before first poll
    val state = mCurrentState; val info = mCurrentInfo
    mainHandler.post { listener.onSafeModeChanged(state, info) }
    // Start polling loop if not already running
    if (!mPolling) {
        mPolling = true
        mainHandler.post(pollRunnable)
    }
}

fun dispose(owner: Any) {
    listeners.remove(owner)
    if (listeners.isEmpty()) {
        mPolling = false
        mainHandler.removeCallbacks(pollRunnable)
    }
}
```

The cached state delivery on `attach()` ensures the UI is populated before the
first 200ms poll completes — no blank screen flash on startup.

## Build Error: AIDL Include Path Wrong

**Error:**
```
vendor/myoem/libs/safemode/src/main/aidl/com/myoem/safemode/ISafeModeCallback.aidl:6:
    couldn't find import for class com.myoem.safemode.VehicleData
```

**Root cause:** Soong passes the include path for AIDL as the module root:
```
-Ivendor/myoem/libs/safemode/
-Ivendor/myoem/libs/safemode/src/
```
But `VehicleData.aidl` is at:
```
vendor/myoem/libs/safemode/src/main/aidl/com/myoem/safemode/VehicleData.aidl
```
The compiler looks for `<include_root>/com/myoem/safemode/VehicleData.aidl`.
With `-Ivendor/myoem/libs/safemode/`, it looks for:
```
vendor/myoem/libs/safemode/com/myoem/safemode/VehicleData.aidl  ← NOT FOUND
```
Needs to look for:
```
vendor/myoem/libs/safemode/src/main/aidl/com/myoem/safemode/VehicleData.aidl  ← FOUND
```

**Fix:** Add `aidl { local_include_dirs: ["src/main/aidl"] }` to the `android_library`:

```bp
android_library {
    name: "safemode_library",
    // ...
    aidl: {
        local_include_dirs: ["src/main/aidl"],  // ← This was missing
    },
}
```

This makes Soong pass `-Ivendor/myoem/libs/safemode/src/main/aidl`, and the path resolves.

---

<a name="part-7"></a>
# Part 7: Phase 4 — The Jetpack Compose App (`SafeModeDemo`)

## UI Design

```
┌─────────────────────────────────────────────────────┐
│  🚗 Speed    ⚙️ Gear     ⛽ Fuel                    │
│  [ 0.0 ]    [ PARK ]   [ 0.0 ]                      │
│   km/h                    L                         │
│                                                     │
│                                                     │
│               ┌─────────────────┐                   │
│               │       ✅        │                   │
│               │  NO SAFE MODE   │   ← green         │
│               │ All features    │                   │
│               │  available      │                   │
│               └─────────────────┘                   │
└─────────────────────────────────────────────────────┘
```

When speed > 5 km/h: card turns yellow, shows "NORMAL SAFE MODE"
When speed > 15 km/h: card turns red, shows "HARD SAFE MODE"

## Activity Lifecycle Integration

```kotlin
class MainActivity : ComponentActivity() {
    private var vehicleInfo by mutableStateOf(VehicleInfo())
    private var safeMode    by mutableStateOf(SafeModeState.NO_SAFE_MODE)

    private val safeModeListener = SafeModeListener { state, info ->
        safeMode    = state   // triggers Compose recomposition
        vehicleInfo = info
    }

    override fun onStart() {
        super.onStart()
        // attach() connects to service (if needed) + delivers current state immediately
        SafeModeManager.getInstance(this).attach(this, safeModeListener)
    }

    override fun onStop() {
        super.onStop()
        // dispose() removes listener; if no other listeners, unregisters callback from service
        SafeModeManager.getInstance(this).dispose(this)
    }
}
```

**Why `onStart()`/`onStop()` and not `onCreate()`/`onDestroy()`?**
When the app goes to the background (another app covers it), `onStop()` is called.
`onDestroy()` is only called when the Activity is finishing. Using `onStart/onStop` means
we stop receiving VHAL events (and reduce Binder traffic) whenever the UI is not visible.

## Compose Color Animation

```kotlin
val targetColor = when (state) {
    SafeModeState.NO_SAFE_MODE     -> Color(0xFF4CAF50)  // Material Green 500
    SafeModeState.NORMAL_SAFE_MODE -> Color(0xFFFFC107)  // Material Amber 500
    SafeModeState.HARD_SAFE_MODE   -> Color(0xFFF44336)  // Material Red 500
}
val animatedColor by animateColorAsState(
    targetValue   = targetColor,
    animationSpec = tween(durationMillis = 600),
    label         = "safemode_color",
)
```

`animateColorAsState()` smoothly interpolates between colors over 600ms whenever
`state` changes. Without this, the card would jump abruptly between colors.

**Text contrast on amber (yellow):** Yellow is light — white text on yellow has poor
contrast. We detect the `NORMAL_SAFE_MODE` state and switch to dark text:

```kotlin
val contentColor = when (state) {
    SafeModeState.NORMAL_SAFE_MODE -> Color(0xFF212121)  // dark on yellow
    else                           -> Color.White         // white on green/red
}
```

## Android.bp for the App

```bp
// SafeModeDemo/Android.bp

soong_namespace {
    imports: ["vendor/myoem/libs/safemode"]  // ← REQUIRED to see safemode_library
}

android_app {
    name: "SafeModeDemo",
    platform_apis: true,       // for hidden ServiceManager API
    certificate: "platform",   // system-signed so hidden API check passes at runtime

    static_libs: [
        "safemode_library",
        "androidx.compose.ui_ui",
        "androidx.compose.material3_material3",
        "androidx.compose.runtime_runtime",
        "androidx.compose.animation_animation",
        "androidx.compose.animation_animation-core",
        "androidx.activity_activity-compose",
        "androidx.compose.foundation_foundation",
        "androidx.compose.foundation_foundation-layout",
        // ... ui-graphics, ui-text, ui-unit, ui-tooling-preview
    ],
}
```

## Build Error: Soong Namespace Isolation

**Error:**
```
Android.bp:51:5: module "safemode_library" not found
```

**Root cause:** Soong enforces **strict namespace isolation**. A module in namespace A
cannot see modules in namespace B — even if B is under the same `vendor/` directory.
`SafeModeDemo` is in namespace `vendor/myoem/apps/SafeModeDemo/`.
`safemode_library` is in namespace `vendor/myoem/libs/safemode/`.
These are different namespaces, so `SafeModeDemo` cannot see `safemode_library` by default.

**Fix:** Declare the import in `soong_namespace{}`:
```bp
soong_namespace {
    imports: ["vendor/myoem/libs/safemode"]
}
```

This is not a security control — it is a build-graph isolation mechanism to prevent
accidental cross-vendor dependencies and speed up build analysis.

**Key lesson:** Every time you get "module X not found" in a Soong build inside `vendor/`,
check whether the module lives in a different Soong namespace and add an `imports:` entry.

---

<a name="part-8"></a>
# Part 8: Build Error Deep Dive — Every Error and Its Fix

## Error 1: Wrong VehicleProperty include

**Error:**
```
fatal error: 'aidl/android/hardware/automotive/vehicle/VehicleProperty.h' file not found
```

**Context:** `SafeModeService.cpp` originally had:
```cpp
#include <aidl/android/hardware/automotive/vehicle/VehicleProperty.h>
```
And used `VehicleProperty::PERF_VEHICLE_SPEED` to name the property IDs.

**Root cause:** `VehicleProperty.h` is generated from `VehicleProperty.aidl` which
has 1000+ entries. The generated header is not always in the NDK include path for vendor binaries.

**Fix:** Remove the include. Use raw hex constants instead:
```cpp
static constexpr int32_t PROP_SPEED = 0x11600207;  // PERF_VEHICLE_SPEED
static constexpr int32_t PROP_GEAR  = 0x11400400;  // CURRENT_GEAR
static constexpr int32_t PROP_FUEL  = 0x45100004;  // FUEL_LEVEL
```
The hex values are stable across VHAL V2/V3 and match what `inject-vhal-event` uses.

---

## Error 2: VhalEventCallback is abstract (missing V3 methods)

**Error:**
```
error: cannot instantiate abstract class 'VhalEventCallback'
note: unimplemented pure virtual method 'onGetValues'
note: unimplemented pure virtual method 'onSetValues'
```

**Root cause:** VHAL V3 added two new pure virtual methods to `IVehicleCallback`:
- `onGetValues(GetValueResults&)` — receives async getValues() responses
- `onSetValues(SetValueResults&)` — receives async setValues() responses

Both must be implemented even if you never call `setValues()`.

**Fix:**
```cpp
// In SafeModeService.h:
::ndk::ScopedAStatus onGetValues(const GetValueResults& results) override;
::ndk::ScopedAStatus onSetValues(const SetValueResults& results) override;

// In SafeModeService.cpp:
::ndk::ScopedAStatus VhalEventCallback::onGetValues(const GetValueResults& results) {
    for (const auto& result : results.payloads) {
        if (result.status != StatusCode::OK || !result.prop.has_value()) continue;
        mParent->onVhalEvent(result.prop.value());
    }
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus VhalEventCallback::onSetValues(const SetValueResults&) {
    return ::ndk::ScopedAStatus::ok();  // no-op
}
```

---

## Error 3: onPropertyEvent signature mismatch

**Error:**
```
error: 'onPropertyEvent' marked 'override' but does not override any member functions
```

**Wrong signature (V2 style):**
```cpp
::ndk::ScopedAStatus onPropertyEvent(
    const std::vector<VehiclePropValue>& values,
    int64_t timestamp) override;
```

**Correct signature (V3):**
```cpp
::ndk::ScopedAStatus onPropertyEvent(
    const VehiclePropValues& propValues,       // parcelable wrapper, NOT vector
    int32_t sharedMemoryFileCount) override;   // int32, NOT int64
```

Two separate changes: (1) parameter type changed from `vector<VehiclePropValue>` to
the V3 wrapper struct `VehiclePropValues`, and (2) the second parameter changed from
`int64_t timestamp` to `int32_t sharedMemoryFileCount`.

---

## Error 4: getValues() wrong call signature (V3 async)

**Error:**
```
error: no matching function for call to 'IVehicle::getValues'
```

**Wrong call (V2 synchronous style):**
```cpp
std::vector<GetValueResult> results;
mVhal->getValues(getRequests, &results);  // synchronous — does NOT exist in V3
```

**Correct call (V3 async):**
```cpp
GetValueRequests getRequests;
getRequests.payloads = {
    {.requestId = 0, .prop = {.areaId = 0, .prop = PROP_SPEED}},
    // ...
};
mVhal->getValues(mVhalCallback, getRequests);  // async — results arrive via onGetValues()
```

---

## Error 5: Runtime crash-loop — FUEL_LEVEL INVALID_ARG

**Symptom in logcat:**
```
E safemoded: VHAL subscribe FATAL for FUEL (0x45100004):
    no config for property, ID: 1158676484: INVALID_ARG
E safemoded: connectToVhal() failed — exiting
```
Then init restarts safemoded → same error → restart loop.

**Root cause:** `IVehicle::subscribe()` is all-or-nothing when called with a batch of
properties. If ANY property in the batch is unsupported, the ENTIRE call returns
`INVALID_ARG` and NO properties are subscribed. The RPi5 VHAL emulator does not support
`FUEL_LEVEL` (0x45100004).

**Wrong approach:**
```cpp
// ONE call with all 3 — if FUEL fails, SPEED and GEAR also fail
mVhal->subscribe(mVhalCallback, {speedOpts, gearOpts, fuelOpts}, 0);
```

**Fix — per-property subscription:**
```cpp
struct PropSubscription {
    const char* name;
    SubscribeOptions opts;
    bool required;  // fatal if required and fails; warning if not
};

PropSubscription props[] = {
    { "SPEED", speedOpts, true  },  // required — drives state machine
    { "GEAR",  gearOpts,  true  },  // required — displayed in UI
    { "FUEL",  fuelOpts,  false },  // optional — RPi5 VHAL doesn't support it
};

for (auto& p : props) {
    auto status = mVhal->subscribe(mVhalCallback, {p.opts}, 0);
    if (status.isOk()) {
        ALOGI("VHAL subscribe OK: %s (prop=0x%x)", p.name, p.opts.propId);
    } else {
        ALOGE("VHAL subscribe %s for %s: %s",
              p.required ? "FATAL" : "WARNING", p.name, status.getDescription().c_str());
        if (p.required) return false;
        // Optional — continue without it
    }
}
```

**Lesson:** In AOSP emulated VHALs, not all properties defined in the AIDL are supported.
Always subscribe one-by-one with graceful handling of optional properties.

---

## Error 6: Runtime — "Failed to connect: null" (DeadObjectException)

**Symptom in logcat:**
```
E SafeModeManager: Failed to connect to SafeModeService: null
```

**Wrong assumption:** "null" means the service was not found.

**Reality:** When `safemoded` was crash-looping (due to Error 5 above), ServiceManager
retains a **ghost Binder entry** for the service name. When the library calls
`ServiceManager.getService("com.myoem.safemode.ISafeModeService")`, it gets back a
dead Binder (the service process exited). The subsequent
`service.registerCallback(mBinderCallback)` call throws `DeadObjectException`.

`DeadObjectException extends RemoteException` and its `message` property is `null`
(it carries no error string). So `e.message` logs as "null", which looks like it means
the service wasn't found.

**Fix 1 (long-term):** Fix the crash-loop first (Error 5). Once safemoded runs stably,
the ghost Binder disappears and clients can connect.

**Fix 2 (debugging):** Log the exception class, not just the message:
```kotlin
Log.e(TAG, "Failed to connect [${e.javaClass.simpleName}]: ${e.message}", e)
// Now logs: "Failed to connect [DeadObjectException]: null"
```

**Fix 3 (immediate):** Force-stop the app after fixing safemoded to clear the dead Binder
reference in the app process:
```bash
adb shell am force-stop com.myoem.safemodedemo
adb shell am start -n com.myoem.safemodedemo/.MainActivity
```

---

<a name="part-9"></a>
# Part 9: Runtime Debugging — Layer by Layer

When a value change in VHAL doesn't reach the app, the problem could be at any of 4 layers.
Here is the systematic debugging sequence:

## Important: `inject-vhal-event` Does NOT Reach safemoded Directly

```bash
adb shell cmd car_service inject-vhal-event 0x11600207 0 3.0
```

This command only injects into `CarPropertyService`'s internal buffer — the value
never reaches `IVehicle::setValues()` at the HAL level. If `safemoded` subscribes
directly to `IVehicle::subscribe()`, it will **never receive these injected values**.
This is why we built the `SAFEMODE_SIM_MODE` file-based simulator instead.

## Layer 1: Is the Simulator Writing Data?

```bash
# On the HOST machine (not adb shell):
cd vendor/myoem/tools/safemode-simulator

# Run one full drive cycle (20 steps × 5s each = 100s total)
python3 simulate_all.py

# Or run continuously (Ctrl+C to stop)
python3 simulate_all.py --loop

# Or faster for testing (2s per step)
python3 simulate_all.py --interval 2
```

Expected simulator output:
```
============================================================
SafeMode Simulator (RPi5 dev mode)
  Steps:    20
  Interval: 5.0s per step
  Loop:     False
  Sim file: /data/local/tmp/safemode_sim.txt
============================================================

  Step   Speed      Gear      Fuel%    SafeModeState
  --------------------------------------------------------
  [01/20]   0.0 km/h  PARK      100.0%  → NO_SAFE_MODE
  [02/20]   1.8 km/h  PARK       98.0%  → NO_SAFE_MODE
  [03/20]   3.6 km/h  NEUTRAL    96.0%  → NO_SAFE_MODE
  [04/20]   5.0 km/h  REVERSE    94.0%  → NORMAL_SAFE_MODE
  ...
  [09/20]  18.0 km/h  DRIVE      80.0%  → HARD_SAFE_MODE
  ...
  [20/20]   0.0 km/h  PARK       62.0%  → NO_SAFE_MODE
```

## Layer 2: Is safemoded Running and Receiving Data?

```bash
# Check safemoded process and SELinux domain
adb shell ps -eZ | grep safemoded
# Expected: u:r:safemoded:s0  ... /vendor/bin/safemoded

# Watch safemoded logs (run alongside the simulator)
adb logcat -s safemoded -v time
# In SAFEMODE_SIM_MODE, expected every 5s:
# I safemoded: SIM: speed=0.00 m/s (0.0 km/h) gear=0x4 fuel=50000 ml
# I safemoded: SIM: speed=0.50 m/s (1.8 km/h) gear=0x4 fuel=49000 ml
# ...
# I safemoded: SIM: speed=4.17 m/s (15.0 km/h) gear=0x8 fuel=43000 ml
#
# Bad: no log → SAFEMODE_SIM_MODE not compiled in, or service not running
# Bad: crash-loop → check for SELinux denial or init.rc issue

# Confirm service is registered with ServiceManager
adb shell service list | grep "com.myoem.safemode"
# Expected: com.myoem.safemode.ISafeModeService/default: [android.os.IBinder]
# Note: MUST include /default — omitting it means the wrong name was used in main.cpp

# Check for SELinux denials
adb logcat -d | grep "avc: denied" | grep safemoded
```

## Layer 3: Is the Binder IPC Working? (C++ Test Client)

```bash
# Use the safemode_client test binary — a faster test than the full app
adb shell /vendor/bin/safemode_client subscribe 30
# Expected: prints VehicleData every time safemoded's state changes
# [0] speed=0.50 m/s (1.80 km/h)  gear=4  fuel=49000 ml
# [1] speed=1.00 m/s (3.60 km/h)  gear=1  fuel=48000 ml

# Or test the synchronous getCurrentData() call:
adb shell /vendor/bin/safemode_client get
# Expected: Current: speed=0.50 m/s  gear=4  fuel=49000 ml
```

## Layer 4: Is SafeModeManager Polling and Connecting?

```bash
# Watch app-side logs (run alongside the simulator)
adb logcat -s SafeModeManager -v time
# Expected on first poll after attach():
# D SafeModeManager: ServiceManager.getService(...) → found (alive=true)
# I SafeModeManager: Connected to SafeModeService (version=1)
# D SafeModeManager: Poll: 0.0 km/h  gear=PARK  fuel=50.0L  → NO_SAFE_MODE
# D SafeModeManager: Poll: 1.8 km/h  gear=PARK  fuel=49.0L  → NO_SAFE_MODE
# D SafeModeManager: Poll: 18.0 km/h gear=DRIVE fuel=44.0L  → HARD_SAFE_MODE
#
# Bad: "Service not found — is safemoded running?" → safemoded not started
# Bad: "Connect failed [DeadObjectException]" → safemoded crash-looping;
#       fix the service first, then force-stop and relaunch the app

# Force-stop app and relaunch (clears dead Binder references in the app process)
adb shell am force-stop com.myoem.safemodedemo
adb shell am start -n com.myoem.safemodedemo/.MainActivity
```

## Full End-to-End Test — All Layers Together

Run three terminals in parallel:

**Terminal 1 (device logs):**
```bash
adb logcat -s SafeModeManager safemoded -v time
```

**Terminal 2 (simulator):**
```bash
cd vendor/myoem/tools/safemode-simulator
python3 simulate_all.py --interval 3
```

**Terminal 3 (optional C++ client):**
```bash
adb shell /vendor/bin/safemode_client subscribe 120
```

Watch the app on the device — you should see:
- Green card → "NO SAFE MODE" (steps 1–8, speed 0–4.2 m/s)
- Yellow card → "NORMAL SAFE MODE" (steps at 5 km/h threshold)
- Red card → "HARD SAFE MODE" (steps 9–15, speed ≥ 4.2 m/s = 15 km/h)
- Yellow card then Green card as speed ramps back down

## Pushing Updated APK to Device

Since `/system` is often mounted read-only even after `adb root`, use the data partition:

```bash
# Build the app
lunch myoem_rpi5_car-trunk_staging-userdebug && m SafeModeDemo safemode_library

# Find the built APK (product dir is 'rpi5', not 'myoem_rpi5_car')
find out/target/product/rpi5 -name "SafeModeDemo.apk"
# Usually: out/target/product/rpi5/system/app/SafeModeDemo/SafeModeDemo.apk

# Push via data partition (avoids read-only /system)
adb push out/target/product/rpi5/system/app/SafeModeDemo/SafeModeDemo.apk \
         /data/local/tmp/SafeModeDemo.apk
adb shell pm install -r /data/local/tmp/SafeModeDemo.apk

# Relaunch
adb shell am force-stop com.myoem.safemodedemo
adb shell am start -n com.myoem.safemodedemo/.MainActivity
```

---

<a name="part-10"></a>
# Part 10: Lessons Learned — AAOS Development on RPi5

## AOSP VHAL API Lessons

### 1. VHAL V3 is NOT backward-compatible with V2

If you find any VHAL example code online, check the AOSP version it was written for.
V2 and V3 have incompatible callback signatures, and V3's `getValues()` is async where
V2's was synchronous. The key changes:

- `onPropertyEvent` parameter types changed (struct wrappers, not raw vectors)
- `onPropertySetError` parameter types changed
- `onGetValues` and `onSetValues` are new mandatory pure virtuals
- `getValues()` is now async (callback-based, not blocking)

### 2. Subscribe one property at a time

`IVehicle::subscribe()` with a batch is all-or-nothing. On any real device, some
VHAL properties will be unsupported. Subscribe in a loop and handle each failure individually.
Mark optional properties (fuel, odometer, etc.) as non-fatal.

### 3. Don't use VehicleProperty.h — use raw hex IDs

The generated header may not be in the include path for vendor binaries. The hex values
are stable across VHAL versions and match the values `inject-vhal-event` accepts.

### 4. The RPi5 AAOS VHAL does not support FUEL_LEVEL

`FUEL_LEVEL (0x45100004)` returns `INVALID_ARG` on subscribe. Design your service to
handle missing optional properties gracefully.

## SELinux Lessons

### 5. Never assume SELinux type names

Before using any type name in a `.te` file, grep for it:
```bash
grep -r "hal_vehicle" system/sepolicy/
grep -r "log_device" system/sepolicy/
```
If a type doesn't appear in `system/sepolicy/`, it doesn't exist in AOSP 15's base policy.

### 6. `log_device` is gone in Android 13+

Logging moved from `/dev/log/*` character devices to the `logd` Unix socket.
The `init_daemon_domain()` macro already grants logging permissions. No explicit
logging rule is needed or allowed.

### 7. `hal_vehicle_server` not `hal_automotive_vehicle_server`

The actual VHAL server domain attribute in AOSP 15 is `hal_vehicle_server` (defined in
`system/sepolicy/vendor/hal_vehicle_default.te`). The intuitive name
`hal_automotive_vehicle_server` doesn't exist.

## Soong Build Lessons

### 8. Soong namespace isolation is strict

Modules in different Soong namespaces cannot see each other. If you get
"module X not found" inside `vendor/`, check whether X lives in a different
`soong_namespace {}` directory. Fix with `imports: ["path/to/namespace"]`.

### 9. AIDL `local_include_dirs` must match your package root

For `android_library` modules with AIDL files nested under non-standard paths
(like `src/main/aidl/`), you must set:
```bp
aidl: {
    local_include_dirs: ["src/main/aidl"],
}
```
Without this, the AIDL compiler can't resolve imports between files in the same package.

### 10. Compose in AOSP is always static

Jetpack Compose is not bundled as a shared framework library in AOSP. Each app using
Compose must include all Compose modules in `static_libs`. This increases APK size
but is the only supported approach in the AOSP build system.

## Binder and IPC Lessons

### 11. ServiceManager ghost Binders cause DeadObjectException with null message

When a native service crash-loops, ServiceManager retains its Binder entry. Clients
that call `getService()` during the crash loop get a dead Binder. The first real IPC
call throws `DeadObjectException` with a null message. This looks like
`"Failed to connect: null"` — which looks like the service wasn't found.

Always log `e.javaClass.simpleName` alongside `e.message`:
```kotlin
Log.e(TAG, "Failed to connect [${e.javaClass.simpleName}]: ${e.message}", e)
```

Fix the crash-loop, then force-stop and relaunch the app.

### 12. Always dispatch callbacks OUTSIDE the mutex

Holding a mutex while calling Binder callbacks across process boundaries risks deadlock.
If the remote client's callback handler calls back into your service (e.g., to
`registerCallback`), it will try to acquire the mutex you already hold.

Pattern: lock → copy data → unlock → dispatch.

### 13. `ndk::SharedRefBase::make<T>()` is mandatory for AIDL NDK objects

The NDK AIDL Binder base classes require `shared_ptr` from `SharedRefBase::make<>()`.
Using `new T()` or `std::make_shared<T>()` corrupts the Binder reference count.

## Lunch Target Lessons

### 14. Use the `_car` target for AAOS / VHAL development

The plain `myoem_rpi5` target has no VHAL and no CarService. For any AAOS feature
involving VHAL, use `myoem_rpi5_car`. Confirm with:
```bash
adb shell service list | grep "android.hardware.automotive.vehicle"
```

### 15. The build output product dir may not match the lunch target name

After `lunch myoem_rpi5_car-...`, built APKs land in `out/target/product/rpi5/`,
not `out/target/product/myoem_rpi5_car/`. The product dir is set by the device's
`PRODUCT_NAME`, not the lunch target name. Check the last build output line:
```
Install: out/target/product/rpi5/system/app/SafeModeDemo
```

## Architecture Lessons

### 16. Service as data pipe, library as business logic

The vendor partition changes only via OTA. Business logic (speed thresholds for
SafeMode levels) should NOT be in the vendor service. Put it in the library/app layer
so it can be updated without an OTA.

### 17. `getCurrentData()` is essential for zero-latency startup

Without a synchronous snapshot method, a new client must wait up to 1 Hz interval
(1 second for a CONTINUOUS SPEED subscription) before it sees any data. The first
call to `getCurrentData()` in `registerCallback()` handler eliminates this delay.

### 18. `DeathRecipient` makes the library resilient to service crashes

If `safemoded` crashes and init.rc restarts it, the library's `mDeathRecipient` fires,
clears `mService`, and the next poll cycle reconnects transparently. Without this,
the app would hold a stale Binder forever and never see data again after a service restart.

## Java/Vendor Binder Lessons (Discovered in This Project)

### 19. Java callbacks from system → vendor are impossible without C++ NDK

Even with `@VintfStability` on the AIDL, a Java `ISafeModeCallback.Stub()` created
in a system process has **SYSTEM stability**. Vendor processes cannot call into
SYSTEM stability binders. The NDK rejects it:

```
Cannot do a user transaction on a system stability binder
(com.myoem.safemode.ISafeModeCallback) in a vendor stability context
```

The Java AIDL compiler simply does not emit `AIBinder_markVintfStability()` for Stub
objects — that annotation is C++ (NDK backend) only.

**Rule:** If your service is in the vendor partition, Java clients **cannot** register
callbacks with it. Use polling (`getCurrentData()`) in the system→vendor direction instead.

### 20. `cmd car_service inject-vhal-event` does NOT reach direct IVehicle subscribers

`inject-vhal-event` writes into `CarPropertyService`'s internal buffer, not into the
HAL layer. Any vendor service that subscribes directly to `IVehicle::subscribe()`
will **never** receive these injected events.

For testing without real vehicle hardware, the only reliable approach is either:
- A file-based simulator (our `SAFEMODE_SIM_MODE` approach)
- A mock VHAL implementation that serves controlled values

### 21. SAFEMODE_SIM_MODE compile flag is the cleanest dev/production swap

By guarding the simulator behind `#ifdef SAFEMODE_SIM_MODE`:
- In dev: add `-DSAFEMODE_SIM_MODE` to Android.bp cflags → file-based simulator
- In production: remove the flag → real VHAL subscription
- Zero other code changes needed

The simulator thread calls the exact same `dispatchToCallbacks()` and `mCurrentData`
update path as the real VHAL event handler. This means if the simulator path works,
the production path will also work — it is truly the same code.

### 22. AAOS blocks apps from launcher when `distractionOptimized` is missing

`CarPackageManagerService` hides apps in the AAOS launcher if the car's driving
state is not `PARKED`, unless the app's activity declares:

```xml
<meta-data
    android:name="distractionOptimized"
    android:value="true" />
```

This is required even when the vehicle speed is 0 — because AAOS reads speed from
`CarPropertyService`/VHAL independently of your safemoded service. If the platform
thinks the car is in DRIVING state (e.g. from a previous `inject-vhal-event`), it
will block the app regardless of what safemoded says.

A Safe Mode monitoring app **must** be distraction-optimized — it is essential for
a driver to see it at any speed. Add this meta-data to the activity.

For dev testing without rebuilding, temporarily disable UX restrictions:
```bash
adb shell cmd car_service enable-uxr false
```

### 23. Service name must include `/default` (VINTF format)

When registering VINTF-stable vendor services, the name format is:
```
com.myoem.safemode.ISafeModeService/default
```
Not just:
```
com.myoem.safemode.ISafeModeService
```

The `/default` is the instance name. It must match exactly in:
1. `main.cpp` → `AServiceManager_addService(..., "...ISafeModeService/default")`
2. `SafeModeManager.kt` → `SERVICE_NAME = "...ISafeModeService/default"`
3. `service_contexts` → `com.myoem.safemode.ISafeModeService/default u:object_r:...`
4. `safemoded.rc` → `interface aidl com.myoem.safemode.ISafeModeService/default`

Using the wrong name causes `ServiceManager.getService()` to return null silently.

---

## Summary: The Complete File List

| File | What It Does |
|---|---|
| `services/safemode/aidl/.../VehicleData.aidl` | Parcelable: speed (m/s), gear (int), fuel (ml) |
| `services/safemode/aidl/.../ISafeModeCallback.aidl` | `oneway` push callback: `onVehicleDataChanged(VehicleData)` |
| `services/safemode/aidl/.../ISafeModeService.aidl` | Service interface: getCurrentData, register/unregisterCallback, getVersion |
| `services/safemode/Android.bp` | `aidl_interface` (ndk+java backends) + `cc_binary safemoded` |
| `services/safemode/safemoded.rc` | Init descriptor: `class main, user system` |
| `services/safemode/include/SafeModeService.h` | `VhalEventCallback` + `SafeModeService` class declarations |
| `services/safemode/src/SafeModeService.cpp` | VHAL subscription, event dispatch, AIDL method implementations |
| `services/safemode/src/main.cpp` | Binder thread pool + ServiceManager registration |
| `services/safemode/sepolicy/private/safemoded.te` | Domain policy: binder_use, add_service, binder_call(hal_vehicle_server) |
| `services/safemode/sepolicy/private/service.te` | `safemoded_service` type with `app_api_service` |
| `services/safemode/sepolicy/private/service_contexts` | Name→label: `com.myoem.safemode.ISafeModeService → safemoded_service` |
| `libs/safemode/Android.bp` | `android_library safemode_library` with AIDL `local_include_dirs` fix |
| `libs/safemode/.../SafeModeManager.kt` | Singleton: Binder connection, `attach()`/`dispose()`, main-thread dispatch |
| `libs/safemode/.../SafeModeState.kt` | Thresholds: <5 km/h=NO, 5–15=NORMAL, ≥15=HARD |
| `libs/safemode/.../VehicleInfo.kt` | Data class + `VehicleGear` constants + computed `speedKmh`, `gearName`, `fuelLevelL` |
| `apps/SafeModeDemo/Android.bp` | `android_app` with `soong_namespace { imports: ["vendor/myoem/libs/safemode"] }` |
| `apps/SafeModeDemo/AndroidManifest.xml` | Includes `distractionOptimized=true` so AAOS shows app at any speed |
| `apps/SafeModeDemo/.../MainActivity.kt` | Compose dashboard: 3 metric cards + animated SafeMode state card |
| `tools/safemode-simulator/simulate_all.py` | Cycles 20 data points every N seconds; writes to safemode_sim.txt via adb |
| `tools/safemode-simulator/data/speed_data.txt` | 20 speed values in m/s covering full NO→NORMAL→HARD→NORMAL→NO cycle |
| `tools/safemode-simulator/data/gear_data.txt` | 20 gear values: PARK→NEUTRAL→REVERSE→DRIVE×12→NEUTRAL→PARK |
| `tools/safemode-simulator/data/fuel_data.txt` | 20 fuel values in ml: 50000→31000 (1000ml per step) |

---

<a name="part-11"></a>
# Part 11: Complete Testing Guide — Bottom-Up

Test from the data source up to the UI. Fix each layer before moving to the next.

## Prerequisites

```bash
# 1. Build everything
lunch myoem_rpi5_car-trunk_staging-userdebug
m safemoded safemode_client SafeModeDemo safemode_library

# 2. Flash or push to device
# Native binaries (need full flash or vendor remount):
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/safemoded /vendor/bin/safemoded
adb push out/target/product/rpi5/vendor/bin/safemode_client /vendor/bin/safemode_client

# App (data partition works without reflash):
adb push out/target/product/rpi5/system/app/SafeModeDemo/SafeModeDemo.apk \
         /data/local/tmp/SafeModeDemo.apk
adb shell pm uninstall --user 0 com.myoem.safemodedemo 2>/dev/null || true
adb shell pm install -r /data/local/tmp/SafeModeDemo.apk

# 3. Restart safemoded
adb shell stop safemoded
adb shell start safemoded

# 4. Disable AAOS driving restrictions for development
adb shell cmd car_service enable-uxr false
```

## Step 1 — Verify safemoded is Running

```bash
adb shell ps -eZ | grep safemoded
# Expected: u:r:safemoded:s0  ... /vendor/bin/safemoded
# Bad: nothing → service not started, check init.rc and SELinux

adb shell service list | grep "com.myoem.safemode"
# Expected: com.myoem.safemode.ISafeModeService/default: [android.os.IBinder]
# Bad: nothing → safemoded didn't register (check logcat -s safemoded)

adb logcat -s safemoded -d | tail -20
# In SAFEMODE_SIM_MODE: "SAFEMODE_SIM_MODE: data source = /data/local/tmp/safemode_sim.txt"
# In production mode:   "VHAL subscribe OK: SPEED" / "VHAL subscribe OK: GEAR"
```

## Step 2 — Test the Simulator Data Flow

```bash
# Open a terminal and watch safemoded logs live
adb logcat -s safemoded -v time &

# Manually write one value and confirm safemoded reads it
adb shell "echo '4.17 8 45000' > /data/local/tmp/safemode_sim.txt"
sleep 1
# Expected: I safemoded: SIM: speed=4.17 m/s (15.0 km/h) gear=0x8 fuel=45000 ml
```

## Step 3 — Test the C++ Test Client (Layer 3 Binder IPC)

```bash
# In one terminal: run the simulator
cd vendor/myoem/tools/safemode-simulator
python3 simulate_all.py --interval 3

# In another terminal: subscribe with the C++ client
adb shell /vendor/bin/safemode_client subscribe 60
# Expected: prints data as simulator values change
# If no output: Binder IPC is broken — check SELinux denials
```

## Step 4 — Test the App (Full Stack)

```bash
# Start the app
adb shell am force-stop com.myoem.safemodedemo
adb shell am start -n com.myoem.safemodedemo/.MainActivity

# In one terminal: watch app logs
adb logcat -s SafeModeManager -v time

# In another terminal: run simulator
cd vendor/myoem/tools/safemode-simulator
python3 simulate_all.py --interval 3 --loop
```

Expected SafeModeManager log sequence:
```
I SafeModeManager: Connected to SafeModeService (version=1)
D SafeModeManager: Poll:  0.0 km/h  gear=PARK   fuel=50.0L  → NO_SAFE_MODE
D SafeModeManager: Poll:  1.8 km/h  gear=PARK   fuel=49.0L  → NO_SAFE_MODE
D SafeModeManager: Poll:  7.2 km/h  gear=DRIVE  fuel=47.0L  → NORMAL_SAFE_MODE
D SafeModeManager: Poll: 15.0 km/h  gear=DRIVE  fuel=44.0L  → HARD_SAFE_MODE
D SafeModeManager: Poll: 30.0 km/h  gear=DRIVE  fuel=42.0L  → HARD_SAFE_MODE
D SafeModeManager: Poll:  7.2 km/h  gear=DRIVE  fuel=38.0L  → NORMAL_SAFE_MODE
D SafeModeManager: Poll:  0.0 km/h  gear=PARK   fuel=36.0L  → NO_SAFE_MODE
```

Expected UI changes:
| Speed Range | Card Color | State Text | Icon |
|---|---|---|---|
| < 5 km/h | Green | NO SAFE MODE | ✅ |
| 5–14.9 km/h | Yellow (amber) | NORMAL SAFE MODE | ⚠️ |
| ≥ 15 km/h | Red | HARD SAFE MODE | 🛑 |

Color transitions are animated over 600ms so you can see the smooth green→yellow→red changes.

## Step 5 — Moving to Production

When ready to connect to real VHAL instead of the file simulator:

```bash
# 1. Remove the sim flag from Android.bp
# In vendor/myoem/services/safemode/Android.bp, remove this line:
#   "-DSAFEMODE_SIM_MODE",

# 2. Rebuild
m safemoded

# 3. Push and restart
adb push out/target/product/rpi5/vendor/bin/safemoded /vendor/bin/safemoded
adb shell stop safemoded && adb shell start safemoded

# safemoded now connects to IVehicle and subscribes to real SPEED + GEAR events.
# No other code changes needed.
```

---

<a name="part-12"></a>
# Part 12: Problems Faced and How They Were Fixed

This section documents the non-obvious runtime problems encountered after the build
succeeded — issues that don't appear in compile output and require runtime investigation.

## Problem 1: Java Callback Binder Stability Error

**When it appeared:** First time SafeModeManager called `service.registerCallback(mBinderCallback)`.

**Symptom in logcat:**
```
E safemoded: onTransact error: Cannot do a user transaction on a system stability
  binder (com.myoem.safemode.ISafeModeCallback) in a vendor stability context
```

**What I tried first:** Added `@VintfStability` annotation to the AIDL interface,
assuming the Java AIDL compiler would then mark the stub with vendor stability. It didn't.

**Root cause:** The Java AIDL compiler never emits `AIBinder_markVintfStability()` for
Java Stub objects. This is a C++ NDK-only concept. Java stubs always have SYSTEM stability.

**Fix:** Completely removed the callback registration approach from SafeModeManager.
Replaced with a 200ms polling loop calling `getCurrentData()` in the system→vendor
direction, which is always allowed. Removed `ISafeModeCallback` from the Java library
entirely (it's still in the AIDL for potential C++ client use).

**Time lost:** Several hours chasing the stability annotation rabbit hole.

## Problem 2: `cmd car_service inject-vhal-event` Had No Effect on safemoded

**When it appeared:** Testing the simulator before the file-based approach existed.

**Symptom:** `inject-vhal-event` printed success, but safemoded showed no speed change in logs.

**Investigation:**
```bash
# Confirmed safemoded was subscribed to VHAL:
adb logcat -s safemoded -d | grep "subscribe"
# I safemoded: VHAL subscribe OK: SPEED (prop=0x11600207)

# Confirmed inject command showed success:
adb shell cmd car_service inject-vhal-event 0x11600207 0 5.0
# Injecting VHAL event: property=PERF_VEHICLE_SPEED...

# But safemoded received nothing.
```

**Root cause:** `inject-vhal-event` injects into `CarPropertyService`'s in-memory
cache — not into the actual VHAL HAL. The HAL-level `IVehicle::subscribe()` callback
is never triggered by this command.

**Fix:** Designed the `SAFEMODE_SIM_MODE` file-based simulator. safemoded reads the
file directly — no VHAL involvement. The Python script writes to the file via adb.

## Problem 3: Service Name Missing `/default` — Silent Connection Failure

**When it appeared:** After the Java callback issue was resolved and polling was added.

**Symptom:**
```
D SafeModeManager: ServiceManager.getService('com.myoem.safemode.ISafeModeService') → null
E SafeModeManager: Service not found — is safemoded running?
```

But `adb shell service list | grep safemode` showed the service WAS registered.

**Investigation:**
```bash
adb shell service list | grep safemode
# com.myoem.safemode.ISafeModeService/default: [android.os.IBinder]
```

The registered name included `/default` but SafeModeManager was looking up the name
WITHOUT `/default`.

**Root cause:** VINTF-stable services use `package.Interface/instance` format. The
`main.cpp` had been updated to register with `/default` but `SafeModeManager.kt` still
used the old name without the instance suffix.

**Fix:** Updated `SERVICE_NAME` in `SafeModeManager.kt`:
```kotlin
// WRONG:
private const val SERVICE_NAME = "com.myoem.safemode.ISafeModeService"

// CORRECT:
private const val SERVICE_NAME = "com.myoem.safemode.ISafeModeService/default"
```

Also updated `safemode_client.cpp` which had the same issue.

## Problem 4: App Hidden in AAOS Launcher / "Can't Be Used While Driving" Toast

**When it appeared:** After all Binder issues were resolved and the app was installable.

**Symptom:** The SafeModeDemo icon was invisible in the AAOS launcher. Opening it
manually via `am start` showed a platform toast: "Can't be used while driving."

**Investigation:**
```bash
adb logcat -d | grep -i "distraction\|uxr\|driving"
# CarPackageManagerService: blocking com.myoem.safemodedemo — not distraction optimized
```

**Root cause:** `CarPackageManagerService` reads driving state from `CarPropertyService`
(independently of safemoded). If the car is not in `PARKED` state, it blocks all apps
that are not marked as distraction-optimized. The RPi5 AAOS build defaults to a
non-PARKED state on some configurations.

This happened even when the simulator showed speed = 0 — because AAOS reads speed
from a completely separate VHAL path, not from safemoded.

**Fix — immediate dev bypass:**
```bash
adb shell cmd car_service enable-uxr false
```

**Fix — permanent (AndroidManifest.xml):**
```xml
<activity android:name=".MainActivity" ...>
    <!-- Required: marks this activity as safe to show while driving -->
    <meta-data
        android:name="distractionOptimized"
        android:value="true" />
</activity>
```

A Safe Mode monitoring app is by definition something the driver needs to see —
marking it as distraction-optimized is semantically correct, not a workaround.

## Problem 5: App Shadow — Old /data/app/ Version Masks /system/app/ Update

**When it appeared:** Every time the APK was rebuilt and pushed to `/system/app/`.

**Symptom:** Rebuilt APK pushed to `/system/app/SafeModeDemo/`. App relaunched. Old
behavior persisted — as if the push never happened.

**Root cause:** Android's package manager **prefers** APKs installed in `/data/app/`
over `/system/app/` for the same package name. Once an app has ever been installed
via `pm install`, it creates a shadow in `/data/app/` that overrides the system copy.

**Fix:**
```bash
# Remove the data-partition shadow (user 0 = primary user)
adb shell pm uninstall --user 0 com.myoem.safemodedemo

# Now install fresh from your latest build
adb shell pm install -r /data/local/tmp/SafeModeDemo.apk
```

After this, the data-partition copy IS the authoritative copy (which is fine for dev).
Future pushes via `pm install -r` update it correctly.

---

*Built on Raspberry Pi 5 — Android 15 (android-15.0.0_r14) — AAOS myoem_rpi5_car target*
*All code lives in `vendor/myoem/` — zero framework or device changes*
