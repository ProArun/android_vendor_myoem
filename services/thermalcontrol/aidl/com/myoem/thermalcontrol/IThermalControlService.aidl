// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalcontrol;

@VintfStability
interface IThermalControlService {
    // ── Read operations ──────────────────────────────────────────────────────

    /** Returns CPU temperature in Celsius (e.g. 42.5). */
    float getCpuTemperatureCelsius();

    /** Returns current fan speed in RPM. Returns -1 if tachometer not available. */
    int getFanSpeedRpm();

    /** Returns true if fan is currently running (PWM > 0). */
    boolean isFanRunning();

    /** Returns current fan speed as a percentage 0–100. */
    int getFanSpeedPercent();

    /** Returns true if fan is in automatic (kernel-controlled) mode. */
    boolean isFanAutoMode();

    // ── Write operations ─────────────────────────────────────────────────────

    /** Turn fan fully on (100%) or fully off (0%). Exits auto mode. */
    void setFanEnabled(boolean enabled);

    /**
     * Set fan speed manually. speedPercent must be 0–100.
     * Exits auto mode and applies the specified PWM duty cycle.
     * Throws ServiceSpecificException(ERROR_INVALID_SPEED) if out of range.
     */
    void setFanSpeed(int speedPercent);

    /**
     * Hand control back to the kernel thermal governor (autoMode=true),
     * or switch to manual mode (autoMode=false).
     */
    void setFanAutoMode(boolean autoMode);

    // ── Error codes ──────────────────────────────────────────────────────────
    const int ERROR_HAL_UNAVAILABLE = 1;  // sysfs path not found at init
    const int ERROR_INVALID_SPEED   = 2;  // speedPercent not in 0–100
    const int ERROR_SYSFS_WRITE     = 3;  // write to sysfs failed
}
