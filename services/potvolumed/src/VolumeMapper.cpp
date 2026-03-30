#define LOG_TAG "potvolumed"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#include "VolumeMapper.h"

#include <algorithm>    // std::max, std::min
#include <cmath>        // std::round()
#include <log/log.h>    // ALOGD

int VolumeMapper::toVolumeIndex(int adcValue) const {
    // Clamp the input to the valid ADC range [0, 1023].
    // Under normal operation adcValue is already in range, but defensive
    // clamping prevents any edge case (e.g. SPI glitch returning 1024+).
    adcValue = std::max(0, std::min(adcValue, kAdcMax));

    // Linear interpolation from [0, kAdcMax] → [0, kVolumeMax]:
    //   ratio = adcValue / 1023.0   (0.0 to 1.0)
    //   index = round(ratio × 15)   (0 to 15)
    //
    // Cast to float first to avoid integer division truncation.
    // Example: adcValue=680, ratio=0.665, index=round(9.97)=10
    float ratio = static_cast<float>(adcValue) / static_cast<float>(kAdcMax);
    int index   = static_cast<int>(std::round(ratio * static_cast<float>(kVolumeMax)));

    // Defensive clamp on output (round() should never exceed range, but be safe)
    index = std::max(0, std::min(index, kVolumeMax));

    ALOGD("VolumeMapper: adc=%d → ratio=%.3f → index=%d/%d",
          adcValue, ratio, index, kVolumeMax);

    return index;
}
