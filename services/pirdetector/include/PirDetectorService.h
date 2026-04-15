// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0
#pragma once

// ─── Generated AIDL stubs (NDK backend) ───────────────────────────────────────
// BnPirDetectorService — the generated "Bn" (Binder-native) base class.
// We inherit from it and override the pure-virtual AIDL methods.
// The NDK backend is used because pirdetectord is a vendor binary:
//   - libbinder_ndk links against system libbinder.so (kHeader='SYST')
//   - This is compatible with Java clients via the same libbinder.so
//   - The cpp backend would use vendor libbinder.so (kHeader='VNDR') → incompatible
#include <aidl/com/myoem/pirdetector/BnPirDetectorService.h>
#include <aidl/com/myoem/pirdetector/IPirDetectorCallback.h>
#include <aidl/com/myoem/pirdetector/MotionEvent.h>

// NDK Binder utilities: ndk::ScopedAStatus, ndk::ScopedAIBinder_DeathRecipient
#include <android/binder_auto_utils.h>

// GPIO HAL — static library from vendor/myoem/hal/pirdetector/
#include "pirdetector/GpioHal.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace aidl::com::myoem::pirdetector {

/**
 * PirDetectorService — the C++ implementation of IPirDetectorService.
 *
 * ── Lifecycle ─────────────────────────────────────────────────────────────────
 *   1. main() creates the service: SharedRefBase::make<PirDetectorService>()
 *   2. main() calls service->start(chipPath, lineOffset)
 *      → GpioHal::init() opens /dev/gpiochip4, requests GPIO17 for edge events
 *      → EventThread spawned: loops on GpioHal::waitForEdge()
 *   3. main() registers with ServiceManager: AServiceManager_addService()
 *   4. main() calls ABinderProcess_joinThreadPool() — blocks serving Binder calls
 *
 * ── EventThread ───────────────────────────────────────────────────────────────
 * A dedicated std::thread calls GpioHal::waitForEdge() in a loop.
 * waitForEdge() blocks inside poll() — zero CPU usage while idle.
 * On a GPIO edge interrupt:
 *   1. Updates mCurrentState (atomic — no lock needed for reads)
 *   2. Builds MotionEvent AIDL parcelable
 *   3. Iterates mCallbacks (under mCallbacksMutex) calling cb->onMotionEvent()
 *      — returns immediately because the callback is "oneway"
 *   4. Loops back to waitForEdge() ready for the next edge
 *
 * ── Client lifecycle & DeathRecipient ────────────────────────────────────────
 * When a client registers its callback, we attach a DeathRecipient to its Binder.
 * If the client process crashes, the DeathRecipient fires and removes the callback
 * from mCallbacks automatically. This prevents accumulation of dead binders and
 * the associated memory/CPU overhead of trying to call them.
 *
 * ── Thread safety ─────────────────────────────────────────────────────────────
 * mCallbacks is protected by mCallbacksMutex.
 * mCurrentState is std::atomic<int> — reads/writes without locking.
 * EventThread and Binder thread pool both access mCallbacks (under the mutex).
 */
class PirDetectorService : public BnPirDetectorService {
public:
    PirDetectorService();
    ~PirDetectorService() override;

    /**
     * Initializes the GPIO HAL and starts the EventThread.
     * Must be called once from main() before registering with ServiceManager.
     *
     * @param chipPath    path to gpiochip device (e.g. "/dev/gpiochip4")
     * @param lineOffset  GPIO line number (BCM numbering, e.g. 17 for GPIO17)
     * @return            true on success, false if GPIO init failed
     */
    bool start(const std::string& chipPath, int lineOffset);

    // ── IPirDetectorService AIDL methods — overrides from BnPirDetectorService ─

    /** Returns the current GPIO line state: 1=motion, 0=no motion. */
    ::ndk::ScopedAStatus getCurrentState(int32_t* out) override;

    /** Subscribe to receive onMotionEvent() callbacks on each GPIO edge. */
    ::ndk::ScopedAStatus registerCallback(
        const std::shared_ptr<IPirDetectorCallback>& callback) override;

    /** Remove a previously registered callback. */
    ::ndk::ScopedAStatus unregisterCallback(
        const std::shared_ptr<IPirDetectorCallback>& callback) override;

    /** Returns the service API version (currently 1). */
    ::ndk::ScopedAStatus getVersion(int32_t* out) override;

private:
    // ── EventThread entry point ───────────────────────────────────────────────
    // Loops on GpioHal::waitForEdge() until shutdown().
    void eventLoop();

    // Sends event to all registered callbacks (caller must NOT hold mCallbacksMutex).
    void notifyCallbacks(const MotionEvent& event);

    // ── DeathRecipient ────────────────────────────────────────────────────────
    // Called by Binder when a registered client process dies.
    // Cookie is the raw pointer of the callback shared_ptr element in mCallbacks.
    // (We store it in a struct to avoid raw pointer aliasing issues.)
    struct CallbackEntry {
        std::shared_ptr<IPirDetectorCallback> callback;
    };

    void onClientDied(CallbackEntry* entry);

    // Static trampoline required by AIBinder_DeathRecipient C API.
    static void onClientDiedStatic(void* cookie);

    // ── GPIO HAL ──────────────────────────────────────────────────────────────
    ::myoem::pirdetector::GpioHal mGpioHal;

    // ── EventThread ───────────────────────────────────────────────────────────
    std::thread mEventThread;

    // Current GPIO line state (1=HIGH/motion, 0=LOW/no motion).
    // Updated atomically by EventThread; read by getCurrentState() on Binder threads.
    std::atomic<int> mCurrentState{0};

    // ── Callback registry ─────────────────────────────────────────────────────
    std::mutex mCallbacksMutex;

    // Each entry owns the callback shared_ptr and is allocated on the heap so
    // its address (used as cookie for DeathRecipient) remains stable.
    std::vector<CallbackEntry*> mCallbackEntries;

    // One DeathRecipient shared by all registered callbacks.
    // Allocated in the constructor, lives for the service lifetime.
    ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;
};

}  // namespace aidl::com::myoem::pirdetector
