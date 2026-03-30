// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

// ─── LOG_TAG must be the very first line before ANY #include ─────────────────
// AOSP's log macros (ALOGI/ALOGE/ALOGD) use LOG_TAG to tag logcat output.
// If LOG_TAG is defined after an #include that itself includes <log/log.h>,
// you will get a "LOG_TAG redefined" compiler warning or the wrong tag.
#define LOG_TAG "safemoded"

#include "SafeModeService.h"

// PRId64 format macro for int64_t — needed for ALOGW format strings
#include <inttypes.h>

// ALOGI / ALOGE / ALOGD / ALOGW
#include <log/log.h>

// AServiceManager_waitForService — blocks until VHAL is registered in ServiceManager.
// Safe to call at boot; avoids a busy-wait polling loop.
// VHAL is accessed via NDK Binder, so we use the NDK manager API here.
#include <android/binder_manager.h>

using namespace ::aidl::android::hardware::automotive::vehicle;

namespace aidl::com::myoem::safemode {

// ─────────────────────────────────────────────────────────────────────────────
// VHAL property IDs (raw hex — do not use VehicleProperty.h)
//
// WHY raw hex and not VehicleProperty enum:
//   VehicleProperty.h is generated from VehicleProperty.aidl — a very large
//   AIDL file (1000+ entries). In some AOSP builds the generated header is not
//   present in the NDK include path for vendor binaries, causing a compile error:
//     fatal error: 'aidl/.../VehicleProperty.h' file not found
//   Using the raw hex values is equivalent, avoids the fragile generated-header
//   dependency, and is consistent with how VHAL emulator injection commands work.
//
// Values from VehicleProperty.aidl (stable across VHAL V2/V3):
//   PERF_VEHICLE_SPEED = 0x11600207 — FLOAT,  GLOBAL, CONTINUOUS
//   CURRENT_GEAR       = 0x11400400 — INT32,  GLOBAL, ON_CHANGE
//   FUEL_LEVEL         = 0x45100004 — FLOAT,  GLOBAL, ON_CHANGE
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int32_t PROP_SPEED = 0x11600207;
static constexpr int32_t PROP_GEAR  = 0x11400400;
static constexpr int32_t PROP_FUEL  = 0x45100004;

// VHAL service name in ServiceManager (AIDL VHAL, version 3)
static constexpr const char* kVhalServiceName =
    "android.hardware.automotive.vehicle.IVehicle/default";

// ─── VhalEventCallback implementation ────────────────────────────────────────

VhalEventCallback::VhalEventCallback(SafeModeService* parent)
    : mParent(parent) {}

/**
 * onPropertyEvent — called by VHAL when a subscribed property changes.
 *
 * propValues may be a batch — VHAL can coalesce multiple events.
 * We iterate all and forward each to the service for processing.
 *
 * This runs on a Binder thread in our process, dispatched by the VHAL server.
 * We must not do heavy work here — just update state and forward.
 */
::ndk::ScopedAStatus VhalEventCallback::onPropertyEvent(
        const VehiclePropValues& propValues,
        int32_t /* sharedMemoryFileCount */) {
    // VehiclePropValues is NOT a vector — it is a VHAL V3 parcelable wrapper:
    //   parcelable VehiclePropValues { VehiclePropValue[] payloads; }
    // The actual events are in propValues.payloads.
    //
    // sharedMemoryFileCount: when >0, some payloads were transferred via shared
    // memory for efficiency (large batches). We subscribe to 3 lightweight
    // scalar properties, so this will always be 0 for us.
    for (const auto& propValue : propValues.payloads) {
        // Forward each individual property event to the service.
        // The service will update mCurrentData and notify all clients.
        mParent->onVhalEvent(propValue);
    }
    return ::ndk::ScopedAStatus::ok();
}

/**
 * onPropertySetError — called if a property SET failed.
 * VehiclePropErrors is a VHAL V3 parcelable wrapper:
 *   parcelable VehiclePropErrors { VehiclePropError[] errors; }
 * We never SET any VHAL properties (read-only), so this is always a no-op.
 */
::ndk::ScopedAStatus VhalEventCallback::onPropertySetError(
        const VehiclePropErrors& /* errors */) {
    // Nothing to do — we never set VHAL properties.
    return ::ndk::ScopedAStatus::ok();
}

/**
 * onGetValues — called by VHAL with the async response to IVehicle::getValues().
 *
 * VHAL V3 made getValues() fully asynchronous: results arrive here instead of
 * blocking the caller. We use this to handle the initial property read issued in
 * connectToVhal() — each result's VehiclePropValue is forwarded to the service
 * exactly like a live subscription event.
 *
 * GetValueResults is a parcelable wrapper:
 *   parcelable GetValueResults { GetValueResult[] payloads; }
 * Each GetValueResult has:
 *   int64 requestId — matches the ID we sent in GetValueRequests
 *   StatusCode status — OK or an error code
 *   VehiclePropValue prop — the value (only valid when status == OK)
 */
::ndk::ScopedAStatus VhalEventCallback::onGetValues(
        const GetValueResults& results) {
    for (const auto& result : results.payloads) {
        if (result.status != StatusCode::OK) {
            ALOGW("onGetValues: requestId=%" PRId64 " status=%d (not OK — skipping)",
                  result.requestId, static_cast<int>(result.status));
            continue;
        }
        if (!result.prop.has_value()) {
            ALOGW("onGetValues: requestId=%" PRId64 " has no value — skipping",
                  result.requestId);
            continue;
        }
        // Reuse the same onVhalEvent() path as live subscription events.
        // This avoids duplicating the SPEED/GEAR/FUEL update logic.
        mParent->onVhalEvent(result.prop.value());
    }
    return ::ndk::ScopedAStatus::ok();
}

/**
 * onSetValues — called by VHAL with async results from IVehicle::setValues().
 * We never call setValues(), so this is permanently a no-op.
 * It MUST be implemented — it is pure virtual in IVehicleCallback.
 */
::ndk::ScopedAStatus VhalEventCallback::onSetValues(
        const SetValueResults& /* results */) {
    return ::ndk::ScopedAStatus::ok();
}

// ─── SafeModeService implementation ──────────────────────────────────────────

SafeModeService::SafeModeService() {
    // mCurrentData is default-initialised by its AIDL-defined default values:
    //   speedMs=0.0, gear=4 (PARK), fuelLevelMl=0.0
    // No explicit initialisation needed here.
    ALOGI("SafeModeService constructed");
}

SafeModeService::~SafeModeService() {
#ifdef SAFEMODE_SIM_MODE
    mSimRunning = false;
    if (mSimThread.joinable()) mSimThread.join();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// SIMULATOR MODE (RPi5 dev — remove -DSAFEMODE_SIM_MODE for production)
//
// Instead of connecting to VHAL, safemoded reads from a local file that Python
// scripts update. The same onVhalEvent / dispatchToCallbacks path is used, so
// moving to production only requires removing the compile flag.
//
// Data file: /data/local/tmp/safemode_sim.txt
// Format:    "<speed_ms> <gear_int> <fuel_ml>"   (one line, space-separated)
// Written by: vendor/myoem/tools/safemode-simulator/simulate_all.py
// ─────────────────────────────────────────────────────────────────────────────
#ifdef SAFEMODE_SIM_MODE

bool SafeModeService::startSimulator() {
    ALOGI("=================================================");
    ALOGI("SAFEMODE_SIM_MODE: data source = %s", SIM_FILE);
    ALOGI("Run: python3 vendor/myoem/tools/safemode-simulator/simulate_all.py");
    ALOGI("=================================================");
    mSimRunning = true;
    mSimThread = std::thread(&SafeModeService::runSimulatorThread, this);
    return true;
}

/**
 * runSimulatorThread — polls SIM_FILE every 500 ms.
 *
 * Reads the current speed/gear/fuel values from the file and calls
 * onVhalEvent() exactly as if a real VHAL subscription event arrived.
 * Only dispatches to callbacks when the values actually change, so
 * registered clients (safemode_client, SafeModeManager) aren't flooded.
 *
 * If the file doesn't exist yet, the thread silently waits (initial state
 * remains at defaults: speed=0, gear=PARK, fuel=0).
 */
void SafeModeService::runSimulatorThread() {
    ALOGI("SIM: simulator thread running");

    float lastSpeed = -1.0f;
    int32_t lastGear = -1;
    float lastFuel = -1.0f;

    while (mSimRunning) {
        FILE* f = fopen(SIM_FILE, "r");
        if (f) {
            float speed = 0.0f, fuel = 0.0f;
            int gear = 4;
            int parsed = fscanf(f, "%f %d %f", &speed, &gear, &fuel);
            fclose(f);

            if (parsed == 3) {
                // Only update + dispatch when values change
                if (speed != lastSpeed || gear != lastGear || fuel != lastFuel) {
                    lastSpeed = speed;
                    lastGear  = gear;
                    lastFuel  = fuel;

                    VehicleData snapshot;
                    {
                        std::lock_guard<std::mutex> lock(mMutex);
                        mCurrentData.speedMs     = speed;
                        mCurrentData.gear        = gear;
                        mCurrentData.fuelLevelMl = fuel;
                        snapshot = mCurrentData;
                    }

                    ALOGI("SIM: speed=%.2f m/s (%.1f km/h)  gear=0x%x  fuel=%.0f ml",
                          speed, speed * 3.6f, gear, fuel);

                    dispatchToCallbacks(snapshot);
                }
            } else {
                ALOGW("SIM: failed to parse %s (expected \"<speed> <gear> <fuel>\")", SIM_FILE);
            }
        }
        // Poll every 500 ms — Python writes every 5 s, so we catch updates quickly
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    ALOGI("SIM: simulator thread stopped");
}

#endif // SAFEMODE_SIM_MODE

/**
 * connectToVhal — called once at startup from main.cpp.
 *
 * Steps:
 *   1. Block until VHAL registers in ServiceManager (AServiceManager_waitForService).
 *   2. Wrap the raw Binder into our AIDL-typed IVehicle interface.
 *   3. Create our callback object.
 *   4. Build SubscribeOptions for the 3 properties we need.
 *   5. Call IVehicle::subscribe() to start receiving push events.
 *   6. Read the initial values synchronously so mCurrentData is populated
 *      before any client calls getCurrentData().
 *
 * Returns false if VHAL is unavailable — main() will exit on false.
 */
bool SafeModeService::connectToVhal() {
    ALOGI("Connecting to VHAL at '%s'", kVhalServiceName);

    // ── Step 1: Get VHAL Binder handle ───────────────────────────────────────
    // AServiceManager_waitForService blocks until VHAL registers itself.
    // On the car build (myoem_rpi5_car), VHAL starts during boot before us,
    // so this returns quickly. Safe to block here — we must not proceed without
    // VHAL since we'd have no data source to serve to clients.
    // Note: we use the NDK manager API here because VHAL is an NDK service.
    ::ndk::SpAIBinder binder(AServiceManager_waitForService(kVhalServiceName));
    if (binder.get() == nullptr) {
        ALOGE("VHAL service '%s' not found", kVhalServiceName);
        return false;
    }

    // ── Step 2: Convert raw AIBinder → typed IVehicle interface ──────────────
    // fromBinder() validates that the Binder implements IVehicle (checks the
    // interface descriptor string). Returns nullptr if there is a type mismatch.
    mVhal = IVehicle::fromBinder(binder);
    if (mVhal == nullptr) {
        ALOGE("Failed to cast VHAL Binder to IVehicle — interface mismatch");
        return false;
    }
    ALOGI("VHAL connected successfully");

    // ── Step 3: Create our callback object ────────────────────────────────────
    // mVhalCallback is kept alive as a member so VHAL can call into it
    // for the lifetime of this service. Without this, the callback object
    // would be destroyed and VHAL would call into a dangling pointer.
    mVhalCallback = ::ndk::SharedRefBase::make<VhalEventCallback>(this);

    // ── Step 4: Build SubscribeOptions ────────────────────────────────────────
    // Each SubscribeOptions entry tells VHAL which property to subscribe to
    // and (for CONTINUOUS properties) at what sample rate in Hz.
    //
    // PERF_VEHICLE_SPEED is CONTINUOUS — needs a sampleRate.
    //   1.0f Hz = one event per second; enough for SafeMode purposes.
    //
    // CURRENT_GEAR and FUEL_LEVEL are ON_CHANGE — VHAL delivers events only
    //   when the value changes. sampleRate = 0.0f for ON_CHANGE properties.
    SubscribeOptions speedOpts;
    speedOpts.propId     = PROP_SPEED;
    speedOpts.areaIds    = {0};     // Area 0 = GLOBAL (no per-seat/per-door area)
    speedOpts.sampleRate = 1.0f;   // Hz — request 1 event per second

    SubscribeOptions gearOpts;
    gearOpts.propId     = PROP_GEAR;
    gearOpts.areaIds    = {0};
    gearOpts.sampleRate = 0.0f;   // ON_CHANGE — no polling needed

    SubscribeOptions fuelOpts;
    fuelOpts.propId     = PROP_FUEL;
    fuelOpts.areaIds    = {0};
    fuelOpts.sampleRate = 0.0f;   // ON_CHANGE

    // ── Step 5: Subscribe to VHAL — one property at a time ───────────────────
    // WHY NOT a single batch call?
    //   IVehicle::subscribe() is all-or-nothing: if ANY property in the batch
    //   is unsupported by this VHAL implementation, the entire call returns
    //   INVALID_ARG and NO properties are subscribed.
    //
    //   On the RPi5 emulated VHAL (myoem_rpi5_car), FUEL_LEVEL (0x45100004)
    //   is not configured — the VHAL returns:
    //     "no config for property, ID: 1158676484: INVALID_ARG"
    //   This caused safemoded to crash-loop at boot.
    //
    //   Fix: subscribe individually so SPEED and GEAR succeed even if FUEL fails.
    //   FUEL is optional for SafeMode — the state machine only uses speed.
    struct PropSubscription {
        const char* name;
        SubscribeOptions opts;
        bool required;  // if true, failure → return false
    };

    PropSubscription props[] = {
        { "SPEED", speedOpts, true  },   // required — drives SafeMode state machine
        { "GEAR",  gearOpts,  true  },   // required — displayed in UI
        { "FUEL",  fuelOpts,  false },   // optional — not all VHALs support it
    };

    int subscribedCount = 0;
    for (auto& p : props) {
        auto status = mVhal->subscribe(mVhalCallback, {p.opts}, 0);
        if (status.isOk()) {
            ALOGI("VHAL subscribe OK: %s (prop=0x%x)", p.name, p.opts.propId);
            ++subscribedCount;
        } else {
            const char* level = p.required ? "FATAL" : "WARNING";
            ALOGE("VHAL subscribe %s for %s (0x%x): %s",
                  level, p.name, p.opts.propId, status.getDescription().c_str());
            if (p.required) {
                return false;
            }
            // Optional property not supported — continue without it
        }
    }
    ALOGI("VHAL subscriptions: %d/%zu active", subscribedCount,
          sizeof(props)/sizeof(props[0]));

    // ── Step 6: Read initial values asynchronously (VHAL V3) ──────────────────
    // VHAL V3 made getValues() ASYNCHRONOUS. Results arrive via onGetValues().
    // Non-fatal if it fails — mCurrentData keeps its AIDL-defined defaults.
    GetValueRequests getRequests;
    getRequests.payloads = {
        {.requestId = 0, .prop = {.areaId = 0, .prop = PROP_SPEED}},
        {.requestId = 1, .prop = {.areaId = 0, .prop = PROP_GEAR}},
        {.requestId = 2, .prop = {.areaId = 0, .prop = PROP_FUEL}},
    };

    auto getStatus = mVhal->getValues(mVhalCallback, getRequests);
    if (!getStatus.isOk()) {
        // Non-fatal: service starts with default-zeroed data.
        // VHAL subscription events will correct values as they arrive.
        ALOGW("Initial getValues() request failed (%s) — using defaults until first event",
              getStatus.getDescription().c_str());
    } else {
        ALOGI("Initial getValues() request sent — results will arrive via onGetValues()");
    }

    return true;
}

/**
 * onVhalEvent — called by VhalEventCallback for each property event.
 *
 * Updates the matching field in mCurrentData and then dispatches the full
 * updated snapshot to all registered callbacks.
 *
 * Threading: called on a VHAL Binder thread. mMutex protects mCurrentData.
 * The dispatch happens OUTSIDE the lock (see dispatchToCallbacks()).
 */
void SafeModeService::onVhalEvent(const VehiclePropValue& propValue) {
    VehicleData snapshot;

    {
        // Lock scope — update the matching field in mCurrentData
        std::lock_guard<std::mutex> lock(mMutex);

        if (propValue.prop == PROP_SPEED) {
            // PERF_VEHICLE_SPEED value is in propValue.value.floatValues[0]
            if (!propValue.value.floatValues.empty()) {
                mCurrentData.speedMs = propValue.value.floatValues[0];
                ALOGD("VHAL SPEED update: %.2f m/s (%.1f km/h)",
                      mCurrentData.speedMs, mCurrentData.speedMs * 3.6f);
            }
        } else if (propValue.prop == PROP_GEAR) {
            // CURRENT_GEAR value is in propValue.value.int32Values[0]
            if (!propValue.value.int32Values.empty()) {
                mCurrentData.gear = propValue.value.int32Values[0];
                ALOGD("VHAL GEAR update: 0x%x", mCurrentData.gear);
            }
        } else if (propValue.prop == PROP_FUEL) {
            // FUEL_LEVEL value is in propValue.value.floatValues[0]
            if (!propValue.value.floatValues.empty()) {
                mCurrentData.fuelLevelMl = propValue.value.floatValues[0];
                ALOGD("VHAL FUEL update: %.0f ml", mCurrentData.fuelLevelMl);
            }
        } else {
            // Unknown property — VHAL should never send us anything we didn't
            // subscribe to, but be defensive.
            ALOGW("Received unexpected VHAL property 0x%x", propValue.prop);
            return;
        }

        // Take a snapshot INSIDE the lock to pass to dispatch (outside lock).
        snapshot = mCurrentData;
    }
    // Lock is released here — safe to call callbacks now.

    dispatchToCallbacks(snapshot);
}

/**
 * dispatchToCallbacks — pushes a data snapshot to all registered clients.
 *
 * MUST be called WITHOUT mMutex held:
 *   - The callback crosses an IPC boundary into the client process.
 *   - If the client calls registerCallback() inside the callback handler,
 *     it would try to acquire mMutex → deadlock.
 *
 * Dead-client handling:
 *   - If a client process died, the AIDL call returns a Binder error.
 *   - We log a warning and mark the callback for removal.
 *   - After iterating all callbacks, we remove the dead ones.
 *   - This avoids modifying the vector while iterating it.
 */
void SafeModeService::dispatchToCallbacks(const VehicleData& data) {
    // Take a snapshot of the callback list under lock, then dispatch outside.
    // This means a newly-registered callback may miss one event — acceptable.
    std::vector<std::shared_ptr<ISafeModeCallback>> callbacks;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        callbacks = mCallbacks;
    }

    if (callbacks.empty()) {
        ALOGD("No clients registered — skipping dispatch");
        return;
    }

    ALOGD("Dispatching vehicle data to %zu client(s)", callbacks.size());

    std::vector<std::shared_ptr<ISafeModeCallback>> deadCallbacks;

    for (const auto& cb : callbacks) {
        auto status = cb->onVehicleDataChanged(data);
        if (!status.isOk()) {
            // Client Binder is dead (process killed, ANR, etc.)
            ALOGW("Callback dispatch failed (%s) — client may have died, removing",
                  status.getDescription().c_str());
            deadCallbacks.push_back(cb);
        }
    }

    // Remove dead callbacks under lock
    if (!deadCallbacks.empty()) {
        std::lock_guard<std::mutex> lock(mMutex);
        for (const auto& dead : deadCallbacks) {
            auto deadBinder = dead->asBinder();
            auto it = std::find_if(mCallbacks.begin(), mCallbacks.end(),
                [&deadBinder](const std::shared_ptr<ISafeModeCallback>& cb) {
                    return cb->asBinder() == deadBinder;
                });
            if (it != mCallbacks.end()) {
                mCallbacks.erase(it);
                ALOGI("Removed dead callback. Active callbacks: %zu", mCallbacks.size());
            }
        }
    }
}

// ─── ISafeModeService AIDL method implementations (NDK backend) ───────────────

/**
 * getCurrentData — synchronous snapshot getter.
 *
 * Called by polling clients every POLL_INTERVAL_MS to get the latest data.
 */
::ndk::ScopedAStatus SafeModeService::getCurrentData(VehicleData* out_data) {
    std::lock_guard<std::mutex> lock(mMutex);
    *out_data = mCurrentData;
    ALOGD("getCurrentData() → speed=%.2f gear=%d fuel=%.0f",
          out_data->speedMs, out_data->gear, out_data->fuelLevelMl);
    return ::ndk::ScopedAStatus::ok();
}

/**
 * registerCallback — adds a client callback to the dispatch list.
 *
 * Idempotent: if the same callback Binder is already registered (checked by
 * pointer equality via asBinder()), it is not added again.
 *
 * NOTE: With the polling approach in SafeModeManager.kt, this is not called
 * in normal operation (callbacks cross the system/vendor Binder stability
 * boundary). Kept for completeness and future @VintfStability upgrade.
 */
::ndk::ScopedAStatus SafeModeService::registerCallback(
        const std::shared_ptr<ISafeModeCallback>& callback) {
    if (callback == nullptr) {
        ALOGE("registerCallback: null callback rejected");
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    {
        std::lock_guard<std::mutex> lock(mMutex);

        // Idempotency check — compare by Binder pointer identity.
        auto newBinder = callback->asBinder();
        for (const auto& existing : mCallbacks) {
            if (existing->asBinder() == newBinder) {
                ALOGD("registerCallback: callback already registered (idempotent)");
                return ::ndk::ScopedAStatus::ok();
            }
        }

        mCallbacks.push_back(callback);
        ALOGI("registerCallback: registered. Active callbacks: %zu", mCallbacks.size());
    }

    // Send the current snapshot immediately to the new client.
    VehicleData snapshot;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        snapshot = mCurrentData;
    }
    auto status = callback->onVehicleDataChanged(snapshot);
    if (!status.isOk()) {
        ALOGW("Initial dispatch to new callback failed: %s",
              status.getDescription().c_str());
    }

    return ::ndk::ScopedAStatus::ok();
}

/**
 * unregisterCallback — removes a callback from the dispatch list.
 *
 * Safe to call even if the callback was never registered (no-op).
 */
::ndk::ScopedAStatus SafeModeService::unregisterCallback(
        const std::shared_ptr<ISafeModeCallback>& callback) {
    if (callback == nullptr) {
        return ::ndk::ScopedAStatus::ok();  // Graceful no-op
    }

    auto targetBinder = callback->asBinder();
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = std::find_if(mCallbacks.begin(), mCallbacks.end(),
        [&targetBinder](const std::shared_ptr<ISafeModeCallback>& existing) {
            return existing->asBinder() == targetBinder;
        });

    if (it != mCallbacks.end()) {
        mCallbacks.erase(it);
        ALOGI("unregisterCallback: removed. Active callbacks: %zu", mCallbacks.size());
    } else {
        ALOGD("unregisterCallback: callback not found (already removed or never registered)");
    }

    return ::ndk::ScopedAStatus::ok();
}

/**
 * getVersion — returns the service API version integer.
 */
::ndk::ScopedAStatus SafeModeService::getVersion(int32_t* out_version) {
    *out_version = SERVICE_VERSION;
    return ::ndk::ScopedAStatus::ok();
}

}  // namespace aidl::com::myoem::safemode
