#define LOG_TAG "thermalcontrold"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "ThermalControlService.h"

static constexpr const char* kServiceName =
        "com.myoem.thermalcontrol.IThermalControlService";

int main() {
    ALOGI("thermalcontrold starting");

    // Step 1: size the binder thread pool
    ABinderProcess_setThreadPoolMaxThreadCount(4);

    // Step 2: start worker threads
    ABinderProcess_startThreadPool();

    // Step 3: create service instance (HAL is created inside the constructor)
    auto service = ndk::SharedRefBase::make<
            aidl::com::myoem::thermalcontrol::ThermalControlService>();

    // Step 4: register with the system ServiceManager
    binder_status_t status = AServiceManager_addService(
            service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("addService('%s') failed: %d", kServiceName, status);
        return 1;
    }

    ALOGI("thermalcontrold registered as '%s'", kServiceName);

    // Step 5: block main thread — worker threads handle incoming calls
    ABinderProcess_joinThreadPool();

    return 0;  // unreachable
}
