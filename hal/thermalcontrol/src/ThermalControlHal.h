// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// ThermalControlHal.h — Concrete HAL implementation for Raspberry Pi 5.
// Internal to libthermalcontrolhal; not exported.

#pragma once

#include <thermalcontrol/IThermalControlHal.h>

#include <cstdint>
#include <string>

namespace myoem::thermalcontrol {

class ThermalControlHal : public IThermalControlHal {
public:
    /**
     * Constructor discovers the hwmon sysfs path at init time.
     * If no suitable hwmon node is found, all fan operations become no-ops
     * that return safe defaults. Temperature reads still work.
     */
    ThermalControlHal();

    float   getCpuTemperatureCelsius() override;
    int32_t getFanSpeedRpm()           override;
    int32_t getFanSpeedPercent()       override;
    bool    isFanRunning()             override;
    bool    isAutoMode()               override;

    bool setFanEnabled(bool enabled)    override;
    bool setFanSpeed(int32_t percent)   override;
    bool setAutoMode(bool autoMode)     override;

private:
    std::string mHwmonPath;  // e.g. "/sys/class/hwmon/hwmon0" — empty if not found
    bool        mAvailable;  // false when hwmon discovery failed
};

}  // namespace myoem::thermalcontrol
