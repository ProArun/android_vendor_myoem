// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// IThermalControlHal.h — Pure virtual HAL interface for CPU temperature and fan control.
//
// This interface is the only thing the service layer knows about. The concrete
// implementation (ThermalControlHal) is a vendor-internal detail. Swap the .cpp
// to support a different board without changing any other layer.

#pragma once

#include <cstdint>
#include <memory>

namespace myoem::thermalcontrol {

class IThermalControlHal {
public:
    virtual ~IThermalControlHal() = default;

    // ── Read operations ───────────────────────────────────────────────────────

    /**
     * Returns CPU temperature in degrees Celsius (e.g. 42.5f).
     * Returns 0.0f if the thermal sysfs node is not readable.
     */
    virtual float getCpuTemperatureCelsius() = 0;

    /**
     * Returns the current fan speed in RPM.
     * Returns -1 if the tachometer input is not available or wired.
     */
    virtual int32_t getFanSpeedRpm() = 0;

    /**
     * Returns the current fan speed as a percentage (0–100).
     * Maps the kernel PWM value (0–255) to 0–100.
     * Returns 0 if the hwmon node is not available.
     */
    virtual int32_t getFanSpeedPercent() = 0;

    /**
     * Returns true if the fan is currently spinning (PWM > 0).
     */
    virtual bool isFanRunning() = 0;

    /**
     * Returns true if the kernel thermal governor is in control of the fan
     * (pwm1_enable == 2). Returns true also when hwmon is unavailable
     * (no-op — nothing to override).
     */
    virtual bool isAutoMode() = 0;

    // ── Write operations ──────────────────────────────────────────────────────

    /**
     * Turn the fan fully on (PWM = 255) or fully off (PWM = 0).
     * Switches the kernel to manual mode (pwm1_enable = 1) first.
     * Returns false if the sysfs write fails or hwmon is unavailable.
     */
    virtual bool setFanEnabled(bool enabled) = 0;

    /**
     * Set fan speed to a specific percentage (0–100).
     * Switches the kernel to manual mode (pwm1_enable = 1) first.
     * Returns false on bad input, sysfs write failure, or unavailability.
     */
    virtual bool setFanSpeed(int32_t percent) = 0;

    /**
     * Hand fan control to the kernel thermal governor (autoMode = true,
     * sets pwm1_enable = 2) or keep in manual mode (autoMode = false,
     * sets pwm1_enable = 1 without changing the current PWM value).
     */
    virtual bool setAutoMode(bool autoMode) = 0;
};

/**
 * Factory function — the only way to create a concrete HAL instance.
 * Returns a ThermalControlHal ready for use. The service layer never
 * needs to know about ThermalControlHal.h.
 */
std::unique_ptr<IThermalControlHal> createThermalControlHal();

}  // namespace myoem::thermalcontrol
