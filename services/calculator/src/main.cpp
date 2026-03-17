// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "calculatord"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "CalculatorService.h"

static constexpr const char* kServiceName =
        "com.myoem.calculator.ICalculatorService";

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
