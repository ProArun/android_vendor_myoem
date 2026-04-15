# PIR Motion Detector — Full Implementation Plan
**Author:** ProArun
**Target:** Raspberry Pi 5 · AOSP android-15.0.0_r14 · `vendor/myoem/`
**Date:** 2026-04-14

---

## Table of Contents
1. [Hardware Requirements & Wiring](#1-hardware-requirements--wiring)
2. [Architecture Overview](#2-architecture-overview)
3. [Design Decisions & Rationale](#3-design-decisions--rationale)
4. [Project Structure](#4-project-structure)
5. [Phase 0 — Hardware Verification (Shell)](#phase-0--hardware-verification-shell)
6. [Phase 1 — GPIO HAL (C++ Layer)](#phase-1--gpio-hal-c-layer)
7. [Phase 2 — Binder Service with AIDL Callbacks](#phase-2--binder-service-with-aidl-callbacks)
8. [Phase 3 — Java Manager Library](#phase-3--java-manager-library)
9. [Phase 4 — Kotlin / Compose App](#phase-4--kotlin--compose-app)
10. [SELinux Policy](#10-selinux-policy)
11. [Build Integration](#11-build-integration)
12. [Testing Strategy](#12-testing-strategy)
13. [Article Series Outline](#13-article-series-outline)
14. [Phase Checklist](#14-phase-checklist)

---

## 1. Hardware Requirements & Wiring

### 1.1 Components Required

| # | Component | Notes |
|---|-----------|-------|
| 1 | **HC-SR501 PIR Sensor** | 5V supply, 3.3V logic output, adjustable sensitivity & delay |
| 2 | Breadboard (half-size or full) | For prototyping connections |
| 3 | Male-to-female jumper wires (×3) | GPIO header (male) → breadboard (female) |
| 4 | Female-to-female jumper wires (×3) | Breadboard → HC-SR501 pins |
| 5 | Raspberry Pi 5 running AOSP 15 | The host device |

> **Alternative sensor**: AM312 (mini PIR). It operates at 3.3V directly (no 5V needed)
> and has a fixed 2-second delay with no adjustments. Simpler to wire but less configurable.
> AM312 is a drop-in replacement for this project — wiring and code are identical.

### 1.2 HC-SR501 Pin-Out

The HC-SR501 has 3 pins on one edge:

```
┌─────────────────────────────┐
│      HC-SR501 (top view)    │
│  [Lens dome]                │
│                             │
│   VCC    OUT    GND         │
│   (1)    (2)    (3)         │
└─────────────────────────────┘
```

| HC-SR501 Pin | Connect To | RPi5 Physical Pin |
|-------------|-----------|-------------------|
| VCC | 5V power | Pin 2 or Pin 4 |
| GND | Ground | Pin 6 or any GND |
| OUT | GPIO17 (BCM) | Pin 11 |

### 1.3 Wiring Diagram

```
RPi5 40-Pin Header
                                                HC-SR501
   Pin 2  (5V)  ──────────────────────────────── VCC
   Pin 6  (GND) ──────────────────────────────── GND
   Pin 11 (GPIO17) ──────[ 1kΩ optional ]─────── OUT
```

> **Why GPIO17?** It is a well-tested general-purpose I/O pin, far from the SPI/I2C
> pins used by the PotVolume project (SPI0). Keeps hardware conflicts minimal.
>
> **Why the optional 1kΩ resistor?** The HC-SR501 output is 3.3V, which is within
> RPi5 GPIO spec. The resistor limits current in case of misconfiguration. You can
> omit it — it is a belt-and-suspenders precaution, not a requirement.

### 1.4 HC-SR501 Calibration Knobs

The sensor has two potentiometers on the PCB:

| Potentiometer | Controls | Range |
|--------------|---------|-------|
| Sx (Sensitivity) | Detection range | ~3m – ~7m |
| Tx (Time delay) | How long OUT stays HIGH after detection | ~3s – ~300s |

And a 2-pin jumper:
- **H** position (jumper on left): **Repeating trigger** — OUT stays HIGH as long as motion continues
- **L** position (jumper on right): **Single trigger** — OUT goes HIGH once then waits Tx

> **Recommended setting for this project**: Jumper to **H** (repeating), Tx to minimum
> (fastest response). This makes the OUT signal go HIGH when motion starts and LOW when
> motion stops, giving us a clean rising + falling edge to detect.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                  PIR Detector App (Kotlin / Compose)                 │
│                                                                      │
│   PirDetectorManager.registerListener(listener)                      │
│   PirDetectorManager.unregisterListener(listener)                    │
│                                                                      │
│   MotionListener.onMotionDetected()  ← called on main thread        │
│   MotionListener.onMotionEnded()     ← called on main thread        │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  Binder IPC (AIDL, VINTF-stable)
┌──────────────────────────────▼───────────────────────────────────────┐
│             pirdetector-manager (Java library)                       │
│   · Binds to PirDetectorService via AIDL                             │
│   · Implements IPirDetectorCallback (receives oneway calls)          │
│   · Re-dispatches to MotionListener on main thread (Handler)         │
│   · Manages lifecycle (register/unregister)                          │
│   · Handles DeathRecipient (service restart)                         │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  Binder IPC (IPirDetectorService AIDL)
┌──────────────────────────────▼───────────────────────────────────────┐
│          pirdetectord (C++ vendor daemon)                            │
│                                                                      │
│   · On init: calls GpioHal.init("/dev/gpiochipN", GPIO17)           │
│   · Spawns EventThread: blocks on GpioHal.waitForEdge()             │
│   · On edge event: iterates callback list → fires IPirDetectorCallback│
│   · Registers callbacks, handles client death via DeathRecipient     │
│   · VINTF-stable — system apps can call it                          │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  Linux GPIO character device ioctl
┌──────────────────────────────▼───────────────────────────────────────┐
│           GpioHal (C++ HAL helper, cc_library_static)               │
│                                                                      │
│   · Opens /dev/gpiochipN                                             │
│   · Calls GPIO_GET_LINEEVENT_IOCTL → event_fd                       │
│   · waitForEdge(): poll(event_fd | shutdown_pipe) — blocks forever  │
│   · On poll() return: read(event_fd) → gpioevent_data               │
│   · shutdown(): writes to self-pipe to unblock waitForEdge()        │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  sysfs / character device
┌──────────────────────────────▼───────────────────────────────────────┐
│           Linux Kernel — GPIO Subsystem (RP1 GPIO driver)           │
│                                                                      │
│   /dev/gpiochip4 — 40-pin header GPIO controller (RPi5)             │
│   Line 17 → GPIO17 → Physical Pin 11                                │
│                                                                      │
│   Interrupt: kernel delivers RISING/FALLING_EDGE event to poll()   │
│   → zero busy-wait, zero CPU when idle                              │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  physical wire
┌──────────────────────────────▼───────────────────────────────────────┐
│                  HC-SR501 PIR Sensor                                 │
│                                                                      │
│   OUT = 3.3V (HIGH) when motion detected                            │
│   OUT = 0V   (LOW)  when no motion                                  │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. Design Decisions & Rationale

### 3.1 GPIO Access Method: Character Device (not sysfs)

| Method | Status | Why Not? |
|--------|--------|---------|
| `/sys/class/gpio/` (sysfs) | **Deprecated** (kernel 4.8+) | Removed in future kernels, not recommended |
| `libgpiod` | **Standard** but absent from AOSP tree | Not in `external/` — would need vendoring |
| **Linux GPIO character device ioctl** | **Standard, zero deps** | ✅ This project's choice |

The GPIO character device (`/dev/gpiochipN`) is the kernel-exported, standard API
for userspace GPIO access. It is what `libgpiod` wraps internally. We call the same
ioctls directly using `<linux/gpio.h>` kernel headers, which are always available in
the Android build system. Zero external dependencies.

**Key advantage**: The event file descriptor returned by `GPIO_GET_LINEEVENT_IOCTL`
supports `poll()`. This means the daemon thread literally calls `poll()` and sleeps
in the kernel scheduler until the hardware GPIO interrupt fires. There is no
busy-wait, no timer, no periodic wake-up — pure interrupt-driven design.

### 3.2 Callback Design: oneway AIDL (not polling)

Polling approach (potvolumed): daemon reads value every 50ms → push to audio.
Callback approach (this project):
- Client registers `IPirDetectorCallback` with the service
- Service calls `cb->onMotionEvent()` the instant GPIO edge fires
- Client receives event asynchronously, on demand
- Zero latency gap between hardware event and app notification

### 3.3 VINTF Stability (same as SafeMode / ThermalControl)

`stability: "vintf"` in the `aidl_interface` block is required for system apps (on
`/system` partition) to call vendor services (on `/vendor`). Without it, libbinder
rejects the call with `BAD_TYPE` due to partition-stability mismatch. Lesson learned
from ThermalControl Phase 4.

### 3.4 RPi5 GPIO Character Device Path

On Raspberry Pi 5, the 40-pin GPIO header is driven by the **RP1** south bridge chip
(not the BCM2712 SoC directly). The RP1 GPIO controller registers in Linux as a
separate gpiochip, typically `/dev/gpiochip4`.

**Always verify on device before hardcoding:**
```bash
adb shell ls /dev/gpiochip*
adb shell cat /sys/bus/gpio/devices/gpiochip4/label
# Expected output: pinctrl-rp1  (this is the 40-pin header controller)
```

The `GpioHal` will accept the chip path as a constructor parameter so it can be
configured without recompiling (via a property or hardcoded constant in the service).

### 3.5 Self-Pipe Trick for Clean Shutdown

`poll()` can only be interrupted by signals or a ready file descriptor. To stop the
event thread cleanly (without sending SIGKILL), `GpioHal` maintains a `pipe2()`. The
event thread monitors both the GPIO event fd AND the pipe read end. `shutdown()` writes
one byte to the pipe write end, which makes `poll()` return immediately, and the thread
sees the shutdown signal and exits.

### 3.6 DeathRecipient for Dead Clients

If an app crashes while registered, its callback Binder object becomes a "dead" binder.
Without cleanup, the service accumulates dead binders and leaks memory. The service
registers an `AIBinder_DeathRecipient` for every client callback. When the client
process dies, the death recipient fires and removes the callback from the list.
This is the same pattern used in SafeMode.

---

## 4. Project Structure

```
vendor/myoem/
├── hal/pirdetector/                    ← Phase 1: GPIO HAL
│   ├── Android.bp
│   ├── include/pirdetector/
│   │   └── GpioHal.h
│   └── src/
│       └── GpioHal.cpp
│
├── services/pirdetector/               ← Phase 2: Binder Service
│   ├── Android.bp
│   ├── aidl/
│   │   └── com/myoem/pirdetector/
│   │       ├── IPirDetectorService.aidl
│   │       ├── IPirDetectorCallback.aidl
│   │       └── MotionEvent.aidl
│   ├── aidl_api/                       ← frozen API snapshots (auto-generated)
│   │   └── pirdetectorservice-aidl/
│   │       └── 1/
│   │           └── com/myoem/pirdetector/*.aidl
│   ├── include/
│   │   └── PirDetectorService.h
│   ├── src/
│   │   ├── PirDetectorService.cpp
│   │   └── main.cpp
│   ├── pirdetectord.rc
│   ├── vintf/
│   │   └── pirdetector.xml
│   └── sepolicy/
│       └── private/
│           ├── pirdetectord.te
│           ├── file_contexts
│           └── service_contexts
│
├── libs/pirdetector/                   ← Phase 3: Java Manager
│   ├── Android.bp
│   └── java/com/myoem/pirdetector/
│       ├── PirDetectorManager.java
│       └── MotionListener.java
│
└── apps/PirDetectorApp/                ← Phase 4: Kotlin App
    ├── Android.bp
    ├── AndroidManifest.xml
    └── src/
        └── com/myoem/pirdetectorapp/
            ├── MainActivity.kt
            ├── MotionViewModel.kt
            ├── MotionContract.kt           ← UiState, UiEvent, UiEffect
            └── ui/
                └── MotionScreen.kt
```

---

## Phase 0 — Hardware Verification (Shell)

**Goal**: Confirm the PIR sensor and GPIO are functional before writing any C++ code.

### Step 1: Discover the gpiochip for the 40-pin header

```bash
# List all gpiochip devices
adb shell ls /dev/gpiochip*

# Check which one is the RP1 (40-pin header controller)
adb shell cat /sys/bus/gpio/devices/gpiochip0/label
adb shell cat /sys/bus/gpio/devices/gpiochip4/label
# Look for: "pinctrl-rp1" — that is the 40-pin header
```

> **Expected on RPi5 AOSP 15**: `/dev/gpiochip4` with label `pinctrl-rp1`.
> But verify — kernel enumeration can change between AOSP builds (same lesson as spidev10.0).

### Step 2: Check GPIO17 via sysfs (quick sanity check)

```bash
# As root (adb root first)
adb root

# Export GPIO17 to sysfs
adb shell "echo 17 > /sys/class/gpio/export"

# Check direction (should default to 'in')
adb shell cat /sys/class/gpio/gpio17/direction

# Read value (should be 0 if no motion)
adb shell cat /sys/class/gpio/gpio17/value

# Wave hand in front of sensor — should read 1
adb shell cat /sys/class/gpio/gpio17/value

# Clean up
adb shell "echo 17 > /sys/class/gpio/unexport"
```

### Step 3: Monitor real-time GPIO changes

```bash
# Poll the GPIO value continuously (5 readings per second)
adb shell "while true; do cat /sys/class/gpio/gpio17/value; sleep 0.2; done"
# Wave hand in front of sensor and watch the output change 0→1→0
```

### Step 4: Verify gpiochip access (for the character device approach)

```bash
# Check that the device file exists and has correct permissions
adb shell ls -la /dev/gpiochip4

# Check SELinux label
adb shell ls -laZ /dev/gpiochip4
# Note the type — likely: u:object_r:gpio_device:s0 or u:object_r:sysfs:s0
```

> **Record the actual SELinux label** — you will need it for the `.te` policy file.

---

## Phase 1 — GPIO HAL (C++ Layer)

**Goal**: A `cc_library_static` that wraps the Linux GPIO character device ioctl,
exposing a clean blocking `waitForEdge()` API to the service.

### 1.1 Header: `include/pirdetector/GpioHal.h`

```cpp
// vendor/myoem/hal/pirdetector/include/pirdetector/GpioHal.h
#pragma once
#include <cstdint>
#include <string>

namespace myoem::pirdetector {

enum class EdgeType {
    RISING,   // Motion started (sensor OUT goes HIGH)
    FALLING,  // Motion ended   (sensor OUT goes LOW)
};

struct GpioEvent {
    EdgeType edge;
    int64_t  timestampNs;  // Monotonic kernel timestamp (nanoseconds)
};

class GpioHal {
public:
    GpioHal() = default;
    ~GpioHal();

    // Opens /dev/gpiochipN, requests GPIO line for edge events.
    // chipPath: e.g. "/dev/gpiochip4"
    // lineOffset: GPIO number (17 for GPIO17)
    // Returns true on success.
    bool init(const std::string& chipPath, int lineOffset);

    // Blocks until RISING or FALLING edge is detected on the GPIO line.
    // Also unblocks when shutdown() is called.
    // Returns true if a real GPIO event occurred.
    // Returns false if shutdown() was called (caller should exit loop).
    bool waitForEdge(GpioEvent& outEvent);

    // Signals waitForEdge() to return false immediately.
    // Thread-safe. Call from the service destructor.
    void shutdown();

private:
    int mEventFd = -1;       // from GPIO_GET_LINEEVENT_IOCTL
    int mShutdownPipe[2] = {-1, -1};  // self-pipe for clean shutdown

    void closeFds();
};

}  // namespace myoem::pirdetector
```

### 1.2 Implementation: `src/GpioHal.cpp`

Key ioctls from `<linux/gpio.h>`:

| ioctl | Purpose |
|-------|---------|
| `GPIO_GET_LINEEVENT_IOCTL` | Request an input line for edge-event monitoring |
| Input struct | `struct gpioevent_request { lineoffset, handleflags, eventflags, consumer_label, fd }` |
| Event read | `struct gpioevent_data { timestamp, id }` where id = `GPIOEVENT_EVENT_RISING_EDGE` or `GPIOEVENT_EVENT_FALLING_EDGE` |

```cpp
// vendor/myoem/hal/pirdetector/src/GpioHal.cpp
#define LOG_TAG "GpioHal"
#include "pirdetector/GpioHal.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include <log/log.h>

namespace myoem::pirdetector {

GpioHal::~GpioHal() { closeFds(); }

bool GpioHal::init(const std::string& chipPath, int lineOffset) {
    // 1. Create self-pipe for shutdown signalling
    if (pipe2(mShutdownPipe, O_CLOEXEC) != 0) {
        ALOGE("pipe2 failed: %s", strerror(errno));
        return false;
    }

    // 2. Open the gpiochip character device
    int chipFd = open(chipPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (chipFd < 0) {
        ALOGE("open(%s) failed: %s", chipPath.c_str(), strerror(errno));
        return false;
    }

    // 3. Request the GPIO line for edge-event monitoring (V1 API)
    struct gpioevent_request req = {};
    req.lineoffset  = static_cast<uint32_t>(lineOffset);
    req.handleflags = GPIOHANDLE_REQUEST_INPUT;
    req.eventflags  = GPIOEVENT_REQUEST_BOTH_EDGES;
    strncpy(req.consumer_label, "pirdetectord", sizeof(req.consumer_label) - 1);

    if (ioctl(chipFd, GPIO_GET_LINEEVENT_IOCTL, &req) != 0) {
        ALOGE("GPIO_GET_LINEEVENT_IOCTL failed: %s", strerror(errno));
        close(chipFd);
        return false;
    }
    close(chipFd);   // chipFd no longer needed — req.fd is our event fd

    mEventFd = req.fd;
    ALOGI("GpioHal initialized: %s line %d, event_fd=%d",
          chipPath.c_str(), lineOffset, mEventFd);
    return true;
}

bool GpioHal::waitForEdge(GpioEvent& outEvent) {
    // poll() on BOTH the GPIO event fd AND the shutdown pipe.
    // This call sleeps in the kernel scheduler until one of two things happens:
    //   a) Hardware GPIO interrupt fires (kernel delivers POLLIN on mEventFd)
    //   b) shutdown() writes to mShutdownPipe[1] (POLLIN on mShutdownPipe[0])
    struct pollfd fds[2] = {
        { mEventFd,          POLLIN, 0 },
        { mShutdownPipe[0],  POLLIN, 0 },
    };

    int ret = poll(fds, 2, -1 /* timeout: block forever */);
    if (ret < 0) {
        if (errno == EINTR) return false;  // signal interrupted — treat as shutdown
        ALOGE("poll() failed: %s", strerror(errno));
        return false;
    }

    // Check shutdown pipe first
    if (fds[1].revents & POLLIN) {
        ALOGI("GpioHal: shutdown signal received");
        return false;
    }

    // GPIO event ready
    if (fds[0].revents & POLLIN) {
        struct gpioevent_data event = {};
        ssize_t n = read(mEventFd, &event, sizeof(event));
        if (n != sizeof(event)) {
            ALOGE("read(event_fd) returned %zd, expected %zu", n, sizeof(event));
            return false;
        }
        outEvent.timestampNs = static_cast<int64_t>(event.timestamp);
        outEvent.edge = (event.id == GPIOEVENT_EVENT_RISING_EDGE)
                        ? EdgeType::RISING : EdgeType::FALLING;
        return true;
    }

    return false;
}

void GpioHal::shutdown() {
    if (mShutdownPipe[1] >= 0) {
        uint8_t byte = 1;
        write(mShutdownPipe[1], &byte, 1);  // wakes poll() in waitForEdge()
    }
}

void GpioHal::closeFds() {
    if (mEventFd >= 0)         { close(mEventFd); mEventFd = -1; }
    if (mShutdownPipe[0] >= 0) { close(mShutdownPipe[0]); mShutdownPipe[0] = -1; }
    if (mShutdownPipe[1] >= 0) { close(mShutdownPipe[1]); mShutdownPipe[1] = -1; }
}

}  // namespace myoem::pirdetector
```

### 1.3 `hal/pirdetector/Android.bp`

```bp
// vendor/myoem/hal/pirdetector/Android.bp
soong_namespace {}

cc_library_static {
    name: "libgpiohal_pirdetector",
    vendor: true,

    srcs: ["src/GpioHal.cpp"],

    export_include_dirs: ["include"],

    shared_libs: ["liblog"],

    cflags: [
        "-Wall",
        "-Wextra",
        "-Werror",
    ],
}
```

> **No special library needed** for `<linux/gpio.h>` — it is a kernel UAPI header
> shipped with every Android build. The `poll()`, `ioctl()`, and `read()` calls
> are from standard libc.

---

## Phase 2 — Binder Service with AIDL Callbacks

### 2.1 AIDL Files

**`MotionEvent.aidl`** — parcelable data bundle:
```aidl
// vendor/myoem/services/pirdetector/aidl/com/myoem/pirdetector/MotionEvent.aidl
package com.myoem.pirdetector;

/**
 * Data bundle sent to clients on every motion state change.
 *
 * motionState values:
 *   MOTION_DETECTED = 1  (rising edge — PIR OUT went HIGH)
 *   MOTION_ENDED    = 0  (falling edge — PIR OUT went LOW)
 */
@VintfStability
parcelable MotionEvent {
    /** MOTION_DETECTED (1) or MOTION_ENDED (0) */
    int motionState;

    /**
     * Monotonic kernel timestamp from the GPIO interrupt, in nanoseconds.
     * Use this for latency measurement or ordering events in the app.
     */
    long timestampNs;
}
```

**`IPirDetectorCallback.aidl`** — oneway callback (client implements this):
```aidl
// vendor/myoem/services/pirdetector/aidl/com/myoem/pirdetector/IPirDetectorCallback.aidl
package com.myoem.pirdetector;

import com.myoem.pirdetector.MotionEvent;

/**
 * IPirDetectorCallback — implemented by the client (Java manager).
 *
 * WHY oneway?
 *   The service's EventThread calls this callback from inside the GPIO interrupt
 *   handler loop. If the callback were synchronous, one slow or dead client would
 *   stall the entire event loop — blocking ALL other clients and missing hardware
 *   edges. With oneway, the call is enqueued in the client's Binder thread pool
 *   and the EventThread returns immediately, ready for the next edge.
 *
 * RESTRICTION: oneway methods must return void.
 */
@VintfStability
oneway interface IPirDetectorCallback {
    /**
     * Called by pirdetectord on every GPIO edge event.
     * Delivered on a Binder thread in the client's process.
     * The PirDetectorManager re-dispatches this to the main thread.
     *
     * @param event  the motion event (state + timestamp)
     */
    void onMotionEvent(in MotionEvent event);
}
```

**`IPirDetectorService.aidl`** — service interface:
```aidl
// vendor/myoem/services/pirdetector/aidl/com/myoem/pirdetector/IPirDetectorService.aidl
package com.myoem.pirdetector;

import com.myoem.pirdetector.IPirDetectorCallback;
import com.myoem.pirdetector.MotionEvent;

/**
 * IPirDetectorService — vendor daemon ↔ Java manager contract.
 *
 * Service name in ServiceManager:
 *   "com.myoem.pirdetector.IPirDetectorService"
 *
 * DESIGN: raw hardware pipe
 *   The service reports raw GPIO edges (RISING = motion detected, FALLING = ended).
 *   It does NOT debounce, filter, or apply business logic. The Java manager or the
 *   app is responsible for any UI logic (e.g., showing "Motion detected for 3s").
 */
@VintfStability
interface IPirDetectorService {

    /** Service API version. Current: 1. */
    int getVersion();

    /**
     * Returns the current GPIO state (1 = PIR HIGH / motion, 0 = PIR LOW / no motion).
     * Call once on connect to get an immediate snapshot before callbacks arrive.
     */
    int getCurrentState();

    /**
     * Subscribe to receive real-time motion events.
     *
     * The callback receives an onMotionEvent() call on every GPIO edge (RISING or FALLING).
     * Register in Activity.onStart() / onResume().
     *
     * Registering the same callback object twice is a no-op.
     *
     * @param callback  client's IPirDetectorCallback implementation
     */
    void registerCallback(IPirDetectorCallback callback);

    /**
     * Unsubscribe a previously registered callback.
     *
     * MUST be called in Activity.onStop() / onDestroy() to avoid memory leaks
     * in the service. The service holds a strong reference to the callback Binder.
     *
     * @param callback  same object passed to registerCallback()
     */
    void unregisterCallback(IPirDetectorCallback callback);
}
```

### 2.2 Service Header: `include/PirDetectorService.h`

```cpp
#pragma once
#define LOG_TAG "pirdetectord"

#include <aidl/com/myoem/pirdetector/BnPirDetectorService.h>
#include <aidl/com/myoem/pirdetector/IPirDetectorCallback.h>
#include <aidl/com/myoem/pirdetector/MotionEvent.h>
#include <android/binder_auto_utils.h>

#include "pirdetector/GpioHal.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace aidl::com::myoem::pirdetector {

class PirDetectorService : public BnPirDetectorService {
public:
    PirDetectorService();
    ~PirDetectorService() override;

    // Called from main() after construction — initializes GpioHal and starts EventThread
    bool start(const std::string& chipPath, int lineOffset);

    // ── IPirDetectorService AIDL methods ────────────────────────────────────
    ::ndk::ScopedAStatus getVersion(int32_t* out) override;
    ::ndk::ScopedAStatus getCurrentState(int32_t* out) override;
    ::ndk::ScopedAStatus registerCallback(
        const std::shared_ptr<IPirDetectorCallback>& callback) override;
    ::ndk::ScopedAStatus unregisterCallback(
        const std::shared_ptr<IPirDetectorCallback>& callback) override;

private:
    // Called when a registered client process dies (AIBinder_DeathRecipient)
    void onClientDied(void* cookie);
    static void onClientDiedStatic(void* cookie);

    // Background thread: blocks on GpioHal::waitForEdge(), notifies callbacks
    void eventLoop();

    // Notifies all registered callbacks with the given event (holds no lock)
    void notifyCallbacks(const MotionEvent& event);

    ::myoem::pirdetector::GpioHal                           mGpioHal;
    std::thread                                              mEventThread;
    std::atomic<int>                                         mCurrentState{0};

    std::mutex                                               mCallbacksMutex;
    std::vector<std::shared_ptr<IPirDetectorCallback>>       mCallbacks;

    ndk::ScopedAIBinder_DeathRecipient                       mDeathRecipient;
};

}  // namespace aidl::com::myoem::pirdetector
```

### 2.3 Service Implementation: `src/PirDetectorService.cpp` (key parts)

```cpp
// eventLoop — the heart of the callback-driven design
void PirDetectorService::eventLoop() {
    ALOGI("EventThread started — blocking on GPIO edge events");
    ::myoem::pirdetector::GpioEvent gpioEvent;

    while (mGpioHal.waitForEdge(gpioEvent)) {
        // Update current state
        int newState = (gpioEvent.edge == ::myoem::pirdetector::EdgeType::RISING) ? 1 : 0;
        mCurrentState.store(newState);

        ALOGI("GPIO edge: %s  (state=%d)",
              newState ? "RISING (motion detected)" : "FALLING (motion ended)", newState);

        // Build AIDL parcelable
        MotionEvent event;
        event.motionState  = newState;
        event.timestampNs  = gpioEvent.timestampNs;

        // Fire all registered callbacks (fire-and-forget due to oneway)
        notifyCallbacks(event);
    }
    ALOGI("EventThread exiting");
}

void PirDetectorService::notifyCallbacks(const MotionEvent& event) {
    std::lock_guard<std::mutex> lock(mCallbacksMutex);
    for (auto it = mCallbacks.begin(); it != mCallbacks.end(); ) {
        auto status = (*it)->onMotionEvent(event);
        if (!status.isOk()) {
            // Dead callback — remove it
            ALOGW("Callback dead, removing");
            it = mCallbacks.erase(it);
        } else {
            ++it;
        }
    }
}
```

### 2.4 `main.cpp`

```cpp
// vendor/myoem/services/pirdetector/src/main.cpp
#define LOG_TAG "pirdetectord"
#include <log/log.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include "PirDetectorService.h"

// GPIO configuration — verify chipPath with: adb shell cat /sys/bus/gpio/devices/gpiochip4/label
static constexpr const char* kGpioChipPath = "/dev/gpiochip4";
static constexpr int         kPirGpioLine  = 17;  // GPIO17 = BCM 17 = Pin 11

static constexpr const char* kServiceName =
    "com.myoem.pirdetector.IPirDetectorService";

int main() {
    ALOGI("pirdetectord starting");

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto service = ndk::SharedRefBase::make<
        aidl::com::myoem::pirdetector::PirDetectorService>();

    if (!service->start(kGpioChipPath, kPirGpioLine)) {
        ALOGE("Failed to initialize GPIO HAL — exiting");
        return 1;
    }

    binder_status_t status = AServiceManager_addService(
        service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("AServiceManager_addService failed: %d", status);
        return 1;
    }

    ALOGI("pirdetectord registered as '%s'", kServiceName);
    ABinderProcess_joinThreadPool();
    return 0;  // never reached
}
```

### 2.5 RC File: `pirdetectord.rc`

```
# vendor/myoem/services/pirdetector/pirdetectord.rc

service pirdetectord /vendor/bin/pirdetectord
    class main
    user system
    group system
    # Restart policy: if the daemon crashes, init restarts it after 4 seconds
    oneshot disabled

on boot
    # GPIO17 is driven by the PIR sensor — input only, no chown/chmod needed
    # (only read access required, and /dev/gpiochip4 handles permissions via group)
    start pirdetectord
```

> **Note**: `/dev/gpiochip4` permissions and SELinux rules control access.
> We do not need to chown individual GPIO pins (unlike sysfs pwm which needed
> chown root system in ThermalControl).

### 2.6 VINTF Manifest: `vintf/pirdetector.xml`

```xml
<!-- vendor/myoem/services/pirdetector/vintf/pirdetector.xml -->
<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>com.myoem.pirdetector</name>
        <version>1</version>
        <fqname>IPirDetectorService/default</fqname>
    </hal>
</manifest>
```

### 2.7 `services/pirdetector/Android.bp`

```bp
// vendor/myoem/services/pirdetector/Android.bp
soong_namespace {
    imports: ["vendor/myoem/hal/pirdetector"],
}

aidl_interface {
    name: "pirdetectorservice-aidl",
    owner: "myoem",
    vendor_available: true,
    stability: "vintf",
    srcs: [
        "aidl/com/myoem/pirdetector/MotionEvent.aidl",
        "aidl/com/myoem/pirdetector/IPirDetectorCallback.aidl",
        "aidl/com/myoem/pirdetector/IPirDetectorService.aidl",
    ],
    local_include_dir: "aidl",
    versions_with_info: [
        { version: "1", imports: [] },
    ],
    backend: {
        cpp:  { enabled: false },   // Never for vendor — kHeader='VNDR' incompatible
        java: { enabled: true  },   // For Java manager
        ndk:  { enabled: true  },   // For C++ daemon — LLNDK, kHeader='SYST'
        rust: { enabled: false },
    },
}

cc_binary {
    name: "pirdetectord",
    vendor: true,
    init_rc: ["pirdetectord.rc"],

    srcs: [
        "src/PirDetectorService.cpp",
        "src/main.cpp",
    ],

    local_include_dirs: ["include"],

    shared_libs: [
        "libbinder_ndk",
        "liblog",
    ],

    static_libs: [
        "pirdetectorservice-aidl-V1-ndk",
        "libgpiohal_pirdetector",           // from hal/pirdetector/
    ],

    cflags: ["-Wall", "-Wextra", "-Werror"],
}

vintf_fragment {
    name: "pirdetector-vintf-fragment",
    src: "vintf/pirdetector.xml",
    vendor: true,
}

// CLI test client
cc_binary {
    name: "pirdetector_client",
    vendor: true,

    srcs: ["test/pirdetector_client.cpp"],

    shared_libs: ["libbinder_ndk", "liblog"],
    static_libs: ["pirdetectorservice-aidl-V1-ndk"],

    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

---

## Phase 3 — Java Manager Library

### 3.1 `MotionListener.java`

```java
// vendor/myoem/libs/pirdetector/java/com/myoem/pirdetector/MotionListener.java
package com.myoem.pirdetector;

/**
 * Callback interface for PIR motion events.
 * All methods are called on the main (UI) thread.
 */
public interface MotionListener {
    /** Called when the PIR sensor detects motion (GPIO rising edge). */
    void onMotionDetected(long timestampNs);

    /** Called when motion stops (GPIO falling edge, PIR OUT goes LOW). */
    void onMotionEnded(long timestampNs);
}
```

### 3.2 `PirDetectorManager.java` (key design)

```java
// vendor/myoem/libs/pirdetector/java/com/myoem/pirdetector/PirDetectorManager.java
package com.myoem.pirdetector;

import android.os.IBinder;
import android.os.Handler;
import android.os.Looper;
import android.os.RemoteException;

public class PirDetectorManager {
    private static final String SERVICE_NAME =
        "com.myoem.pirdetector.IPirDetectorService";

    private final IPirDetectorService mService;
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());
    private MotionListener mListener;

    // The AIDL callback stub — receives calls on a Binder thread,
    // re-dispatches to MotionListener on the main thread.
    private final IPirDetectorCallback.Stub mCallback = new IPirDetectorCallback.Stub() {
        @Override
        public void onMotionEvent(MotionEvent event) {
            // This runs on a Binder thread — post to main thread
            mMainHandler.post(() -> {
                if (mListener == null) return;
                if (event.motionState == 1) {
                    mListener.onMotionDetected(event.timestampNs);
                } else {
                    mListener.onMotionEnded(event.timestampNs);
                }
            });
        }
    };

    // Constructor: receives IBinder from the app (which calls ServiceManager.checkService)
    public PirDetectorManager(IBinder binder) {
        mService = IPirDetectorService.Stub.asInterface(binder);
    }

    public void registerListener(MotionListener listener) throws RemoteException {
        mListener = listener;
        mService.registerCallback(mCallback);
    }

    public void unregisterListener() throws RemoteException {
        mService.unregisterCallback(mCallback);
        mListener = null;
    }

    /** Synchronous snapshot — call once to get current state without waiting for event. */
    public int getCurrentState() throws RemoteException {
        return mService.getCurrentState();
    }
}
```

### 3.3 `libs/pirdetector/Android.bp`

```bp
// vendor/myoem/libs/pirdetector/Android.bp
soong_namespace {
    imports: ["vendor/myoem/services/pirdetector"],
}

java_library {
    name: "pirdetector-manager",
    vendor: true,

    srcs: ["java/**/*.java"],

    libs: [
        "pirdetectorservice-aidl-V1-java",
    ],

    // Uses ServiceManager — the app (platform_apis:true) passes the IBinder in.
    // This library itself only needs public SDK types.
    sdk_version: "system_current",
}
```

---

## Phase 4 — Kotlin / Compose App

### 4.1 Contract: `MotionContract.kt`

```kotlin
// MVI contract
data class MotionUiState(
    val motionDetected: Boolean = false,
    val lastEventTimestampNs: Long = 0L,
    val error: String? = null
)

sealed class MotionUiEvent {
    data object StartMonitoring : MotionUiEvent()
    data object StopMonitoring  : MotionUiEvent()
}

sealed class MotionUiEffect {
    data class ShowError(val message: String) : MotionUiEffect()
}
```

### 4.2 ViewModel: `MotionViewModel.kt`

```kotlin
class MotionViewModel(application: Application) : AndroidViewModel(application) {
    private val _uiState  = MutableStateFlow(MotionUiState())
    val uiState: StateFlow<MotionUiState> = _uiState.asStateFlow()

    private val _uiEffect = MutableSharedFlow<MotionUiEffect>()
    val uiEffect: SharedFlow<MotionUiEffect> = _uiEffect.asSharedFlow()

    private var manager: PirDetectorManager? = null

    fun onEvent(event: MotionUiEvent) {
        when (event) {
            MotionUiEvent.StartMonitoring -> startMonitoring()
            MotionUiEvent.StopMonitoring  -> stopMonitoring()
        }
    }

    private fun startMonitoring() {
        val binder = ServiceManager.checkService(
            "com.myoem.pirdetector.IPirDetectorService")
        if (binder == null) {
            viewModelScope.launch {
                _uiEffect.emit(MotionUiEffect.ShowError("pirdetectord not running"))
            }
            return
        }

        manager = PirDetectorManager(binder)
        val listener = object : MotionListener {
            override fun onMotionDetected(timestampNs: Long) {
                _uiState.update { it.copy(motionDetected = true, lastEventTimestampNs = timestampNs) }
            }
            override fun onMotionEnded(timestampNs: Long) {
                _uiState.update { it.copy(motionDetected = false, lastEventTimestampNs = timestampNs) }
            }
        }
        manager?.registerListener(listener)

        // Sync initial state
        val currentState = manager?.getCurrentState() ?: 0
        _uiState.update { it.copy(motionDetected = currentState == 1) }
    }

    private fun stopMonitoring() {
        manager?.unregisterListener()
        manager = null
    }

    override fun onCleared() {
        super.onCleared()
        stopMonitoring()
    }
}
```

### 4.3 Screen: `MotionScreen.kt`

```kotlin
@Composable
fun MotionScreen(
    uiState: MotionUiState,
    onEvent: (MotionUiEvent) -> Unit
) {
    val motionColor = if (uiState.motionDetected)
        MaterialTheme.colorScheme.error
    else
        MaterialTheme.colorScheme.surfaceVariant

    Column(
        modifier = Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // Pulsing indicator
        Box(
            modifier = Modifier
                .size(160.dp)
                .clip(CircleShape)
                .background(motionColor),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = if (uiState.motionDetected) "MOTION" else "IDLE",
                style = MaterialTheme.typography.headlineMedium,
                color = MaterialTheme.colorScheme.onError
            )
        }

        Spacer(modifier = Modifier.height(32.dp))

        Text(
            text = if (uiState.motionDetected) "Object Detected" else "No Object Present",
            style = MaterialTheme.typography.titleLarge
        )
    }
}
```

### 4.4 `apps/PirDetectorApp/Android.bp`

```bp
// vendor/myoem/apps/PirDetectorApp/Android.bp
soong_namespace {
    imports: [
        "vendor/myoem/services/pirdetector",
        "vendor/myoem/libs/pirdetector",
    ],
}

android_app {
    name: "PirDetectorApp",
    vendor: true,

    srcs: ["src/**/*.kt"],

    // platform_apis: true allows calling @hide ServiceManager
    platform_apis: true,
    certificate: "platform",

    static_libs: [
        "pirdetector-manager",
        "pirdetectorservice-aidl-V1-java",
        // Compose BOM — use versions from your existing ThermalMonitor app
        "androidx.compose.ui_ui",
        "androidx.compose.material3_material3",
        "androidx.compose.ui_ui-tooling",
        "androidx.activity_activity-compose",
        "androidx.lifecycle_lifecycle-viewmodel-compose",
        "androidx.lifecycle_lifecycle-runtime-compose",
    ],

    manifest: "AndroidManifest.xml",
}
```

---

## 10. SELinux Policy

### `sepolicy/private/pirdetectord.te`

```
# vendor/myoem/services/pirdetector/sepolicy/private/pirdetectord.te

# Define the domain for pirdetectord
type pirdetectord, domain;
type pirdetectord_exec, exec_type, vendor_file_type, file_type;

# Domain transition: init → pirdetectord on exec of pirdetectord_exec
init_daemon_domain(pirdetectord)

# Allow Binder IPC
binder_use(pirdetectord)
binder_service(pirdetectord)

# Allow service registration in ServiceManager
allow pirdetectord servicemanager:binder { call transfer };
allow pirdetectord pirdetector_service:service_manager { add find };

# ── GPIO character device access ──────────────────────────────────────────────
# /dev/gpiochip4 — verify actual SELinux type with:
#   adb shell ls -laZ /dev/gpiochip4
# If type is gpio_device (AOSP base policy defines this):
allow pirdetectord gpio_device:chr_file { open read ioctl };

# If the device appears as a different type, add a custom type:
# type pirdetectord_gpio, dev_type;
# and label /dev/gpiochip4 in file_contexts:
# /dev/gpiochip4  u:object_r:pirdetectord_gpio:s0
# allow pirdetectord pirdetectord_gpio:chr_file { open read ioctl };

# Allow logging
allow pirdetectord log_device:chr_file { open read write };
```

> **Critical step before writing policy**: Check the actual SELinux type:
> ```bash
> adb shell ls -laZ /dev/gpiochip4
> # Output example: crw-rw---- root system u:object_r:gpio_device:s0
> ```
> Use `gpio_device` if that's what you see. If it's `sysfs` or a custom type,
> adjust the `.te` accordingly. Never assume — this is the lesson from hwmon on RPi5.

### `sepolicy/private/service_contexts`

```
# vendor/myoem/services/pirdetector/sepolicy/private/service_contexts
com.myoem.pirdetector.IPirDetectorService    u:object_r:pirdetector_service:s0
```

### `sepolicy/private/file_contexts`

```
# vendor/myoem/services/pirdetector/sepolicy/private/file_contexts
/vendor/bin/pirdetectord    u:object_r:pirdetectord_exec:s0
```

---

## 11. Build Integration

### `myoem_base.mk` additions

```makefile
# PIR Detector — Phase 1 (HAL only): no additions needed
# PIR Detector — Phase 2 (Service):
PRODUCT_SOONG_NAMESPACES        += vendor/myoem/hal/pirdetector
PRODUCT_SOONG_NAMESPACES        += vendor/myoem/services/pirdetector
PRODUCT_PACKAGES                += pirdetectord pirdetector_client
PRODUCT_PRIVATE_SEPOLICY_DIRS   += vendor/myoem/services/pirdetector/sepolicy/private

# PIR Detector — Phase 3 (Java Manager):
PRODUCT_SOONG_NAMESPACES        += vendor/myoem/libs/pirdetector

# PIR Detector — Phase 4 (App):
PRODUCT_SOONG_NAMESPACES        += vendor/myoem/apps/PirDetectorApp
PRODUCT_PACKAGES                += PirDetectorApp
```

### Build commands (incremental)

```bash
# Phase 1 — HAL only
lunch myoem_rpi5-trunk_staging-userdebug
m libgpiohal_pirdetector

# Phase 2 — Service
m pirdetectord pirdetector_client

# Freeze AIDL API (required for vintf stability — run once after first AIDL draft)
m pirdetectorservice-aidl-update-api

# Phase 3 — Manager
m pirdetector-manager

# Phase 4 — App
m PirDetectorApp

# Full vendor image
m vendorimage
```

### Dev-iteration push (without full flash)

```bash
# Push service binary
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/pirdetectord /vendor/bin/pirdetectord
adb shell chmod 755 /vendor/bin/pirdetectord
adb shell stop pirdetectord && adb shell start pirdetectord

# Push app APK
adb push out/target/product/rpi5/vendor/app/PirDetectorApp/PirDetectorApp.apk \
    /vendor/app/PirDetectorApp/PirDetectorApp.apk
adb shell pm install -r /vendor/app/PirDetectorApp/PirDetectorApp.apk
```

---

## 12. Testing Strategy

### Phase 0: Hardware

```bash
# 1. Confirm gpiochip path
adb shell cat /sys/bus/gpio/devices/gpiochip4/label    # expect: pinctrl-rp1

# 2. Confirm PIR sensor responds
adb shell "echo 17 > /sys/class/gpio/export"
adb shell "echo in > /sys/class/gpio/gpio17/direction"
adb shell cat /sys/class/gpio/gpio17/value            # 0 = no motion
# (wave hand) 
adb shell cat /sys/class/gpio/gpio17/value            # 1 = motion
adb shell "echo 17 > /sys/class/gpio/unexport"
```

### Phase 2: Service

```bash
# 1. Verify service started
adb shell ps -A | grep pirdetector

# 2. Check its SELinux domain
adb shell ps -eZ | grep pirdetector    # expect: u:r:pirdetectord:s0

# 3. Check service registered in ServiceManager
adb shell service list | grep pirdetector
# expect: com.myoem.pirdetector.IPirDetectorService

# 4. Run CLI test client
adb shell /vendor/bin/pirdetector_client
# Client should print current state and then show events as you wave hand

# 5. Check for SELinux denials
adb logcat -d | grep "avc: denied" | grep pirdetect

# 6. Check logcat
adb logcat -s pirdetectord GpioHal
```

### Phase 4: App

```bash
# 1. Launch app
adb shell am start -n com.myoem.pirdetectorapp/.MainActivity

# 2. Wave hand in front of sensor — app should show "MOTION" / "Object Detected"
# 3. Remove hand — app should show "IDLE" / "No Object Present"
# 4. Kill app, verify no leak in service (callbacks properly unregistered)
adb logcat -s pirdetectord | grep "callback"
```

---

## 13. Article Series Outline

**Series title**: *"Building an Interrupt-Driven PIR Motion Detector on AOSP — Full Vendor Stack"*

| Part | Title | Content |
|------|-------|---------|
| 1 | **How PIR Sensors Work** | Pyroelectric effect, HC-SR501 datasheet, signal characteristics, wiring |
| 2 | **Architecture — The Callback Stack** | Why callbacks beat polling; GPIO char device vs sysfs; full architecture diagram |
| 3 | **Phase 0 — Hardware Verification** | Shell commands to test GPIO, gpiochip discovery, sysfs quick-check |
| 4 | **Phase 1 — GPIO HAL** | Linux GPIO V1 ioctl API, `GPIO_GET_LINEEVENT_IOCTL`, poll(), self-pipe trick, Android.bp |
| 5 | **Phase 2a — AIDL Design** | oneway callbacks, MotionEvent parcelable, VINTF stability, why NDK backend |
| 6 | **Phase 2b — The Service (pirdetectord)** | EventThread design, DeathRecipient, notifyCallbacks, main.cpp |
| 7 | **Phase 2c — RC File, VINTF, SELinux** | gpio_device type, file_contexts, service_contexts, .te policy |
| 8 | **Phase 3 — Java Manager Library** | PirDetectorManager, MotionListener, Handler re-dispatch, sdk_version choices |
| 9 | **Phase 4 — Kotlin / Compose App** | MVI contract, ViewModel, Compose UI, ServiceManager.checkService() |
| 10 | **Debugging Guide** | avc: denied, gpiochip path quirks, callback leak debugging, logcat commands |
| 11 | **Lessons Learned** | Interrupt vs polling trade-offs, UAPI vs libgpiod, VINTF stability recap |

---

## 14. Phase Checklist

### Phase 0 — Hardware ✅ / ⬜
- [ ] HC-SR501 wired to RPi5 (VCC→Pin2, GND→Pin6, OUT→Pin11)
- [ ] Verify gpiochip path: `adb shell cat /sys/bus/gpio/devices/gpiochip4/label`
- [ ] Test PIR via sysfs: value changes 0→1 on motion
- [ ] Record actual SELinux type of `/dev/gpiochip4`

### Phase 1 — GPIO HAL ⬜
- [ ] `hal/pirdetector/` directory created
- [ ] `GpioHal.h` written (init, waitForEdge, shutdown)
- [ ] `GpioHal.cpp` written (GPIO_GET_LINEEVENT_IOCTL + poll + self-pipe)
- [ ] `Android.bp` written (`cc_library_static libgpiohal_pirdetector`)
- [ ] Builds: `m libgpiohal_pirdetector`

### Phase 2 — Binder Service ⬜
- [ ] AIDL files written (`MotionEvent`, `IPirDetectorCallback`, `IPirDetectorService`)
- [ ] `PirDetectorService.h` + `.cpp` written (EventThread, callbacks, DeathRecipient)
- [ ] `main.cpp` written
- [ ] `pirdetectord.rc` written
- [ ] `vintf/pirdetector.xml` written
- [ ] SELinux files written (`.te`, `file_contexts`, `service_contexts`)
- [ ] `Android.bp` written for service + test client
- [ ] API frozen: `m pirdetectorservice-aidl-update-api`
- [ ] Builds: `m pirdetectord pirdetector_client`
- [ ] `myoem_base.mk` updated (Namespaces, PRODUCT_PACKAGES, SEPOLICY)
- [ ] Full image flashed / pushed, service starts on boot
- [ ] CLI client confirms events on motion
- [ ] No SELinux denials in logcat

### Phase 3 — Java Manager ⬜
- [ ] `MotionListener.java` written
- [ ] `PirDetectorManager.java` written (IBinder constructor, registerListener, unregisterListener, getCurrentState)
- [ ] `Android.bp` written (`java_library pirdetector-manager`)
- [ ] Builds: `m pirdetector-manager`

### Phase 4 — Kotlin App ⬜
- [ ] `MotionContract.kt` written (UiState, UiEvent, UiEffect)
- [ ] `MotionViewModel.kt` written (MVI, ServiceManager.checkService, lifecycle)
- [ ] `MotionScreen.kt` written (Compose, color indicator, status text)
- [ ] `AndroidManifest.xml` written
- [ ] `Android.bp` written (`android_app PirDetectorApp`)
- [ ] Builds: `m PirDetectorApp`
- [ ] App launches, shows current PIR state on connect
- [ ] Motion detected → UI updates in real-time
- [ ] Motion ends → UI updates back to IDLE
- [ ] App killed → no callback leak (verify in logcat)
