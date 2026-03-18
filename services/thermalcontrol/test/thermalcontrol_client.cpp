#define LOG_TAG "thermalcontrol_client"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// CLI test client for thermalcontrold.
//
// Usage:
//   thermalcontrol_client temp          — print CPU temperature
//   thermalcontrol_client rpm           — print fan RPM
//   thermalcontrol_client percent       — print fan speed percent
//   thermalcontrol_client running       — print whether fan is running
//   thermalcontrol_client auto_status   — print whether fan is in auto mode
//   thermalcontrol_client on            — turn fan fully on
//   thermalcontrol_client off           — turn fan off
//   thermalcontrol_client speed <N>     — set fan speed to N% (0–100)
//   thermalcontrol_client auto          — return fan to kernel auto mode
//   thermalcontrol_client manual        — set fan to manual mode (no speed change)

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <aidl/com/myoem/thermalcontrol/IThermalControlService.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using aidl::com::myoem::thermalcontrol::IThermalControlService;

static constexpr const char* kServiceName =
        "com.myoem.thermalcontrol.IThermalControlService/default";

static void printUsage(const char* prog) {
    fprintf(stderr,
            "Usage: %s <command> [args]\n"
            "Commands:\n"
            "  temp          Print CPU temperature in °C\n"
            "  rpm           Print fan speed in RPM (-1 = not available)\n"
            "  percent       Print fan speed as percentage (0–100)\n"
            "  running       Print whether fan is currently running\n"
            "  auto_status   Print whether fan is in auto mode\n"
            "  on            Turn fan fully on (exits auto mode)\n"
            "  off           Turn fan off (exits auto mode)\n"
            "  speed <N>     Set fan speed to N%% (0–100, exits auto mode)\n"
            "  auto          Return fan to kernel auto mode\n"
            "  manual        Switch to manual mode (no speed change)\n",
            prog);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Clients don't receive calls — no worker threads needed
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    ndk::SpAIBinder binder(AServiceManager_checkService(kServiceName));
    if (binder.get() == nullptr) {
        fprintf(stderr, "ERROR: thermalcontrold not running (service '%s' not found)\n",
                kServiceName);
        return 1;
    }

    auto svc = IThermalControlService::fromBinder(binder);

    const std::string cmd(argv[1]);

    if (cmd == "temp") {
        float temp = 0.0f;
        auto status = svc->getCpuTemperatureCelsius(&temp);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("CPU temperature: %.1f °C\n", temp);

    } else if (cmd == "rpm") {
        int32_t rpm = 0;
        auto status = svc->getFanSpeedRpm(&rpm);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        if (rpm < 0) {
            printf("Fan RPM: N/A (tachometer not wired)\n");
        } else {
            printf("Fan RPM: %d\n", rpm);
        }

    } else if (cmd == "percent") {
        int32_t pct = 0;
        auto status = svc->getFanSpeedPercent(&pct);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("Fan speed: %d%%\n", pct);

    } else if (cmd == "running") {
        bool running = false;
        auto status = svc->isFanRunning(&running);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("Fan running: %s\n", running ? "yes" : "no");

    } else if (cmd == "auto_status") {
        bool isAuto = false;
        auto status = svc->isFanAutoMode(&isAuto);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("Fan mode: %s\n", isAuto ? "AUTO (kernel controlled)" : "MANUAL");

    } else if (cmd == "on") {
        auto status = svc->setFanEnabled(true);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("Fan turned ON (100%%)\n");

    } else if (cmd == "off") {
        auto status = svc->setFanEnabled(false);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("Fan turned OFF\n");

    } else if (cmd == "speed") {
        if (argc < 3) {
            fprintf(stderr, "ERROR: 'speed' requires a percentage argument (0–100)\n");
            return 1;
        }
        int pct = atoi(argv[2]);
        auto status = svc->setFanSpeed(pct);
        if (!status.isOk()) {
            int errCode = status.getServiceSpecificError();
            if (errCode == IThermalControlService::ERROR_INVALID_SPEED) {
                fprintf(stderr, "ERROR: Invalid speed %d (must be 0–100)\n", pct);
            } else {
                fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            }
            return 1;
        }
        printf("Fan speed set to %d%%\n", pct);

    } else if (cmd == "auto") {
        auto status = svc->setFanAutoMode(true);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("Fan returned to AUTO mode (kernel controlled)\n");

    } else if (cmd == "manual") {
        auto status = svc->setFanAutoMode(false);
        if (!status.isOk()) {
            fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
            return 1;
        }
        printf("Fan switched to MANUAL mode\n");

    } else {
        fprintf(stderr, "ERROR: Unknown command '%s'\n", cmd.c_str());
        printUsage(argv[0]);
        return 1;
    }

    return 0;
}
