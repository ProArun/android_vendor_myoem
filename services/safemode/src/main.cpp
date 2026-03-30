// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

// LOG_TAG must be the first line — before any #include
#define LOG_TAG "safemoded"

// NDK Binder process/thread pool management
#include <android/binder_process.h>

// NDK ServiceManager — AServiceManager_addService() registers our service
#include <android/binder_manager.h>

// ALOGI, ALOGE
#include <log/log.h>

// Our service implementation
#include "SafeModeService.h"

/**
 * main() — entry point for the safemoded binary.
 *
 * Execution flow:
 *
 *  1. Configure the NDK Binder thread pool.
 *     ABinderProcess_setThreadPoolMaxThreadCount(N) reserves N threads.
 *     These threads handle:
 *       - Incoming IPC calls from clients (getCurrentData)
 *       - Incoming VHAL callbacks (onPropertyEvent from the VHAL server)
 *     4 threads is sufficient: 1-2 for clients + 1-2 for VHAL events.
 *
 *  2. Start the thread pool.
 *     ABinderProcess_startThreadPool() spawns the worker threads.
 *     The main thread continues executing — it is not consumed yet.
 *
 *  3. Create and initialise the service.
 *     SafeModeService::connectToVhal() blocks until VHAL is available and
 *     subscribes to the 3 vehicle properties. If this fails, we exit.
 *
 *  4. Register with ServiceManager.
 *     AServiceManager_addService() makes the service discoverable by name.
 *     Java/Kotlin clients call ServiceManager.getService() with this exact name.
 *
 *     NOTE: Do NOT call AIBinder_markVintfStability() here manually — the AIDL
 *     compiler generates that call at class-registration time when the
 *     aidl_interface uses stability:"vintf". Calling it again here causes a
 *     FATAL crash ("already marked as vendor stability").
 *
 *  5. Block the main thread in the Binder thread pool.
 *     ABinderProcess_joinThreadPool() makes the main thread available for
 *     handling Binder transactions. The process now runs indefinitely.
 *     This call never returns (unless the process is killed).
 *
 * WHY NDK Binder (libbinder_ndk):
 *   libbinder_ndk.so is LLNDK — it lives only in /system/lib64/ and uses
 *   the SYSTEM libbinder.so internally. The Parcel kHeader ('SYST') matches
 *   what Java clients write, so Java → NDK transactions work correctly.
 *
 *   A vendor cc_binary using cpp libbinder loads the VENDOR copy
 *   (kHeader='VNDR'). Java writes 'SYST' → enforceInterface fails →
 *   STATUS_BAD_TYPE → IllegalArgumentException on the Java side.
 *   NDK avoids this entirely.
 *
 * VINTF STABILITY:
 *   cc_binary { vendor: true } compiles with -D__ANDROID_VENDOR__, which makes
 *   binder objects in this process get VENDOR stability by default.
 *   System processes (Java app) cannot call VENDOR-stability binders.
 *   stability:"vintf" in Android.bp makes the AIDL compiler emit
 *   AIBinder_markVintfStability() at class-registration time, upgrading all
 *   service binders to VINTF stability (cross-partition level) automatically.
 */
int main() {
    ALOGI("safemoded starting (version %d)", 1);

    // ── Step 1 & 2: Set up NDK Binder thread pool ─────────────────────────────
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    ALOGI("NDK Binder thread pool started (max 4 threads)");

    // ── Step 3: Create the service and connect to the data source ────────────
    // SharedRefBase::make<> is the NDK equivalent of new + sp<>.
    std::shared_ptr<::aidl::com::myoem::safemode::SafeModeService> service =
        ::ndk::SharedRefBase::make<::aidl::com::myoem::safemode::SafeModeService>();

    // ── Data source selection (compile-time) ─────────────────────────────────
    //
    // SAFEMODE_SIM_MODE (RPi5 dev):
    //   Reads /data/local/tmp/safemode_sim.txt written by Python simulator.
    //   No VHAL connection. Same dispatchToCallbacks() path as production.
    //   Enable: -DSAFEMODE_SIM_MODE in Android.bp cflags (already set below).
    //
    // Production (real vehicle / real VHAL):
    //   Remove -DSAFEMODE_SIM_MODE from Android.bp cflags.
    //   connectToVhal() blocks until VHAL appears, subscribes, and reads initial
    //   values. No other code changes needed.
    //
#ifdef SAFEMODE_SIM_MODE
    if (!service->startSimulator()) {
        ALOGE("startSimulator() failed — exiting");
        return 1;
    }
#else
    // connectToVhal() blocks until VHAL is available, then subscribes.
    // Exit hard on failure — without VHAL we have no data source.
    if (!service->connectToVhal()) {
        ALOGE("connectToVhal() failed — exiting");
        return 1;
    }
#endif

    // ── Step 4: Register with ServiceManager ──────────────────────────────────
    // Service name MUST match in:
    //   - This file
    //   - vintf/manifest.xml  (VINTF declaration — required for VINTF stability)
    //   - sepolicy/private/service_contexts  (SELinux label)
    //   - safemode_library/SafeModeManager.kt (client lookup)
    //
    // VINTF format: "package.InterfaceName/instance"
    //   package   = com.myoem.safemode
    //   interface = ISafeModeService
    //   instance  = default
    static constexpr const char* kServiceName =
        "com.myoem.safemode.ISafeModeService/default";

    // stability:"vintf" in Android.bp causes the AIDL compiler to emit
    // AIBinder_markVintfStability() at class-registration time — no manual call needed.
    binder_status_t status = AServiceManager_addService(
        service->asBinder().get(), kServiceName);
    if (status != STATUS_OK) {
        ALOGE("AServiceManager_addService('%s') failed: %d", kServiceName, status);
        return 1;
    }
    ALOGI("SafeModeService registered as '%s'", kServiceName);

    // ── Step 5: Block the main thread and serve IPC calls forever ─────────────
    ALOGI("safemoded ready — waiting for clients");
    ABinderProcess_joinThreadPool();

    // Unreachable unless the process is explicitly killed.
    return 0;
}
