// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#define LOG_TAG "bmid"

#include "BMIService.h"
#include <log/log.h>

namespace aidl::com::myoem::bmi {

// BMI = weight(kg) / (height(m) * height(m))
ndk::ScopedAStatus BMIService::getBMI(
        float height, float weight, float* _aidl_return) {
    if (height <= 0.0f || weight <= 0.0f) {
        ALOGE("getBMI: invalid input — height=%.2f weight=%.2f", height, weight);
        return ndk::ScopedAStatus::fromServiceSpecificError(
                IBMIService::ERROR_INVALID_INPUT);
    }
    *_aidl_return = weight / (height * height);
    ALOGD("getBMI(height=%.2f, weight=%.2f) = %.2f", height, weight, *_aidl_return);
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::com::myoem::bmi
