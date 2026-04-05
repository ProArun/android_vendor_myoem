// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "calculatord"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "CalculatorService.h"

// VINTF AIDL HAL name format: <interface_descriptor>/<instance>
// The "/default" suffix + the VINTF manifest fragment in vintf/calculatord.xml
// together allow system-partition callers (e.g. BMICalculatorA JNI) to reach
// this vendor service. The service manager sets VINTF stability on the proxy
// automatically when the name matches a manifest entry.
static constexpr const char* kServiceName =
        "com.myoem.calculator.ICalculatorService/default";

int main() {
    ALOGI("calculatord starting");

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto service = ndk::SharedRefBase::make<
            aidl::com::myoem::calculator::CalculatorService>();

    binder_status_t status = AServiceManager_addService(
            service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("addService('%s') failed: %d", kServiceName, status);
        return 1;
    }

    ALOGI("calculatord registered as '%s'", kServiceName);

    ABinderProcess_joinThreadPool();
    return 0;
}
