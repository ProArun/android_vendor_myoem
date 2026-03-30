# PotVolume Feature — Complete Implementation Plan

**Project:** `vendor/myoem/services/potvolumed`
**Target:** Raspberry Pi 5 · AOSP Android 15 (`android-15.0.0_r14`)
**Feature:** Control Android system volume using a physical rotary potentiometer via MCP3008 SPI ADC
**Author:** ProArun
**Date:** March 2026

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware Requirements](#hardware-requirements)
3. [Software Requirements](#software-requirements)
4. [Tools & Toolchain](#tools--toolchain)
5. [Programming Languages](#programming-languages)
6. [System Architecture](#system-architecture)
7. [Component Design](#component-design)
8. [File & Directory Structure](#file--directory-structure)
9. [Data Flow — End to End](#data-flow--end-to-end)
10. [SELinux Policy Plan](#selinux-policy-plan)
11. [Build Integration](#build-integration)
12. [Implementation Phases](#implementation-phases)
13. [Debugging & Verification Commands](#debugging--verification-commands)
14. [Known Risks & Mitigations](#known-risks--mitigations)

---

## Overview

The goal of this project is to physically control Android's system (media) volume by turning a rotary
potentiometer knob. The potentiometer produces a varying analog voltage (0–3.3V). Because the
Raspberry Pi 5 has no on-board analog-to-digital converter (ADC), we connect an external MCP3008
ADC chip over the SPI bus. The MCP3008 converts the analog voltage into a 10-bit digital value
(range 0–1023). A native C++ daemon (`potvolumed`) running in the Android vendor partition reads
this value periodically via the Linux SPI character device (`/dev/spidev0.0`), maps it to an
Android volume index, and calls `AudioSystem::setStreamVolumeIndex()` from `libaudioclient` to
update the media volume in real time.

This project sits entirely inside `vendor/myoem/` and requires zero changes to
`device/brcm/rpi5/` or `frameworks/`.

---

## Hardware Requirements

### Component List

| # | Component | Specification | Purpose |
|---|-----------|---------------|---------|
| 1 | Raspberry Pi 5 | 4GB/8GB RAM, AOSP 15 flashed | Host system, SPI master |
| 2 | Rotary Potentiometer | 10kΩ linear taper | Generates 0–3.3V analog voltage |
| 3 | MCP3008 ADC | 8-channel, 10-bit, SPI interface | Converts analog voltage to digital value |
| 4 | Breadboard | Full or half size | Prototyping platform |
| 5 | Jumper wires | Male-to-male, various colors | Electrical connections |

### MCP3008 — What It Does

The MCP3008 is a Serial Peripheral Interface (SPI) Analog-to-Digital Converter. It has:
- **8 analog input channels** (CH0–CH7) — we use CH0
- **10-bit resolution** — output range 0 to 1023
- **SPI interface** — communicates over 4 wires: CLK, MOSI, MISO, CS
- **Reference voltage = VDD** — when VDD=3.3V, 1023 = 3.3V, 0 = 0V

### Raspberry Pi 5 — SPI Pins (BCM numbering)

The RPi5 exposes SPI0 on the 40-pin GPIO header:

```
Physical Pin │ BCM GPIO │ SPI0 Function
─────────────┼──────────┼───────────────
Pin 19       │ GPIO 10  │ MOSI  (Master Out Slave In)
Pin 21       │ GPIO 9   │ MISO  (Master In Slave Out)
Pin 23       │ GPIO 11  │ SCLK  (SPI Clock)
Pin 24       │ GPIO 8   │ CE0   (Chip Enable / CS)
Pin 1        │ —        │ 3.3V  (Power)
Pin 6        │ —        │ GND   (Ground)
```

### Wiring Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                    BREADBOARD WIRING                             │
│                                                                  │
│   Raspberry Pi 5 (40-pin)          MCP3008 (DIP-16)             │
│   ─────────────────────            ──────────────────            │
│                                                                  │
│   3.3V   [Pin 1]  ──────────────── VDD  [Pin 16]                │
│   3.3V   [Pin 1]  ──────────────── VREF [Pin 15]  ← ref voltage │
│   GND    [Pin 6]  ──────────────── AGND [Pin 14]                │
│   SCLK   [Pin 23] ──────────────── CLK  [Pin 13]                │
│   MISO   [Pin 21] ──────────────── DOUT [Pin 12]                │
│   MOSI   [Pin 19] ──────────────── DIN  [Pin 11]                │
│   CE0    [Pin 24] ──────────────── CS   [Pin 10]                │
│   GND    [Pin 6]  ──────────────── DGND [Pin 9]                 │
│                                                                  │
│                        MCP3008 CH0 [Pin 1]  ──→ Pot Wiper       │
│                                                                  │
│   Potentiometer (3 pins):                                        │
│   ┌───────────────────────────────────────────┐                 │
│   │  Left end  ──→ GND                        │                 │
│   │  Wiper     ──→ MCP3008 CH0 (pin 1)        │                 │
│   │  Right end ──→ 3.3V                       │                 │
│   └───────────────────────────────────────────┘                 │
└──────────────────────────────────────────────────────────────────┘
```

### SPI Signal Explanation

When the daemon sends a 3-byte SPI transaction to MCP3008, the chip returns the ADC reading:

```
Byte 0 sent: 0x01          ← Start bit
Byte 1 sent: 0x80          ← Single-ended mode, Channel 0 (1000 0000)
Byte 2 sent: 0x00          ← Don't care (clocking in result)

Byte 0 received: ignored
Byte 1 received: lower 2 bits = bits 9:8 of ADC result
Byte 2 received: bits 7:0 of ADC result

Final value = ((response[1] & 0x03) << 8) | response[2]
Range: 0 (knob fully left / 0V) to 1023 (knob fully right / 3.3V)
```

---

## Software Requirements

### Linux Kernel Requirements

| Requirement | Detail |
|-------------|--------|
| SPI master driver | `spi-bcm2835` — already built into RPi5 kernel |
| SPI device node | `/dev/spidev0.0` — exposed by `spidev` kernel module |
| SPI speed | 1 MHz max (MCP3008 supports up to 3.6 MHz at 3.3V) |
| SPI mode | Mode 0 (CPOL=0, CPHA=0) |

> **Note:** On AOSP RPi5, verify `/dev/spidev0.0` exists at runtime:
> `adb shell ls -la /dev/spidev*`
> If missing, the RPi5 device tree or `config.txt` may need `dtparam=spi=on`.

### Android System Requirements

| Requirement | Detail |
|-------------|--------|
| AOSP Version | Android 15 (`android-15.0.0_r14`) |
| Partition | Vendor (`/vendor/bin/potvolumed`) |
| Volume stream | `AUDIO_STREAM_MUSIC` — media volume |
| Volume API | `AudioSystem::setStreamVolumeIndex()` from `libaudioclient` (VNDK) |
| Service startup | `init` via `.rc` file, class `main`, user `system` |

### VNDK Library Used

`libaudioclient` is a VNDK-stable library — vendor code can link against it legally in AOSP.
It provides `AudioSystem` namespace with direct access to `AudioFlinger` and `AudioService`
via Binder IPC under the hood.

```cpp
#include <media/AudioSystem.h>

// Set STREAM_MUSIC to volume index 10 (out of 15)
AudioSystem::setStreamVolumeIndex(
    AUDIO_STREAM_MUSIC,
    10,
    AUDIO_DEVICE_OUT_DEFAULT
);
```

---

## Tools & Toolchain

### Build Tools

| Tool | Version / Location | Purpose |
|------|--------------------|---------|
| AOSP build system | `android-15.0.0_r14` | Full system build |
| Soong (Android.bp) | Built into AOSP | Module build rules |
| `m potvolumed` | AOSP make shorthand | Build only this daemon |
| `lunch` | AOSP env setup | `myoem_rpi5-trunk_staging-userdebug` |
| `clang++` | AOSP prebuilt toolchain | C++ compiler for ARM64 |

### Development & Debug Tools

| Tool | Command | Purpose |
|------|---------|---------|
| adb shell | `adb shell ls /dev/spidev*` | Verify SPI device node |
| adb logcat | `adb logcat -s potvolumed` | Stream daemon logs |
| adb push | `adb push potvolumed /vendor/bin/` | Dev iteration without full flash |
| adb shell ps | `adb shell ps -eZ \| grep potvolumed` | Check SELinux domain |
| adb shell | `adb shell service call audio 3 i32 3 i32 10 i32 0` | Manual volume test |
| adb logcat | `adb logcat \| grep "avc: denied"` | SELinux denial hunting |
| adb shell | `adb shell dumpsys audio` | Audio system state dump |
| adb shell | `adb shell mount -o remount,rw /vendor` | Remount vendor for dev push |

### Testing Tools

| Tool | Purpose |
|------|---------|
| Custom C++ SPI test binary | Standalone SPI read test before integrating into daemon |
| `spidev_test` (Linux utility) | Low-level SPI loopback test to verify wiring |
| Python `spidev` library (on host) | Quick ADC value verification on standard Raspberry Pi OS before AOSP |

---

## Programming Languages

| Language | File Types | Where Used | Why |
|----------|-----------|------------|-----|
| **C++17** | `.cpp`, `.h` | Daemon source code | Native performance, direct access to Linux SPI ioctl, VNDK libraries |
| **AIDL** | `.aidl` | Optional bridge interface | Only needed if libaudioclient approach hits permission wall |
| **Soong DSL** | `Android.bp` | Build system | AOSP standard — replaces Makefiles for modules |
| **SELinux policy** | `.te`, `file_contexts`, `service_contexts` | Security policy | Mandatory for any vendor daemon in AOSP |
| **Init RC** | `.rc` | Service startup | Tells Android init how to launch and manage the daemon |

---

## System Architecture

### Full Stack — Layer by Layer

```
┌──────────────────────────────────────────────────────────────────┐
│                        HARDWARE LAYER                            │
│                                                                  │
│   ┌──────────────────┐   analog    ┌─────────────────────────┐  │
│   │   Potentiometer  │ ──voltage──▶│   MCP3008 (SPI ADC)     │  │
│   │   (0–3.3V)       │             │   10-bit output 0–1023  │  │
│   └──────────────────┘             └──────────┬──────────────┘  │
│                                               │ SPI (4 wires)   │
│                                       ┌───────▼──────────┐      │
│                                       │  RPi5 GPIO/SPI0  │      │
│                                       │  MOSI/MISO/CLK/CS│      │
│                                       └───────┬──────────┘      │
└───────────────────────────────────────────────┼─────────────────┘
                                                │
┌───────────────────────────────────────────────▼─────────────────┐
│                      LINUX KERNEL LAYER                          │
│                                                                  │
│   spi-bcm2835 driver (SPI master)                               │
│   spidev module (userspace bridge)                               │
│                                                                  │
│                     /dev/spidev0.0                               │
│          (character device — read via ioctl)                     │
└───────────────────────────────────────────────┬─────────────────┘
                                                │ ioctl(SPI_IOC_MESSAGE)
┌───────────────────────────────────────────────▼─────────────────┐
│                VENDOR NATIVE DAEMON (C++)                        │
│           vendor/myoem/services/potvolumed                       │
│                                                                  │
│   ┌──────────────┐   ┌──────────────┐   ┌───────────────────┐  │
│   │  SpiReader   │──▶│  AdcDecoder  │──▶│   AdcFilter       │  │
│   │              │   │  MCP3008     │   │   Low-pass +      │  │
│   │ /dev/spidev  │   │  3-byte SPI  │   │   dead zone       │  │
│   │ ioctl calls  │   │  frame       │   │   (±5 counts)     │  │
│   └──────────────┘   └──────────────┘   └────────┬──────────┘  │
│                                                   │             │
│                                         ┌─────────▼──────────┐  │
│                                         │   VolumeMapper     │  │
│                                         │   0–1023 →         │  │
│                                         │   0–15 (steps)     │  │
│                                         └─────────┬──────────┘  │
│                                                   │             │
│                                         ┌─────────▼──────────┐  │
│                                         │  VolumeController  │  │
│                                         │  AudioSystem::     │  │
│                                         │  setStreamVolume   │  │
│                                         │  IndexInternally   │  │
│                                         └─────────┬──────────┘  │
└───────────────────────────────────────────────────┼─────────────┘
                                                    │ Binder IPC
┌───────────────────────────────────────────────────▼─────────────┐
│                  ANDROID FRAMEWORK LAYER                         │
│                                                                  │
│   IAudioService (AIDL Binder interface)                          │
│        │                                                         │
│        ▼                                                         │
│   AudioService.java  ──▶  AudioManager  ──▶  STREAM_MUSIC       │
│                                                                  │
│   Volume steps: 0 (mute) to 15 (max)                            │
└──────────────────────────────────────────────────────────────────┘
```

### Daemon Internal Architecture

```
potvolumed
│
├── main.cpp
│     Opens /dev/spidev0.0
│     Starts poll loop (every 10ms)
│     Catches SIGTERM for clean shutdown
│
├── SpiReader.h / SpiReader.cpp
│     Wraps Linux SPI ioctl
│     Sends MCP3008 3-byte frame
│     Returns raw 10-bit value (0–1023)
│
├── AdcFilter.h / AdcFilter.cpp
│     Low-pass filter: smooths jitter
│     Dead zone: ignore changes < ±5 counts
│     Only fires callback when value meaningfully changes
│
├── VolumeMapper.h / VolumeMapper.cpp
│     Maps 0–1023 ADC range to 0–15 Android volume index
│     Formula: index = (raw_value * maxVolumeIndex) / 1023
│     Clamps output to [0, maxVolumeIndex]
│
└── VolumeController.h / VolumeController.cpp
      Calls AudioSystem::setStreamVolumeIndex()
      Logs every change with old → new value
      Handles errors from AudioFlinger (returns status_t)
```

---

## Component Design

### SpiReader

Responsibility: Communicate with MCP3008 over `/dev/spidev0.0` using Linux SPI ioctl.

```
Input:  SPI device path, channel number (0–7)
Output: 10-bit integer (0–1023)
Key:    3-byte transaction — send start bit + channel config, receive ADC bits
Error:  Returns -1 on ioctl failure, logs errno
```

The MCP3008 SPI frame format:

```
Byte[0] TX = 0x01         (start bit)
Byte[1] TX = 0x80         (single-ended, CH0 = 1000_0000b)
Byte[2] TX = 0x00         (padding for clocking out result)

Byte[1] RX bits[1:0]      (ADC result bits 9:8)
Byte[2] RX bits[7:0]      (ADC result bits 7:0)

result = ((rx[1] & 0x03) << 8) | rx[2]
```

### AdcFilter

Responsibility: Remove electrical noise and prevent volume from flickering at rest.

```
State:     last_stable_value (int), filter_accumulator (float)
Algorithm: Exponential moving average  →  filtered = α*new + (1-α)*old
           α = 0.2 (gentle smoothing)
Dead zone: if |filtered - last_stable| < 5  →  skip (no change)
Output:    Only emits new value when change is significant
```

### VolumeMapper

Responsibility: Convert 10-bit ADC value to Android STREAM_MUSIC volume index.

```
Android STREAM_MUSIC maximum index = 15 (queried via AudioSystem::getStreamMaxVolume)
Formula: volume_index = round((adc_value / 1023.0) * max_index)
Clamp:   max(0, min(volume_index, max_index))
```

### VolumeController

Responsibility: Apply volume to Android audio system.

```
Library:   libaudioclient (VNDK)
API call:  AudioSystem::setStreamVolumeIndex(
               AUDIO_STREAM_MUSIC,
               volume_index,
               AUDIO_DEVICE_OUT_DEFAULT
           )
Return:    status_t — check NO_ERROR, log on failure
Throttle:  Do not call if index unchanged (compare with last_set_index)
```

---

## File & Directory Structure

```
vendor/myoem/
└── services/
    └── potvolumed/
        │
        ├── Android.bp                  ← cc_binary build rules
        │
        ├── src/
        │   ├── main.cpp                ← Entry point, SPI open, poll loop
        │   ├── SpiReader.h             ← SPI ioctl wrapper declaration
        │   ├── SpiReader.cpp           ← SPI ioctl wrapper implementation
        │   ├── AdcFilter.h             ← Low-pass + dead zone filter
        │   ├── AdcFilter.cpp
        │   ├── VolumeMapper.h          ← ADC → volume index mapping
        │   ├── VolumeMapper.cpp
        │   ├── VolumeController.h      ← AudioSystem call wrapper
        │   └── VolumeController.cpp
        │
        ├── potvolumed.rc               ← init service definition
        │
        └── sepolicy/
            └── private/
                ├── potvolumed.te       ← Type enforcement rules
                ├── file_contexts       ← Label for /vendor/bin/potvolumed
                └── service_contexts    ← (if registering a named service)
```

---

## Data Flow — End to End

```
Step 1 — Knob turned:
  User rotates potentiometer knob
  Wiper voltage changes: e.g., 1.65V (mid-point)

Step 2 — ADC conversion:
  MCP3008 samples CH0 voltage
  Converts 1.65V → ~512 (10-bit: 512/1023 * 3.3V ≈ 1.65V)

Step 3 — SPI transaction:
  potvolumed sends 3-byte frame via ioctl to /dev/spidev0.0
  Kernel SPI driver clocks the bits to MCP3008
  MCP3008 returns ADC value in response bytes
  SpiReader decodes: raw = 512

Step 4 — Filtering:
  AdcFilter applies EMA: smoothed ≈ 510 (after a few readings)
  Dead zone check: |510 - last_stable(0)| = 510 > 5 → pass through
  last_stable updated to 510

Step 5 — Volume mapping:
  VolumeMapper: index = round(510 / 1023.0 * 15) = round(7.48) = 7
  Android volume index = 7 out of 15

Step 6 — Volume set:
  VolumeController calls:
    AudioSystem::setStreamVolumeIndex(AUDIO_STREAM_MUSIC, 7, AUDIO_DEVICE_OUT_DEFAULT)
  This travels via Binder IPC → AudioService.java → AudioManager
  Android adjusts media volume to 7/15 ≈ 47%

Step 7 — Logging:
  potvolumed logs: "ADC=510 → volume 7/15 (prev: 0)"
  Visible in: adb logcat -s potvolumed
```

---

## SELinux Policy Plan

Android requires every vendor binary to run in a defined SELinux domain. Without policy, init
will refuse to start the daemon (or it runs in a wrong domain with AVC denials).

### Files Needed

**`potvolumed.te`** — type enforcement rules

```
# Declare the type for the daemon process
type potvolumed, domain;
type potvolumed_exec, exec_type, vendor_file_type, file_type;

# Allow init to start potvolumed (domain transition)
init_daemon_domain(potvolumed)

# Allow potvolumed to read/write /dev/spidev0.0
allow potvolumed spidev_device:chr_file { open read write ioctl };

# Allow potvolumed to call AudioService via Binder
binder_call(potvolumed, audioserver)
allow potvolumed audioserver_service:service_manager find;

# Allow logging
allow potvolumed logd:unix_dgram_socket sendto;

# Allow reading system properties (needed by libaudioclient)
allow potvolumed system_prop:property_service read;
```

**`file_contexts`** — assign SELinux label to the binary

```
/vendor/bin/potvolumed    u:object_r:potvolumed_exec:s0
/dev/spidev[0-9]+\.[0-9]+ u:object_r:spidev_device:s0
```

> **Note:** The `spidev_device` type may need to be declared in `file_contexts`
> if it doesn't exist in AOSP 15 base policy. Verify with:
> `adb shell ls -laZ /dev/spidev*`

### SELinux Dev Workflow

During development, set permissive to get the daemon running first:
```bash
adb root
adb shell setenforce 0          # permissive mode — no denials enforced
adb logcat | grep "avc: denied" # collect all denials to write policy from
```

Then move to enforcing after policy is correct.

---

## Build Integration

### Android.bp

```bp
cc_binary {
    name: "potvolumed",
    srcs: [
        "src/main.cpp",
        "src/SpiReader.cpp",
        "src/AdcFilter.cpp",
        "src/VolumeMapper.cpp",
        "src/VolumeController.cpp",
    ],
    init_rc: ["potvolumed.rc"],
    shared_libs: [
        "libaudioclient",   // AudioSystem::setStreamVolumeIndex
        "liblog",           // ALOGI, ALOGE
        "libutils",         // status_t, String8
        "libbinder",        // Binder IPC (used internally by libaudioclient)
    ],
    cflags: ["-Wall", "-Werror"],
    vendor: true,           // installs to /vendor/bin/
}
```

### myoem_base.mk additions

```makefile
# PotVolume daemon
PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/potvolumed
PRODUCT_PACKAGES          += potvolumed
PRODUCT_PRIVATE_SEPOLICY_DIRS += vendor/myoem/services/potvolumed/sepolicy/private
```

### potvolumed.rc

```
service potvolumed /vendor/bin/potvolumed
    class main
    user system
    group system
    oneshot_stop
```

> `user system` — runs as system, which can access AudioService via Binder.
> `group system` — needed for `/dev/spidev0.0` access (will need udev/ueventd rule for group).

### ueventd rule for SPI device permissions

In `vendor/myoem/` or device tree, add a ueventd rule so system user can open `/dev/spidev0.0`:

```
# /vendor/etc/ueventd.rc  (or device ueventd.rc)
/dev/spidev0.0   0660   system   system
```

---

## Implementation Phases

| Phase | Goal | Deliverable | Notes |
|-------|------|-------------|-------|
| **Phase 1** | Verify SPI hardware | `adb shell ls /dev/spidev*` shows `spidev0.0` | Check RPi5 config.txt for `dtparam=spi=on` |
| **Phase 2** | SPI read test | Standalone C++ binary reads MCP3008, prints raw values | Test outside of daemon first |
| **Phase 3** | Volume control test | `adb shell service call audio` sets volume manually | Confirm AudioService call works |
| **Phase 4** | Daemon scaffold | `potvolumed` starts, logs ADC values, no volume yet | RC file, SELinux permissive |
| **Phase 5** | Full daemon | ADC → filter → map → volume — end to end working | libaudioclient integrated |
| **Phase 6** | SELinux enforcing | All AVC denials resolved, `setenforce 1` works | Collect denials from Phase 4/5 |
| **Phase 7** | Polish | Noise filtering, smooth UX, edge cases (0 and max) | Production quality |

---

## Debugging & Verification Commands

```bash
# ── Hardware / Kernel ──────────────────────────────────────────────
# Check SPI device node exists
adb shell ls -la /dev/spidev*

# Check SPI device SELinux label
adb shell ls -laZ /dev/spidev*

# Check SPI module loaded
adb shell lsmod | grep spi

# ── Daemon ─────────────────────────────────────────────────────────
# Stream daemon logs
adb logcat -s potvolumed

# Check daemon is running and in correct SELinux domain
adb shell ps -eZ | grep potvolumed

# Check daemon started via init
adb shell getprop init.svc.potvolumed

# Restart daemon (after push of new binary)
adb shell stop potvolumed
adb shell start potvolumed

# ── Volume / Audio ─────────────────────────────────────────────────
# Dump audio system state (current volumes)
adb shell dumpsys audio | grep -A5 "STREAM_MUSIC"

# Manually set STREAM_MUSIC volume to index 10 via service call
# service call audio 3 → setStreamVolumeIndex(stream=3, index=10, device=0)
adb shell service call audio 3 i32 3 i32 10 i32 0

# ── SELinux ────────────────────────────────────────────────────────
# Watch for AVC denials
adb logcat | grep "avc: denied"

# Set permissive (development only)
adb root && adb shell setenforce 0

# Check current mode
adb shell getenforce

# ── Dev Iteration (no full rebuild) ────────────────────────────────
# Remount vendor partition as read-write
adb shell mount -o remount,rw /vendor

# Push new binary
adb push out/target/product/myoem_rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed

# Fix permissions
adb shell chmod 755 /vendor/bin/potvolumed

# Restart daemon
adb shell stop potvolumed && adb shell start potvolumed
```

---

## Known Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|-----------|--------|------------|
| `/dev/spidev0.0` does not exist in AOSP RPi5 | Medium | Blocker | Check `config.txt` for `dtparam=spi=on`; may need device tree overlay |
| `libaudioclient` not accessible from vendor | Medium | Blocker | Fallback: AIDL bridge via privileged system app, or `cmd media_session volume` shell call |
| SELinux blocks `potvolumed` ↔ audioserver Binder | High | Blocker | Start with `setenforce 0`, collect all denials, write exact policy |
| `/dev/spidev0.0` owned by root:root, system cannot open | High | Blocker | Add ueventd rule `0660 system system`; add `chown` in `.rc` on-boot |
| Potentiometer noise causes volume to flicker | High | Poor UX | Dead zone ±5 ADC counts + low-pass filter (EMA α=0.2) |
| MCP3008 hwmon index changes across boots | Low | N/A | MCP3008 uses SPI, not hwmon — no discovery needed |
| `AudioSystem::setStreamVolumeIndex` API changed in AOSP 15 | Low | Blocker | Verify header in `frameworks/av/include/media/AudioSystem.h` |

---

## Summary

This project introduces a clean, single-daemon implementation that bridges physical hardware
(potentiometer + MCP3008 SPI ADC) to Android's audio subsystem using only vendor-space C++.
No framework modifications are needed. The daemon follows the same structural pattern established
by `thermalcontrold` in this vendor tree — RC file, SELinux policy, Soong build, `vendor/myoem/`
location. The most likely integration challenges are SPI device permissions and SELinux policy for
the AudioService Binder call, both of which have well-defined mitigation paths documented above.

---

*Plan file for: `vendor/myoem/services/potvolumed`*
*Related: `THERMALCONTROL_PLAN.md`, `SAFEMODE_PLAN.md`*
