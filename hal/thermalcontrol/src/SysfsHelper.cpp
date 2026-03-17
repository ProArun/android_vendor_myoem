#define LOG_TAG "libthermalcontrolhal"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#include "SysfsHelper.h"

#include <log/log.h>

#include <fstream>
#include <string>
#include <sys/stat.h>

namespace myoem::thermalcontrol {

int sysfsReadInt(const std::string& path, int defaultVal) {
    std::ifstream f(path);
    if (!f.is_open()) {
        ALOGW("sysfsReadInt: cannot open '%s'", path.c_str());
        return defaultVal;
    }
    int val;
    f >> val;
    if (f.fail()) {
        ALOGW("sysfsReadInt: parse error in '%s'", path.c_str());
        return defaultVal;
    }
    return val;
}

float sysfsReadFloat(const std::string& path, float defaultVal) {
    std::ifstream f(path);
    if (!f.is_open()) {
        ALOGW("sysfsReadFloat: cannot open '%s'", path.c_str());
        return defaultVal;
    }
    float val;
    f >> val;
    if (f.fail()) {
        ALOGW("sysfsReadFloat: parse error in '%s'", path.c_str());
        return defaultVal;
    }
    return val;
}

bool sysfsWriteInt(const std::string& path, int value) {
    std::ofstream f(path);
    if (!f.is_open()) {
        ALOGE("sysfsWriteInt: cannot open '%s' for writing", path.c_str());
        return false;
    }
    f << value << "\n";
    if (f.fail()) {
        ALOGE("sysfsWriteInt: write failed to '%s'", path.c_str());
        return false;
    }
    return true;
}

std::string discoverHwmonPath() {
    // Try hwmon0 through hwmon15. The RPi5 cooling fan (pwm-fan driver)
    // exposes a pwm1 file in its hwmon directory. The index is not fixed
    // across kernel versions or boot order, so we scan dynamically.
    for (int i = 0; i < 16; i++) {
        std::string dir  = "/sys/class/hwmon/hwmon" + std::to_string(i);
        std::string pwm1 = dir + "/pwm1";

        struct stat st{};
        if (stat(pwm1.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
            ALOGI("discoverHwmonPath: fan hwmon found at '%s'", dir.c_str());
            return dir;
        }
    }
    ALOGE("discoverHwmonPath: no hwmon with 'pwm1' found in /sys/class/hwmon/");
    return "";
}

}  // namespace myoem::thermalcontrol
