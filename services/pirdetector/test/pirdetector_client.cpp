// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// pirdetector_client — CLI test client for pirdetectord
//
// Usage on device:
//   adb shell /vendor/bin/pirdetector_client
//
// What it does:
//   1. Connects to pirdetectord via ServiceManager
//   2. Calls getVersion() and getCurrentState() (synchronous)
//   3. Registers a callback to receive real-time motion events
//   4. Prints each event as it arrives
//   5. Runs until Ctrl+C
//
// This validates the full AIDL callback path without needing the Java app.

// ─── LOG_TAG must be first ────────────────────────────────────────────────────
#define LOG_TAG "pirdetector_client"

#include <log/log.h>

#include <aidl/com/myoem/pirdetector/BnPirDetectorCallback.h>
#include <aidl/com/myoem/pirdetector/IPirDetectorService.h>
#include <aidl/com/myoem/pirdetector/MotionEvent.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <stdio.h>
#include <signal.h>
#include <atomic>

using namespace aidl::com::myoem::pirdetector;

// ─── Signal handling ──────────────────────────────────────────────────────────
static std::atomic<bool> gRunning{true};
static void handleSignal(int /*sig*/) { gRunning.store(false); }

// ─── Callback implementation ─────────────────────────────────────────────────
//
// BnPirDetectorCallback is the generated "Bn" base class for the callback.
// We implement onMotionEvent() — this is called by the service on each GPIO edge.
// Since the interface is "oneway", this runs on a Binder thread in our process.
class TestCallback : public BnPirDetectorCallback {
public:
    ::ndk::ScopedAStatus onMotionEvent(const MotionEvent& event) override {
        if (event.motionState == 1) {
            printf("[CALLBACK] MOTION DETECTED  — timestamp: %lld ns\n",
                   static_cast<long long>(event.timestampNs));
        } else {
            printf("[CALLBACK] MOTION ENDED     — timestamp: %lld ns\n",
                   static_cast<long long>(event.timestampNs));
        }
        fflush(stdout);
        return ::ndk::ScopedAStatus::ok();
    }
};

int main() {
    signal(SIGINT,  handleSignal);
    signal(SIGTERM, handleSignal);

    printf("pirdetector_client starting...\n");

    // ── Step 1: Start Binder thread pool ─────────────────────────────────────
    // We need at least one thread to receive the oneway callback calls.
    ABinderProcess_setThreadPoolMaxThreadCount(1);
    ABinderProcess_startThreadPool();

    // ── Step 2: Get the service from ServiceManager ───────────────────────────
    static constexpr const char* kServiceName =
        "com.myoem.pirdetector.IPirDetectorService";

    printf("Waiting for service '%s'...\n", kServiceName);

    // AServiceManager_waitForService blocks until the service is registered.
    // Useful during boot when the client starts before the daemon.
    ::ndk::SpAIBinder binder(AServiceManager_waitForService(kServiceName));
    if (!binder.get()) {
        fprintf(stderr, "Failed to get service '%s'\n", kServiceName);
        return 1;
    }

    std::shared_ptr<IPirDetectorService> service =
        IPirDetectorService::fromBinder(binder);
    if (!service) {
        fprintf(stderr, "fromBinder() returned null\n");
        return 1;
    }

    printf("Connected to pirdetectord\n");

    // ── Step 3: Query version ─────────────────────────────────────────────────
    int32_t version = 0;
    auto status = service->getVersion(&version);
    if (!status.isOk()) {
        fprintf(stderr, "getVersion() failed: %s\n", status.getDescription().c_str());
        return 1;
    }
    printf("Service version: %d\n", version);

    // ── Step 4: Query current state ───────────────────────────────────────────
    int32_t currentState = 0;
    status = service->getCurrentState(&currentState);
    if (!status.isOk()) {
        fprintf(stderr, "getCurrentState() failed: %s\n", status.getDescription().c_str());
        return 1;
    }
    printf("Current PIR state: %s (%d)\n",
           currentState == 1 ? "MOTION DETECTED" : "NO MOTION",
           currentState);

    // ── Step 5: Register callback ─────────────────────────────────────────────
    auto callback = ::ndk::SharedRefBase::make<TestCallback>();

    status = service->registerCallback(callback);
    if (!status.isOk()) {
        fprintf(stderr, "registerCallback() failed: %s\n", status.getDescription().c_str());
        return 1;
    }
    printf("Callback registered. Listening for motion events (Ctrl+C to exit)...\n\n");

    // ── Step 6: Wait for events ───────────────────────────────────────────────
    while (gRunning.load()) {
        sleep(1);
    }

    // ── Step 7: Clean up ──────────────────────────────────────────────────────
    printf("\nUnregistering callback...\n");
    status = service->unregisterCallback(callback);
    if (!status.isOk()) {
        fprintf(stderr, "unregisterCallback() failed: %s\n", status.getDescription().c_str());
    }

    printf("pirdetector_client done.\n");
    return 0;
}
