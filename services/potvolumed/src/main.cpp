#define LOG_TAG "potvolumed"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
//
// potvolumed — Potentiometer Volume Daemon
//
// Polls MCP3008 ADC at 20Hz via SPI, filters the readings, and injects
// KEY_VOLUMEUP / KEY_VOLUMEDOWN events through a uinput virtual device
// to control Android media volume.

#include <log/log.h>

#include <csignal>      // signal(), SIGTERM, SIGINT
#include <atomic>       // std::atomic<bool>
#include <chrono>       // std::chrono::milliseconds
#include <thread>       // std::this_thread::sleep_for

#include "SpiReader.h"
#include "AdcFilter.h"
#include "VolumeMapper.h"
#include "VolumeController.h"

// spidev0.0 = standard SPI0 on the 40-pin GPIO header:
//   CLK=GPIO11(pin23), MOSI=GPIO10(pin19), MISO=GPIO9(pin21), CE0=GPIO8(pin24)
// This is the device to use when the MCP3008 is wired to the standard header.
// spidev10.0 also exists but maps to a different controller (not header pins).
// dtparam=spi=on in /boot/config.txt must be active for spidev0.0 to appear.
static constexpr char kSpiDevice[]    = "/dev/spidev0.0";
static constexpr int  kAdcChannel     = 0;
static constexpr int  kPollIntervalMs = 50;   // 20 Hz

static std::atomic<bool> gRunning{true};

static void onSignal(int /*signum*/) {
    gRunning = false;
}

int main() {
    ALOGI("potvolumed starting (spi=%s ch=%d poll=%dms)",
          kSpiDevice, kAdcChannel, kPollIntervalMs);

    signal(SIGTERM, onSignal);
    signal(SIGINT,  onSignal);

    SpiReader        spi(kSpiDevice, kAdcChannel);
    AdcFilter        filter;
    VolumeMapper     mapper;
    VolumeController controller;

    // Open the SPI device. Fails if /dev/spidev0.0 is not present or
    // permissions are wrong (check RC file chown / SELinux denials).
    if (!spi.open()) {
        ALOGE("Cannot open SPI device %s — daemon cannot start", kSpiDevice);
        return 1;
    }

    // Open the uinput virtual device. Fails if /dev/uinput is not accessible
    // or SELinux blocks it. Check: adb logcat | grep "avc: denied"
    if (!controller.open()) {
        ALOGE("Cannot open uinput device — daemon cannot start");
        return 1;
    }

    ALOGI("SPI and uinput ready. Entering poll loop.");

    while (gRunning) {
        int raw = spi.read();
        if (raw < 0) {
            ALOGE("SPI read error — skipping cycle");
            std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
            continue;
        }

        bool changed = false;
        int  stable  = filter.update(raw, &changed);

        if (changed) {
            int index = mapper.toVolumeIndex(stable);
            controller.setVolume(index);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }

    ALOGI("potvolumed shutting down cleanly");
    return 0;
}
