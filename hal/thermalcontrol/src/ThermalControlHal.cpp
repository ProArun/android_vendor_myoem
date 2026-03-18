#define LOG_TAG "libthermalcontrolhal"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#include "ThermalControlHal.h"
#include "SysfsHelper.h"

#include <log/log.h>

namespace myoem::thermalcontrol {

// CPU temperature: millidegrees Celsius (divide by 1000 to get °C)
static constexpr const char* kCpuTempPath = "/sys/class/thermal/thermal_zone0/temp";

// Kernel PWM range for the RPi5 pwm-fan driver
static constexpr int kPwmMax = 255;

// pwm1_enable values
static constexpr int kPwmEnableManual = 1;
static constexpr int kPwmEnableAuto   = 2;

// ─────────────────────────────────────────────────────────────────────────────

ThermalControlHal::ThermalControlHal() {
    mHwmonPath = discoverHwmonPath();
    mAvailable = !mHwmonPath.empty();

    if (mAvailable) {
        ALOGI("ThermalControlHal: initialized — hwmon at '%s'", mHwmonPath.c_str());
    } else {
        ALOGE("ThermalControlHal: fan hwmon not available — "
              "fan control disabled, temperature still readable");
    }
}

// ── Read operations ───────────────────────────────────────────────────────────

float ThermalControlHal::getCpuTemperatureCelsius() {
    // The kernel reports temperature in millidegrees (e.g. 45000 = 45.0 °C)
    int millideg = sysfsReadInt(kCpuTempPath, 0);
    return static_cast<float>(millideg) / 1000.0f;
}

int32_t ThermalControlHal::getFanSpeedRpm() {
    if (!mAvailable) return -1;
    // fan1_input may not exist if the tachometer wire is not connected
    return sysfsReadInt(mHwmonPath + "/fan1_input", -1);
}

int32_t ThermalControlHal::getFanSpeedPercent() {
    if (!mAvailable) return 0;
    int pwm = sysfsReadInt(mHwmonPath + "/pwm1", 0);
    // Map 0–255 → 0–100, rounding up to avoid returning 0 for a running fan
    return (pwm * 100 + kPwmMax - 1) / kPwmMax;
}

bool ThermalControlHal::isFanRunning() {
    if (!mAvailable) return false;
    return sysfsReadInt(mHwmonPath + "/pwm1", 0) > 0;
}

bool ThermalControlHal::isAutoMode() {
    if (!mAvailable) return true;  // treat unavailable as auto (safe default)
    return sysfsReadInt(mHwmonPath + "/pwm1_enable", kPwmEnableAuto) == kPwmEnableAuto;
}

// ── Write operations ──────────────────────────────────────────────────────────

bool ThermalControlHal::setFanEnabled(bool enabled) {
    if (!mAvailable) {
        ALOGE("setFanEnabled: hwmon not available");
        return false;
    }
    // Switch to manual mode so the kernel does not override our value
    if (!sysfsWriteInt(mHwmonPath + "/pwm1_enable", kPwmEnableManual)) {
        return false;
    }
    int pwmVal = enabled ? kPwmMax : 0;
    bool ok = sysfsWriteInt(mHwmonPath + "/pwm1", pwmVal);
    ALOGD("setFanEnabled(%s) pwm=%d %s",
          enabled ? "true" : "false", pwmVal, ok ? "OK" : "FAILED");
    return ok;
}

bool ThermalControlHal::setFanSpeed(int32_t percent) {
    if (!mAvailable) {
        ALOGE("setFanSpeed: hwmon not available");
        return false;
    }
    if (percent < 0 || percent > 100) {
        ALOGE("setFanSpeed: invalid percent=%d (must be 0–100)", percent);
        return false;
    }
    // Switch to manual mode so the kernel does not override our value
    if (!sysfsWriteInt(mHwmonPath + "/pwm1_enable", kPwmEnableManual)) {
        return false;
    }
    // Map 0–100 → 0–255
    int pwmVal = (percent * kPwmMax) / 100;
    bool ok = sysfsWriteInt(mHwmonPath + "/pwm1", pwmVal);
    ALOGD("setFanSpeed(%d%%) pwm=%d %s", percent, pwmVal, ok ? "OK" : "FAILED");
    return ok;
}

bool ThermalControlHal::setAutoMode(bool autoMode) {
    if (!mAvailable) {
        ALOGE("setAutoMode: hwmon not available");
        return false;
    }
    int mode = autoMode ? kPwmEnableAuto : kPwmEnableManual;
    bool ok = sysfsWriteInt(mHwmonPath + "/pwm1_enable", mode);
    ALOGD("setAutoMode(%s) enable=%d %s",
          autoMode ? "true" : "false", mode, ok ? "OK" : "FAILED");
    return ok;
}

// ── Factory ───────────────────────────────────────────────────────────────────

std::unique_ptr<IThermalControlHal> createThermalControlHal() {
    return std::make_unique<ThermalControlHal>();
}

}  // namespace myoem::thermalcontrol
