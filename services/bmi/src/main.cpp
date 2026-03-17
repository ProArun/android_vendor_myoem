// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "bmid"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "BMIService.h"

static constexpr const char* kServiceName = "com.myoem.bmi.IBMIService";

int main() {
    ALOGI("bmid starting");

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto service = ndk::SharedRefBase::make<aidl::com::myoem::bmi::BMIService>();

    binder_status_t status = AServiceManager_addService(
            service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("addService('%s') failed: %d", kServiceName, status);
        return 1;
    }

    ALOGI("bmid registered as '%s'", kServiceName);

    ABinderProcess_joinThreadPool();
    return 0;
}
