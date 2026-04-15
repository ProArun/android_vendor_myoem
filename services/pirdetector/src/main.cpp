// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

// ─── LOG_TAG must be the very first line before ANY #include ──────────────────
#define LOG_TAG "pirdetectord"

#include <log/log.h>

// NDK Binder process and service manager APIs
#include <android/binder_manager.h>    // AServiceManager_addService
#include <android/binder_process.h>    // ABinderProcess_setThreadPoolMaxThreadCount
                                       // ABinderProcess_startThreadPool
                                       // ABinderProcess_joinThreadPool

#include "PirDetectorService.h"

// Declared in PirDetectorService.cpp — sets the global weak_ptr for DeathRecipient
void setPirDetectorServiceRef(std::shared_ptr<aidl::com::myoem::pirdetector::PirDetectorService> svc);

// ── GPIO configuration ────────────────────────────────────────────────────────
//
// GPIO chip path — on Raspberry Pi 5, the 40-pin header is driven by the RP1
// south bridge chip, which registers as a separate gpiochip from the BCM2712 SoC.
//
// VERIFY BEFORE FIRST BOOT:
//   adb shell ls /dev/gpiochip*
//   adb shell cat /sys/bus/gpio/devices/gpiochip4/label
//   Expected output: "pinctrl-rp1"   ← this is the 40-pin header controller
//
// VERIFIED on this build: /dev/gpiochip0 is the RP1 (pinctrl-rp1, 54 GPIOs).
//   adb shell cat /sys/class/gpio/gpiochip569/label  → "pinctrl-rp1"
//   adb shell ls /sys/class/gpio/gpiochip569/device/ → contains "gpiochip0" symlink
// The sysfs class numbering (gpiochip569) differs from the /dev/ numbering (gpiochip0).
// Only /dev/gpiochip* nodes exist as character devices — use /dev/gpiochip0.
//
// GPIO line offset — BCM numbering (not physical pin numbering):
//   GPIO17 = BCM 17 = physical pin 11 = the pin we wired the PIR sensor to.
static constexpr const char* kGpioChipPath = "/dev/gpiochip0";
static constexpr int         kPirGpioLine  = 17;

// ── ServiceManager name ───────────────────────────────────────────────────────
// VINTF-stable services MUST use "type/instance" format:
//   com.myoem.pirdetector.IPirDetectorService/default
//
// This matches the VINTF manifest fqname: IPirDetectorService/default
// The full descriptor prefix "com.myoem.pirdetector." is implied by the HAL name.
//
// Must match exactly:
//   1. This string in main.cpp
//   2. The VINTF manifest fqname in vintf/pirdetector.xml
//   3. The service_contexts SELinux label
//   4. PirDetectorManager.java SERVICE_NAME constant
static constexpr const char* kServiceName =
    "com.myoem.pirdetector.IPirDetectorService/default";

int main() {
    ALOGI("pirdetectord starting (GPIO chip=%s line=%d)", kGpioChipPath, kPirGpioLine);

    // ── Step 1: Configure the Binder thread pool ──────────────────────────────
    // 4 threads: enough to serve simultaneous registerCallback / unregisterCallback
    // calls from multiple client apps while the EventThread fires callbacks.
    // The EventThread itself is NOT a Binder thread — it is a plain std::thread.
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    // ── Step 2: Create the service ────────────────────────────────────────────
    // SharedRefBase::make<T>() creates the object with an internal ref-count
    // compatible with ndk::SpAIBinder (the Binder smart pointer).
    auto service = ::ndk::SharedRefBase::make<
        aidl::com::myoem::pirdetector::PirDetectorService>();

    // Register the global weak_ptr so DeathRecipient can find the service.
    // Must be done before start() spawns the EventThread.
    setPirDetectorServiceRef(service);

    // ── Step 3: Initialize GPIO HAL and start EventThread ────────────────────
    if (!service->start(kGpioChipPath, kPirGpioLine)) {
        ALOGE("Failed to initialize GPIO HAL — exiting");
        return 1;
    }

    // ── Step 4: Register with ServiceManager ─────────────────────────────────
    // AServiceManager_addService() requires:
    //   1. The service Binder (service->asBinder().get())
    //   2. The service name (must match VINTF manifest and service_contexts)
    //
    // WHY does this succeed for a VINTF-stable interface?
    //   The AIDL compiler (because of stability:"vintf") emits a call to
    //   AIBinder_markVintfStability() inside the generated BnPirDetectorService
    //   constructor. This upgrades the binder object to VINTF stability before
    //   we reach this line, so ServiceManager accepts the registration.
    //
    //   Calling AIBinder_markVintfStability() manually in main.cpp is NOT needed
    //   and would cause a FATAL crash (double-marking). Lesson from ThermalControl.
    binder_status_t status = AServiceManager_addService(
        service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("AServiceManager_addService('%s') failed with status %d",
              kServiceName, status);
        return 1;
    }

    ALOGI("pirdetectord registered as '%s' — joining thread pool", kServiceName);

    // ── Step 5: Block in the Binder thread pool ───────────────────────────────
    // This call never returns (until the process is killed).
    // The Binder thread pool handles incoming registerCallback / unregisterCallback
    // / getCurrentState / getVersion calls from clients.
    // The EventThread (started in step 3) runs concurrently on its own thread.
    ABinderProcess_joinThreadPool();

    // Unreachable — here for completeness
    return 0;
}
