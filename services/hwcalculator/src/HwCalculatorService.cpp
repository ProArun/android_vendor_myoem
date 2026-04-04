#define LOG_TAG "hwcalculatord"

#include "HwCalculatorService.h"
#include <log/log.h>

namespace aidl::com::myoem::hwcalculator {

ndk::ScopedAStatus HwCalculatorService::add(
        int32_t a, int32_t b, int32_t* _aidl_return) {
    *_aidl_return = a + b;
    ALOGD("add(%d, %d) = %d", a, b, *_aidl_return);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HwCalculatorService::subtract(
        int32_t a, int32_t b, int32_t* _aidl_return) {
    *_aidl_return = a - b;
    ALOGD("subtract(%d, %d) = %d", a, b, *_aidl_return);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HwCalculatorService::multiply(
        int32_t a, int32_t b, int32_t* _aidl_return) {
    *_aidl_return = a * b;
    ALOGD("multiply(%d, %d) = %d", a, b, *_aidl_return);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HwCalculatorService::divide(
        int32_t a, int32_t b, int32_t* _aidl_return) {
    if (b == 0) {
        ALOGE("divide(%d, 0): division by zero", a);
        return ndk::ScopedAStatus::fromServiceSpecificError(
                IHwCalculatorService::ERROR_DIVIDE_BY_ZERO);
    }
    *_aidl_return = a / b;
    ALOGD("divide(%d, %d) = %d", a, b, *_aidl_return);
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::com::myoem::hwcalculator
