// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

// ─── LOG_TAG must be the very first line before ANY #include ──────────────────
#define LOG_TAG "pirdetectord"

#include "PirDetectorService.h"

#include <log/log.h>
#include <android/binder_ibinder.h>  // AIBinder_DeathRecipient_new, AIBinder_linkToDeath

namespace aidl::com::myoem::pirdetector {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

PirDetectorService::PirDetectorService()
    // Create the DeathRecipient once, shared by all client callbacks.
    // onClientDiedStatic is a static C-style function (required by the NDK C API).
    : mDeathRecipient(AIBinder_DeathRecipient_new(PirDetectorService::onClientDiedStatic)) {
    ALOGI("PirDetectorService created");
}

PirDetectorService::~PirDetectorService() {
    ALOGI("PirDetectorService shutting down");

    // Signal the EventThread to exit its waitForEdge() blocking call.
    mGpioHal.shutdown();

    // Wait for the EventThread to finish cleanly before destroying members.
    // Without this join(), the thread might access mGpioHal or mCallbackEntries
    // after they are destroyed — undefined behaviour.
    if (mEventThread.joinable()) {
        mEventThread.join();
        ALOGI("EventThread joined");
    }

    // Clean up all remaining callback entries (clients that didn't unregister)
    std::lock_guard<std::mutex> lock(mCallbacksMutex);
    for (CallbackEntry* entry : mCallbackEntries) {
        delete entry;
    }
    mCallbackEntries.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// start() — initialize GPIO HAL and spawn the EventThread
// ─────────────────────────────────────────────────────────────────────────────
bool PirDetectorService::start(const std::string& chipPath, int lineOffset) {
    ALOGI("Initializing GPIO HAL: chip=%s line=%d", chipPath.c_str(), lineOffset);

    if (!mGpioHal.init(chipPath, lineOffset)) {
        ALOGE("GpioHal::init() failed — pirdetectord cannot start");
        return false;
    }

    // Spawn the EventThread.
    // std::thread captures 'this' by pointer — the service object outlives the
    // thread because the destructor joins before any member is destroyed.
    mEventThread = std::thread([this]() { eventLoop(); });

    ALOGI("EventThread started, service ready");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// eventLoop() — the heart of the callback-driven design
//
// This runs on a dedicated thread (mEventThread). It calls waitForEdge() which
// blocks in poll() until a hardware GPIO interrupt fires.
//
// On each edge:
//   1. Determine the new state (1=rising/motion, 0=falling/ended)
//   2. Update mCurrentState atomically
//   3. Build the MotionEvent parcelable
//   4. Call notifyCallbacks() — fires all oneway callbacks, returns immediately
//   5. Loop back to waitForEdge() ready for the next edge
//
// The loop exits when mGpioHal.shutdown() is called (from the destructor),
// which causes waitForEdge() to return false via the self-pipe mechanism.
// ─────────────────────────────────────────────────────────────────────────────
void PirDetectorService::eventLoop() {
    ALOGI("EventThread running — blocking on GPIO edge events");

    ::myoem::pirdetector::GpioEvent gpioEvent;

    while (mGpioHal.waitForEdge(gpioEvent)) {
        // Translate HAL edge to motion state
        int newState = (gpioEvent.edge == ::myoem::pirdetector::EdgeType::RISING) ? 1 : 0;

        // Update atomically — getCurrentState() reads this without a lock
        mCurrentState.store(newState, std::memory_order_relaxed);

        ALOGI("GPIO edge: %s → motionState=%d",
              (newState == 1) ? "RISING (motion detected)" : "FALLING (motion ended)",
              newState);

        // Build AIDL parcelable
        MotionEvent event;
        event.motionState = newState;
        event.timestampNs = gpioEvent.timestampNs;

        // Notify all registered callbacks (fire-and-forget, no blocking)
        notifyCallbacks(event);
    }

    ALOGI("EventThread exiting");
}

// ─────────────────────────────────────────────────────────────────────────────
// notifyCallbacks() — deliver MotionEvent to all registered clients
//
// Takes the mutex, iterates the list, fires each callback.
// Since IPirDetectorCallback is "oneway", each ->onMotionEvent() call returns
// immediately — the Binder runtime enqueues the call in the client's thread pool.
//
// Dead callbacks (returned an error status) are removed from the list here as a
// secondary safety net (primary removal is via DeathRecipient).
// ─────────────────────────────────────────────────────────────────────────────
void PirDetectorService::notifyCallbacks(const MotionEvent& event) {
    std::lock_guard<std::mutex> lock(mCallbacksMutex);

    for (auto it = mCallbackEntries.begin(); it != mCallbackEntries.end(); ) {
        CallbackEntry* entry = *it;
        ::ndk::ScopedAStatus status = entry->callback->onMotionEvent(event);

        if (!status.isOk()) {
            ALOGW("Callback returned error (%d) — removing dead callback",
                  status.getStatus());
            delete entry;
            it = mCallbackEntries.erase(it);
        } else {
            ++it;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// AIDL method: getCurrentState
//
// Returns the last known GPIO state (1 or 0) immediately, without waiting for
// the next edge. Called by the manager once on connect so the UI shows the
// correct state right away.
// ─────────────────────────────────────────────────────────────────────────────
::ndk::ScopedAStatus PirDetectorService::getCurrentState(int32_t* out) {
    *out = mCurrentState.load(std::memory_order_relaxed);
    ALOGD("getCurrentState() → %d", *out);
    return ::ndk::ScopedAStatus::ok();
}

// ─────────────────────────────────────────────────────────────────────────────
// AIDL method: registerCallback
//
// Adds the client's callback to mCallbackEntries and attaches a DeathRecipient.
// The DeathRecipient ensures the callback is removed automatically if the client
// process crashes (avoids accumulating dead binders).
//
// Idempotent: if the same callback Binder is registered twice, we skip the
// second registration (check by comparing AIBinder* pointers).
// ─────────────────────────────────────────────────────────────────────────────
::ndk::ScopedAStatus PirDetectorService::registerCallback(
        const std::shared_ptr<IPirDetectorCallback>& callback) {
    if (!callback) {
        ALOGE("registerCallback: null callback rejected");
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    std::lock_guard<std::mutex> lock(mCallbacksMutex);

    // Idempotency check: compare raw AIBinder* pointers
    AIBinder* incomingBinder = callback->asBinder().get();
    for (const CallbackEntry* entry : mCallbackEntries) {
        if (entry->callback->asBinder().get() == incomingBinder) {
            ALOGD("registerCallback: callback already registered, ignoring");
            return ::ndk::ScopedAStatus::ok();
        }
    }

    // Allocate a heap-stable entry — its address serves as the cookie
    // for AIBinder_linkToDeath so onClientDiedStatic can find it.
    CallbackEntry* entry = new CallbackEntry{callback};
    mCallbackEntries.push_back(entry);

    // Attach DeathRecipient to this client's Binder.
    // The cookie (entry*) is passed back to onClientDiedStatic when the client dies.
    binder_status_t linkStatus = AIBinder_linkToDeath(
        callback->asBinder().get(),
        mDeathRecipient.get(),
        static_cast<void*>(entry));

    if (linkStatus != STATUS_OK) {
        // Client already dead — remove immediately
        ALOGW("registerCallback: AIBinder_linkToDeath failed (%d), client already dead?",
              linkStatus);
        mCallbackEntries.pop_back();
        delete entry;
        return ::ndk::ScopedAStatus::fromServiceSpecificError(linkStatus);
    }

    ALOGI("registerCallback: registered new client (total=%zu)", mCallbackEntries.size());
    return ::ndk::ScopedAStatus::ok();
}

// ─────────────────────────────────────────────────────────────────────────────
// AIDL method: unregisterCallback
//
// Removes the callback from mCallbackEntries, unlinks the DeathRecipient,
// and frees the entry. Safe to call even if the callback was never registered.
// ─────────────────────────────────────────────────────────────────────────────
::ndk::ScopedAStatus PirDetectorService::unregisterCallback(
        const std::shared_ptr<IPirDetectorCallback>& callback) {
    if (!callback) {
        return ::ndk::ScopedAStatus::ok();  // no-op
    }

    std::lock_guard<std::mutex> lock(mCallbacksMutex);

    AIBinder* targetBinder = callback->asBinder().get();
    for (auto it = mCallbackEntries.begin(); it != mCallbackEntries.end(); ++it) {
        CallbackEntry* entry = *it;
        if (entry->callback->asBinder().get() == targetBinder) {
            // Unlink the death recipient before deleting the entry.
            // This prevents onClientDiedStatic from firing with a dangling cookie.
            AIBinder_unlinkToDeath(
                targetBinder,
                mDeathRecipient.get(),
                static_cast<void*>(entry));

            mCallbackEntries.erase(it);
            delete entry;

            ALOGI("unregisterCallback: removed client (remaining=%zu)",
                  mCallbackEntries.size());
            return ::ndk::ScopedAStatus::ok();
        }
    }

    ALOGD("unregisterCallback: callback not found (already removed or never registered)");
    return ::ndk::ScopedAStatus::ok();
}

// ─────────────────────────────────────────────────────────────────────────────
// AIDL method: getVersion
// ─────────────────────────────────────────────────────────────────────────────
::ndk::ScopedAStatus PirDetectorService::getVersion(int32_t* out) {
    *out = 1;
    return ::ndk::ScopedAStatus::ok();
}

// ─────────────────────────────────────────────────────────────────────────────
// DeathRecipient — static trampoline required by the NDK C API
//
// AIBinder_DeathRecipient requires a C function pointer: void(*)(void*).
// We store 'this' (PirDetectorService*) encoded in the cookie alongside the
// CallbackEntry* so we can dispatch to the member function.
//
// Here we encode only the CallbackEntry* in the cookie. To reach the service
// we use a global/static approach: since there is exactly one PirDetectorService
// instance, we pass the entry pointer and use the lambda approach that captures
// the service via the entry's parent reference.
//
// Simpler alternative used here: a static map isn't needed because the entry
// itself holds the callback, and cleanup just needs to find & remove it from
// the vector. We walk mCallbackEntries to find the matching entry.
// ─────────────────────────────────────────────────────────────────────────────

// We need access to the service instance from the static callback.
// Store a weak_ptr in the cookie alongside the entry.
// To avoid complexity, we use a two-pointer struct stored in the entry itself.

// Simpler design: cookie = CallbackEntry*, service pointer stored in a
// thread-local or static — but that's fragile. Better: put the service
// pointer in a wrapper struct. We'll use a static function that knows
// how to reach the service via the entry (the entry has a pointer back).

// Actually the cleanest approach: make CallbackEntry hold a pointer to the service.
// We set it in registerCallback.

// NOTE: The header declares onClientDiedStatic(void* cookie) and onClientDied(CallbackEntry*).
// In registerCallback we stored the entry* as cookie.
// In onClientDiedStatic we cast cookie → CallbackEntry* and find the service via
// a pointer we embed in CallbackEntry. Let's patch the approach by storing
// PirDetectorService* in the entry (done here, extending the CallbackEntry usage).

// Since CallbackEntry is private to the impl and cookie=entry*, we walk the
// service's list to find and remove it. We need a reference back to the service.
// Easiest: store the service pointer in CallbackEntry at registration time.
// CallbackEntry is defined in the header as: { shared_ptr<callback> }.
// We can add: PirDetectorService* service; to that struct — but the header is
// already written. Instead, we use a global weak_ptr (acceptable for a singleton).

// ── Singleton weak-reference (safe for a vendor daemon with one instance) ────
// Not static — accessed by the global-namespace setPirDetectorServiceRef() below.
std::weak_ptr<PirDetectorService> gServiceWeakRef;

/*static*/
void PirDetectorService::onClientDiedStatic(void* cookie) {
    // cookie = CallbackEntry* set in registerCallback.
    // We lock the service and remove this entry from the list.
    auto svc = gServiceWeakRef.lock();
    if (!svc) {
        ALOGW("onClientDiedStatic: service already gone");
        return;
    }
    CallbackEntry* entry = static_cast<CallbackEntry*>(cookie);
    svc->onClientDied(entry);
}

void PirDetectorService::onClientDied(CallbackEntry* entry) {
    std::lock_guard<std::mutex> lock(mCallbacksMutex);
    for (auto it = mCallbackEntries.begin(); it != mCallbackEntries.end(); ++it) {
        if (*it == entry) {
            mCallbackEntries.erase(it);
            delete entry;
            ALOGI("onClientDied: dead client removed (remaining=%zu)",
                  mCallbackEntries.size());
            return;
        }
    }
    ALOGW("onClientDied: entry not found (already removed?)");
}

}  // namespace aidl::com::myoem::pirdetector

// ── Global-namespace free function — declared in main.cpp ────────────────────
// Must be outside the AIDL namespace so main.cpp can declare and call it without
// a namespace qualifier. gServiceWeakRef lives in the namespace above; the lambda
// captures it by reference through the static storage duration.
void setPirDetectorServiceRef(
        std::shared_ptr<aidl::com::myoem::pirdetector::PirDetectorService> svc) {
    aidl::com::myoem::pirdetector::gServiceWeakRef = svc;
}
