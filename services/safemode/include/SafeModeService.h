// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

#pragma once

// ─── Our AIDL-generated C++ stubs (NDK backend) ──────────────────────────────
// BnSafeModeService = "Binder Native" base class — we inherit from this to
// implement the server side. The "Bn" prefix is the AIDL naming convention.
//
// NDK backend headers live at <aidl/com/myoem/safemode/...>.
// libbinder_ndk is LLNDK: a single binary shared between system and vendor.
// It uses the SYSTEM libbinder under the hood (same kHeader='SYST' as Java).
// This makes Java ↔ NDK transactions work correctly.
#include <aidl/com/myoem/safemode/BnSafeModeService.h>
#include <aidl/com/myoem/safemode/ISafeModeCallback.h>
#include <aidl/com/myoem/safemode/VehicleData.h>

// ─── VHAL AIDL stubs (direct HAL access) ────────────────────────────────────
// IVehicle — the main VHAL interface we subscribe to
// BnVehicleCallback — "Binder Native" base for receiving VHAL push events
#include <aidl/android/hardware/automotive/vehicle/IVehicle.h>
#include <aidl/android/hardware/automotive/vehicle/BnVehicleCallback.h>
#include <aidl/android/hardware/automotive/vehicle/VehiclePropValue.h>
#include <aidl/android/hardware/automotive/vehicle/SubscribeOptions.h>
// V3 wraps batched results in parcelable structs (not raw vectors):
//   VehiclePropValues  { VehiclePropValue[] payloads; }  — push events
//   VehiclePropErrors  { VehiclePropError[] errors;   }  — set errors
//   GetValueResults    { GetValueResult[]  payloads;  }  — async get responses
//   SetValueResults    { SetValueResult[]  payloads;  }  — async set responses
#include <aidl/android/hardware/automotive/vehicle/VehiclePropValues.h>
#include <aidl/android/hardware/automotive/vehicle/VehiclePropErrors.h>
#include <aidl/android/hardware/automotive/vehicle/GetValueResults.h>
#include <aidl/android/hardware/automotive/vehicle/SetValueResults.h>
#include <aidl/android/hardware/automotive/vehicle/GetValueRequests.h>

// ─── Standard C++ ────────────────────────────────────────────────────────────
#include <mutex>
#include <vector>
#include <memory>

#ifdef SAFEMODE_SIM_MODE
#include <atomic>
#include <thread>
#endif

namespace aidl::com::myoem::safemode {

// ─────────────────────────────────────────────────────────────────────────────
// Forward declaration of the inner VHAL callback class.
// It is defined inside SafeModeService to keep it an implementation detail.
// ─────────────────────────────────────────────────────────────────────────────
class SafeModeService;

/**
 * VhalEventCallback — receives raw property events from VHAL.
 *
 * This is an inner helper, not part of the public ISafeModeService interface.
 * It is registered with IVehicle::subscribe() at startup. When VHAL pushes a
 * SPEED, GEAR, or FUEL event, onPropertyEvent() is called on a VHAL thread.
 * It forwards the event to SafeModeService::onVhalEvent() for processing.
 *
 * Inherits from BnVehicleCallback — the auto-generated Binder NDK base class
 * for the IVehicleCallback AIDL interface defined in the VHAL HAL package.
 */
class VhalEventCallback :
    public ::aidl::android::hardware::automotive::vehicle::BnVehicleCallback {
public:
    // Parent is stored as a raw pointer (not shared_ptr) to avoid a circular
    // reference: SafeModeService owns this callback, callback points back.
    explicit VhalEventCallback(SafeModeService* parent);

    // Called by VHAL when subscribed property values change.
    // propValues.payloads may contain events for multiple properties in one batch.
    //
    // V3 API change vs older VHAL versions:
    //   OLD (pre-V3): onPropertyEvent(vector<VehiclePropValue>, int64_t timestamp)
    //   NEW (V3):     onPropertyEvent(VehiclePropValues, int32_t sharedMemoryFileCount)
    //
    // VehiclePropValues is a parcelable wrapper: { VehiclePropValue[] payloads; }
    // sharedMemoryFileCount: number of shared-memory files for large batches (we ignore it).
    ::ndk::ScopedAStatus onPropertyEvent(
        const ::aidl::android::hardware::automotive::vehicle::VehiclePropValues& propValues,
        int32_t sharedMemoryFileCount) override;

    // Called by VHAL when a property set request fails.
    // VehiclePropErrors is a parcelable wrapper: { VehiclePropError[] errors; }
    // We don't set any properties, so this is always a no-op for us.
    ::ndk::ScopedAStatus onPropertySetError(
        const ::aidl::android::hardware::automotive::vehicle::VehiclePropErrors& errors) override;

    // ── V3-only async get/set response callbacks ──────────────────────────────
    // In VHAL V3, IVehicle::getValues() and setValues() are ASYNCHRONOUS.
    // Instead of blocking and returning results directly, you pass a callback
    // and results arrive here. Both are pure virtual in IVehicleCallback — we
    // MUST implement them even if unused, or VhalEventCallback is abstract.
    //
    // onGetValues: called with results from IVehicle::getValues(callback, requests).
    //   We use this to process the initial property read done in connectToVhal().
    ::ndk::ScopedAStatus onGetValues(
        const ::aidl::android::hardware::automotive::vehicle::GetValueResults& results) override;

    // onSetValues: called with results from IVehicle::setValues(callback, requests).
    //   We never SET any VHAL properties, so this is a no-op. Must be implemented
    //   because it is pure virtual in IVehicleCallback.
    ::ndk::ScopedAStatus onSetValues(
        const ::aidl::android::hardware::automotive::vehicle::SetValueResults& results) override;

private:
    SafeModeService* mParent;   // Not owned — parent outlives this callback
};

// ─────────────────────────────────────────────────────────────────────────────
/**
 * SafeModeService — implements ISafeModeService AIDL interface (NDK backend).
 *
 * Responsibilities:
 *   1. Connect to VHAL at startup and subscribe to 3 properties.
 *   2. Maintain mCurrentData — the latest snapshot from VHAL.
 *   3. Dispatch mCurrentData to all registered ISafeModeCallback clients
 *      whenever any subscribed property changes.
 *   4. Serve getCurrentData() synchronously for newly-connected clients.
 *   5. Manage the callback list (register / unregister).
 *
 * Threading model:
 *   - VHAL events arrive on a Binder thread from the VHAL process.
 *   - Client IPC calls (registerCallback, getCurrentData) also arrive on
 *     Binder threads from the client process.
 *   - mMutex protects mCurrentData and mCallbacks from concurrent access.
 *   - Callbacks are dispatched OUTSIDE the lock to avoid deadlock
 *     (if a callback tries to call back into us while we hold the lock).
 *
 * Backend note:
 *   libbinder_ndk is LLNDK — it lives only in /system/lib64/ and uses the
 *   SYSTEM libbinder.so internally. This means the Parcel kHeader ('SYST')
 *   matches what Java clients write, so Java → NDK transactions work correctly.
 */
class SafeModeService : public BnSafeModeService {
public:
    SafeModeService();
    ~SafeModeService();

    // ── ISafeModeService AIDL implementation ──────────────────────────────────

    /** Returns the latest vehicle data snapshot (synchronous, lock-protected). */
    ::ndk::ScopedAStatus getCurrentData(VehicleData* out_data) override;

    /** Adds callback to mCallbacks list. Idempotent (safe to call twice). */
    ::ndk::ScopedAStatus registerCallback(
        const std::shared_ptr<ISafeModeCallback>& callback) override;

    /** Removes callback from mCallbacks list. Safe if never registered. */
    ::ndk::ScopedAStatus unregisterCallback(
        const std::shared_ptr<ISafeModeCallback>& callback) override;

    /** Returns SERVICE_VERSION constant. */
    ::ndk::ScopedAStatus getVersion(int32_t* out_version) override;

    // ── Called by VhalEventCallback ───────────────────────────────────────────

    /**
     * Processes a single VHAL property event.
     * Updates mCurrentData and dispatches to all registered callbacks.
     * Called on a VHAL Binder thread — must be thread-safe.
     */
    void onVhalEvent(
        const ::aidl::android::hardware::automotive::vehicle::VehiclePropValue& propValue);

    // ── Called by main.cpp during startup ────────────────────────────────────

    /**
     * Connects to VHAL and subscribes to SPEED, GEAR, and FUEL properties.
     * Returns true on success, false if VHAL is unreachable.
     * Must be called before the service is registered with ServiceManager.
     *
     * NOT called in SAFEMODE_SIM_MODE — startSimulator() is used instead.
     */
    bool connectToVhal();

#ifdef SAFEMODE_SIM_MODE
    /**
     * RPi5 dev mode: starts the simulator thread which reads from SIM_FILE.
     *
     * The Python simulator scripts write one line to SIM_FILE every 5 seconds:
     *   "<speed_ms> <gear_int> <fuel_ml>"
     *
     * The thread reads the file every 500 ms, parses the values, updates
     * mCurrentData, and dispatches to callbacks — exactly the same path as
     * real VHAL events. No VHAL connection is made in this mode.
     *
     * PRODUCTION: remove -DSAFEMODE_SIM_MODE from Android.bp cflags and
     * call connectToVhal() instead. Zero other code changes needed.
     */
    bool startSimulator();
#endif

private:
    // ── Internal helpers ──────────────────────────────────────────────────────

    /**
     * Sends the current data snapshot to all registered callbacks.
     * MUST be called WITHOUT holding mMutex (callbacks can call back into us).
     *
     * If a callback throws (client process died), it is silently removed from
     * the list. The next dispatch will skip dead callbacks automatically.
     */
    void dispatchToCallbacks(const VehicleData& data);

    // ── State ─────────────────────────────────────────────────────────────────

    static constexpr int32_t SERVICE_VERSION = 1;

    // Handle to VHAL. Initialized in connectToVhal(). Null if VHAL not found.
    std::shared_ptr<::aidl::android::hardware::automotive::vehicle::IVehicle> mVhal;

    // Our VHAL callback object. Registered with IVehicle::subscribe().
    // Kept alive here so it is not destroyed while VHAL still holds a reference.
    std::shared_ptr<VhalEventCallback> mVhalCallback;

    // Protects both mCurrentData and mCallbacks.
    // Held briefly — never held while dispatching callbacks.
    std::mutex mMutex;

    // The most recent aggregated vehicle data snapshot.
    // Updated piecemeal as individual VHAL events arrive.
    VehicleData mCurrentData;

    // List of active client callbacks. Populated by registerCallback(),
    // emptied by unregisterCallback() and cleaned up on dead-client errors.
    std::vector<std::shared_ptr<ISafeModeCallback>> mCallbacks;

#ifdef SAFEMODE_SIM_MODE
    // ── Simulator-only state (RPi5 dev mode) ─────────────────────────────────
    // Path to the file that Python scripts write vehicle data into.
    // Format: "<speed_ms> <gear_int> <fuel_ml>" — one line, space-separated.
    static constexpr const char* SIM_FILE = "/data/local/tmp/safemode_sim.txt";

    std::atomic<bool> mSimRunning{false};
    std::thread       mSimThread;

    void runSimulatorThread();
#endif
};

}  // namespace aidl::com::myoem::safemode
