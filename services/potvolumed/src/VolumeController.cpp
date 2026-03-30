#define LOG_TAG "potvolumed"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#include "VolumeController.h"

#include <fcntl.h>              // open(), O_WRONLY, O_NONBLOCK
#include <unistd.h>             // close(), write()
#include <sys/ioctl.h>          // ioctl()
#include <linux/uinput.h>       // UI_SET_EVBIT, UI_DEV_SETUP, struct uinput_setup, ...
#include <linux/input.h>        // EV_KEY, EV_SYN, KEY_VOLUMEUP, SYN_REPORT, struct input_event
#include <cerrno>               // errno
#include <cstring>              // strerror(), strncpy()
#include <cstdlib>              // abs()
#include <log/log.h>            // ALOGI, ALOGE

VolumeController::VolumeController()  = default;
VolumeController::~VolumeController() { close(); }

bool VolumeController::open() {
    // O_WRONLY:    we only write events to uinput (never read from it)
    // O_NONBLOCK:  don't block if the kernel input queue is full (defensive)
    mFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (mFd < 0) {
        ALOGE("open(/dev/uinput) failed: %s", strerror(errno));
        return false;
    }

    // UI_SET_EVBIT — tell the kernel which event types this device produces.
    // EV_KEY: the device sends key press/release events (like a keyboard).
    // EV_SYN: the device sends sync events to batch-flush event reports.
    //         EV_SYN is mandatory — InputReader requires it after every event.
    if (ioctl(mFd, UI_SET_EVBIT, EV_KEY) < 0 ||
        ioctl(mFd, UI_SET_EVBIT, EV_SYN) < 0) {
        ALOGE("UI_SET_EVBIT failed: %s", strerror(errno));
        ::close(mFd); mFd = -1;
        return false;
    }

    // UI_SET_KEYBIT — register exactly which key codes this device can send.
    // Must be called for every key code before UI_DEV_CREATE.
    // Android's PhoneWindowManager maps these keys to AudioManager volume calls.
    if (ioctl(mFd, UI_SET_KEYBIT, KEY_VOLUMEUP)   < 0 ||
        ioctl(mFd, UI_SET_KEYBIT, KEY_VOLUMEDOWN)  < 0) {
        ALOGE("UI_SET_KEYBIT failed: %s", strerror(errno));
        ::close(mFd); mFd = -1;
        return false;
    }

    // UI_DEV_SETUP — describe the virtual device to the kernel.
    // name:    shown in /proc/bus/input/devices and Android input logs
    // bustype: BUS_USB is conventional for virtual devices; doesn't affect behavior
    // vendor/product/version: arbitrary identifiers; won't clash with real hardware
    struct uinput_setup usetup{};
    strncpy(usetup.name, "PotVolume Knob", UINPUT_MAX_NAME_SIZE - 1);
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1209;   // pid.codes open-source vendor ID (harmless for dev)
    usetup.id.product = 0x0001;
    usetup.id.version = 1;

    if (ioctl(mFd, UI_DEV_SETUP, &usetup) < 0) {
        ALOGE("UI_DEV_SETUP failed: %s", strerror(errno));
        ::close(mFd); mFd = -1;
        return false;
    }

    // UI_DEV_CREATE — publish the device. After this, the kernel creates
    // /dev/input/eventN and Android's InputReader starts watching it.
    if (ioctl(mFd, UI_DEV_CREATE) < 0) {
        ALOGE("UI_DEV_CREATE failed: %s", strerror(errno));
        ::close(mFd); mFd = -1;
        return false;
    }

    ALOGI("Virtual input device 'PotVolume Knob' created on /dev/uinput");
    return true;
}

void VolumeController::close() {
    if (mFd >= 0) {
        // UI_DEV_DESTROY — remove the virtual device from the kernel input system.
        // Android's InputReader will see the device disappear cleanly.
        ioctl(mFd, UI_DEV_DESTROY);
        ::close(mFd);
        mFd = -1;
        ALOGI("Virtual input device destroyed");
    }
}

bool VolumeController::sendKeyEvent(int keyCode, int value) {
    // input_event is the kernel's standard event struct.
    // type:  EV_KEY for key events
    // code:  the specific key (KEY_VOLUMEUP or KEY_VOLUMEDOWN)
    // value: 1 = key pressed, 0 = key released
    // time:  kernel fills this; we leave it zero (uinput ignores it)
    struct input_event ev{};
    ev.type  = EV_KEY;
    ev.code  = static_cast<__u16>(keyCode);
    ev.value = value;
    if (write(mFd, &ev, sizeof(ev)) != sizeof(ev)) {
        ALOGE("write(EV_KEY code=%d val=%d) failed: %s", keyCode, value, strerror(errno));
        return false;
    }
    return true;
}

bool VolumeController::sendSyncEvent() {
    // EV_SYN / SYN_REPORT tells the kernel input layer "this batch of events
    // is complete, deliver them to listeners now". Android's InputReader will
    // not process the preceding key event until it receives this sync.
    struct input_event ev{};
    ev.type  = EV_SYN;
    ev.code  = SYN_REPORT;
    ev.value = 0;
    if (write(mFd, &ev, sizeof(ev)) != sizeof(ev)) {
        ALOGE("write(EV_SYN) failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool VolumeController::setVolume(int volumeIndex) {
    if (mFd < 0) {
        ALOGE("setVolume called but uinput device is not open");
        return false;
    }

    // Boot behaviour: on the very first call, record the knob's initial
    // position as the reference without sending any key events.
    // This prevents a volume jump at startup.
    if (mLastIndex < 0) {
        mLastIndex = volumeIndex;
        ALOGI("VolumeController: boot reference set to index=%d (no events sent)",
              volumeIndex);
        return true;
    }

    if (volumeIndex == mLastIndex) return true;  // nothing to do

    // Compute how many steps to move and in which direction.
    int delta   = volumeIndex - mLastIndex;
    int keyCode = (delta > 0) ? KEY_VOLUMEUP : KEY_VOLUMEDOWN;
    int steps   = std::abs(delta);

    ALOGI("Volume: index %d → %d (Δ=%+d, %d × %s)",
          mLastIndex, volumeIndex, delta, steps,
          (delta > 0) ? "KEY_VOLUMEUP" : "KEY_VOLUMEDOWN");

    // Send one full press+release cycle per step.
    // Each cycle moves Android's STREAM_MUSIC volume by exactly 1 index.
    for (int i = 0; i < steps; i++) {
        if (!sendKeyEvent(keyCode, 1)) return false;   // key down
        if (!sendSyncEvent())          return false;   // flush to InputReader
        if (!sendKeyEvent(keyCode, 0)) return false;   // key up
        if (!sendSyncEvent())          return false;   // flush to InputReader
    }

    mLastIndex = volumeIndex;
    return true;
}
