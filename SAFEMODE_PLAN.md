# SafeMode Project — Detailed Implementation Plan
**Author:** ProArun
**Target:** Raspberry Pi 5 · AOSP android-15.0.0_r14 · vendor/myoem/
**Date:** 2026-03-20

---

## Table of Contents
1. [Architecture Overview](#1-architecture-overview)
2. [Design Decisions & Rationale](#2-design-decisions--rationale)
3. [Project Structure](#3-project-structure)
4. [Phase 0 — AIDL Contract](#phase-0--aidl-contract)
5. [Phase 1 — SafeModeService (C++ Native Service)](#phase-1--safemodeservice-c-native-service)
6. [Phase 2 — safemode_library (Android AAR)](#phase-2--safemode_library-android-aar)
7. [Phase 3 — VHAL Simulator Scripts (Python)](#phase-3--vhal-simulator-scripts-python)
8. [Phase 4 — Sample Test App](#phase-4--sample-test-app)
9. [SELinux Policy](#selinux-policy)
10. [Build Integration](#build-integration)
11. [Testing Strategy](#testing-strategy)
12. [Phase Checklist](#phase-checklist)

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Android Application                          │
│  (any app — just adds safemode_library as Gradle dependency)   │
│                                                                  │
│   SafeModeManager.attach(this, listener)  ←── SafeModeListener  │
│   SafeModeManager.dispose()                    .onSafeModeChanged(SafeModeState)
└────────────────────────┬────────────────────────────────────────┘
                         │  (Binder IPC via AIDL)
┌────────────────────────▼────────────────────────────────────────┐
│                  safemode_library (AAR)                         │
│   · Binds to SafeModeService via AIDL                           │
│   · Translates raw data → SafeModeState enum                    │
│   · Manages Activity lifecycle (attach / dispose)               │
│   · Handles threading (callbacks on main thread)                │
│   · Publishable as Maven artifact                               │
└────────────────────────┬────────────────────────────────────────┘
                         │  (Binder IPC — ISafeModeService AIDL)
┌────────────────────────▼────────────────────────────────────────┐
│               SafeModeService (C++ Native Service)              │
│   · Reads VHAL: PERF_VEHICLE_SPEED, CURRENT_GEAR, FUEL_LEVEL   │
│   · Subscribes to VHAL property change events                   │
│   · Pushes data to all registered ISafeModeCallback clients     │
│   · Runs as system service, boots with Android                  │
└────────────────────────┬────────────────────────────────────────┘
                         │  (IVhalClient)
┌────────────────────────▼────────────────────────────────────────┐
│                         VHAL                                    │
│   (on RPi5: default reference VHAL — injectable via adb)       │
│   Injected by Python simulator scripts during development       │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. Design Decisions & Rationale

### Q: Where should SafeModeListener and SafeModeState enum live — in the service or the library?

**Answer: In safemode_library. The service is a raw data pipe.**

| Concern | Service | Library ✅ |
|---------|---------|-----------|
| Business logic (speed thresholds) | ❌ hardcoded, requires service redeployment | ✅ lives in library — can change via AAR version bump |
| Multiple libraries on same service | ❌ conflict if each wants different thresholds | ✅ each library can define its own logic |
| App API surface | ❌ exposes raw AIDL types | ✅ exposes clean Kotlin/Java types |
| Separation of concerns | ❌ service does too much | ✅ service = data, library = logic |

**Rule:** The service exposes **raw facts** (speed in m/s, gear int, fuel float). The library owns **business logic** (SafeModeState derivation, threshold values, enum definition).

### SafeModeState Thresholds (defined in library, not service)
| State | Condition |
|-------|-----------|
| `NO_SAFE_MODE` | speed < 5 km/h |
| `NORMAL_SAFE_MODE` | 5 km/h ≤ speed ≤ 15 km/h |
| `HARD_SAFE_MODE` | speed > 15 km/h |

> Speed from VHAL is in **m/s** → convert: `km/h = m/s × 3.6`

### VHAL Properties Used
| Data | VHAL Property | ID | Type | Unit |
|------|-------------|-----|------|------|
| Vehicle speed | `PERF_VEHICLE_SPEED` | `0x11600207` | FLOAT | m/s |
| Gear position | `CURRENT_GEAR` | `0x11400400` | INT32 | VehicleGear enum |
| Fuel level | `FUEL_LEVEL` | `0x45100004` | FLOAT | milliliters |

### Library Independence Strategy
The `safemode_library` is a **separate Android Studio (Gradle) project** that:
- Can be built independently of the AOSP tree
- Is publishable as a Maven artifact (`com.myoem:safemode-library:1.0.0`)
- Contains the AIDL files internally (app developer never sees AIDL)
- App developer only needs: `implementation 'com.myoem:safemode-library:1.0.0'`

It is also buildable via Soong (`android_library`) for integration within the AOSP system image during development.

---

## 3. Project Structure

```
vendor/myoem/
│
├── SAFEMODE_PLAN.md                         ← this file
│
├── services/safemode/                       ← PHASE 1: C++ Native Service
│   ├── Android.bp
│   ├── aidl/
│   │   └── com/myoem/safemode/
│   │       ├── ISafeModeService.aidl        ← main service interface
│   │       ├── ISafeModeCallback.aidl       ← callback interface
│   │       └── VehicleData.aidl             ← Parcelable data bundle
│   ├── include/
│   │   └── SafeModeService.h
│   ├── src/
│   │   ├── SafeModeService.cpp              ← AIDL impl + VHAL subscription
│   │   └── main.cpp                         ← ServiceManager registration
│   ├── safemoded.rc                         ← init service descriptor
│   └── sepolicy/
│       └── private/
│           ├── safemoded.te                 ← type enforcement
│           ├── file_contexts                ← binary label
│           ├── service_contexts             ← AIDL service label
│           └── service.te                   ← service type declaration
│
├── libs/safemode/                           ← PHASE 2: Android AAR Library
│   ├── Android.bp                           ← Soong build (for AOSP)
│   ├── build.gradle.kts                     ← Gradle build (for standalone)
│   ├── settings.gradle.kts
│   ├── AndroidManifest.xml
│   └── src/main/
│       ├── aidl/
│       │   └── com/myoem/safemode/          ← copy of service AIDL files
│       │       ├── ISafeModeService.aidl
│       │       ├── ISafeModeCallback.aidl
│       │       └── VehicleData.aidl
│       └── java/com/myoem/safemode/
│           ├── SafeModeManager.kt           ← singleton, binds service, lifecycle
│           ├── SafeModeListener.kt          ← interface for app developer
│           ├── SafeModeState.kt             ← enum: NO / NORMAL / HARD
│           └── VehicleInfo.kt               ← data class for app-facing data
│
└── tools/safemode-simulator/               ← PHASE 3: RPi5 Test Tooling
    ├── README.md
    ├── data/
    │   ├── speed_data.txt                   ← 20 speed values (m/s)
    │   ├── gear_data.txt                    ← 20 gear values
    │   └── fuel_data.txt                    ← 20 fuel values (ml)
    ├── simulate_speed.py                    ← injects speed via adb
    ├── simulate_gear.py                     ← injects gear via adb
    └── simulate_fuel.py                     ← injects fuel via adb
```

---

## Phase 0 — AIDL Contract

> The AIDL interface is the **contract** between service and library. Define it first, before writing any implementation. Never change it without versioning.

### File: `VehicleData.aidl`
```aidl
package com.myoem.safemode;

parcelable VehicleData {
    float speedMs      = 0.0f;  // m/s — raw from VHAL PERF_VEHICLE_SPEED
    int   gear         = 0;     // raw from VHAL CURRENT_GEAR (VehicleGear enum values)
    float fuelLevelMl  = 0.0f;  // milliliters — raw from VHAL FUEL_LEVEL
}
```

### File: `ISafeModeCallback.aidl`
```aidl
package com.myoem.safemode;

import com.myoem.safemode.VehicleData;

/**
 * One-way callback pushed from SafeModeService to registered clients.
 * oneway = non-blocking fire-and-forget (service does not wait for client return).
 */
oneway interface ISafeModeCallback {
    void onVehicleDataChanged(in VehicleData data);
}
```

### File: `ISafeModeService.aidl`
```aidl
package com.myoem.safemode;

import com.myoem.safemode.ISafeModeCallback;
import com.myoem.safemode.VehicleData;

/**
 * SafeModeService — raw vehicle data provider.
 * Exposes current snapshot and push-based subscription.
 * Business logic (SafeModeState derivation) lives in safemode_library, NOT here.
 */
interface ISafeModeService {

    /** Returns the current vehicle data snapshot synchronously. */
    VehicleData getCurrentData();

    /**
     * Subscribe to receive real-time vehicle data updates.
     * Callback is invoked on any change to speed, gear, or fuel.
     * Client must call unregisterCallback() to avoid service-side memory leak.
     */
    void registerCallback(ISafeModeCallback callback);

    /** Unsubscribe a previously registered callback. */
    void unregisterCallback(ISafeModeCallback callback);

    /** Returns the service version for forward/backward compatibility checks. */
    int getVersion();
}
```

---

## Phase 1 — SafeModeService (C++ Native Service)

### 1.1 Android.bp
```bp
// --- AIDL Interface ---
aidl_interface {
    name: "ISafeModeInterface",
    vendor_available: true,
    srcs: [
        "aidl/com/myoem/safemode/ISafeModeService.aidl",
        "aidl/com/myoem/safemode/ISafeModeCallback.aidl",
        "aidl/com/myoem/safemode/VehicleData.aidl",
    ],
    frozen: false,
    unstable: true,
    local_include_dir: "aidl",
    backend: {
        cpp: { enabled: false },
        ndk: { enabled: true },   // vendor services must use NDK binder
        java: { enabled: true },  // for the Android library (safemode_library)
        rust: { enabled: false },
    },
}

// --- Native Service Binary ---
cc_binary {
    name: "safemoded",
    vendor: true,
    srcs: [
        "src/SafeModeService.cpp",
        "src/main.cpp",
    ],
    local_include_dirs: ["include"],
    shared_libs: [
        "libbinder_ndk",          // NDK binder — mandatory for vendor services
        "libvhalclient",          // IVhalClient for VHAL access
        "libutils",
        "libbase",
        "liblog",
    ],
    static_libs: [
        "ISafeModeInterface-V1-ndk", // generated from aidl_interface
    ],
    defaults: ["vhalclient_defaults"],
    stl: "libc++",
    init_rc: ["safemoded.rc"],
    cflags: ["-Wall", "-Werror"],
}
```

### 1.2 SafeModeService.h
```cpp
#pragma once

#include <aidl/com/myoem/safemode/BnSafeModeService.h>
#include <aidl/com/myoem/safemode/ISafeModeCallback.h>
#include <aidl/com/myoem/safemode/VehicleData.h>
#include <IVhalClient.h>

#include <mutex>
#include <vector>

namespace com::myoem::safemode {

class SafeModeService
    : public ::aidl::com::myoem::safemode::BnSafeModeService {
public:
    SafeModeService();
    ~SafeModeService();

    // ISafeModeService AIDL implementation
    ::ndk::ScopedAStatus getCurrentData(
        ::aidl::com::myoem::safemode::VehicleData* out) override;

    ::ndk::ScopedAStatus registerCallback(
        const std::shared_ptr<::aidl::com::myoem::safemode::ISafeModeCallback>& cb) override;

    ::ndk::ScopedAStatus unregisterCallback(
        const std::shared_ptr<::aidl::com::myoem::safemode::ISafeModeCallback>& cb) override;

    ::ndk::ScopedAStatus getVersion(int32_t* out) override;

    // Called during startup — subscribes to VHAL property changes
    bool startVhalSubscription();

private:
    void onVhalPropertyEvent(
        const ::aidl::android::hardware::automotive::vehicle::VehiclePropValue& value);

    void dispatchToCallbacks(
        const ::aidl::com::myoem::safemode::VehicleData& data);

    std::shared_ptr<::android::frameworks::automotive::vhal::IVhalClient> mVhalClient;

    std::mutex mMutex;
    ::aidl::com::myoem::safemode::VehicleData mCurrentData;
    std::vector<std::shared_ptr<::aidl::com::myoem::safemode::ISafeModeCallback>> mCallbacks;

    static constexpr int32_t SERVICE_VERSION = 1;
};

} // namespace com::myoem::safemode
```

### 1.3 SafeModeService.cpp — Key Logic

```cpp
#define LOG_TAG "SafeModeService"   // MUST be first line

#include "SafeModeService.h"
#include <android-base/logging.h>
#include <aidl/android/hardware/automotive/vehicle/VehicleProperty.h>

using namespace ::aidl::android::hardware::automotive::vehicle;

// VHAL property IDs
static constexpr int32_t PROP_SPEED  = (int32_t)VehicleProperty::PERF_VEHICLE_SPEED;
static constexpr int32_t PROP_GEAR   = (int32_t)VehicleProperty::CURRENT_GEAR;
static constexpr int32_t PROP_FUEL   = (int32_t)VehicleProperty::FUEL_LEVEL;

// ...

bool SafeModeService::startVhalSubscription() {
    mVhalClient = IVhalClient::tryCreate();
    if (!mVhalClient) {
        LOG(ERROR) << "Failed to connect to VHAL";
        return false;
    }

    auto callback = std::make_shared<IVhalClient::OnPropertyEventCallback>(
        [this](const auto& values) {
            for (const auto& v : values) onVhalPropertyEvent(v);
        });

    std::vector<SubscribeOptions> opts = {
        {.propId = PROP_SPEED,  .areaIds = {0}},
        {.propId = PROP_GEAR,   .areaIds = {0}},
        {.propId = PROP_FUEL,   .areaIds = {0}},
    };

    auto result = mVhalClient->subscribe(callback, opts, /*maxSharedMemory=*/0);
    if (!result.ok()) {
        LOG(ERROR) << "VHAL subscribe failed: " << result.error().message();
        return false;
    }
    return true;
}

void SafeModeService::onVhalPropertyEvent(const VehiclePropValue& value) {
    std::lock_guard<std::mutex> lock(mMutex);
    switch (value.prop) {
        case PROP_SPEED:
            mCurrentData.speedMs = value.value.floatValues[0];
            break;
        case PROP_GEAR:
            mCurrentData.gear = value.value.int32Values[0];
            break;
        case PROP_FUEL:
            mCurrentData.fuelLevelMl = value.value.floatValues[0];
            break;
        default: return;
    }
    dispatchToCallbacks(mCurrentData);
}

void SafeModeService::dispatchToCallbacks(const VehicleData& data) {
    // mMutex already held by caller — copy callbacks to avoid deadlock
    auto cbs = mCallbacks;
    // dispatch outside lock
    for (auto& cb : cbs) {
        auto status = cb->onVehicleDataChanged(data);
        if (!status.isOk()) {
            LOG(WARNING) << "Callback dispatch failed — client may have died";
        }
    }
}
```

### 1.4 main.cpp
```cpp
#define LOG_TAG "safemoded"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android-base/logging.h>
#include "SafeModeService.h"

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto service = ::ndk::SharedRefBase::make<com::myoem::safemode::SafeModeService>();

    if (!service->startVhalSubscription()) {
        LOG(ERROR) << "VHAL subscription failed — exiting";
        return 1;
    }

    const std::string instance =
        std::string(com::myoem::safemode::SafeModeService::descriptor) + "/default";

    binder_exception_t ex = AServiceManager_addService(
        service->asBinder().get(), instance.c_str());

    if (ex != EX_NONE) {
        LOG(ERROR) << "Failed to register SafeModeService: " << ex;
        return 1;
    }

    LOG(INFO) << "SafeModeService started — waiting for clients";
    ABinderProcess_joinThreadPool();
    return 0; // unreachable
}
```

### 1.5 safemoded.rc
```rc
service safemoded /vendor/bin/safemoded
    class main
    user system
    group system
    interface aidl com.myoem.safemode.ISafeModeService/default
    oneshot
    disabled

on boot
    start safemoded
```

---

## Phase 2 — safemode_library (Android AAR)

> This is an **independent Android Studio project** that can be built via Gradle and published as a Maven artifact. It is also buildable via Soong for AOSP integration.

### 2.1 Library API Design

The library exposes **exactly 4 things** to the app developer:
1. `SafeModeManager` — singleton with `attach()` and `dispose()`
2. `SafeModeListener` — interface to implement in the Activity
3. `SafeModeState` — enum with the 3 states
4. `VehicleInfo` — data class with speed (km/h), gear (human-readable), fuel (%)

### 2.2 SafeModeState.kt
```kotlin
package com.myoem.safemode

/**
 * Represents the current safe mode level based on vehicle speed.
 * Thresholds are intentionally defined in the library (not the service)
 * so they can evolve independently of the running system service.
 */
enum class SafeModeState {
    /** Vehicle speed < 5 km/h. All features available. */
    NO_SAFE_MODE,

    /** Vehicle speed 5–15 km/h. Moderate restrictions apply. */
    NORMAL_SAFE_MODE,

    /** Vehicle speed > 15 km/h. Strict restrictions apply. */
    HARD_SAFE_MODE;

    companion object {
        private const val THRESHOLD_NORMAL_KMH = 5.0f
        private const val THRESHOLD_HARD_KMH   = 15.0f

        /**
         * Derives SafeModeState from raw speed.
         * @param speedMs speed in m/s as received from VHAL
         */
        internal fun fromSpeedMs(speedMs: Float): SafeModeState {
            val kmh = speedMs * 3.6f
            return when {
                kmh < THRESHOLD_NORMAL_KMH  -> NO_SAFE_MODE
                kmh <= THRESHOLD_HARD_KMH   -> NORMAL_SAFE_MODE
                else                        -> HARD_SAFE_MODE
            }
        }
    }
}
```

### 2.3 VehicleInfo.kt
```kotlin
package com.myoem.safemode

/**
 * App-facing vehicle data.
 * All units are human-readable — conversion from raw VHAL units is done internally.
 */
data class VehicleInfo(
    /** Vehicle speed in km/h (converted from m/s). */
    val speedKmh: Float,

    /** Gear position as a human-readable string (e.g., "PARK", "DRIVE", "REVERSE"). */
    val gearLabel: String,

    /** Raw gear integer (matches android.car.VehicleGear constants). */
    val gearRaw: Int,

    /** Fuel level as percentage 0–100. */
    val fuelPercent: Float,

    /** Derived safe mode state based on current speed. */
    val safeModeState: SafeModeState
)
```

### 2.4 SafeModeListener.kt
```kotlin
package com.myoem.safemode

/**
 * Listener interface for receiving SafeMode and vehicle data updates.
 * Implement this in your Activity or ViewModel.
 * All callbacks are delivered on the main (UI) thread.
 */
interface SafeModeListener {

    /**
     * Called when the SafeMode state changes (speed crosses a threshold).
     * Also called immediately on attach() with the current state.
     */
    fun onSafeModeChanged(state: SafeModeState, vehicleInfo: VehicleInfo)

    /**
     * Called on every vehicle data update (speed, gear, or fuel change).
     * May be called frequently — keep this method lightweight.
     */
    fun onVehicleDataUpdated(vehicleInfo: VehicleInfo) {}  // default no-op

    /**
     * Called if the connection to SafeModeService is lost.
     * The manager will attempt reconnection automatically.
     */
    fun onServiceDisconnected() {}  // default no-op
}
```

### 2.5 SafeModeManager.kt
```kotlin
package com.myoem.safemode

import android.content.ComponentName
import android.content.Context
import android.content.ServiceConnection
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import com.myoem.safemode.ISafeModeCallback
import com.myoem.safemode.ISafeModeService
import com.myoem.safemode.VehicleData

/**
 * SafeModeManager — the single entry point of the safemode_library.
 *
 * Usage:
 *   // In Activity.onCreate()
 *   SafeModeManager.attach(this, myListener)
 *
 *   // In Activity.onDestroy()
 *   SafeModeManager.dispose(this)
 */
object SafeModeManager {

    private const val TAG = "SafeModeManager"
    private const val SERVICE_NAME = "com.myoem.safemode.ISafeModeService/default"

    // Last known fuel tank capacity (default 50L; override via configure())
    private var fuelTankCapacityMl: Float = 50_000f

    private val mainHandler = Handler(Looper.getMainLooper())

    @Volatile private var service: ISafeModeService? = null
    @Volatile private var lastState: SafeModeState = SafeModeState.NO_SAFE_MODE
    @Volatile private var lastData: VehicleData? = null

    // Per-context listener registrations
    private val listeners = mutableMapOf<Context, SafeModeListener>()
    private val connections = mutableMapOf<Context, ServiceConnection>()

    /**
     * Attach a listener for the given context (Activity/Fragment).
     * Binds to SafeModeService if not already connected.
     * Immediately delivers the current state to the listener.
     *
     * @param context  the Activity/Fragment context (used for lifecycle scoping)
     * @param listener the SafeModeListener implementation
     */
    fun attach(context: Context, listener: SafeModeListener) {
        listeners[context] = listener

        if (service == null) {
            val conn = buildServiceConnection()
            connections[context] = conn
            // Use ServiceManager for system service binding
            val binder = android.os.ServiceManager.getService(SERVICE_NAME)
            if (binder != null) {
                onServiceBound(ISafeModeService.Stub.asInterface(binder))
            } else {
                Log.e(TAG, "SafeModeService not found — is it running?")
                listener.onServiceDisconnected()
            }
        } else {
            // Already connected — deliver current state immediately
            lastData?.let { deliverUpdate(listener, it) }
        }
    }

    /**
     * Dispose the listener for the given context.
     * Unregisters the AIDL callback if no more listeners remain.
     * Call this in Activity.onDestroy() to prevent leaks.
     */
    fun dispose(context: Context) {
        listeners.remove(context)
        connections.remove(context)
        if (listeners.isEmpty()) {
            try { service?.unregisterCallback(aidlCallback) } catch (_: Exception) {}
            service = null
        }
    }

    /**
     * Optional: configure the fuel tank capacity for percentage calculation.
     * Default: 50,000 ml (50 litres).
     */
    fun configure(fuelTankCapacityMl: Float) {
        this.fuelTankCapacityMl = fuelTankCapacityMl
    }

    // ─── Private ────────────────────────────────────────────────────────

    private fun onServiceBound(svc: ISafeModeService) {
        service = svc
        svc.registerCallback(aidlCallback)
        // Deliver current snapshot immediately to all current listeners
        val snapshot = svc.getCurrentData()
        lastData = snapshot
        snapshot?.let { data ->
            listeners.values.forEach { deliverUpdate(it, data) }
        }
    }

    private fun buildServiceConnection() = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            onServiceBound(ISafeModeService.Stub.asInterface(binder))
        }
        override fun onServiceDisconnected(name: ComponentName) {
            service = null
            mainHandler.post {
                listeners.values.forEach { it.onServiceDisconnected() }
            }
        }
    }

    private val aidlCallback = object : ISafeModeCallback.Stub() {
        override fun onVehicleDataChanged(data: VehicleData) {
            lastData = data
            mainHandler.post {
                listeners.values.forEach { deliverUpdate(it, data) }
            }
        }
    }

    private fun deliverUpdate(listener: SafeModeListener, data: VehicleData) {
        val info = buildVehicleInfo(data)
        val newState = info.safeModeState
        listener.onVehicleDataUpdated(info)
        if (newState != lastState) {
            lastState = newState
            listener.onSafeModeChanged(newState, info)
        }
    }

    private fun buildVehicleInfo(data: VehicleData): VehicleInfo {
        val kmh = data.speedMs * 3.6f
        val fuelPct = (data.fuelLevelMl / fuelTankCapacityMl * 100f).coerceIn(0f, 100f)
        return VehicleInfo(
            speedKmh      = kmh,
            gearLabel     = gearLabel(data.gear),
            gearRaw       = data.gear,
            fuelPercent   = fuelPct,
            safeModeState = SafeModeState.fromSpeedMs(data.speedMs)
        )
    }

    private fun gearLabel(gear: Int): String = when (gear) {
        0x0000 -> "UNKNOWN"
        0x0001 -> "NEUTRAL"
        0x0002 -> "REVERSE"
        0x0004 -> "PARK"
        0x0008 -> "DRIVE"
        0x0010 -> "GEAR_1"
        0x0020 -> "GEAR_2"
        0x0040 -> "GEAR_3"
        0x0080 -> "GEAR_4"
        0x0100 -> "GEAR_5"
        0x0200 -> "GEAR_6"
        0x0400 -> "GEAR_7"
        0x0800 -> "GEAR_8"
        0x1000 -> "GEAR_9"
        else   -> "GEAR_$gear"
    }
}
```

### 2.6 Gradle build.gradle.kts (standalone library project)
```kotlin
plugins {
    id("com.android.library")
    id("org.jetbrains.kotlin.android")
    id("maven-publish")       // for publishing to Maven local/remote
}

android {
    namespace  = "com.myoem.safemode"
    compileSdk = 35
    minSdk     = 29           // Android 10+ (AAOS baseline)

    defaultConfig {
        aidlPackagedList += listOf("com.myoem.safemode")
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }
    sourceSets["main"].aidl.srcDir("src/main/aidl")
}

dependencies {
    // no external dependencies — lean library
}

// Publish to Maven local for app integration
publishing {
    publications {
        create<MavenPublication>("release") {
            groupId    = "com.myoem"
            artifactId = "safemode-library"
            version    = "1.0.0"
            afterEvaluate { from(components["release"]) }
        }
    }
}
```

### 2.7 Soong Android.bp (for AOSP build)
```bp
android_library {
    name: "safemode-library",
    sdk_version: "system_current",
    srcs: [
        "src/main/java/**/*.kt",
        "src/main/aidl/**/*.aidl",
    ],
    libs: [
        "ISafeModeInterface-V1-java",  // generated by aidl_interface in service
    ],
    static_libs: [],
    manifest: "AndroidManifest.xml",
    min_sdk_version: "29",
}
```

---

## Phase 3 — VHAL Simulator Scripts (Python)

> Since RPi5 has no real vehicle hardware, these scripts inject fake VHAL properties via `adb shell cmd car_service inject-vhal-event`. Each script reads from a `.txt` file and injects values with a configurable interval.

### VHAL Property IDs for injection
```
PERF_VEHICLE_SPEED = 0x11600207
CURRENT_GEAR       = 0x11400400
FUEL_LEVEL         = 0x45100004
```

### File: `data/speed_data.txt`
```
# Vehicle speed values in m/s
# SafeModeState: NO_SAFE_MODE < 1.389 m/s, NORMAL_SAFE_MODE 1.389–4.167, HARD_SAFE_MODE > 4.167
0.0
0.5
1.0
1.4
1.5
2.0
2.5
3.0
3.5
4.0
4.2
4.5
5.0
6.0
7.5
9.0
10.0
11.0
12.5
14.0
```

### File: `data/gear_data.txt`
```
# Gear values (VehicleGear enum hex values as integers)
# 4=PARK, 1=NEUTRAL, 2=REVERSE, 8=DRIVE
4
4
1
2
8
8
8
8
8
8
8
8
8
8
8
8
1
4
4
4
```

### File: `data/fuel_data.txt`
```
# Fuel level in milliliters (full tank = 50000 ml = 50 litres)
50000
48500
47000
45000
43000
41500
39000
37000
35000
32000
30000
27500
25000
22000
19000
16000
13000
10000
7000
4000
```

### File: `simulate_speed.py`
```python
#!/usr/bin/env python3
"""
simulate_speed.py — Injects fake vehicle speed into VHAL via adb.
Usage: python3 simulate_speed.py [--interval 2] [--loop]
"""

import subprocess
import time
import argparse
import os

PROP_ID  = "0x11600207"   # PERF_VEHICLE_SPEED
AREA_ID  = "0"
DATA_FILE = os.path.join(os.path.dirname(__file__), "data", "speed_data.txt")

def read_values(path: str) -> list[str]:
    values = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                values.append(line)
    return values

def inject(value: str) -> None:
    cmd = [
        "adb", "shell", "cmd", "car_service",
        "inject-vhal-event", PROP_ID, AREA_ID, value
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    kmh = float(value) * 3.6
    print(f"  Speed: {value} m/s ({kmh:.1f} km/h) → {result.returncode == 0 and 'OK' or result.stderr.strip()}")

def main():
    parser = argparse.ArgumentParser(description="Simulate vehicle speed on VHAL")
    parser.add_argument("--interval", type=float, default=2.0,
                        help="Seconds between each injection (default: 2)")
    parser.add_argument("--loop", action="store_true",
                        help="Loop through values continuously")
    args = parser.parse_args()

    values = read_values(DATA_FILE)
    print(f"Loaded {len(values)} speed values from {DATA_FILE}")
    print(f"Injection interval: {args.interval}s | Loop: {args.loop}\n")

    try:
        while True:
            for v in values:
                inject(v)
                time.sleep(args.interval)
            if not args.loop:
                break
    except KeyboardInterrupt:
        print("\nSimulation stopped.")
        # Reset to 0 on exit
        inject("0.0")

if __name__ == "__main__":
    main()
```

### File: `simulate_gear.py`
```python
#!/usr/bin/env python3
"""
simulate_gear.py — Injects fake gear position into VHAL via adb.
Usage: python3 simulate_gear.py [--interval 3] [--loop]
"""

import subprocess
import time
import argparse
import os

PROP_ID   = "0x11400400"  # CURRENT_GEAR
AREA_ID   = "0"
DATA_FILE = os.path.join(os.path.dirname(__file__), "data", "gear_data.txt")

GEAR_LABELS = {
    "0": "UNKNOWN", "1": "NEUTRAL", "2": "REVERSE",
    "4": "PARK",    "8": "DRIVE",
    "16": "GEAR_1", "32": "GEAR_2", "64": "GEAR_3",
}

def read_values(path: str) -> list[str]:
    values = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                values.append(line)
    return values

def inject(value: str) -> None:
    cmd = [
        "adb", "shell", "cmd", "car_service",
        "inject-vhal-event", PROP_ID, AREA_ID, value
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    label = GEAR_LABELS.get(value, f"GEAR_{value}")
    print(f"  Gear: {label} (raw={value}) → {result.returncode == 0 and 'OK' or result.stderr.strip()}")

def main():
    parser = argparse.ArgumentParser(description="Simulate gear position on VHAL")
    parser.add_argument("--interval", type=float, default=3.0)
    parser.add_argument("--loop", action="store_true")
    args = parser.parse_args()

    values = read_values(DATA_FILE)
    print(f"Loaded {len(values)} gear values. Interval: {args.interval}s\n")

    try:
        while True:
            for v in values:
                inject(v)
                time.sleep(args.interval)
            if not args.loop:
                break
    except KeyboardInterrupt:
        print("\nSimulation stopped.")
        inject("4")  # Reset to PARK

if __name__ == "__main__":
    main()
```

### File: `simulate_fuel.py`
```python
#!/usr/bin/env python3
"""
simulate_fuel.py — Injects fake fuel level into VHAL via adb.
Usage: python3 simulate_fuel.py [--interval 5] [--loop]
"""

import subprocess
import time
import argparse
import os

PROP_ID       = "0x45100004"  # FUEL_LEVEL
AREA_ID       = "0"
DATA_FILE     = os.path.join(os.path.dirname(__file__), "data", "fuel_data.txt")
TANK_CAPACITY = 50000.0       # ml

def read_values(path: str) -> list[str]:
    values = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line and not line.startswith("#"):
                values.append(line)
    return values

def inject(value: str) -> None:
    cmd = [
        "adb", "shell", "cmd", "car_service",
        "inject-vhal-event", PROP_ID, AREA_ID, value
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    pct = float(value) / TANK_CAPACITY * 100.0
    print(f"  Fuel: {value} ml ({pct:.1f}%) → {result.returncode == 0 and 'OK' or result.stderr.strip()}")

def main():
    parser = argparse.ArgumentParser(description="Simulate fuel level on VHAL")
    parser.add_argument("--interval", type=float, default=5.0)
    parser.add_argument("--loop", action="store_true")
    args = parser.parse_args()

    values = read_values(DATA_FILE)
    print(f"Loaded {len(values)} fuel values. Interval: {args.interval}s\n")

    try:
        while True:
            for v in values:
                inject(v)
                time.sleep(args.interval)
            if not args.loop:
                break
    except KeyboardInterrupt:
        print("\nSimulation stopped.")

if __name__ == "__main__":
    main()
```

---

## Phase 4 — Sample Test App

> A minimal Android app in `packages/apps/SafeModeDemo/` (or standalone Gradle project) that demonstrates the library API. Built via Soong inside AOSP.

### Sample Activity usage
```kotlin
class MainActivity : AppCompatActivity(), SafeModeListener {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        // attach — single call, manages everything
        SafeModeManager.attach(this, this)
    }

    override fun onDestroy() {
        super.onDestroy()
        SafeModeManager.dispose(this)
    }

    // SafeModeListener callbacks (delivered on main thread)

    override fun onSafeModeChanged(state: SafeModeState, vehicleInfo: VehicleInfo) {
        when (state) {
            SafeModeState.NO_SAFE_MODE     -> showUI("All features available")
            SafeModeState.NORMAL_SAFE_MODE -> showUI("Moderate restrictions")
            SafeModeState.HARD_SAFE_MODE   -> showUI("Strict restrictions — speed ${vehicleInfo.speedKmh} km/h")
        }
    }

    override fun onVehicleDataUpdated(vehicleInfo: VehicleInfo) {
        tvSpeed.text  = "${vehicleInfo.speedKmh} km/h"
        tvGear.text   = vehicleInfo.gearLabel
        tvFuel.text   = "${vehicleInfo.fuelPercent}%"
    }
}
```

---

## SELinux Policy

### `sepolicy/private/safemoded.te`
```
type safemoded, domain;
type safemoded_exec, exec_type, vendor_file_type, file_type;

init_daemon_domain(safemoded)

# Allow VHAL access
vndbinder_use(safemoded)
binder_use(safemoded)
binder_call(safemoded, vhal_default_hw_agent)

# Allow ServiceManager registration
add_service(safemoded, safemode_service)

# Allow logging
allow safemoded log_device:chr_file rw_file_perms;
```

### `sepolicy/private/service_contexts`
```
com.myoem.safemode.ISafeModeService/default    u:object_r:safemode_service:s0
```

### `sepolicy/private/service.te`
```
type safemode_service, service_manager_type;
```

### `sepolicy/private/file_contexts`
```
/vendor/bin/safemoded    u:object_r:safemoded_exec:s0
```

---

## Build Integration

### Additions to `vendor/myoem/myoem_base.mk`
```makefile
# SafeMode Service
PRODUCT_SOONG_NAMESPACES    += vendor/myoem/services/safemode
PRODUCT_PACKAGES            += safemoded
PRODUCT_PRIVATE_SEPOLICY_DIRS += vendor/myoem/services/safemode/sepolicy/private

# SafeMode Library (for AOSP build — also distributable as AAR)
PRODUCT_SOONG_NAMESPACES    += vendor/myoem/libs/safemode
PRODUCT_PACKAGES            += safemode-library
```

### Build commands
```bash
# Build service
m safemoded

# Build library
m safemode-library

# Push service binary for dev iteration (no full rebuild)
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/safemoded /vendor/bin/
adb shell stop safemoded && adb shell start safemoded

# Publish library as Maven artifact (from libs/safemode/ directory)
./gradlew publishToMavenLocal
```

---

## Testing Strategy

### Unit Tests (library)
- `SafeModeStateTest` — verify threshold logic (4.9 km/h → NO, 5.0 → NORMAL, 15.1 → HARD)
- `SafeModeManagerTest` — mock AIDL service, verify callback delivery on main thread
- `VehicleInfoTest` — verify unit conversions (m/s → km/h, ml → %)

### Integration Tests (on device)
```bash
# 1. Verify service is running
adb shell service check com.myoem.safemode.ISafeModeService/default

# 2. Inject speed and observe callbacks
python3 vendor/myoem/tools/safemode-simulator/simulate_speed.py --interval 2

# 3. Verify logcat
adb logcat -s SafeModeService SafeModeManager

# 4. Verify service dumps properly
adb shell dumpsys -l | grep safemode
```

### Simulator Script Usage
```bash
# Run all 3 simulators simultaneously (3 separate terminals)
python3 simulate_speed.py --interval 2 --loop
python3 simulate_gear.py  --interval 3 --loop
python3 simulate_fuel.py  --interval 5 --loop
```

---

## Phase Checklist

- [x] **Phase 0** — AIDL Contract
  - [x] `VehicleData.aidl` — `@VintfStability` parcelable
  - [x] `ISafeModeCallback.aidl` — `@VintfStability oneway`
  - [x] `ISafeModeService.aidl` — `@VintfStability`, 4 methods
  - [x] Frozen API snapshot at `aidl_api/safemodeservice-aidl/1/`

- [x] **Phase 1** — SafeModeService (C++ Native Service)
  - [x] `Android.bp` — `stability:"vintf"`, `versions_with_info`, NDK + Java backends
  - [x] `SafeModeService.h/cpp` — VHAL subscription + `#ifdef SAFEMODE_SIM_MODE`
  - [x] `main.cpp` — registers as `com.myoem.safemode.ISafeModeService/default`
  - [x] `safemoded.rc`
  - [x] SELinux policy (4 files) — `safemode_service` type, permissive on RPi5
  - [x] VINTF manifest — `vintf/manifest.xml`
  - [x] Build: `m safemoded` ✓
  - [x] Verify: `adb shell service list | grep safemode` → registered ✓
  - [x] `safemode_client snapshot` returns version=1, gear=PARK ✓
  - [x] `safemode_client subscribe` receives callbacks ✓

- [x] **Phase 2** — safemode_library
  - [x] `SafeModeState.kt` — thresholds 5/15 km/h
  - [x] `VehicleInfo.kt` — computed speedKmh, gearName, fuelLevelL
  - [x] `SafeModeListener.kt` — `fun interface` with `onSafeModeChanged`
  - [x] `SafeModeManager.kt` — polling 200ms (see note below)
  - [x] AIDL files copied from service
  - [x] `Android.bp` (Soong)
  - [ ] `build.gradle.kts` (Gradle standalone project — future)
  - [ ] Unit tests

- [x] **Phase 3** — VHAL Simulator Scripts
  - [x] `data/speed_data.txt` (20 values — full drive cycle)
  - [ ] `data/gear_data.txt` (20 values)
  - [ ] `data/fuel_data.txt` (20 values)
  - [ ] `simulate_speed.py`
  - [ ] `simulate_gear.py`
  - [ ] `simulate_fuel.py`
  - [ ] Verify injection: `python3 simulate_speed.py`

- [x] **Phase 3** (continued)
  - [x] `data/gear_data.txt` (20 values — realistic drive cycle)
  - [x] `data/fuel_data.txt` (20 values — 100% → 62%)
  - [x] `simulate_all.py` — combined script, 1 value/5s, writes to `/data/local/tmp/safemode_sim.txt`
  - [x] Verified: safemoded sim thread reads file every 500ms, dispatches to callbacks ✓
  - [x] Verified: SafeModeManager poll picks up new data within 200ms ✓
  - [x] Verified: full end-to-end pipeline: Python → safemoded → SafeModeManager → App ✓

- [x] **Phase 4** — Sample Test App
  - [x] `SafeModeDemo` app built and installed
  - [x] Uses `SafeModeManager.getInstance(this).attach(this, listener)`
  - [x] End-to-end verified with simulator ✓

---

## Lessons Learned (additions to Key Rules)

### Binder Stability: Why Polling (System→Vendor) Not Callbacks (Vendor→System)

The Java AIDL compiler does **NOT** mark `ISafeModeCallback.Stub()` as VINTF stable, even for
`@VintfStability` interfaces. The resulting binder has SYSTEM stability. The vendor safemoded
process cannot call into a SYSTEM stability binder:
```
E BpBinder: Cannot do a user transaction on a system stability binder
            (com.myoem.safemode.ISafeModeCallback) in a vendor stability context.
```

**Rule:** Java system apps MUST use polling (system→vendor direction). Vendor→system callbacks
only work from C++ clients that can call `AIBinder_markVintfStability()` on their callback.

### SAFEMODE_SIM_MODE: Dev vs Production Data Source

The `-DSAFEMODE_SIM_MODE` compile flag in `Android.bp` switches safemoded's data source:
- **Dev (RPi5):** reads `/data/local/tmp/safemode_sim.txt` written by Python scripts
- **Production:** remove the flag → safemoded connects to VHAL subscription

**To go to production:** remove `-DSAFEMODE_SIM_MODE` from `Android.bp` cflags, rebuild, push.
No other code changes needed. VHAL subscription code (`connectToVhal()`) is compiled in as-is.

### `cmd car_service inject-vhal-event` Does NOT Reach safemoded

This command injects into CarPropertyService's internal buffer only. Direct `IVehicle` subscribers
(like safemoded) never see these events. Use the Python simulator scripts instead.

### SafeModeManager Poll: 200ms — Fast Enough for 5s Sim Updates

With the Python simulator updating every 5 seconds and SafeModeManager polling every 200ms,
the app sees new data within 200ms of the Python script writing to the file. This is fast enough
for the RPi5 dev use case.

---

## Key Rules (from ThermalControl lessons applied here)

1. `#define LOG_TAG` must be first line in every `.cpp`
2. Vendor service uses `libbinder_ndk` + NDK AIDL backend — never `libbinder`
3. AIDL `oneway` on callback interface — prevents client-side blocking of service thread
4. **Java app polls (system→vendor); does NOT use callbacks (vendor→system)** — Java binders lack VINTF stability marking
5. `SafeModeManager.dispose()` is mandatory in `onStop()` — stops polling loop
6. SELinux: verify label with `adb shell ls -laZ` before writing policy — never assume
7. Dev iteration: push binary + `adb shell stop/start safemoded`
8. The service is a **data pipe** — no business logic, no SafeModeState, no thresholds
9. **SAFEMODE_SIM_MODE**: remove from Android.bp cflags to switch from file sim to real VHAL

---

## Phase-by-Phase Verification Test Plan

> Run these commands **in order** after completing each phase.
> Each command is a gate — do not proceed to the next phase if a gate fails.
> All commands assume `adb root` has been run first.

---

### ✅ Phase 0 Verification — AIDL Contract

**Goal:** Confirm AIDL builds without errors and generates correct stubs.

```bash
# Step 1: Build the AIDL interface only
m ISafeModeInterface

# Step 2: Confirm generated C++ NDK stubs exist
ls out/soong/.intermediates/vendor/myoem/services/safemode/ISafeModeInterface-V1-ndk/
# Expected: android_vendor.35_arm64_armv8-a_shared/ directory with generated headers

# Step 3: Confirm generated Java stubs exist
ls out/soong/.intermediates/vendor/myoem/services/safemode/ISafeModeInterface-V1-java/
# Expected: combined/ directory with ISafeModeService.java, ISafeModeCallback.java, VehicleData.java

# Step 4: Inspect generated AIDL Java stub to verify method signatures
grep -n "getCurrentData\|registerCallback\|unregisterCallback\|getVersion" \
  out/soong/.intermediates/vendor/myoem/services/safemode/ISafeModeInterface-V1-java/combined/*.java
# Expected: all 4 methods visible
```

**Gate:** Build succeeds, all 4 generated Java methods present. ✅

---

### ✅ Phase 1 Verification — SafeModeService (C++ Native Service)

**Goal:** Service builds, installs, starts on boot, registers with ServiceManager, connects to VHAL.

#### Step 1.1 — Build
```bash
m safemoded
# Expected: BUILD SUCCESSFUL, no errors
# Binary location:
ls -la out/target/product/rpi5/vendor/bin/safemoded
```

#### Step 1.2 — Push to device (dev iteration, no full reflash)
```bash
adb root
adb shell mount -o remount,rw /vendor

# Push binary
adb push out/target/product/rpi5/vendor/bin/safemoded /vendor/bin/safemoded
adb shell chmod 755 /vendor/bin/safemoded

# Push RC file (push SOURCE, not build output)
adb push vendor/myoem/services/safemode/safemoded.rc /vendor/etc/init/safemoded.rc

# Reboot to let init pick up the RC file
adb reboot
adb wait-for-device
adb root
```

#### Step 1.3 — Verify service process is running
```bash
adb shell ps -eZ | grep safemoded
# Expected output (example):
# u:r:safemoded:s0   system   1234  1  safemoded
#
# ✅ PASS: process is running under correct SELinux domain u:r:safemoded:s0
# ❌ FAIL: if process missing → check adb logcat -b kernel | grep safemoded
```

#### Step 1.4 — Verify service registered with ServiceManager
```bash
adb shell service list | grep safemode
# Expected:
# com.myoem.safemode.ISafeModeService/default: [com.myoem.safemode.ISafeModeService]
#
# ❌ FAIL: if missing → service crashed during startup, check logcat
```

#### Step 1.5 — Verify no SELinux denials
```bash
adb logcat -d | grep "avc: denied" | grep safemoded
# Expected: no output (empty)
#
# ❌ FAIL: if denials present → fix safemoded.te and re-push policy
```

#### Step 1.6 — Verify VHAL connection via logcat
```bash
adb logcat -s SafeModeService | head -30
# Expected lines:
#   SafeModeService: SafeModeService started — waiting for clients
#   SafeModeService: VHAL subscription registered for SPEED, GEAR, FUEL
#
# ❌ FAIL: "Failed to connect to VHAL" → VHAL not running, check:
adb shell service list | grep vehicle
```

#### Step 1.7 — Inject a VHAL value and verify service receives it
```bash
# Terminal 1: watch logcat
adb logcat -s SafeModeService

# Terminal 2: inject speed value of 10 m/s
adb shell cmd car_service inject-vhal-event 0x11600207 0 10.0
# Expected in logcat:
#   SafeModeService: VHAL event received — prop=PERF_VEHICLE_SPEED value=10.0
#   SafeModeService: Dispatching to 0 callbacks
# (0 callbacks is OK at this stage — no clients connected yet)
```

#### Step 1.8 — Verify service dump
```bash
adb shell dumpsys -l | grep -i safemode
# Expected: com.myoem.safemode.ISafeModeService/default listed

adb shell service check com.myoem.safemode.ISafeModeService
# Expected: Service com.myoem.safemode.ISafeModeService: found
```

**Gate:** Process running, correct SELinux domain, registered in ServiceManager, no AVC denials, VHAL events received in logcat. ✅

---

### ✅ Phase 2 Verification — safemode_library

**Goal:** Library builds as AAR, SafeModeState logic is correct, library binds to service.

#### Step 2.1 — Unit test SafeModeState thresholds (no device needed)
```bash
# From libs/safemode/ directory (standalone Gradle project)
cd vendor/myoem/libs/safemode

./gradlew test
# Expected: BUILD SUCCESSFUL, all tests pass
#
# Tests verify:
#   0.0 m/s  (0.0 km/h)   → NO_SAFE_MODE
#   1.38 m/s (4.99 km/h)  → NO_SAFE_MODE       ← boundary -0.01
#   1.39 m/s (5.0 km/h)   → NORMAL_SAFE_MODE   ← boundary exact
#   4.16 m/s (14.99 km/h) → NORMAL_SAFE_MODE   ← boundary -0.01
#   4.17 m/s (15.0 km/h)  → HARD_SAFE_MODE     ← boundary exact
#   14.0 m/s (50.4 km/h)  → HARD_SAFE_MODE
```

#### Step 2.2 — Build AAR
```bash
./gradlew assembleRelease
# Expected: BUILD SUCCESSFUL
# AAR location:
ls libs/safemode/build/outputs/aar/safemode-library-release.aar
```

#### Step 2.3 — Publish to Maven local
```bash
./gradlew publishToMavenLocal
# Verify published:
ls ~/.m2/repository/com/myoem/safemode-library/1.0.0/
# Expected: safemode-library-1.0.0.aar, safemode-library-1.0.0.pom
```

#### Step 2.4 — Build via Soong (AOSP integration check)
```bash
# From AOSP root
m safemode-library
# Expected: BUILD SUCCESSFUL
```

#### Step 2.5 — Verify library binds to service on device
```bash
# Install test app that uses safemode_library (Phase 4 demo app)
adb install out/target/product/rpi5/system/app/SafeModeDemo/SafeModeDemo.apk

# Watch logcat for library binding
adb logcat -s SafeModeManager
# Expected on app launch:
#   SafeModeManager: Binding to com.myoem.safemode.ISafeModeService/default
#   SafeModeManager: Service connected
#   SafeModeManager: Delivering initial snapshot — speed=0.0 gear=PARK fuel=100%
#   SafeModeManager: SafeModeState → NO_SAFE_MODE
```

#### Step 2.6 — Verify callback delivery with live injection
```bash
# Terminal 1: watch SafeModeManager logs
adb logcat -s SafeModeManager

# Terminal 2: inject speed crossing NORMAL threshold (1.4 m/s = 5.04 km/h)
adb shell cmd car_service inject-vhal-event 0x11600207 0 1.4
# Expected in logcat:
#   SafeModeManager: SafeModeState changed: NO_SAFE_MODE → NORMAL_SAFE_MODE

# Terminal 2: inject speed crossing HARD threshold (4.2 m/s = 15.12 km/h)
adb shell cmd car_service inject-vhal-event 0x11600207 0 4.2
# Expected in logcat:
#   SafeModeManager: SafeModeState changed: NORMAL_SAFE_MODE → HARD_SAFE_MODE

# Terminal 2: inject speed back below normal
adb shell cmd car_service inject-vhal-event 0x11600207 0 0.5
# Expected in logcat:
#   SafeModeManager: SafeModeState changed: HARD_SAFE_MODE → NO_SAFE_MODE
```

#### Step 2.7 — Verify no callback leak after dispose()
```bash
# Kill the app
adb shell am force-stop com.myoem.safedemo

# Watch service logcat — should log that callback was unregistered
adb logcat -s SafeModeService | grep -i "unregister\|callback"
# Expected:
#   SafeModeService: Client callback unregistered. Active callbacks: 0
```

**Gate:** All unit tests pass, AAR built, Maven artifact present, service binding confirmed in logcat, all 3 state transitions logged, no callback leak. ✅

---

### ✅ Phase 3 Verification — VHAL Simulator Scripts

**Goal:** All 3 scripts inject values cleanly, values are received by service, full cycle works.

#### Step 3.1 — Verify adb connectivity before running scripts
```bash
adb devices
# Expected: rpi5 device listed as "device" (not "offline")

adb shell cmd car_service -h | head -5
# Expected: car_service usage help printed (confirms cmd car_service available)
```

#### Step 3.2 — Test speed simulator (single pass, no loop)
```bash
cd vendor/myoem/tools/safemode-simulator

python3 simulate_speed.py --interval 1
# Expected output (20 lines, one per value):
#   Speed: 0.0 m/s (0.0 km/h) → OK
#   Speed: 0.5 m/s (1.8 km/h) → OK
#   ...
#   Speed: 14.0 m/s (50.4 km/h) → OK
# No "FAIL" or non-zero exit codes
```

#### Step 3.3 — Verify speed values received by service during script run
```bash
# Terminal 1: watch service
adb logcat -s SafeModeService | grep SPEED

# Terminal 2: run script
python3 simulate_speed.py --interval 2
# Expected in Terminal 1: 20 log lines showing speed events received
```

#### Step 3.4 — Test gear simulator
```bash
python3 simulate_gear.py --interval 1
# Expected output:
#   Gear: PARK (raw=4) → OK
#   Gear: NEUTRAL (raw=1) → OK
#   Gear: REVERSE (raw=2) → OK
#   Gear: DRIVE (raw=8) → OK
#   ...
```

#### Step 3.5 — Test fuel simulator
```bash
python3 simulate_fuel.py --interval 1
# Expected output:
#   Fuel: 50000 ml (100.0%) → OK
#   Fuel: 48500 ml (97.0%) → OK
#   ...
#   Fuel: 4000 ml (8.0%) → OK
```

#### Step 3.6 — Full end-to-end: all 3 simulators + app visible on screen
```bash
# Terminal 1: speed (most important — drives SafeModeState changes)
python3 simulate_speed.py --interval 3 --loop

# Terminal 2: gear
python3 simulate_gear.py --interval 4 --loop

# Terminal 3: fuel
python3 simulate_fuel.py --interval 6 --loop

# Terminal 4: watch full pipeline
adb logcat -s SafeModeService SafeModeManager
# Expected: a continuous stream of:
#   SafeModeService: VHAL event — SPEED / GEAR / FUEL
#   SafeModeManager: onVehicleDataUpdated → speed=X gear=Y fuel=Z%
#   SafeModeManager: SafeModeState changed: ... (when threshold crossed)
```

#### Step 3.7 — Verify boundary values specifically
```bash
# Inject exactly at NORMAL boundary (5.0 km/h = 1.3889 m/s)
adb shell cmd car_service inject-vhal-event 0x11600207 0 1.3889
adb logcat -s SafeModeManager -d | tail -3
# Expected: NORMAL_SAFE_MODE

# Inject 1 unit below (4.99 km/h = 1.3861 m/s)
adb shell cmd car_service inject-vhal-event 0x11600207 0 1.3861
adb logcat -s SafeModeManager -d | tail -3
# Expected: NO_SAFE_MODE

# Inject exactly at HARD boundary (15.0 km/h = 4.1667 m/s)
adb shell cmd car_service inject-vhal-event 0x11600207 0 4.1667
adb logcat -s SafeModeManager -d | tail -3
# Expected: HARD_SAFE_MODE
```

**Gate:** All 3 scripts run without errors, service receives all events, boundary values produce correct SafeModeState transitions. ✅

---

### ✅ Phase 4 Verification — Sample Test App (End-to-End)

**Goal:** App correctly displays vehicle data, reacts to state changes, no ANR, no memory leak.

#### Step 4.1 — Install and launch
```bash
adb install -r out/target/product/rpi5/system/app/SafeModeDemo/SafeModeDemo.apk
adb shell am start -n com.myoem.safedemo/.MainActivity
# Expected: app launches without crash, shows initial state
```

#### Step 4.2 — Verify initial state displayed
```bash
# App should show: Speed: 0.0 km/h | Gear: PARK | Fuel: 100% | State: NO_SAFE_MODE
# Inject initial values to confirm display updates:
adb shell cmd car_service inject-vhal-event 0x11600207 0 0.0
adb shell cmd car_service inject-vhal-event 0x11400400 0 4
adb shell cmd car_service inject-vhal-event 0x45100004 0 50000
# Observe app UI updates
```

#### Step 4.3 — Verify state transition UI changes
```bash
# NO_SAFE_MODE → inject 0.0 m/s
adb shell cmd car_service inject-vhal-event 0x11600207 0 0.0
# App UI should show: state indicator GREEN, "All features available"

# NORMAL_SAFE_MODE → inject 2.78 m/s (10 km/h)
adb shell cmd car_service inject-vhal-event 0x11600207 0 2.78
# App UI should show: state indicator YELLOW, "Moderate restrictions"

# HARD_SAFE_MODE → inject 5.56 m/s (20 km/h)
adb shell cmd car_service inject-vhal-event 0x11600207 0 5.56
# App UI should show: state indicator RED, "Strict restrictions"
```

#### Step 4.4 — Verify no ANR (stress test)
```bash
# Inject 50 rapid speed changes
for i in $(seq 1 50); do
    adb shell cmd car_service inject-vhal-event 0x11600207 0 $(( RANDOM % 15 )).0
    sleep 0.1
done
# App should remain responsive — no ANR dialog
# Check: adb logcat | grep -i "ANR\|not responding"
```

#### Step 4.5 — Verify no callback leak (lifecycle test)
```bash
# Check active callbacks before kill: should be 1
adb logcat -s SafeModeService -d | grep "Active callbacks"

# Kill app
adb shell am force-stop com.myoem.safedemo

# Check active callbacks after kill: should be 0
adb logcat -s SafeModeService -d | tail -5 | grep "Active callbacks"
# Expected: Active callbacks: 0
```

#### Step 4.6 — Verify app recovers after service restart
```bash
# Kill service (simulates crash)
adb shell kill $(adb shell pidof safemoded)
sleep 2

# Service should auto-restart (class main)
adb shell ps -e | grep safemoded
# Expected: safemoded process present again

# Inject value — app should receive it after reconnect
adb shell cmd car_service inject-vhal-event 0x11600207 0 3.0
adb logcat -s SafeModeManager | tail -3
# Expected: onServiceConnected logged, data received
```

**Gate:** App displays all 3 vehicle data fields, all 3 state transitions visible in UI, no ANR, no callback leak, recovers from service restart. ✅

---

### ✅ Full Pipeline Smoke Test (Run after all phases complete)

> One final test that exercises the complete system end-to-end with all simulators running.

```bash
#!/bin/bash
# full_pipeline_test.sh

echo "=== SafeMode Full Pipeline Test ==="
echo ""

# 1. Check device connected
echo "[1] ADB device check..."
adb devices | grep -v "List of" | grep "device$" && echo "  PASS" || { echo "  FAIL: no device"; exit 1; }

# 2. Check service running
echo "[2] SafeModeService process check..."
adb shell ps -e | grep safemoded && echo "  PASS" || { echo "  FAIL: safemoded not running"; exit 1; }

# 3. Check ServiceManager registration
echo "[3] ServiceManager registration check..."
adb shell service list | grep "com.myoem.safemode" && echo "  PASS" || { echo "  FAIL: not registered"; exit 1; }

# 4. Check no SELinux denials
echo "[4] SELinux denial check..."
COUNT=$(adb logcat -d | grep "avc: denied" | grep safemoded | wc -l)
[ "$COUNT" -eq 0 ] && echo "  PASS (0 denials)" || echo "  WARN: $COUNT denials found"

# 5. Speed injection test
echo "[5] VHAL speed injection test..."
adb shell cmd car_service inject-vhal-event 0x11600207 0 5.0 && echo "  PASS" || echo "  FAIL"

# 6. Gear injection test
echo "[6] VHAL gear injection test..."
adb shell cmd car_service inject-vhal-event 0x11400400 0 8 && echo "  PASS" || echo "  FAIL"

# 7. Fuel injection test
echo "[7] VHAL fuel injection test..."
adb shell cmd car_service inject-vhal-event 0x45100004 0 25000 && echo "  PASS" || echo "  FAIL"

echo ""
echo "=== Pipeline test complete ==="
```

```bash
# Run it:
bash vendor/myoem/tools/safemode-simulator/full_pipeline_test.sh
```

---

## Article Notes — Implementation Story

> **Purpose:** These notes capture the *why* behind every decision so the article can explain the thinking process, not just the code. Reference these when writing.

---

### Article Title Ideas
- *"Building a Vendor SafeMode Service for Android Automotive on Raspberry Pi 5 — From VHAL to AAR"*
- *"Full-Stack AOSP Development: Native C++ Service → AIDL → Android Library → App"*
- *"How Android OEMs Expose Vehicle Data to Third-Party Apps: A Complete Guide"*

---

### Story Arc (article narrative)

```
Problem → Why VHAL alone isn't enough for app developers
    ↓
Solution → A layered architecture: Service | Library | App
    ↓
Challenge 1 → Where does business logic live? (Service vs Library debate)
    ↓
Challenge 2 → NDK Binder in vendor — why not libbinder?
    ↓
Challenge 3 → AIDL oneway — why callbacks must be non-blocking
    ↓
Challenge 4 → No real hardware on RPi5 — the VHAL injection technique
    ↓
Result → App developer writes 2 lines of code to get real-time vehicle data
```

---

### Key Concept Explanations (for article)

#### Concept 1: Why the service is a "data pipe"
- **Context:** Many developers instinct is to put business logic in the service (e.g., decide SafeModeState in C++)
- **Problem with that:** Service is in the vendor partition, requires OTA to update. Thresholds like 5 km/h and 15 km/h are policy decisions — they belong to the product layer, not the platform layer.
- **Our decision:** Service only emits raw VHAL values (m/s, gear int, ml). Library derives SafeModeState.
- **Result:** Update thresholds by shipping a new AAR version — no service redeployment needed.

#### Concept 2: libbinder vs libbinder_ndk
- **Context:** Vendor partition cannot use system partition libraries directly (Treble separation).
- **Problem:** `libbinder` is a system library. Vendor binaries that link against it break Treble compliance.
- **Rule:** Vendor services must use `libbinder_ndk` (the NDK-stable ABI) which crosses the vendor/system boundary safely.
- **In practice:** AIDL backend must be `ndk: { enabled: true }`, `cpp: { enabled: false }`.

#### Concept 3: AIDL `oneway` on callbacks
- **Context:** The service calls back into app clients when VHAL events arrive.
- **Problem without oneway:** Service thread blocks waiting for each client's callback to return. If one client is slow (doing UI work), all clients are delayed. If client is dead, service hangs.
- **Solution:** `oneway interface ISafeModeCallback` — service fires-and-forgets. Client receives asynchronously.
- **Article angle:** "This is the same pattern used in Android's LocationManager and AudioManager callbacks."

#### Concept 4: VHAL Injection — the RPi5 workaround that is actually standard
- **Context:** RPi5 has no CAN bus, no real vehicle data.
- **The tool:** `adb shell cmd car_service inject-vhal-event` is the **official** AAOS tool — used by Google's own AAOS emulator team for testing.
- **Article angle:** This isn't a workaround — this is how every AAOS engineer tests VHAL-dependent code. Even on real vehicles, developers use injection to simulate edge cases (e.g., fuel = 0, speed = 200 km/h).

#### Concept 5: The dual build system (Soong + Gradle)
- **Problem:** Library needs to be built inside AOSP (Soong) for system image integration AND distributable as an AAR (Gradle/Maven) for external app developers.
- **Solution:** Maintain both `Android.bp` and `build.gradle.kts`. The AIDL files are the single source of truth — copied into the library project.
- **Article angle:** This is the same pattern used by Android's own `car-lib` and MediaSession APIs.

#### Concept 6: SafeModeManager as a singleton with lifecycle scoping
- **Design choice:** `SafeModeManager.attach(context, listener)` / `.dispose(context)` — per-context scoping means multiple Activities can independently subscribe and clean up.
- **Why singleton:** One Binder connection to the service per process — not one per Activity. Efficient.
- **Why not ViewModel:** The manager itself is not tied to any Jetpack arch — app developers can use it from ViewModel, Activity, or Service as they prefer.

#### Concept 7: SELinux and the vendor service domain
- **Context:** Every executable in the vendor partition needs its own SELinux domain.
- **The gotcha:** `sysfs_hwmon` type doesn't exist in AOSP 15 base policy (learned from ThermalControl). Always verify with `adb shell ls -laZ`.
- **For the article:** Show the 4-file SELinux pattern: `.te`, `file_contexts`, `service_contexts`, `service.te`. Explain each file's role.

---

### Code Moments Worth Highlighting in Article

| Moment | File | Why it matters |
|--------|------|----------------|
| `#define LOG_TAG` as first line | Every `.cpp` | AOSP strict rule — include order matters |
| `ABinderProcess_setThreadPoolMaxThreadCount(4)` | `main.cpp` | Vendor service thread pool setup |
| `oneway interface ISafeModeCallback` | AIDL | The non-blocking callback pattern |
| `SafeModeState.fromSpeedMs()` | `SafeModeState.kt` | Business logic in library, not service |
| `SafeModeManager.attach(this, this)` | Sample app | The final developer experience — 1 line |
| `adb shell cmd car_service inject-vhal-event` | Simulator | Official VHAL test injection |
| `chown root system` + `chmod 0664` in RC | `safemoded.rc` | Permission pattern for sysfs-like resources |

---

### Bugs to Document in Article (predict from ThermalControl experience)

| Predicted Bug | Likely Cause | Fix |
|---------------|-------------|-----|
| Service doesn't start | SELinux domain missing or RC syntax error | Check `adb logcat -b kernel | grep safemoded` |
| "Failed to register" in logcat | Service name mismatch between main.cpp and service_contexts | Must match exactly |
| Callback never fires | Forgot `oneway` or client never registered | Verify with `adb logcat -s SafeModeService` |
| ANR in app | AIDL call on main thread | Move bind/call to background coroutine |
| `inject-vhal-event` returns error | Wrong property ID format | Use hex with `0x` prefix |
| VHAL subscription silently fails | `vhalclient_defaults` missing from Android.bp | Add `defaults: ["vhalclient_defaults"]` |
| Library AAR build fails | AIDL stubs not copied into library project | Copy all 3 AIDL files to `src/main/aidl/` |

---

### Actual Build Errors Hit During Implementation (real, not predicted)

These are real errors encountered and fixed during the actual build — include these verbatim in the article as a "debugging walkthrough" section. Readers hitting the same errors will find this via search.

#### Build Error 1 — Soong namespace isolation: app can't see library module

**Error message:**
```
error: vendor/myoem/apps/SafeModeDemo/Android.bp:11:1: "SafeModeDemo" depends on undefined module "safemode_library".
Module "SafeModeDemo" is defined in namespace "vendor/myoem/apps/SafeModeDemo" which can read these 2 namespaces: ["vendor/myoem/apps/SafeModeDemo" "."]
Module "safemode_library" can be found in these namespaces: ["vendor/myoem/libs/safemode"]
```

**Root cause:**
Soong namespaces are strictly isolated by default. Each `soong_namespace {}` block creates a visibility boundary. A module in namespace `vendor/myoem/apps/SafeModeDemo` cannot see modules in `vendor/myoem/libs/safemode` unless the dependency is explicitly declared.

**Fix:**
Add `imports` to the consuming module's `soong_namespace {}`:
```bp
// vendor/myoem/apps/SafeModeDemo/Android.bp
soong_namespace {
    imports: ["vendor/myoem/libs/safemode"]
}
```

**Article insight:**
Soong namespace isolation is an intentional design — it prevents OEM modules from accidentally depending on each other's internal modules. The `imports` field is how you explicitly allow cross-namespace dependencies. Always add it when a module in one vendor subdirectory depends on a module in another.

---

#### Build Error 2 — SELinux: `unknown type hal_automotive_vehicle_server`

**Error message:**
```
vendor/myoem/services/safemode/sepolicy/private/safemoded.te:59:
ERROR 'unknown type hal_automotive_vehicle_server' at token ';'
allow safemoded hal_automotive_vehicle_server:binder { call transfer };
checkpolicy: error(s) encountered while parsing configuration
```

**Root cause:**
`hal_automotive_vehicle_server` is the standard AOSP type for the Vehicle HAL process, defined in `system/sepolicy/public/hal_automotive_vehicle.te`. However, this type is only compiled into the policy when the target includes full automotive HAL policy. The RPi5 AOSP build does not include this type in `product_sepolicy.cil`.

**The rule from CLAUDE.md:** "Never assume a SELinux type exists — verify with `adb shell ls -laZ <path>`" — equally applies to domain types. Never assume a SELinux domain type exists without checking the device build.

**Fix (two steps):**

Step 1 — Remove/comment out the `binder_call` line to unblock the build:
```
# binder_call(safemoded, hal_automotive_vehicle_server)  ← commented until verified
```

Step 2 — After booting, find the real VHAL domain name:
```bash
# Find the VHAL process domain
adb shell ps -eZ | grep -i vehicle

# Or check AVC denials — safemoded → VHAL binder call will be denied
# The denial shows the real target domain name:
adb logcat -d | grep "avc: denied" | grep safemoded
# Look for: tcontext=u:r:<REAL_DOMAIN>:s0
```

Then add the correct `binder_call(safemoded, <REAL_DOMAIN>)` to `safemoded.te`.

**Article insight:**
This is a classic AOSP pitfall. SELinux policy is built from the *intersection* of base policy + device policy + vendor private policy. A type that exists in AOSP's reference automotive builds may not exist in your device's compiled policy. Always build first, then check for unknown type errors — never write SELinux types from documentation alone.

---

#### Build Error 3 — SELinux: `unknown type log_device`

**Error message:**
```
vendor/myoem/services/safemode/sepolicy/private/safemoded.te:82:
ERROR 'unknown type log_device' at token ';'
allow safemoded log_device:chr_file { getattr open read ... };
```

**Root cause:**
`log_device` was the SELinux type for `/dev/log/*` character devices used in Android ≤ 12. In Android 13+, the `/dev/log/` device nodes were removed and all logging was moved to the `logd` daemon via a Unix socket (`/dev/socket/logd`). The type `log_device` no longer exists in AOSP 15's base policy.

**Fix:**
Remove the explicit `allow safemoded log_device` rule entirely. `init_daemon_domain(safemoded)` already grants the necessary `logd` socket permissions — the rule is both wrong (unknown type) and redundant (already covered).

```
# DO NOT add: allow safemoded log_device:chr_file rw_file_perms;
# log_device does not exist in AOSP 15. init_daemon_domain() covers logging.
```

**Article insight:**
Many AOSP SELinux tutorials and blog posts still show the old `log_device` pattern — they were written for Android 10–12. In AOSP 15, omit it entirely. This is a common copy-paste mistake when adapting older `.te` examples.

---

#### Build Error 4 — VHAL V3 API: `VehiclePropValues` wrapper + signature change

**Error messages:**
```
error: non-virtual member function marked 'override' hides virtual member function
  onPropertyEvent(const VehiclePropValues& in_propValues, int32_t in_sharedMemoryFileCount)
  vs our: onPropertyEvent(const std::vector<VehiclePropValue>&, int64_t timestamp)

error: non-virtual member function marked 'override' hides virtual member function
  onPropertySetError(const VehiclePropErrors& in_errors)
  vs our: onPropertySetError(const std::vector<VehiclePropError>&)

fatal error: 'aidl/android/hardware/automotive/vehicle/VehicleProperty.h' file not found
```

**Root cause — three related issues in the VHAL V3 API:**

1. **V3 wraps vectors in parcelable structs.** `onPropertyEvent` no longer takes `std::vector<VehiclePropValue>`. Instead VHAL V3 uses:
   ```
   parcelable VehiclePropValues { VehiclePropValue[] payloads; }
   parcelable VehiclePropErrors { VehiclePropError[] errors;   }
   ```
   The actual events are in `propValues.payloads`, not `propValues` directly.

2. **The second parameter changed completely.** `int64_t timestamp` (V2) → `int32_t sharedMemoryFileCount` (V3). V3 supports transferring large batches via shared memory; the count tells the receiver how many shared-memory FDs to expect.

3. **`VehicleProperty.h` not in NDK path for vendor.** The generated header exists in the AOSP tree but is not exported to vendor NDK includes in all builds. Using the raw hex IDs directly is the correct vendor-safe approach.

**Fix:**
```cpp
// SafeModeService.h
::ndk::ScopedAStatus onPropertyEvent(
    const VehiclePropValues& propValues,        // wrapper, not vector
    int32_t sharedMemoryFileCount) override;    // V3: shm count, not timestamp

::ndk::ScopedAStatus onPropertySetError(
    const VehiclePropErrors& errors) override;  // wrapper, not vector

// SafeModeService.cpp
// Remove: #include <aidl/android/hardware/automotive/vehicle/VehicleProperty.h>
// Use raw hex: static constexpr int32_t PROP_SPEED = 0x11600207;
// Iterate: for (const auto& pv : propValues.payloads) { ... }
```

**Article insight:**
VHAL V3 is a significant API break from V2. Any blog post or AOSP example written before Android 13 will show the old `vector<VehiclePropValue>` signature — it will not compile against V3. The struct wrappers exist to support batched shared-memory transfers without changing the AIDL method signature in future versions. When implementing `IVehicleCallback`, always look at the generated `IVehicleCallback.h` header to confirm the exact signatures for your VHAL version.

---

#### Build Error 5 — VHAL V3: `IVehicleCallback` has 4 pure virtuals; `getValues()` is async

**Error messages:**
```
error: allocating an object of abstract class type 'VhalEventCallback'
  note: unimplemented pure virtual method 'onGetValues' in 'VhalEventCallback'
  note: unimplemented pure virtual method 'onSetValues' in 'VhalEventCallback'

error: no viable conversion from 'std::vector<GetValueRequest>' to
       'const std::shared_ptr<IVehicleCallback>'
  note: 'getValues' declared as:
    getValues(const shared_ptr<IVehicleCallback>& callback,
              const GetValueRequests& requests)
```

**Root cause — VHAL V3 is a fully async API:**

V3's `IVehicleCallback` has **4** pure virtual methods (we only implemented 2):
```cpp
onGetValues(const GetValueResults& results)   // ← NEW in V3 — was missing
onSetValues(const SetValueResults& results)   // ← NEW in V3 — was missing
onPropertyEvent(const VehiclePropValues& ...)
onPropertySetError(const VehiclePropErrors& ...)
```
Missing any pure virtual makes the class abstract and causes `make<VhalEventCallback>()` to fail.

Also, `IVehicle::getValues()` changed from synchronous to asynchronous in V3:
```cpp
// V2 (old — DOES NOT EXIST in V3):
getValues(vector<GetValueRequest>, vector<GetValueResult>*)

// V3 (actual):
getValues(const shared_ptr<IVehicleCallback>& callback,
          const GetValueRequests& requests)
// Results arrive via: callback->onGetValues(GetValueResults)
```

**Fix:**
- Add `onGetValues()` to forward results to `onVhalEvent()` (handles initial read)
- Add `onSetValues()` as a no-op (we never set properties)
- Replace the initial `getValues(requests, &results)` call with `getValues(mVhalCallback, GetValueRequests{...})`
- Results arrive asynchronously via `onGetValues()`, which calls `onVhalEvent()` for each

**Article insight:**
VHAL V3 unified the entire API around async callbacks — even "get" operations are non-blocking. This is by design for AAOS performance: the main thread is never blocked on HAL I/O. The pattern mirrors how Android's `BluetoothGatt` API works (async reads, responses via callback). Implement ALL pure virtuals in `IVehicleCallback` even if they're no-ops — forgetting any one makes the class abstract.

---

#### Build Error 6 — AIDL include root mismatch: `Couldn't find import for class com.myoem.safemode.VehicleData`

**Error message:**
```
ERROR: vendor/myoem/libs/safemode/src/main/aidl/com/myoem/safemode/ISafeModeCallback.aidl:
Couldn't find import for class com.myoem.safemode.VehicleData. Searched here:
 - vendor/myoem/libs/safemode/
 - vendor/myoem/libs/safemode/src/
```

**Root cause:**
The AIDL compiler resolves an import like `import com.myoem.safemode.VehicleData;` by looking for a file at:
```
<include_root>/com/myoem/safemode/VehicleData.aidl
```
Soong was passing `-Ivendor/myoem/libs/safemode/src/` as the include root, but the AIDL files live at `src/main/aidl/`. So the compiler was looking for:
```
vendor/myoem/libs/safemode/src/com/myoem/safemode/VehicleData.aidl   ← doesn't exist
```
instead of:
```
vendor/myoem/libs/safemode/src/main/aidl/com/myoem/safemode/VehicleData.aidl   ← correct
```

**Fix:**
Add an `aidl.local_include_dirs` block to the `android_library` module:
```bp
android_library {
    name: "safemode_library",
    srcs: ["src/main/aidl/**/*.aidl", "src/main/java/**/*.kt"],
    aidl: {
        local_include_dirs: ["src/main/aidl"],  // ← this line
    },
    ...
}
```
This makes Soong pass `-Ivendor/myoem/libs/safemode/src/main/aidl` so the package path resolves correctly.

**Article insight:**
This is the most common mistake when using a standard Android Gradle project layout (`src/main/aidl/`) inside Soong. Soong does not automatically infer the AIDL root from the source tree structure — you must declare it explicitly. The rule: the AIDL include root must be the directory that is the *package root*, not the directory that contains `.aidl` files. For package `com.myoem.safemode`, the root is the directory whose child is `com/`.

---

### Article Series Structure (suggested)

```
Part 1: The Problem & Architecture
  - What is SafeMode in AAOS?
  - Why a layered approach (service → library → app)?
  - High-level component diagram

Part 2: The AIDL Contract
  - Designing AIDL interfaces: ISafeModeService, ISafeModeCallback, VehicleData
  - The oneway keyword: why it matters for callbacks
  - Soong aidl_interface: generating stubs for C++ and Java

Part 3: The C++ Native Vendor Service
  - NDK Binder vs system Binder (Treble compliance)
  - Connecting to VHAL via IVhalClient
  - Subscribing to property change events
  - SELinux policy for vendor services (the 4-file pattern)
  - RC files and boot-aware services

Part 4: The Android Library (AAR)
  - Dual build: Soong + Gradle
  - SafeModeState — business logic in the library
  - SafeModeManager singleton with lifecycle scoping
  - Publishing to Maven local

Part 5: Testing on RPi5 without real hardware
  - The VHAL injection technique (cmd car_service inject-vhal-event)
  - Writing Python simulators for speed, gear, fuel
  - Full pipeline verification

Part 6: Developer Experience
  - Using safemode_library in a real Android app
  - 2-line integration: attach() and dispose()
  - Lessons learned & debugging guide
```
