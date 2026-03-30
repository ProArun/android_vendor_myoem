// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
#pragma once

// VolumeController — controls Android media volume by injecting KEY_VOLUMEUP
// and KEY_VOLUMEDOWN events through the Linux uinput virtual input device.
//
// WHY uinput INSTEAD OF libaudioclient:
//   In AOSP 15, libaudioclient does not have an image:vendor variant.
//   It is a system-only library that vendor cc_binary modules cannot link
//   against. Attempting to do so produces:
//     "dependency libaudioclient missing variant: os:android,image:vendor"
//
//   The uinput approach avoids this entirely:
//   - /dev/uinput is a standard Linux kernel character device
//   - No system library beyond liblog is needed
//   - Android's InputReader already handles KEY_VOLUMEUP/DOWN and routes them
//     through PhoneWindowManager → AudioManager → STREAM_MUSIC volume change
//   - This is exactly how physical hardware volume buttons work on real devices
//
// HOW uinput WORKS:
//   uinput (User-space input) lets a process create a virtual input device.
//   Once created, the process writes input_event structs to the fd. The kernel
//   input subsystem delivers them to Android exactly as if a real keyboard or
//   button pressed KEY_VOLUMEUP / KEY_VOLUMEDOWN.
//
//   Setup sequence:
//     1. open("/dev/uinput", O_WRONLY)
//     2. ioctl(UI_SET_EVBIT,  EV_KEY)           — enable key event type
//     3. ioctl(UI_SET_KEYBIT, KEY_VOLUMEUP)     — register key
//     4. ioctl(UI_SET_KEYBIT, KEY_VOLUMEDOWN)   — register key
//     5. ioctl(UI_SET_EVBIT,  EV_SYN)           — enable sync events (required)
//     6. ioctl(UI_DEV_SETUP,  &usetup)          — set device name/id
//     7. ioctl(UI_DEV_CREATE)                   — publish to kernel input system
//
//   Per key press (one volume step):
//     write(EV_KEY, KEY_VOLUMEUP, 1)   — key down
//     write(EV_SYN, SYN_REPORT, 0)    — flush to InputReader
//     write(EV_KEY, KEY_VOLUMEUP, 0)  — key up
//     write(EV_SYN, SYN_REPORT, 0)    — flush to InputReader
//
// DELTA-BASED VOLUME CONTROL:
//   uinput sends discrete events (not absolute values), so we track the last
//   volume index internally and compute a delta:
//     delta = newIndex - lastIndex
//     delta > 0: send delta KEY_VOLUMEUP presses
//     delta < 0: send |delta| KEY_VOLUMEDOWN presses
//
//   On the very first call (mLastIndex = -1): we record the knob's initial
//   position without sending any key events. This avoids a volume jump at boot.
//   The knob's first position becomes the reference point silently.

class VolumeController {
public:
    VolumeController();
    ~VolumeController();

    // open() creates the virtual input device on /dev/uinput.
    // Must be called once before setVolume(). Returns true on success.
    bool open();

    void close();

    // setVolume() sends KEY_VOLUMEUP or KEY_VOLUMEDOWN events to move from
    // the last known index to volumeIndex. No-op if index unchanged.
    bool setVolume(int volumeIndex);

private:
    bool sendKeyEvent(int keyCode, int value);
    bool sendSyncEvent();

    int mFd{-1};
    int mLastIndex{-1};   // -1 = boot state: record reference, send no events
};
