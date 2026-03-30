// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

// LOG_TAG first — always
#define LOG_TAG "safemode_client"

// NDK Binder API for looking up services
#include <android/binder_manager.h>
// NDK Binder process/thread setup
#include <android/binder_process.h>

// ALOGI, ALOGE
#include <log/log.h>

// Our AIDL-generated client stubs
#include <aidl/com/myoem/safemode/ISafeModeService.h>
#include <aidl/com/myoem/safemode/ISafeModeCallback.h>
#include <aidl/com/myoem/safemode/BnSafeModeCallback.h>
#include <aidl/com/myoem/safemode/VehicleData.h>

#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

using namespace aidl::com::myoem::safemode;

// Service name — must match kServiceName in main.cpp
static constexpr const char* kServiceName = "com.myoem.safemode.ISafeModeService/default";

// ─────────────────────────────────────────────────────────────────────────────
/**
 * TestCallback — a simple ISafeModeCallback implementation for the test client.
 *
 * Prints received vehicle data to stdout so we can visually verify the service
 * is dispatching events correctly.
 *
 * Inherits from BnSafeModeCallback — the "Binder Native" base class generated
 * from ISafeModeCallback.aidl. BnSafeModeCallback handles Binder serialisation;
 * we only override the business method.
 */
class TestCallback : public BnSafeModeCallback {
public:
    std::atomic<int> callCount{0};

    /**
     * Called by SafeModeService whenever vehicle data changes.
     * Prints the data and converts speed to km/h for readability.
     */
    ::ndk::ScopedAStatus onVehicleDataChanged(const VehicleData& data) override {
        callCount++;
        float kmh = data.speedMs * 3.6f;

        // Derive SafeModeState (same logic as the library — for display only)
        const char* state = "NO_SAFE_MODE";
        if      (kmh >= 15.0f) state = "HARD_SAFE_MODE";
        else if (kmh >= 5.0f)  state = "NORMAL_SAFE_MODE";

        std::cout << "[" << callCount.load() << "] "
                  << "Speed: " << kmh         << " km/h ("  << data.speedMs << " m/s) | "
                  << "Gear: 0x" << std::hex   << data.gear  << std::dec << " | "
                  << "Fuel: "   << data.fuelLevelMl << " ml | "
                  << "State: "  << state       << "\n"
                  << std::flush;
        return ::ndk::ScopedAStatus::ok();
    }
};

// ─────────────────────────────────────────────────────────────────────────────

static void printUsage(const char* progName) {
    std::cerr << "Usage: " << progName << " [snapshot|subscribe <sec>]\n"
              << "  snapshot        — print current vehicle data once and exit\n"
              << "  subscribe <sec> — subscribe to callbacks for <sec> seconds\n"
              << "\nExamples:\n"
              << "  adb shell /vendor/bin/safemode_client snapshot\n"
              << "  adb shell /vendor/bin/safemode_client subscribe 10\n"
              << "\nTo inject test values via VHAL:\n"
              << "  adb shell cmd car_service inject-vhal-event 0x11600207 0 5.56   # 20 km/h\n"
              << "  adb shell cmd car_service inject-vhal-event 0x11600207 0 13.89  # 50 km/h\n"
              << "  adb shell cmd car_service inject-vhal-event 0x11400400 0 8      # DRIVE\n"
              << "\nGear codes: NEUTRAL=1 REVERSE=2 PARK=4 DRIVE=8 GEAR1=16 GEAR2=32\n";
}

/**
 * getService — connects to SafeModeService via ServiceManager.
 *
 * AServiceManager_getService() is non-blocking — returns nullptr immediately
 * if the service is not registered. Use AServiceManager_waitForService() to
 * block until it appears (useful at boot time).
 */
static std::shared_ptr<ISafeModeService> getService() {
    ::ndk::SpAIBinder binder(AServiceManager_getService(kServiceName));
    if (binder.get() == nullptr) {
        std::cerr << "ERROR: Service '" << kServiceName << "' not found.\n"
                  << "  Is safemoded running? Try: adb shell ps -e | grep safemoded\n";
        return nullptr;
    }
    auto service = ISafeModeService::fromBinder(binder);
    if (service == nullptr) {
        std::cerr << "ERROR: Binder cast to ISafeModeService failed\n";
    }
    return service;
}

// ─────────────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    // Binder thread pool needed even in the client so we can receive callbacks.
    // Without startThreadPool(), the callback Binder can't be served.
    ABinderProcess_setThreadPoolMaxThreadCount(2);
    ABinderProcess_startThreadPool();

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];

    // ── Connect to service ────────────────────────────────────────────────────
    auto service = getService();
    if (!service) return 1;

    // ── Check service version ─────────────────────────────────────────────────
    int32_t version = 0;
    service->getVersion(&version);
    std::cout << "SafeModeService version: " << version << "\n";

    if (command == "snapshot") {
        // ── Snapshot mode: one synchronous read ──────────────────────────────
        VehicleData data;
        auto status = service->getCurrentData(&data);
        if (!status.isOk()) {
            std::cerr << "ERROR: getCurrentData() failed: "
                      << status.getDescription() << "\n";
            return 1;
        }

        float kmh  = data.speedMs * 3.6f;
        const char* state = "NO_SAFE_MODE";
        if      (kmh >= 15.0f) state = "HARD_SAFE_MODE";
        else if (kmh >= 5.0f)  state = "NORMAL_SAFE_MODE";

        std::cout << "\n── Vehicle Data Snapshot ──\n"
                  << "  Speed : " << kmh            << " km/h (" << data.speedMs << " m/s)\n"
                  << "  Gear  : 0x" << std::hex     << data.gear << std::dec    << "\n"
                  << "  Fuel  : " << data.fuelLevelMl << " ml\n"
                  << "  State : " << state           << "\n\n";

    } else if (command == "subscribe") {
        // ── Subscribe mode: register callback and wait ────────────────────────
        int durationSec = 30;  // default
        if (argc >= 3) {
            durationSec = std::stoi(argv[2]);
        }
        std::cout << "Subscribing to SafeModeService for " << durationSec << " seconds...\n"
                  << "Inject values with: adb shell cmd car_service inject-vhal-event 0x11600207 0 5.0\n\n";

        // Create our callback — this object is the server side of ISafeModeCallback.
        // SafeModeService will call onVehicleDataChanged() on it.
        auto callback = ::ndk::SharedRefBase::make<TestCallback>();

        auto regStatus = service->registerCallback(callback);
        if (!regStatus.isOk()) {
            std::cerr << "ERROR: registerCallback() failed: "
                      << regStatus.getDescription() << "\n";
            return 1;
        }
        std::cout << "Callback registered. Waiting for events...\n\n";

        // Wait for the specified duration, then clean up.
        std::this_thread::sleep_for(std::chrono::seconds(durationSec));

        service->unregisterCallback(callback);
        std::cout << "\nUnregistered callback. Total events received: "
                  << callback->callCount.load() << "\n";

    } else {
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
