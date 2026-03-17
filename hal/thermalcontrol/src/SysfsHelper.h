// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// SysfsHelper.h — Low-level sysfs file I/O helpers.
// Internal to libthermalcontrolhal; not exported.

#pragma once

#include <cstdint>
#include <string>

namespace myoem::thermalcontrol {

/**
 * Read a single integer from a sysfs file.
 * Returns defaultVal if the file cannot be opened or parsed.
 */
int sysfsReadInt(const std::string& path, int defaultVal = -1);

/**
 * Read a single float from a sysfs file.
 * Returns defaultVal if the file cannot be opened or parsed.
 */
float sysfsReadFloat(const std::string& path, float defaultVal = -1.0f);

/**
 * Write a single integer to a sysfs file followed by newline.
 * Returns true on success, false on open or write failure.
 */
bool sysfsWriteInt(const std::string& path, int value);

/**
 * Scan /sys/class/hwmon/hwmon0..hwmon15 and return the first directory
 * that contains a "pwm1" file (the RPi5 cooling fan hwmon node).
 * Returns an empty string if no matching node is found.
 */
std::string discoverHwmonPath();

}  // namespace myoem::thermalcontrol
