// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// Usage: calculator_client <add|sub|mul|div> <a> <b>

#define LOG_TAG "calculator_client"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <aidl/com/myoem/calculator/ICalculatorService.h>
#include <log/log.h>

#include <cstdlib>
#include <cstring>
#include <iostream>

using aidl::com::myoem::calculator::ICalculatorService;

static std::shared_ptr<ICalculatorService> getService() {
    ndk::SpAIBinder binder(AServiceManager_checkService(
            "com.myoem.calculator.ICalculatorService/default"));
    if (binder.get() == nullptr) {
        std::cerr << "ERROR: calculatord not found. Is it running?\n"
                  << "       adb shell service list | grep calculator\n";
        return nullptr;
    }
    return ICalculatorService::fromBinder(binder);
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <add|sub|mul|div> <a> <b>\n"
                  << "  e.g. calculator_client add 10 3\n";
        return 1;
    }

    const char* op = argv[1];
    int32_t a = std::atoi(argv[2]);
    int32_t b = std::atoi(argv[3]);

    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<ICalculatorService> svc = getService();
    if (svc == nullptr) return 1;

    int32_t result = 0;
    ndk::ScopedAStatus status;

    if (std::strcmp(op, "add") == 0)      status = svc->add(a, b, &result);
    else if (std::strcmp(op, "sub") == 0) status = svc->subtract(a, b, &result);
    else if (std::strcmp(op, "mul") == 0) status = svc->multiply(a, b, &result);
    else if (std::strcmp(op, "div") == 0) status = svc->divide(a, b, &result);
    else {
        std::cerr << "ERROR: unknown operation '" << op << "'\n";
        return 1;
    }

    if (!status.isOk()) {
        std::cerr << "ERROR: " << status.getDescription() << "\n";
        return 1;
    }

    std::cout << a << " " << op << " " << b << " = " << result << "\n";
    return 0;
}
