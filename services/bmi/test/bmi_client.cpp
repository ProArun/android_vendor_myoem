// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// Usage: bmi_client <height_m> <weight_kg>

#define LOG_TAG "bmi_client"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <aidl/com/myoem/bmi/IBMIService.h>
#include <log/log.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>

using aidl::com::myoem::bmi::IBMIService;

static std::shared_ptr<IBMIService> getService() {
    ndk::SpAIBinder binder(AServiceManager_checkService(
            "com.myoem.bmi.IBMIService/default"));
    if (binder.get() == nullptr) {
        std::cerr << "ERROR: bmid not found. Is it running?\n"
                  << "       adb shell service list | grep bmi\n";
        return nullptr;
    }
    return IBMIService::fromBinder(binder);
}

static const char* bmiCategory(float bmi) {
    if (bmi < 18.5f) return "Underweight";
    if (bmi < 25.0f) return "Normal weight";
    if (bmi < 30.0f) return "Overweight";
    return "Obese";
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <height_m> <weight_kg>\n"
                  << "  e.g. bmi_client 1.75 70.0\n";
        return 1;
    }

    float height = std::atof(argv[1]);
    float weight = std::atof(argv[2]);

    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<IBMIService> svc = getService();
    if (svc == nullptr) return 1;

    float bmi = 0.0f;
    ndk::ScopedAStatus status = svc->getBMI(height, weight, &bmi);

    if (!status.isOk()) {
        std::cerr << "ERROR: " << status.getDescription() << "\n";
        return 1;
    }

    std::cout << std::fixed << std::setprecision(2)
              << "Height : " << height << " m\n"
              << "Weight : " << weight << " kg\n"
              << "BMI    : " << bmi    << "\n"
              << "Status : " << bmiCategory(bmi) << "\n";
    return 0;
}
