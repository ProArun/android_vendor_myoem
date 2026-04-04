#define LOG_TAG "hwcalculatord"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "HwCalculatorService.h"

static constexpr const char* kServiceName =
        "com.myoem.hwcalculator.IHwCalculatorService";

int main() {
    ALOGI("hwcalculatord starting");

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto service = ndk::SharedRefBase::make<
            aidl::com::myoem::hwcalculator::HwCalculatorService>();

    binder_status_t status = AServiceManager_addService(
            service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("addService('%s') failed: %d", kServiceName, status);
        return 1;
    }

    ALOGI("hwcalculatord registered as '%s'", kServiceName);

    ABinderProcess_joinThreadPool();
    return 0;
}