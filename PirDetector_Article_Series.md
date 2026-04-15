# Building an Interrupt-Driven PIR Motion Detector on AOSP
## From a Breadboard Wire to a Jetpack Compose App — Full Vendor Stack on Raspberry Pi 5

**A Complete Engineering Journal**

---

> **About this article**: This is a real engineering journal of building a complete
> vendor-only AOSP stack — GPIO HAL → Interrupt-driven Binder Service → Java Manager
> → Kotlin App — on a Raspberry Pi 5 running Android 15 (`android-15.0.0_r14`).
> Every hardware decision, every architectural trade-off, every error message, and every
> debugging command is documented exactly as it happened.
>
> **What makes this project different from the previous ones in this series**: The sensor
> does not need to be polled. The HC-SR501 PIR sensor raises a GPIO pin the instant it
> detects motion. The right architecture here is **interrupt-driven callbacks** — the
> daemon sleeps in the kernel scheduler until the hardware fires, then immediately
> notifies all registered Android app clients. Zero busy-waiting.

---

## Table of Contents

1. [How PIR Sensors Work — Hardware Foundations](#part-1)
2. [Architecture — The Full Callback Stack](#part-2)
3. [Phase 0 — Hardware Verification (No Code Required)](#part-3)
4. [Phase 1 — The GPIO HAL in C++](#part-4)
5. [Phase 2a — Designing the AIDL Interface](#part-5)
6. [Phase 2b — The Binder Service (pirdetectord)](#part-6)
7. [Phase 2c — RC File, VINTF Manifest, and SELinux](#part-7)
8. [Phase 3 — Java Manager Library](#part-8)
9. [Phase 4 — Kotlin / Jetpack Compose App](#part-9)
10. [Debugging Guide — Layer by Layer](#part-10)
11. [Lessons Learned](#part-11)

---

<a name="part-1"></a>
# Part 1: How PIR Sensors Work — Hardware Foundations

## The Physical Principle

PIR stands for **Passive Infrared**. Unlike active sensors (ultrasonic, laser), a PIR
sensor does not emit anything. It passively detects infrared radiation — heat — emitted
by all objects above absolute zero.

The key component inside an HC-SR501 is a **pyroelectric element**: a crystal that
generates a small electric charge when its temperature changes. The element is split into
two halves. When a warm body (person, animal, or object) moves in front of the sensor,
one half is illuminated by the Fresnel lens before the other. The difference in infrared
energy between the two halves generates a voltage change. That change is amplified,
compared against a threshold, and converted into a clean digital output signal.

```
                     ┌─────────────────────────────────────┐
                     │         HC-SR501 INTERNALS           │
                     │                                     │
                     │  [Fresnel Lens]                     │
                     │      │                              │
                     │      ▼                              │
                     │  [Pyroelectric Element]             │
                     │   (2 halves in opposition)          │
                     │      │                              │
                     │      ▼                              │
                     │  [Op-Amp / Comparator]              │
                     │      │                              │
                     │      ▼                              │
                     │  [BISS0001 controller IC]           │
                     │      │                              │
                     │  OUT pin ──────────────────────────► GPIO17 on RPi5
                     └─────────────────────────────────────┘
```

**Important**: The sensor detects **motion**, not presence. A completely stationary person
standing in front of the sensor will eventually cause the OUT signal to drop back LOW.

## The HC-SR501 Output Signal

The HC-SR501 has a 3.3V digital output (compatible with RPi5 GPIO directly):

| OUT Voltage | Meaning |
|------------|---------|
| 3.3V (HIGH) | Motion detected — infrared delta above threshold |
| 0V (LOW) | No motion (or motion ended, after Tx delay) |

There are two **trigger modes**, selected by a jumper on the PCB:

| Jumper Position | Mode | Behaviour |
|----------------|------|-----------|
| H (repeating) | Continuous | OUT stays HIGH as long as motion continues. HIGH extends on each new detection. |
| L (single) | One-shot | OUT goes HIGH once, then waits Tx delay, then goes LOW regardless of motion. |

**For this project, use H (repeating trigger)**. This gives us clean RISING on motion
start and FALLING on motion end — exactly what the AIDL callback model needs.

Two potentiometers adjust the sensor:
- **Sx** (sensitivity): range ~3m to ~7m
- **Tx** (delay): how long OUT stays HIGH after last detection, ~3s to ~300s

Set Tx to minimum for the fastest response during testing.

## Hardware Required

| # | Component | Purpose |
|---|-----------|---------|
| 1 | **HC-SR501 PIR sensor** | The motion detector |
| 2 | Breadboard (half-size minimum) | Prototyping the connections |
| 3 | Male-to-female jumper wires ×3 | RPi5 GPIO header to breadboard |
| 4 | Female-to-female jumper wires ×3 | Breadboard to HC-SR501 pins |
| 5 | Raspberry Pi 5 running AOSP 15 | Host device |

**Alternative**: AM312 mini PIR sensor. Operates at 3.3V directly (no 5V line needed),
fixed 2-second delay, no adjustments. Wiring and all code are identical — it is a drop-in
replacement.

## Wiring

The HC-SR501 has 3 pins on one edge of the board:

```
┌─────────────────────────────────────────────┐
│           HC-SR501 (viewed from front)       │
│                                             │
│        [Fresnel Dome Lens]                  │
│                                             │
│    VCC         OUT         GND              │
│    (1)         (2)         (3)              │
└─────────────────────────────────────────────┘
```

Connect to the RPi5 40-pin header:

```
RPi5 Header (BCM numbering)          HC-SR501
────────────────────────────────────────────────
Pin 2   (5V power)      ──────────── VCC
Pin 6   (Ground)        ──────────── GND
Pin 11  (GPIO17 / BCM17)──────────── OUT
```

```
Physical breadboard layout:
                                                  HC-SR501
   RPi5 Pin 2  (5V)  ──────────────────────────── VCC
   RPi5 Pin 6  (GND) ──────────────────────────── GND
   RPi5 Pin 11 (GPIO17) ────[ 1kΩ optional ]───── OUT

   Note: The 1kΩ resistor between GPIO17 and OUT is optional.
   HC-SR501 output is 3.3V which is within RPi5 GPIO spec.
   The resistor is a belt-and-suspenders current limiter only.
```

> **Why GPIO17?** It is a general-purpose, tested I/O pin with no conflicting functions
> (not SPI, I2C, UART, or PWM). It is far from the SPI pins used in the PotVolume
> project, avoiding any hardware conflicts.

---

<a name="part-2"></a>
# Part 2: Architecture — The Full Callback Stack

## Polling vs Callbacks — The Core Design Decision

In the PotVolume project, the daemon ran a tight poll loop:
```
while(true) { read SPI → map value → set volume → sleep 50ms }
```

For the PIR sensor, polling would waste CPU reading a value that changes rarely (only
when someone walks past). More importantly, polling introduces latency: a 50ms poll
interval means detection could be delayed by up to 50ms.

The Linux kernel already knows the instant the GPIO pin changes state — it is notified
by the hardware interrupt controller. Our job is simply to ask the kernel to wake us up
when that happens. That is the `poll()` system call combined with the GPIO event file
descriptor.

```
Polling approach (PotVolume):
  daemon wakes every 50ms → reads GPIO → CPU burn → up to 50ms latency

Interrupt-driven approach (this project):
  daemon calls poll() → kernel puts thread to SLEEP → hardware interrupt fires
  → kernel wakes the thread instantly → daemon fires callbacks
  CPU usage: 0% while idle. Latency: microseconds.
```

## The GPIO Character Device API

The sysfs GPIO interface (`/sys/class/gpio/`) is deprecated since kernel 4.8. The
official replacement is the **GPIO character device** (`/dev/gpiochipN`). This API:

- Returns a real file descriptor from `ioctl()` that supports `poll()`
- Captures the hardware interrupt timestamp inside the kernel interrupt handler
  (more accurate than reading a timestamp after userspace wakes up)
- Is what `libgpiod` wraps internally — we use the same ioctls directly

The relevant kernel header is `<linux/gpio.h>` (UAPI — always available in the Android
build system). No external library dependency.

## The Complete Stack

```
┌──────────────────────────────────────────────────────────────────────┐
│                  PirDetectorApp (Kotlin / Compose)                   │
│                                                                      │
│   MotionViewModel.onEvent(Connect)                                   │
│   MotionUiState { motionDetected, lastEventTimestampNs }             │
│   MotionScreen — animated circle indicator                           │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  Java API (same process)
┌──────────────────────────────▼───────────────────────────────────────┐
│            pirdetector-manager (java_library)                        │
│                                                                      │
│   PirDetectorManager(IBinder binder)                                 │
│   registerListener(MotionListener)                                   │
│   unregisterListener()                                               │
│   getCurrentState() → 0 or 1                                        │
│                                                                      │
│   Internally: implements IPirDetectorCallback.Stub                   │
│   Dispatches Binder thread callbacks → main thread via Handler      │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  Binder IPC (VINTF-stable AIDL)
┌──────────────────────────────▼───────────────────────────────────────┐
│          pirdetectord (C++ vendor daemon)                            │
│                                                                      │
│   EventThread: GpioHal::waitForEdge() → notifyCallbacks()           │
│   Binder pool: registerCallback / unregisterCallback / getVersion   │
│   AIBinder_DeathRecipient: auto-removes dead client callbacks        │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  ioctl + poll()
┌──────────────────────────────▼───────────────────────────────────────┐
│           GpioHal (cc_library_static)                               │
│                                                                      │
│   init("/dev/gpiochip4", 17) → GPIO_GET_LINEEVENT_IOCTL → event_fd │
│   waitForEdge() → poll(event_fd | shutdown_pipe) → read() edge data │
│   shutdown() → write to self-pipe → poll() returns → thread exits   │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  hardware interrupt → kernel GPIO driver
┌──────────────────────────────▼───────────────────────────────────────┐
│           Linux Kernel — RP1 GPIO Controller (/dev/gpiochip4)       │
│                                                                      │
│   GPIO line 17 → interrupt on rising AND falling edges              │
│   Delivers gpioevent_data { timestamp_ns, edge_id } to event_fd    │
└──────────────────────────────┬───────────────────────────────────────┘
                               │  3.3V / 0V signal
┌──────────────────────────────▼───────────────────────────────────────┐
│                    HC-SR501 PIR Sensor                               │
│                                                                      │
│   OUT = 3.3V (HIGH) when motion detected (rising edge)             │
│   OUT = 0V   (LOW)  when motion ends    (falling edge)             │
└──────────────────────────────────────────────────────────────────────┘
```

## File Layout

```
vendor/myoem/
├── hal/pirdetector/
│   ├── Android.bp                     ← cc_library_static libgpiohal_pirdetector
│   ├── include/pirdetector/
│   │   └── GpioHal.h                  ← GpioHal class interface
│   └── src/
│       └── GpioHal.cpp                ← ioctl + poll() + self-pipe
│
├── services/pirdetector/
│   ├── Android.bp                     ← aidl_interface + cc_binary + vintf_fragment
│   ├── aidl/com/myoem/pirdetector/
│   │   ├── MotionEvent.aidl           ← parcelable: motionState + timestampNs
│   │   ├── IPirDetectorCallback.aidl  ← oneway callback: service → client
│   │   └── IPirDetectorService.aidl   ← main interface: client → service
│   ├── include/
│   │   └── PirDetectorService.h
│   ├── src/
│   │   ├── PirDetectorService.cpp     ← EventThread + callbacks + DeathRecipient
│   │   └── main.cpp                   ← AServiceManager_addService + thread pool
│   ├── pirdetectord.rc                ← init service definition
│   ├── vintf/pirdetector.xml          ← VINTF manifest fragment
│   ├── test/pirdetector_client.cpp    ← CLI test client
│   └── sepolicy/private/
│       ├── pirdetectord.te            ← SELinux domain policy
│       ├── file_contexts              ← /vendor/bin/pirdetectord label
│       └── service_contexts           ← ServiceManager service label
│
├── libs/pirdetector/
│   ├── Android.bp                     ← java_library pirdetector-manager
│   └── java/com/myoem/pirdetector/
│       ├── MotionListener.java        ← app-facing callback interface
│       └── PirDetectorManager.java    ← IBinder → MotionListener bridge
│
└── apps/PirDetectorApp/
    ├── Android.bp                     ← android_app PirDetectorApp
    ├── AndroidManifest.xml
    └── src/com/myoem/pirdetectorapp/
        ├── MotionContract.kt          ← UiState, UiEvent, UiEffect
        ├── MotionViewModel.kt         ← MVI ViewModel + ServiceManager call
        ├── MainActivity.kt            ← Compose host + DisposableEffect lifecycle
        └── ui/
            └── MotionScreen.kt        ← stateless Compose screen + 3 previews
```

---

<a name="part-3"></a>
# Part 3: Phase 0 — Hardware Verification (No Code Required)

Before writing a single line of C++, spend 10 minutes verifying the hardware.
This saves hours of debugging later.

## Step 1: Find the GPIO Character Device for the 40-Pin Header

On Raspberry Pi 5, the 40-pin header GPIO controller is the **RP1** south bridge chip —
a separate silicon die from the BCM2712 main SoC. The RP1 registers as its own gpiochip
in Linux.

```bash
# List all gpiochip devices on the RPi5
adb shell ls /dev/gpiochip*
# Output: /dev/gpiochip0  /dev/gpiochip4  /dev/gpiochip10  ...

# Find which one is the 40-pin header (RP1)
adb shell cat /sys/bus/gpio/devices/gpiochip0/label
# Might be: "raspberrypi-exp-gpio" (the on-board LED/power controller)

adb shell cat /sys/bus/gpio/devices/gpiochip4/label
# Expected: "pinctrl-rp1"  ← this is the 40-pin header GPIO controller
```

> **Record this path.** The kernel enumeration can change between AOSP builds
> (we learned this lesson with `/dev/spidev10.0` in the PotVolume project). The
> `main.cpp` of `pirdetectord` has `kGpioChipPath = "/dev/gpiochip4"` — update
> this if your device shows a different number.

## Step 2: Check the SELinux Label of the GPIO Device

```bash
adb shell ls -laZ /dev/gpiochip4
# Output example:
# crw-rw---- root system u:object_r:gpio_device:s0 /dev/gpiochip4
#                         ^^^^^^^^^^^^^^^^^^^^^^^^^
#                         Record this type!
```

You need this label for the SELinux policy. If the type is anything other than
`gpio_device`, update `pirdetectord.te` accordingly before building.

## Step 3: Verify the PIR Sensor via Legacy sysfs

Before using the character device API, do a quick sanity check with the simpler sysfs
interface. This confirms the sensor is wired correctly and responding.

```bash
# Become root (needed to write to sysfs)
adb root

# Export GPIO17 to sysfs (makes it accessible as a file)
adb shell "echo 17 > /sys/class/gpio/export"

# Set direction to input (PIR is an input device)
adb shell "echo in > /sys/class/gpio/gpio17/direction"

# Verify: should read 'in'
adb shell cat /sys/class/gpio/gpio17/direction

# Read the current value (0 = no motion, 1 = motion)
adb shell cat /sys/class/gpio/gpio17/value
# Expected when nothing is moving: 0

# Wave your hand in front of the sensor
adb shell cat /sys/class/gpio/gpio17/value
# Expected when motion detected: 1

# Real-time monitoring: read once per 200ms, watch it change
adb shell "while true; do cat /sys/class/gpio/gpio17/value; sleep 0.2; done"
# Output: 0 0 0 0 1 1 1 1 1 0 0 0  ← note the 1s when hand waves

# Clean up
adb shell "echo 17 > /sys/class/gpio/unexport"
```

**If the value never changes**: Check wiring. VCC → Pin 2, GND → Pin 6, OUT → Pin 11.
Check that the jumper is in the H position. Try bringing your hand very close to the lens.

**If the value is stuck at 1**: The sensor may be in single-trigger mode (L position).
Move the jumper to H (repeating trigger).

## Step 4: Check GPIO Permissions

```bash
# Verify the device file is accessible by the 'system' user
# (pirdetectord runs as system)
adb shell ls -la /dev/gpiochip4
# crw-rw---- root system ... /dev/gpiochip4
#                 ^^^^^^ group=system — pirdetectord (user=system) can open it

# Verify our target GPIO line is not already claimed by another process
adb shell cat /sys/kernel/debug/gpio | grep 17
# Should be empty — line 17 is unclaimed
```

---

<a name="part-4"></a>
# Part 4: Phase 1 — The GPIO HAL in C++

## Design: Why a Static Library?

The GPIO HAL is a `cc_library_static` rather than a `cc_library_shared`. In the
ThermalControl project, the HAL was a shared library because it was independently
testable and potentially replaceable. Here, only one consumer exists: `pirdetectord`.
A static library is simpler — it links directly into the binary, no `.so` on the
vendor partition, no SONAME to manage.

## The GpioHal Interface

```cpp
// vendor/myoem/hal/pirdetector/include/pirdetector/GpioHal.h

enum class EdgeType { RISING, FALLING };

struct GpioEvent {
    EdgeType edge;
    int64_t  timestampNs;  // from the kernel interrupt handler (CLOCK_MONOTONIC)
};

class GpioHal {
public:
    bool init(const std::string& chipPath, int lineOffset);
    bool waitForEdge(GpioEvent& outEvent);  // BLOCKS until edge or shutdown
    void shutdown();                         // thread-safe, wakes waitForEdge()
};
```

Three methods. That is the entire HAL interface.

## The Core ioctl: GPIO_GET_LINEEVENT_IOCTL

The Linux GPIO V1 character device API provides a single ioctl to request an input line
for edge-event monitoring:

```cpp
// Open the gpiochip character device
int chipFd = open("/dev/gpiochip4", O_RDONLY | O_CLOEXEC);

// Fill the request structure
struct gpioevent_request req = {};
req.lineoffset  = 17;                         // GPIO17 = BCM 17 = Pin 11
req.handleflags = GPIOHANDLE_REQUEST_INPUT;   // configure as input
req.eventflags  = GPIOEVENT_REQUEST_BOTH_EDGES; // interrupt on RISING and FALLING
strncpy(req.consumer_label, "pirdetectord", sizeof(req.consumer_label) - 1);

// Issue the ioctl
ioctl(chipFd, GPIO_GET_LINEEVENT_IOCTL, &req);
// req.fd is now a file descriptor that supports poll()

close(chipFd);     // chip fd no longer needed
mEventFd = req.fd; // this is the fd we poll on
```

After this ioctl:
- The kernel has configured the GPIO interrupt for both rising and falling edges
- `req.fd` is a new file descriptor
- `poll(req.fd, POLLIN)` sleeps until the next edge interrupt
- `read(req.fd, &event, sizeof(event))` returns a `gpioevent_data` with the timestamp
  and edge direction

## The waitForEdge() Implementation

The heart of the interrupt-driven design:

```cpp
bool GpioHal::waitForEdge(GpioEvent& outEvent) {
    // Monitor both the GPIO event fd AND the shutdown self-pipe
    struct pollfd fds[2];
    fds[0] = { mEventFd,         POLLIN, 0 };  // GPIO hardware interrupt
    fds[1] = { mShutdownPipe[0], POLLIN, 0 };  // shutdown signal

    // This call sleeps in the kernel scheduler until one of two things happens:
    //   a) Hardware interrupt fires (GPIO edge) → fds[0] becomes POLLIN-ready
    //   b) shutdown() writes a byte to the pipe → fds[1] becomes POLLIN-ready
    //
    // CPU usage while blocking: exactly 0%
    int ret = poll(fds, 2, -1 /* block forever */);

    if (ret < 0) { /* error or signal */ return false; }

    // Check shutdown first — if both are ready, honour shutdown
    if (fds[1].revents & POLLIN) {
        ALOGI("GpioHal: shutdown signal received");
        return false;
    }

    // GPIO edge ready — read the event data
    if (fds[0].revents & POLLIN) {
        struct gpioevent_data event = {};
        read(mEventFd, &event, sizeof(event));

        outEvent.timestampNs = static_cast<int64_t>(event.timestamp);
        outEvent.edge = (event.id == GPIOEVENT_EVENT_RISING_EDGE)
                        ? EdgeType::RISING : EdgeType::FALLING;
        return true;
    }
    return false;
}
```

## The Self-Pipe Trick

`poll()` blocks indefinitely. The only ways to wake it are:
1. A signal interrupts it (EINTR) — unreliable and requires signal handling
2. One of the file descriptors becomes ready

We create a `pipe2()` at init time. `waitForEdge()` polls the read end. `shutdown()`
writes one byte to the write end. This is completely reliable, thread-safe (Linux write
of ≤ `PIPE_BUF` bytes is atomic), and requires zero signal handling.

```cpp
void GpioHal::shutdown() {
    if (mShutdownPipe[1] >= 0) {
        uint8_t byte = 1;
        write(mShutdownPipe[1], &byte, 1); // atomic — safe from any thread
    }
}
```

## Android.bp for the GPIO HAL

```bp
// vendor/myoem/hal/pirdetector/Android.bp
soong_namespace {}

cc_library_static {
    name: "libgpiohal_pirdetector",
    vendor: true,
    srcs: ["src/GpioHal.cpp"],
    export_include_dirs: ["include"],  // pirdetectord can: #include "pirdetector/GpioHal.h"
    shared_libs: ["liblog"],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

No exotic dependencies. `<linux/gpio.h>` is a kernel UAPI header — always available.
`poll()`, `ioctl()`, `read()`, `write()` — standard POSIX libc. Just `liblog` for
logcat output.

---

<a name="part-5"></a>
# Part 5: Phase 2a — Designing the AIDL Interface

## Three Files, One Pattern

The AIDL layer for a callback-driven service always follows the same three-file pattern:

1. **Data parcelable** — the payload that flows from service to client
2. **Callback interface** — client implements this; service calls it (`oneway`)
3. **Service interface** — client calls these; service implements them

```
IPirDetectorService.aidl   ← client → service
    registerCallback(IPirDetectorCallback cb)
    unregisterCallback(IPirDetectorCallback cb)
    getCurrentState() → int
    getVersion() → int

IPirDetectorCallback.aidl  ← service → client (oneway)
    onMotionEvent(MotionEvent event)

MotionEvent.aidl           ← parcelable
    int motionState         (1=detected, 0=ended)
    long timestampNs        (kernel interrupt timestamp)
```

## Why oneway on the Callback?

The service fires `onMotionEvent()` from the EventThread — the same thread that loops on
`GpioHal::waitForEdge()`. Without `oneway`:

```
EventThread calls cb->onMotionEvent()
    → waits for client to return from the method
    → client does UI work (takes 16ms for a frame render)
    → EventThread is BLOCKED for 16ms
    → if sensor fires again during those 16ms, we miss the edge
```

With `oneway`:

```
EventThread calls cb->onMotionEvent()    // returns IMMEDIATELY
    → Binder runtime enqueues the call in the client's thread pool
    → EventThread loops back to waitForEdge() instantly
    → no edges are missed, no client can block the service
```

A dead client (process crashed) with a synchronous call would block the service for
~30 seconds (Binder timeout). With `oneway`, a dead client causes an immediate error
return and we remove it from the list.

## @VintfStability — The Cross-Partition Requirement

All three AIDL files carry `@VintfStability`:

```aidl
// MotionEvent.aidl
@VintfStability
parcelable MotionEvent { ... }

// IPirDetectorCallback.aidl
@VintfStability
oneway interface IPirDetectorCallback { ... }

// IPirDetectorService.aidl
@VintfStability
interface IPirDetectorService { ... }
```

This is required because:
- `pirdetectord` is a vendor binary (vendor partition, `kHeader='VNDR'`)
- `PirDetectorApp` is compiled with `platform_apis: true` (system partition, `kHeader='SYST'`)
- Without `@VintfStability`, libbinder rejects the call with `BAD_TYPE` —
  partition stability mismatch

With `@VintfStability`, the AIDL compiler emits `AIBinder_markVintfStability()` inside
the generated `BnPirDetectorService` constructor. This upgrades the service Binder to
VINTF (cross-partition) stability before `AServiceManager_addService()` is called.

> **Lesson from ThermalControl Phase 4**: Do NOT call `AIBinder_markVintfStability()`
> manually in `main.cpp`. The compiler already generates that call. A second call causes
> a FATAL crash: "AIBinder_markVintfStability called on already-marked binder".

## NDK Backend for the Daemon — Not cpp

The `aidl_interface` block uses:
```bp
backend: {
    cpp:  { enabled: false },   // WRONG — kHeader='VNDR', incompatible with Java
    java: { enabled: true  },   // For pirdetector-manager
    ndk:  { enabled: true  },   // For pirdetectord — kHeader='SYST', cross-partition safe
    rust: { enabled: false },
}
```

`libbinder_ndk` (used by the NDK backend) is an LLNDK library — it links against the
**system** `libbinder.so` at runtime (`kHeader='SYST'`). Java clients also use the
system `libbinder.so`. They can communicate.

The cpp backend links against the **vendor** `libbinder.so` (`kHeader='VNDR'`). Java
clients cannot communicate with it. This is the most common mistake in vendor AIDL work.

## Freezing the API

`stability: "vintf"` requires a frozen API. After finalising the AIDL files:

```bash
lunch myoem_rpi5-trunk_staging-userdebug
m pirdetectorservice-aidl-update-api
```

This creates `aidl_api/pirdetectorservice-aidl/1/` with frozen snapshots of all three
AIDL files. After freezing, changes to AIDL files must be backwards-compatible (add
methods only; never remove or rename). If you need to break compatibility, bump to
version 2 and update `versions_with_info`.

---

<a name="part-6"></a>
# Part 6: Phase 2b — The Binder Service (pirdetectord)

## Three Concurrent Entities

`pirdetectord` has three concurrent entities sharing state:

| Entity | Thread | Responsibility |
|--------|--------|---------------|
| EventThread | `std::thread` | Calls `waitForEdge()`, notifies callbacks |
| Binder thread pool | NDK-managed | Serves `registerCallback`, `unregisterCallback`, `getCurrentState`, `getVersion` |
| DeathRecipient | Binder thread | Fires when a client process crashes |

`mCallbackEntries` is the shared state. All access is protected by `mCallbacksMutex`.
`mCurrentState` is `std::atomic<int>` — reads by the Binder pool and writes by the
EventThread require no lock.

## The EventThread

```cpp
void PirDetectorService::eventLoop() {
    ALOGI("EventThread running — blocking on GPIO edge events");

    ::myoem::pirdetector::GpioEvent gpioEvent;

    while (mGpioHal.waitForEdge(gpioEvent)) {
        // Translate edge to state
        int newState = (gpioEvent.edge == ::myoem::pirdetector::EdgeType::RISING) ? 1 : 0;
        mCurrentState.store(newState, std::memory_order_relaxed);

        ALOGI("GPIO edge: %s → state=%d",
              newState ? "RISING (motion detected)" : "FALLING (motion ended)",
              newState);

        // Build AIDL parcelable
        MotionEvent event;
        event.motionState = newState;
        event.timestampNs = gpioEvent.timestampNs;

        // Fire all callbacks — each call returns immediately (oneway)
        notifyCallbacks(event);

        // Loop back to waitForEdge() — ready for the next edge immediately
    }
    ALOGI("EventThread exiting");
}
```

Notice the loop body: detect edge → update state → fire callbacks → loop. The entire
callback firing is non-blocking (because `oneway`). The EventThread is always ready for
the next GPIO edge within microseconds of the previous one.

## The DeathRecipient Pattern

When a client registers its callback, we attach a `DeathRecipient` to its Binder:

```cpp
// Register DeathRecipient when a client subscribes
CallbackEntry* entry = new CallbackEntry{callback};
mCallbackEntries.push_back(entry);

AIBinder_linkToDeath(
    callback->asBinder().get(),
    mDeathRecipient.get(),
    static_cast<void*>(entry));   // cookie = entry* (heap-stable)
```

When the client process crashes:
```
Binder runtime detects dead Binder
    → calls onClientDiedStatic(cookie)
    → cookie = CallbackEntry*
    → service removes entry from mCallbackEntries
    → deletes entry
```

Without this: every crashed app permanently occupies a slot in `mCallbackEntries`.
The service tries to call its dead callback on every GPIO edge, gets an error each time,
and leaks memory indefinitely.

## registerCallback — Idempotency Check

```cpp
::ndk::ScopedAStatus PirDetectorService::registerCallback(
        const std::shared_ptr<IPirDetectorCallback>& callback) {
    std::lock_guard<std::mutex> lock(mCallbacksMutex);

    // Idempotency: same Binder registered twice → no-op
    // Compare raw AIBinder* pointers (not the shared_ptr — two shared_ptrs
    // can hold different ref-counts to the same underlying binder object)
    AIBinder* incomingBinder = callback->asBinder().get();
    for (const CallbackEntry* entry : mCallbackEntries) {
        if (entry->callback->asBinder().get() == incomingBinder) {
            ALOGD("registerCallback: already registered, ignoring");
            return ::ndk::ScopedAStatus::ok();
        }
    }
    // ... add new entry
}
```

## main.cpp — The 5-Step Service Registration Pattern

Every NDK Binder service follows this exact pattern:

```cpp
int main() {
    // 1. Configure thread pool (before creating any Binders)
    ABinderProcess_setThreadPoolMaxThreadCount(4);

    // 2. Start worker threads
    ABinderProcess_startThreadPool();

    // 3. Create service instance (SharedRefBase for NDK ref-counting)
    auto service = ::ndk::SharedRefBase::make<
        aidl::com::myoem::pirdetector::PirDetectorService>();

    // 4. Initialize hardware + spawn EventThread
    if (!service->start("/dev/gpiochip4", 17)) {
        ALOGE("GPIO init failed");
        return 1;
    }

    // 5. Register with ServiceManager
    AServiceManager_addService(service->asBinder().get(),
        "com.myoem.pirdetector.IPirDetectorService");

    // 6. Block main thread in Binder loop (never returns)
    ABinderProcess_joinThreadPool();
    return 0;
}
```

---

<a name="part-7"></a>
# Part 7: Phase 2c — RC File, VINTF Manifest, and SELinux

## The RC File

```rc
# pirdetectord.rc
service pirdetectord /vendor/bin/pirdetectord
    class main
    user system
    group system
```

No `chown`/`chmod` needed (unlike ThermalControl's hwmon sysfs permissions). The GPIO
character device (`/dev/gpiochip4`) is owned by `root:system 0660` — the `system` user
can open it directly.

`class main` starts the daemon when the "main" class starts — after `ServiceManager`,
`hwservicemanager`, and `vndservicemanager` are all running. This guarantees that
`AServiceManager_addService()` succeeds on first call.

## The VINTF Manifest Fragment

```xml
<!-- vintf/pirdetector.xml -->
<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>com.myoem.pirdetector</name>
        <version>1</version>
        <fqname>IPirDetectorService/default</fqname>
    </hal>
</manifest>
```

This fragment is merged into the device manifest at build time. At runtime, when
`pirdetectord` calls `AServiceManager_addService()`, ServiceManager checks that the
service name appears in the merged VINTF manifest. If it doesn't, registration is
rejected with:

```
VINTF requires "com.myoem.pirdetector.IPirDetectorService" to be declared in manifest
```

The name in the `<fqname>` tag must be: `<interface_name>/<instance>`. The instance
name is always "default" for single-instance vendor services.

## SELinux Policy

### pirdetectord.te

```
type pirdetectord, domain;
type pirdetectord_exec, exec_type, vendor_file_type, file_type;

init_daemon_domain(pirdetectord)

binder_use(pirdetectord)
binder_service(pirdetectord)

allow pirdetectord servicemanager:binder { call transfer };
allow pirdetectord pirdetector_service:service_manager { add find };

# GPIO character device — verify actual type first:
# adb shell ls -laZ /dev/gpiochip4
allow pirdetectord gpio_device:chr_file { open read ioctl };

logd_writer(pirdetectord)
```

### file_contexts

```
/vendor/bin/pirdetectord    u:object_r:pirdetectord_exec:s0
```

### service_contexts

```
com.myoem.pirdetector.IPirDetectorService    u:object_r:pirdetector_service:s0
```

## myoem_base.mk additions

```makefile
# PRODUCT_SOONG_NAMESPACES
PRODUCT_SOONG_NAMESPACES += \
    vendor/myoem/hal/pirdetector \
    vendor/myoem/services/pirdetector \
    vendor/myoem/libs/pirdetector \
    vendor/myoem/apps/PirDetectorApp

# PRODUCT_PACKAGES
PRODUCT_PACKAGES += \
    pirdetectord \
    pirdetector-vintf-fragment \
    pirdetector_client \
    pirdetector-manager \
    PirDetectorApp

# BOARD_VENDOR_SEPOLICY_DIRS
BOARD_VENDOR_SEPOLICY_DIRS += \
    vendor/myoem/services/pirdetector/sepolicy/private
```

---

<a name="part-8"></a>
# Part 8: Phase 3 — Java Manager Library

## The Threading Problem

When `pirdetectord` calls `onMotionEvent()` on a registered callback, it arrives on a
**Binder thread** in the client process — not the main (UI) thread. Android UI is
single-threaded; touching a View or updating LiveData from a Binder thread causes a
`CalledFromWrongThreadException` or `IllegalStateException`.

`PirDetectorManager` solves this transparently using a `Handler`:

```java
// PirDetectorManager.java

private final Handler mMainHandler = new Handler(Looper.getMainLooper());

private final IPirDetectorCallback.Stub mCallback = new IPirDetectorCallback.Stub() {
    @Override
    public void onMotionEvent(MotionEvent event) {
        // Called on a BINDER THREAD — do not touch UI here

        final MotionListener listener = mListener; // volatile read
        if (listener == null) return;

        final long ts = event.timestampNs;
        final boolean detected = (event.motionState == 1);

        // Post to main thread
        mMainHandler.post(() -> {
            if (mListener == null) return; // re-check after queue delay
            if (detected) {
                listener.onMotionDetected(ts);
            } else {
                listener.onMotionEnded(ts);
            }
        });
    }
};
```

The app's `MotionListener` is always called on the main thread. The app developer never
needs to think about threading.

## The IBinder Constructor Pattern

```java
// Constructor receives an IBinder — NOT an AIDL interface type directly
// The app (platform_apis:true) calls ServiceManager.checkService() and passes it here
public PirDetectorManager(IBinder binder) {
    mService = IPirDetectorService.Stub.asInterface(binder);
}
```

Why not call `ServiceManager` inside this library? Because `ServiceManager` is `@hide`
— it is not in any public or system SDK surface. If this library called it directly,
it would need `platform_apis: true`, which is not appropriate for a library that should
be usable by any app. The app has `platform_apis: true` and is responsible for the
`ServiceManager.checkService()` call.

This is the same pattern used in `ThermalControlManager` and `PirDetectorManager`.

## The getCurrentState() Bootstrap Call

```java
public void registerListener(MotionListener listener) throws RemoteException {
    mListener = listener;
    mService.registerCallback(mCallback);
}

public int getCurrentState() throws RemoteException {
    return mService.getCurrentState();
}
```

Always call `getCurrentState()` immediately after `registerListener()`:

```java
manager.registerListener(motionListener);
int currentState = manager.getCurrentState();
// Update UI with current state — no waiting for the next GPIO edge
```

Without this: if the sensor is already HIGH when the app connects, the UI would show
"No Motion" until the next falling edge — which could be many minutes later.

---

<a name="part-9"></a>
# Part 9: Phase 4 — Kotlin / Jetpack Compose App

## MVI Contract

```kotlin
// MotionContract.kt

data class MotionUiState(
    val motionDetected: Boolean = false,
    val lastEventTimestampNs: Long = 0L,
    val serviceConnected: Boolean = false,
    val error: String? = null,
)

sealed class MotionUiEvent {
    data object Connect    : MotionUiEvent()
    data object Disconnect : MotionUiEvent()
}

sealed class MotionUiEffect {
    data class ShowError(val message: String) : MotionUiEffect()
}
```

Three types. Three files in the contract. Clean separation.

## ViewModel — The ServiceManager Bridge

```kotlin
// MotionViewModel.kt (key section)

private fun connect() {
    viewModelScope.launch {
        // ServiceManager is @hide — available because platform_apis: true
        val binder: IBinder? = ServiceManager.checkService(PirDetectorManager.SERVICE_NAME)

        if (binder == null) {
            _uiEffect.emit(MotionUiEffect.ShowError("PIR service unavailable"))
            return@launch
        }

        manager = PirDetectorManager(binder)

        // Sync current state before first GPIO edge arrives
        val initialState = manager!!.getCurrentState()
        _uiState.update { it.copy(serviceConnected = true, motionDetected = initialState == 1) }

        // Register listener — callbacks come on main thread
        manager!!.registerListener(object : MotionListener {
            override fun onMotionDetected(timestampNs: Long) {
                _uiState.update { it.copy(motionDetected = true, lastEventTimestampNs = timestampNs) }
            }
            override fun onMotionEnded(timestampNs: Long) {
                _uiState.update { it.copy(motionDetected = false, lastEventTimestampNs = timestampNs) }
            }
        })
    }
}
```

## DisposableEffect for Lifecycle Management

```kotlin
// MainActivity.kt

setContent {
    val viewModel: MotionViewModel = viewModel()

    // Connect when composable enters composition (Activity starts)
    // Disconnect when composable leaves composition (Activity goes background)
    DisposableEffect(Unit) {
        viewModel.onEvent(MotionUiEvent.Connect)
        onDispose {
            viewModel.onEvent(MotionUiEvent.Disconnect)
        }
    }

    // Collect effects (Snackbar messages)
    LaunchedEffect(viewModel.uiEffect) {
        viewModel.uiEffect.collect { effect ->
            when (effect) {
                is MotionUiEffect.ShowError -> snackbarHostState.showSnackbar(effect.message)
            }
        }
    }

    MotionScreen(uiState = uiState, snackbarHostState = snackbarHostState)
}
```

## The Compose Screen

```kotlin
// MotionScreen.kt (simplified)

@Composable
fun MotionScreen(uiState: MotionUiState, snackbarHostState: SnackbarHostState) {
    // Animate colour transition: surfaceVariant (idle) ↔ error/red (motion)
    val indicatorColor by animateColorAsState(
        targetValue = if (uiState.motionDetected)
            MaterialTheme.colorScheme.error
        else
            MaterialTheme.colorScheme.surfaceVariant,
        animationSpec = tween(300),
        label = "indicatorColor"
    )

    // Subtle scale pulse when motion active
    val indicatorScale by animateFloatAsState(
        targetValue = if (uiState.motionDetected) 1.1f else 1.0f,
        animationSpec = tween(300),
        label = "indicatorScale"
    )

    Box(
        modifier = Modifier.size(200.dp).scale(indicatorScale)
            .clip(CircleShape).background(indicatorColor),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = if (uiState.motionDetected) "MOTION" else "IDLE",
            style = MaterialTheme.typography.headlineLarge,
            fontWeight = FontWeight.Bold,
        )
    }

    Text(
        text = if (uiState.motionDetected) "Object / Person Detected" else "No Object Present",
        style = MaterialTheme.typography.titleLarge,
    )
}
```

## Android.bp

```bp
android_app {
    name: "PirDetectorApp",
    vendor: true,
    platform_apis: true,      // for ServiceManager.checkService()
    certificate: "platform",  // required alongside platform_apis
    privileged: true,         // install to /vendor/priv-app/

    srcs: ["src/**/*.kt"],
    manifest: "AndroidManifest.xml",

    static_libs: [
        "pirdetector-manager",
        "pirdetectorservice-aidl-V1-java",
        "kotlin-stdlib",
        "kotlinx-coroutines-android",
        "androidx.core_core-ktx",
        "androidx.activity_activity-compose",
        "androidx.compose.runtime_runtime",
        "androidx.compose.ui_ui",
        "androidx.compose.foundation_foundation",
        "androidx.compose.animation_animation",
        "androidx.compose.material3_material3",
        "androidx.lifecycle_lifecycle-viewmodel-ktx",
        "androidx.lifecycle_lifecycle-viewmodel-compose",
        "androidx.lifecycle_lifecycle-runtime-compose",
    ],
    optimize: { enabled: false },
}
```

---

<a name="part-10"></a>
# Part 10: Debugging Guide — Layer by Layer

This section is a living reference. Every issue encountered during the actual build and
testing of this project is documented here, with the exact error, root cause, and fix.

---

## Build-Time Issues

### Bug: Soong bootstrap fails — `pirdetectorservice-aidl-V1-java` undefined (ENCOUNTERED)

**Full error:**
```
error: vendor/myoem/libs/pirdetector/Android.bp:32:1: "pirdetector-manager" depends
on undefined module "pirdetectorservice-aidl-V1-java".
Or did you mean ["pirdetectorservice-aidl-V2-java"]?
fatal errors encountered
```

**Root cause — chicken-and-egg problem:**

`versions_with_info: [{version: "1"}]` in `Android.bp` tells Soong to generate the
versioned stubs library `pirdetectorservice-aidl-V1-java` by compiling the **frozen
snapshot** at `aidl_api/pirdetectorservice-aidl/1/`. But that directory doesn't exist
yet — it is normally created by `m pirdetectorservice-aidl-update-api`. However,
`m pirdetectorservice-aidl-update-api` requires Soong to bootstrap successfully first.
Soong fails to bootstrap because the library the snapshot would produce is missing.
Classic deadlock.

The "Or did you mean V2-java?" is Soong's fuzzy-match over all modules in the entire
tree — it found some other aidl_interface somewhere that happens to have a V2. It is
not related to our interface. Ignore the suggestion.

**Fix — manually seed the frozen snapshot:**

```bash
# 1. Create the snapshot directory structure
mkdir -p vendor/myoem/services/pirdetector/aidl_api/pirdetectorservice-aidl/1/com/myoem/pirdetector

# 2. Copy the source AIDL files into the snapshot (exact copies)
cp vendor/myoem/services/pirdetector/aidl/com/myoem/pirdetector/MotionEvent.aidl \
   vendor/myoem/services/pirdetector/aidl_api/pirdetectorservice-aidl/1/com/myoem/pirdetector/

cp vendor/myoem/services/pirdetector/aidl/com/myoem/pirdetector/IPirDetectorCallback.aidl \
   vendor/myoem/services/pirdetector/aidl_api/pirdetectorservice-aidl/1/com/myoem/pirdetector/

cp vendor/myoem/services/pirdetector/aidl/com/myoem/pirdetector/IPirDetectorService.aidl \
   vendor/myoem/services/pirdetector/aidl_api/pirdetectorservice-aidl/1/com/myoem/pirdetector/

# 3. Create a placeholder .hash file (update-api will rewrite it with the correct value)
echo "placeholder_run_update-api_to_fix" > \
   vendor/myoem/services/pirdetector/aidl_api/pirdetectorservice-aidl/1/.hash
```

After step 3, Soong can find the snapshot directory, generate `pirdetectorservice-aidl-V1-java`
and `pirdetectorservice-aidl-V1-ndk`, and bootstrap succeeds.

```bash
# 4. Now run update-api — this overwrites the placeholder .hash with the correct value
m pirdetectorservice-aidl-update-api
# Success: .hash is now correct. Soong will not complain again.
```

The resulting directory tree after the fix:
```
aidl_api/pirdetectorservice-aidl/1/
├── .hash                              ← SHA256 written by update-api
└── com/myoem/pirdetector/
    ├── MotionEvent.aidl
    ├── IPirDetectorCallback.aidl
    └── IPirDetectorService.aidl
```

**Why does this work?**

Soong only needs the snapshot files to exist in order to generate the versioned stubs
module. The `.hash` file is validated during the build's API-check phase — not during
Soong's bootstrap / analysis phase. A placeholder `.hash` lets Soong bootstrap, the build
generates the stubs, and `update-api` fixes the hash. After `update-api`, subsequent
builds pass API-check with no warnings.

**Better fix discovered — `frozen: false` (ENCOUNTERED, preferred)**

After applying the snapshot workaround above, the Android.bp linter automatically added
`frozen: false` to the `aidl_interface` block:

```bp
aidl_interface {
    name: "pirdetectorservice-aidl",
    stability: "vintf",
    versions_with_info: [{ version: "1", imports: [] }],
    frozen: false,   // ← linter added this
    ...
}
```

With `frozen: false`, Soong generates `pirdetectorservice-aidl-V1-java` and
`pirdetectorservice-aidl-V1-ndk` **directly from the source AIDL files** in `aidl/`,
without requiring the `aidl_api/` snapshot directory to exist. This completely avoids
the chicken-and-egg problem.

The `aidl_api/` directory we created as a workaround can be deleted:

```bash
rm -rf vendor/myoem/services/pirdetector/aidl_api/
```

`frozen: false` is the correct setting during active development. When the interface is
finalised and ready for production (needs backwards-compatibility enforcement), change it
to `frozen: true` and run `m pirdetectorservice-aidl-update-api` to create the snapshot.

**Lesson:** For a new VINTF-stable AIDL interface under active development, always add
`frozen: false` to the `aidl_interface` block. This removes the snapshot bootstrapping
requirement entirely. Switch to `frozen: true` only when the API is stable and you want
the build system to enforce backwards compatibility.

---

### Bug: `vendor: true` + `platform_apis: true` — Soong panic (ENCOUNTERED)

**Full error:**
```
error: vendor/myoem/apps/PirDetectorApp/Android.bp:39:1: module "PirDetectorApp"
variant "android_common": sdk_version: sdk_version must have a value when the module
is located at vendor or product (only if PRODUCT_ENFORCE_PRODUCT_PARTITION_INTERFACE
is set).
internal error: panic in GenerateBuildActions for module "PirDetectorApp" variant
"android_common"
rule_builder paths cannot be nil
```

**Root cause:**

`android_app` modules on the **vendor partition** (`vendor: true`) must declare an
explicit `sdk_version` (e.g. `"current"`, `"system_current"`). They are not allowed
to use `platform_apis: true` because:

- `platform_apis: true` compiles against the full hidden API surface
- That surface is only available on the **system** partition
- Vendor partition apps must use a bounded SDK surface — they cannot see `@hide` APIs

The combination `vendor: true` + `platform_apis: true` is fundamentally contradictory.
Soong catches it at bootstrap and panics with the "rule_builder paths cannot be nil"
internal error (a consequence of the bad state, not the root cause).

**Fix — remove `vendor: true` from the app and its manager library:**

Apps that need `platform_apis: true` (to call `ServiceManager.checkService()`) must
install on the **system partition**. The source file lives in `vendor/myoem/apps/`
(source location) but the built APK goes to `/system/app/` (install location).
These are different concepts. Same pattern used by ThermalMonitor, SafeModeDemo,
BMICalculatorA — none of them have `vendor: true`.

```bp
// WRONG — causes Soong panic:
android_app {
    name: "PirDetectorApp",
    vendor: true,           // ← vendor partition
    platform_apis: true,    // ← contradicts vendor
    ...
}

// CORRECT — matches ThermalMonitor and SafeModeDemo:
android_app {
    name: "PirDetectorApp",
    // NO vendor: true — app installs to /system/app/
    platform_apis: true,    // needed for ServiceManager.checkService()
    certificate: "platform",
    ...
}
```

Also remove `vendor: true` from `pirdetector-manager` (`java_library`). A
`java_library` with `vendor: true` cannot be statically linked into a system app.
The library follows the app's partition.

```bp
// pirdetector-manager — WRONG:
java_library {
    name: "pirdetector-manager",
    vendor: true,       // ← remove this
    sdk_version: "system_current",
    ...
}

// CORRECT:
java_library {
    name: "pirdetector-manager",
    // no vendor: true
    sdk_version: "system_current",
    ...
}
```

**Lesson:** Source location (`vendor/myoem/apps/`) and install partition are
independent in AOSP. Apps that call `@hide` APIs via `platform_apis: true` are
system apps — they install to `/system/app/` or `/system/priv-app/` regardless of
where their source lives in the tree.

---

### Bug: soong_namespace import error — "module not found"

**Error:**
```
error: vendor/myoem/services/pirdetector/Android.bp:7:5:
soong_namespace: import "vendor/myoem/hal/pirdetector" not found
```

**Cause**: The namespace `vendor/myoem/hal/pirdetector` is not yet listed in
`PRODUCT_SOONG_NAMESPACES` in `myoem_base.mk`.

**Fix:**
```makefile
# In vendor/myoem/common/myoem_base.mk
PRODUCT_SOONG_NAMESPACES += \
    vendor/myoem/hal/pirdetector \
    vendor/myoem/services/pirdetector \
    vendor/myoem/libs/pirdetector \
    vendor/myoem/apps/PirDetectorApp
```

---

### Bug: java_library sdk_version mismatch with AIDL stubs

**Error:**
```
error: vendor/myoem/libs/pirdetector/Android.bp:
sdk_version "system_current" is not compatible with
dependency "pirdetectorservice-aidl-V1-java" which has sdk_version "current"
```

**Cause**: The generated Java AIDL stubs from a `stability: "vintf"` interface default
to `sdk_version: "current"` (public SDK only). The manager uses `sdk_version:
"system_current"` which is a superset — this error shouldn't occur. If it does,
the AIDL interface may be missing the `vendor_available: true` flag.

**Fix:**
```bp
aidl_interface {
    name: "pirdetectorservice-aidl",
    vendor_available: true,   // ← required — allows both vendor and system modules to use the stubs
    stability: "vintf",
    ...
}
```

---

### Bug: `libgpiohal_pirdetector` not found in pirdetectord link

**Error:**
```
error: vendor/myoem/services/pirdetector/Android.bp:
static_libs module "libgpiohal_pirdetector" is not visible from
"vendor/myoem/services/pirdetector"
```

**Cause**: `soong_namespace` in `services/pirdetector/Android.bp` is missing the import
for the HAL namespace.

**Fix:**
```bp
// services/pirdetector/Android.bp
soong_namespace {
    imports: ["vendor/myoem/hal/pirdetector"],  // ← add this
}
```

---

## Boot-Time / Service Issues

### Bug: pirdetectord not starting — no entry in process list

**Diagnose:**
```bash
# Check if the daemon started
adb shell ps -A | grep pirdetect
# Nothing? The service didn't start.

# Check init logs for the service
adb shell logcat -d -b all | grep pirdetectord
# Look for: "could not find '/vendor/bin/pirdetectord'"
# or: "service 'pirdetectord' failed to start"

# Verify the binary exists on the device
adb shell ls -la /vendor/bin/pirdetectord
# If not found: the binary wasn't installed (PRODUCT_PACKAGES issue)
```

**Common causes:**
1. Binary not in `PRODUCT_PACKAGES` in `myoem_base.mk` → add `pirdetectord`
2. RC file not installed → check `init_rc: ["pirdetectord.rc"]` in Android.bp
3. SELinux domain transition failed → see SELinux section below

---

### Bug: pirdetectord starts but crashes immediately — "GPIO init failed"

**Diagnose:**
```bash
# Check logcat (use the LOG_TAG from main.cpp)
adb logcat -s pirdetectord GpioHal
# Look for: "open(/dev/gpiochip4) failed: Permission denied"
# or: "GPIO_GET_LINEEVENT_IOCTL failed on /dev/gpiochip4 line 17: No such device"
```

**If "Permission denied"**: SELinux is blocking the `open()`. Check denials:
```bash
adb logcat -d | grep "avc: denied" | grep pirdetect
# Example:
# avc: denied { open } for pid=1234 comm="pirdetectord"
#   name="gpiochip4" dev="tmpfs" ino=5678
#   scontext=u:r:pirdetectord:s0 tcontext=u:object_r:gpio_device:s0
#   tclass=chr_file permissive=0
```

Fix: the `.te` file is missing the `allow pirdetectord gpio_device:chr_file { open read ioctl };` rule. Rebuild the image or use permissive mode during development:
```bash
adb root
adb shell setenforce 0   # permissive — for development ONLY
adb logcat -s pirdetectord GpioHal   # retry
```

**If "No such device"**: The gpiochip path is wrong. Verify:
```bash
adb shell cat /sys/bus/gpio/devices/gpiochip4/label
# If NOT "pinctrl-rp1", try other numbers:
adb shell cat /sys/bus/gpio/devices/gpiochip0/label
adb shell cat /sys/bus/gpio/devices/gpiochip1/label
```

Update `kGpioChipPath` in `main.cpp` and rebuild.

**If "GPIO_GET_LINEEVENT_IOCTL failed ... Invalid argument"**: The line number (17) is
wrong, or the GPIO line is already in use by another process. Check:
```bash
adb shell cat /sys/kernel/debug/gpio | grep 17
# If a consumer label is shown, another driver owns line 17
```

---

### Bug: Service starts but not found in ServiceManager

```bash
# Check if service is registered
adb shell service list | grep pirdetect
# Nothing? Service didn't register.

# Check for VINTF manifest error
adb logcat -d | grep "VINTF"
# Look for: "VINTF requires ... to be declared in manifest"
```

**If VINTF error**: The `vintf_fragment` module is missing from `PRODUCT_PACKAGES`:
```makefile
PRODUCT_PACKAGES += \
    pirdetectord \
    pirdetector-vintf-fragment   # ← this one
```

**If no VINTF error but service not listed**: ServiceManager rejected the registration.
```bash
adb logcat -s pirdetectord
# Look for: "AServiceManager_addService failed with status N"
```

Common cause: service name in `main.cpp` doesn't match `service_contexts`:
- `main.cpp`: `"com.myoem.pirdetector.IPirDetectorService"`
- `service_contexts`: `com.myoem.pirdetector.IPirDetectorService`

These must be identical character-for-character.

---

## SELinux Issues

### Methodology: Always Diagnose Before Writing Policy

Never guess SELinux rules. Follow this exact sequence:

```bash
# Step 1: Check what's actually denied
adb logcat -d | grep "avc: denied" | grep pirdetect

# Step 2: Identify the source context (scontext), target context (tcontext),
#          target class (tclass), and permissions denied
# Example denial:
# avc: denied { ioctl } for pid=1234 comm="pirdetectord"
#   path="/dev/gpiochip4"
#   scontext=u:r:pirdetectord:s0    ← pirdetectord domain
#   tcontext=u:object_r:gpio_device:s0  ← gpio_device type
#   tclass=chr_file                 ← character file
#   permissive=0

# Step 3: Translate to a policy rule
# allow <scontext_type> <tcontext_type>:<tclass> { <permissions> };
# allow pirdetectord gpio_device:chr_file { ioctl };

# Step 4: Add to pirdetectord.te and rebuild
```

### Common SELinux Denials for pirdetectord

| Denial | Permission | Fix |
|--------|-----------|-----|
| Cannot open `/dev/gpiochip4` | `open` | `allow pirdetectord gpio_device:chr_file open;` |
| Cannot ioctl on event fd | `ioctl` | `allow pirdetectord gpio_device:chr_file ioctl;` |
| Cannot read from event fd | `read` | `allow pirdetectord gpio_device:chr_file read;` |
| Cannot write to logd | `write` | `logd_writer(pirdetectord)` macro |
| Service registration fails | `add` | `allow pirdetectord pirdetector_service:service_manager add;` |

### Checking pirdetectord's SELinux Domain

```bash
# Verify the daemon is in the correct domain
adb shell ps -eZ | grep pirdetect
# Expected: u:r:pirdetectord:s0  system  /vendor/bin/pirdetectord
# Problem:  u:r:init:s0          system  /vendor/bin/pirdetectord
#   → file_contexts is missing or the label on the binary is wrong

# Check the binary's label on device
adb shell ls -laZ /vendor/bin/pirdetectord
# Expected: -rwxr-xr-x root root u:object_r:pirdetectord_exec:s0 pirdetectord
# If label is wrong (e.g. vendor_file:s0), the domain transition won't fire
```

If the binary has the wrong label, it means the `file_contexts` file wasn't applied.
Check that `BOARD_VENDOR_SEPOLICY_DIRS` includes `vendor/myoem/services/pirdetector/sepolicy/private`.

---

## Runtime / Callback Issues

### Bug: CLI client connects but receives no events

```bash
# Run the CLI client
adb shell /vendor/bin/pirdetector_client
# Connected, current state printed, but no callbacks when hand waves

# Check EventThread is running
adb logcat -s GpioHal pirdetectord
# Should see: "GpioHal initialized: chip=/dev/gpiochip4 line=17 event_fd=5"
# Should see: "EventThread running — blocking on GPIO edge events"
# If neither: start() wasn't called or returned false
```

**If EventThread is running but no edge events**: Test with the sysfs method (Part 3):
```bash
adb shell "echo 17 > /sys/class/gpio/export"
adb shell "while true; do cat /sys/class/gpio/gpio17/value; sleep 0.2; done"
# Wave hand — if value changes: GPIO is working, but GpioHal event fd isn't firing
# If value does NOT change: sensor wiring problem (check VCC, GND, OUT connections)
```

**If sysfs value changes but GpioHal doesn't fire**: The GPIO interrupt may not be
configured for the RP1 chip. Check the kernel message log:
```bash
adb logcat -b kernel | grep gpio
# Look for any errors related to gpio17 or gpiochip4
```

### Bug: Callbacks fire on connect but never update after that

**Cause**: The MotionListener was set to null before callbacks arrived, or the
DisposableEffect's `onDispose` triggered too early.

```bash
# Check if the callback is still registered on the service side
adb logcat -s pirdetectord
# On each GPIO edge, you should see:
#   "GPIO edge: RISING (motion detected) → state=1"
#   "GPIO edge: FALLING (motion ended) → state=0"
# If you see edges but no UI updates:
#   → the Binder callback is firing but the Handler dispatch isn't reaching the UI
#   → add a Log.d to PirDetectorManager.onMotionEvent() to confirm it's called
```

### Bug: App crashes with "DeadObjectException" on registerCallback

**Cause**: `pirdetectord` crashed after the app looked it up but before registration.

```bash
# Check service status
adb shell service list | grep pirdetect
# If not listed: daemon crashed

# Check logcat for crash reason
adb logcat -s pirdetectord
# Common: GpioHal::init() failed because gpiochip4 label changed
```

### Bug: App shows "PIR service unavailable" on first launch

**Cause**: `ServiceManager.checkService()` returned null — daemon not ready yet.

```bash
# Verify service is registered
adb shell service list | grep pirdetect

# Check timing: the app may launch before pirdetectord registers
# pirdetectord uses class main — it starts after boot is complete
# If the app launches during boot, ServiceManager.checkService() (not waitForService)
# can return null

# Quick fix for testing: use AServiceManager_waitForService in the manager
# (blocks until the service appears — not for production but fine for dev iteration)
```

For production: implement a retry loop in `PirDetectorManager` or use a `ServiceConnection`
pattern with exponential backoff.

### Bug: Memory leak — callback entries accumulate on service side

**Diagnose:**
```bash
# Check how many callbacks are registered
adb logcat -s pirdetectord | grep "callback"
# On registerCallback: "registered new client (total=N)"
# On unregisterCallback: "removed client (remaining=N)"
# If total keeps growing: unregisterListener() is not being called

# Kill the app without stopping
adb shell am force-stop com.myoem.pirdetectorapp

# Check if DeathRecipient fired
adb logcat -s pirdetectord | grep "dead"
# Should see: "dead client removed (remaining=N)"
# If not: DeathRecipient wasn't linked properly
```

---

## Dev-Iteration Workflow (No Full Flash Needed)

For rapid iteration on the C++ daemon (changing service logic):

```bash
# 1. Build just the daemon
m pirdetectord

# 2. Push to device (no need to flash the full image)
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/pirdetectord /vendor/bin/pirdetectord
adb shell chmod 755 /vendor/bin/pirdetectord

# 3. Restart the daemon
adb shell stop pirdetectord
adb shell start pirdetectord

# 4. Confirm it's running
adb shell ps -A | grep pirdetect

# 5. Watch logs
adb logcat -s pirdetectord GpioHal -v time
```

For the app APK:
```bash
m PirDetectorApp
adb push out/target/product/rpi5/vendor/app/PirDetectorApp/PirDetectorApp.apk \
    /vendor/app/PirDetectorApp/
# Then either:
adb shell pm install -r /vendor/app/PirDetectorApp/PirDetectorApp.apk
# Or force-stop and re-open from launcher (APK is picked up automatically)
```

For SELinux policy changes (requires a full vendor image rebuild):
```bash
m vendorimage
# Then write the new vendor image to the SD card:
# sudo dd if=out/target/product/rpi5/vendor.img of=/dev/sdX bs=4M status=progress
```

---

## Complete Diagnostic Command Reference

```bash
# ── Service status ────────────────────────────────────────────────────────────
adb shell ps -A | grep pirdetect                    # Is it running?
adb shell ps -eZ | grep pirdetect                   # What SELinux domain?
adb shell service list | grep pirdetect             # Registered in ServiceManager?
adb shell dumpsys -l | grep pirdetect               # Alternative service check

# ── Hardware ──────────────────────────────────────────────────────────────────
adb shell ls /dev/gpiochip*                         # All gpiochip devices
adb shell cat /sys/bus/gpio/devices/gpiochip4/label # Is it pinctrl-rp1?
adb shell ls -laZ /dev/gpiochip4                    # Permissions + SELinux label
adb shell cat /sys/kernel/debug/gpio                # All claimed GPIO lines

# Quick PIR sensor test (sysfs method)
adb root
adb shell "echo 17 > /sys/class/gpio/export"
adb shell "while true; do cat /sys/class/gpio/gpio17/value; sleep 0.2; done"
adb shell "echo 17 > /sys/class/gpio/unexport"

# ── Logs ──────────────────────────────────────────────────────────────────────
adb logcat -s pirdetectord GpioHal                  # pirdetectord output only
adb logcat -s pirdetectord GpioHal -v time          # With timestamps
adb logcat -b kernel | grep gpio                    # Kernel GPIO messages
adb logcat -d | grep "avc: denied" | grep pirdetect # SELinux denials

# ── CLI test client ───────────────────────────────────────────────────────────
adb shell /vendor/bin/pirdetector_client             # Full callback test

# ── App ───────────────────────────────────────────────────────────────────────
adb shell am start -n com.myoem.pirdetectorapp/.MainActivity
adb shell am force-stop com.myoem.pirdetectorapp
adb logcat -s PirDetectorManager MotionViewModel     # App-side logs
```

---

<a name="part-11"></a>
# Part 11: Lessons Learned

## 1. The GPIO Character Device API Is the Right Way

Avoid the temptation to use `/sys/class/gpio/`. It is deprecated, it requires polling
(no `poll()`-able fd), and it will be removed in future kernels. The character device
API (`GPIO_GET_LINEEVENT_IOCTL`) is two extra lines of setup and gives you
interrupt-driven events for free.

## 2. Interrupt-Driven vs Polling: Know When to Use Each

| Pattern | Use When | Example |
|---------|---------|---------|
| Poll loop | Input changes continuously (ADC, encoder) | PotVolume (knob position) |
| Interrupt (poll + event fd) | Input changes rarely on discrete events | PIR sensor, button press |
| VHAL subscription | Input from the vehicle data bus | SafeMode (speed, gear) |

The PIR sensor's output is purely event-driven (RISING/FALLING). There is no meaningful
"current ADC value" to poll every 50ms. The interrupt model is the only right choice.

## 3. The Self-Pipe Trick Is a Universal Pattern

Whenever you have a thread blocking in `poll()`, `select()`, `epoll_wait()`, or `read()`
on a socket/fd, and you need to stop it cleanly from another thread, use the self-pipe
trick. Create a `pipe2(O_CLOEXEC)`. The blocking thread polls the read end. The stopping
thread writes one byte to the write end. Clean, reliable, portable.

Signals (SIGUSR1, etc.) work too, but they have edge cases (signal masks, async-signal
safety, race conditions). Pipes have none of those problems.

## 4. @VintfStability: The Compiler Does the Work — Don't Duplicate It

When `stability: "vintf"` is set in `aidl_interface`, the generated `Bn` class
constructor already calls `AIBinder_markVintfStability()`. You do not need to call it
in `main.cpp`. Calling it twice causes a FATAL crash. Trust the generated code.

This was also the lesson from ThermalControl Phase 4. It bears repeating.

## 5. The DeathRecipient Is Not Optional

Every Binder callback registration needs a paired `DeathRecipient`. Client processes
crash. Users force-stop apps. Without the DeathRecipient:
- Dead callbacks accumulate in the service's list
- The service tries to call them on every hardware event
- Each call gets an error return
- Memory and CPU leak proportional to the number of crashes

The `AIBinder_linkToDeath` + `AIBinder_unlinkToDeath` pair is five lines of code. Write
them every time.

## 6. Always Verify the gpiochip Path on the Actual Device

RPi5 has multiple gpiochip devices. The 40-pin header controller (RP1) is usually
`gpiochip4`, but kernel enumeration order can change between AOSP builds. Always verify:

```bash
adb shell cat /sys/bus/gpio/devices/gpiochip4/label
# Expected: "pinctrl-rp1"
```

Never hardcode a gpiochip number without verifying on the target device. Same lesson as
`/dev/spidev10.0` in the PotVolume project.

## 7. The IBinder Constructor Pattern for Java Libraries

A Java manager library that wraps an AIDL service should not call `ServiceManager`
internally. That forces the library to use `platform_apis: true`, which is not
appropriate for a library. Instead:
- The library takes an `IBinder` in its constructor
- The app (which does have `platform_apis: true`) calls `ServiceManager.checkService()`
- The app passes the `IBinder` to the manager

This keeps the library's `sdk_version` at `"system_current"` (not platform APIs),
making it usable by a wider range of callers.

## 8. getCurrentState() Is Not Optional

A callback-only design has a boot race: the app registers the callback at T=0, the first
GPIO event may not arrive until T+2 minutes. During those 2 minutes, the app shows the
wrong state. Always call `getCurrentState()` synchronously immediately after
`registerCallback()` to get the current hardware state without waiting for the next edge.

## 9. DisposableEffect for Lifecycle — Not onStop()

The idiomatic Compose way to handle Activity lifecycle is `DisposableEffect(Unit)`:
- The setup block runs when the composable enters composition (like `onStart`)
- `onDispose` runs when the composable leaves composition (like `onStop`)

This keeps lifecycle management inside the composable tree, visible to developers
reading the UI code, rather than split across `onCreate`, `onStart`, and `onStop`.

## 10. SELinux: Verify the Actual Type, Never Assume

From the ThermalControl project: `sysfs_hwmon` doesn't exist — hwmon files use the
generic `sysfs` type on RPi5 AOSP 15.

From this project: `/dev/gpiochipN` may have type `gpio_device` or something else
depending on the build. Always run `adb shell ls -laZ /dev/gpiochip0` before writing
the `.te` file. Ten seconds of checking saves an hour of debugging.

## 11. `oneway` Callbacks Need a Reverse `binder_call` SELinux Rule

When a vendor service fires a `oneway` callback into a client app, the Binder call goes
in the **reverse** direction — from the service domain into the app domain. This requires
its own `binder_call` rule that is easy to forget.

```te
# Forward: app calls the service (the obvious one)
binder_call(platform_app, pirdetectord)   # usually handled by AOSP base policy

# Reverse: service fires callback into app (the one you'll forget)
binder_call(pirdetectord, platform_app)   # YOU must add this
```

Forgetting the reverse rule causes callbacks to be **silently dropped** with no error in
the service. The only evidence is an `avc: denied { call }` in dmesg — which is easy to
miss because `permissive=1` means the denial is logged but not enforced during dev testing.

**Always scan dmesg for binder denials after testing in permissive mode:**
```bash
adb shell dmesg | grep "avc.*denied.*binder"
```

## 12. `/dev/gpiochipN` File Permissions — The RC File `on boot` Pattern

Kernel-created device nodes (`/dev/gpiochipN`, `/dev/spidevN`, hwmon files) default to
`root:root 0600`. If your daemon runs as `user=system`, it cannot open them.

Always add an `on boot` section to your RC file:
```
on boot
    chown root system /dev/gpiochip0
    chmod 0660 /dev/gpiochip0
```

The `class main` service starts after `on boot` completes, so permissions are set in time.
This exact pattern was required by ThermalControl (hwmon) and PIR Detector (gpiochip).

---

## What's Next

This project completes a natural progression:

| Project | Hardware | Pattern | New Concept |
|---------|---------|---------|------------|
| Calculator | None | AIDL service | Basic Binder |
| ThermalControl | sysfs (fan) | HAL + AIDL + Manager + App | Full stack |
| SafeMode | VHAL | Callback (VHAL subscription) | oneway callbacks |
| PotVolume | SPI ADC | Single daemon, no AIDL | Poll loop, audio API |
| **PIR Detector** | **GPIO interrupt** | **Full stack + interrupt-driven callbacks** | **GPIO char device, EventThread** |

Natural next steps:
- **Multiple sensors**: add a second PIR on GPIO27, report which zone detected motion
- **Debouncing in the manager**: suppress rapid RISING/FALLING pairs within 100ms
- **Persistence**: record motion events to Room DB via the app
- **Notification**: show a system notification when motion is detected while the app is in the background

---

*This article documents the real build of `vendor/myoem/services/pirdetector/` on
Raspberry Pi 5 AOSP android-15.0.0_r14. Errors will be added as they are discovered
during the actual build and test cycle.*

---

<a name="part-errors"></a>
# Part 12: Real Build — Every Error We Hit and How We Fixed It

> This section is a verbatim engineering journal. Every error message, every dead end,
> every diagnosis, and the exact fix applied is recorded here in chronological order.
> If you are following along and hit the same error, jump straight to the matching section.

---

## Error 1 — `vendor: true` + `platform_apis: true` Soong Panic

### The Error

```
sdk_version must have a value when the module is located at vendor or product
internal error: panic in GenerateBuildActions
rule_builder paths cannot be nil
```

**Build command that triggered it:**
```bash
m pirdetectord pirdetector_client
```

### Root Cause

`android_app` modules with `vendor: true` cannot also have `platform_apis: true`. The hidden
API surface (required for `ServiceManager.checkService()`) only exists on the system partition.
Vendor apps must use a bounded `sdk_version` — they cannot call `@hide` APIs.

Soong panics (not just errors) because the combination creates nil paths in the rule builder.

### How We Diagnosed It

The error message itself is clear. We cross-referenced with working apps in the project:
`ThermalMonitor` and `SafeModeDemo` both use `platform_apis: true` without `vendor: true`
and install to the system partition even though their source lives in `vendor/myoem/apps/`.

**Key insight**: Source location ≠ install partition. An `android_app` without `vendor: true`
installs to `/system/app/` regardless of where its source sits.

### Fix

In `vendor/myoem/apps/PirDetectorApp/Android.bp`:
```bp
android_app {
    name: "PirDetectorApp",
    // NO vendor: true — apps with platform_apis must live on system partition
    platform_apis: true,
    certificate: "platform",
    ...
}
```

Also removed `vendor: true` from `vendor/myoem/libs/pirdetector/Android.bp` for the same reason:
a `java_library` with `vendor: true` cannot be statically linked into a system app.

### Build Command After Fix

```bash
m pirdetectord pirdetector_client
```

---

## Error 2 — AIDL Bootstrap Deadlock: `-V1-java` Undefined

### The Error

```
"pirdetector-manager" depends on undefined module "pirdetectorservice-aidl-V1-java".
Or did you mean ["pirdetectorservice-aidl-V2-java" "pirdetectorservice-aidl-api"]?
```

**Build command that triggered it:**
```bash
m pirdetectord pirdetector_client
m pirdetector-manager PirDetectorApp
```

### Root Cause

This is the classic AOSP AIDL bootstrap chicken-and-egg problem:

1. `versions_with_info: [{version: "1"}]` tells Soong to generate `pirdetectorservice-aidl-V1-ndk`
   and `pirdetectorservice-aidl-V1-java`.
2. For those names to exist, Soong needs the `aidl_api/pirdetectorservice-aidl/1/` snapshot directory.
3. That snapshot is created by running `m pirdetectorservice-aidl-update-api`.
4. But `update-api` itself needs Soong to bootstrap — which needs the snapshot — deadlock.

When the snapshot was deleted (during an earlier attempt to simplify the setup), Soong treats
the current source as V2 (because it thinks V1 is "the deleted frozen version"), hence the
suggestion `pirdetectorservice-aidl-V2-java`.

### Approaches We Tried (and Why They Failed)

**Attempt A — `frozen: false` only (no `versions_with_info`):**
Soong still generates `-V1-` names with `stability: "vintf"`. The manifest API check still
runs. Adding `frozen: false` alone does not remove the snapshot requirement.

**Attempt B — `unstable: true`:**
```
module "pirdetectorservice-aidl_interface": stability: must be empty when "unstable" is true
```
`unstable: true` and `stability: "vintf"` are mutually exclusive. Soong rejects the combination.

**Attempt C — Manual snapshot creation with wrong `.hash`:**
We created the `aidl_api/1/` directory and files manually but used the wrong hash
(`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` — SHA1 of empty string).
The build caught the mismatch:
```
ERROR: Modification detected of stable AIDL API file
```

**Attempt D — `frozen: false` with a valid snapshot:**
```
Interface pirdetectorservice-aidl can not be marked `frozen: false` if there are no changes
between the current version and the last frozen version.
```
`frozen: false` means "the source has diverged from the last frozen snapshot." If source ==
snapshot, you must use `frozen: true`.

### Final Fix (What Actually Works)

**Step 1:** Manually create the `aidl_api/1/` snapshot with the correct stripped AIDL
(no comments, no imports — just the interface skeleton as the AIDL compiler produces):

```
aidl_api/pirdetectorservice-aidl/1/com/myoem/pirdetector/MotionEvent.aidl
aidl_api/pirdetectorservice-aidl/1/com/myoem/pirdetector/IPirDetectorCallback.aidl
aidl_api/pirdetectorservice-aidl/1/com/myoem/pirdetector/IPirDetectorService.aidl
aidl_api/pirdetectorservice-aidl/1/.hash
```

Content format (stripped — no copyright, no comments, fully-qualified type names):
```aidl
package com.myoem.pirdetector;
@VintfStability
parcelable MotionEvent {
  int motionState;
  long timestampNs;
}
```

**Step 2:** Compute the correct hash:
```bash
cd vendor/myoem/services/pirdetector/aidl_api/pirdetectorservice-aidl/1
{ find ./ -name "*.aidl" -print0 | LC_ALL=C sort -z | xargs -0 sha1sum && echo latest-version; } \
  | sha1sum | cut -d " " -f 1
```

Write the output into `.hash`.

**Step 3:** Also create the `current/` directory with identical content and hash (Soong checks
both `1/` and `current/` with `versions_with_info`):
```bash
mkdir -p aidl_api/pirdetectorservice-aidl/current/com/myoem/pirdetector
# copy same .aidl files, same .hash
```

**Step 4:** Set `frozen: true` in `Android.bp` (source == snapshot == frozen):
```bp
aidl_interface {
    name: "pirdetectorservice-aidl",
    stability: "vintf",
    versions_with_info: [
        { version: "1", imports: [] },
    ],
    frozen: true,   // source matches snapshot exactly
    ...
}
```

**Step 5:** Run `update-api` once to let Soong regenerate the canonical snapshot:
```bash
m pirdetectorservice-aidl-update-api
```

After this, `pirdetectorservice-aidl-V1-ndk` and `pirdetectorservice-aidl-V1-java` resolve
and the build proceeds.

### Key Rule Learned

> With `stability: "vintf"` + `versions_with_info`, Soong always generates `-V1-` prefixed
> stubs. Use `frozen: true` when source matches the snapshot. Use `frozen: false` only when
> you are actively developing new API methods that aren't yet in the snapshot.

---

## Error 3 — Linker Error: `setPirDetectorServiceRef` Undefined

### The Error

```
ld.lld: error: undefined symbol: _Z24setPirDetectorServiceRefNSt3__110shared_ptrIN4aidl3com5myoem11pirdetector18PirDetectorServiceEEE
>>> referenced by main.cpp:66
clang++: error: linker command failed with exit code 1
```

**Build command that triggered it:**
```bash
m pirdetectord pirdetector_client
```

### Root Cause

`main.cpp` forward-declares `setPirDetectorServiceRef` as a **global namespace** free function:
```cpp
void setPirDetectorServiceRef(std::shared_ptr<aidl::com::myoem::pirdetector::PirDetectorService> svc);
```

But in `PirDetectorService.cpp` the function was defined **inside**
`namespace aidl::com::myoem::pirdetector { ... }`. The mangled names differ — the linker
cannot find the global-namespace symbol.

### How We Diagnosed It

The mangled name in the error contains `_Z24setPirDetectorServiceRef` — a C++ mangled global
function. Reading `PirDetectorService.cpp` showed the function was inside the namespace closing
brace. The function must live at global scope to match the declaration in `main.cpp`.

### Fix

Moved `gServiceWeakRef` and `setPirDetectorServiceRef` **outside** the namespace:

```cpp
}  // namespace aidl::com::myoem::pirdetector   ← namespace closes here

// Global-namespace free function — declared in main.cpp
void setPirDetectorServiceRef(
        std::shared_ptr<aidl::com::myoem::pirdetector::PirDetectorService> svc) {
    aidl::com::myoem::pirdetector::gServiceWeakRef = svc;
}
```

Also changed `gServiceWeakRef` from `static` to non-static (file-scope) so the global
function above can access it.

---

## Error 4 — `GpioHal::init()` Failed — Wrong gpiochip Number

### The Symptom

```
04-05 12:34:03.094  1979 E pirdetectord: GpioHal::init() failed — pirdetectord cannot start
04-05 12:34:03.094  1979 E pirdetectord: Failed to initialize GPIO HAL — exiting
```

No SELinux denial in dmesg. The daemon exits immediately.

**Debugging commands used:**
```bash
adb logcat -s pirdetectord -d
adb shell "/vendor/bin/pirdetectord 2>&1"
adb shell dmesg | grep -E "pirdetectord|avc.*pirdetect"
```

### Root Cause

The code used `/dev/gpiochip4` (a common assumption for RPi5). On this specific AOSP 15
build, only `/dev/gpiochip0` through `/dev/gpiochip13` exist. The `/dev/gpiochip4` path
simply does not exist — `open()` fails.

### How We Diagnosed It

```bash
# Check what gpiochip devices actually exist
adb shell ls -la /dev/gpiochip*
# → only /dev/gpiochip0, gpiochip10, gpiochip11, gpiochip12, gpiochip13

# Find which one is the RP1 (40-pin header controller)
adb shell "for chip in /sys/class/gpio/gpiochip*; do \
  echo -n \"\$(basename \$chip) (ngpio=\$(cat \$chip/ngpio)): \"; \
  cat \$chip/label; done"
# → gpiochip569 (ngpio=54): pinctrl-rp1   ← this is the RP1

# Map sysfs number to /dev/ node
adb shell "ls /sys/class/gpio/gpiochip569/device/"
# → contains "gpiochip0" symlink → /dev/gpiochip0 is the RP1
```

**Key insight**: The sysfs class numbering (gpiochip569) has NO relation to the
`/dev/gpiochipN` number (gpiochip0). The `/dev/` nodes are numbered by kernel registration
order, the sysfs class nodes are numbered by GPIO base offset.

### Fix

In `vendor/myoem/services/pirdetector/src/main.cpp`:
```cpp
static constexpr const char* kGpioChipPath = "/dev/gpiochip0";  // was /dev/gpiochip4
```

**Verification command after fix:**
```bash
adb logcat -s pirdetectord -d
# Expected:
# I pirdetectord: EventThread started, service ready
# I pirdetectord: EventThread running — blocking on GPIO edge events
```

---

## Error 5 — `AServiceManager_addService` Failed with Status -3

### The Symptom

```
E pirdetectord: AServiceManager_addService('com.myoem.pirdetector.IPirDetectorService') failed with status -3
```

**Debugging command:**
```bash
adb logcat -s servicemanager -d | grep pirdetector
```

### Root Cause (Part 1): Wrong Service Name Format

ServiceManager output revealed:
```
E servicemanager: VINTF HALs require names in the format type/instance
  (e.g. some.package.foo.IFoo/default) but got: com.myoem.pirdetector.IPirDetectorService
```

VINTF-stable services **must** register with the `type/instance` format.
Our code used just the interface name without the `/default` suffix.

### Fix (Part 1)

In `main.cpp`:
```cpp
// WRONG:
static constexpr const char* kServiceName =
    "com.myoem.pirdetector.IPirDetectorService";

// CORRECT — VINTF requires type/instance format:
static constexpr const char* kServiceName =
    "com.myoem.pirdetector.IPirDetectorService/default";
```

Same fix in `PirDetectorManager.java`:
```java
public static final String SERVICE_NAME =
    "com.myoem.pirdetector.IPirDetectorService/default";
```

### Root Cause (Part 2): VINTF Manifest Not Read After Push

After fixing the name format, the error persisted:
```
I servicemanager: Could not find com.myoem.pirdetector.IPirDetectorService/default
  in the VINTF manifest. No alternative instances declared in VINTF.
```

The VINTF manifest `pirdetector.xml` had been pushed to the device but `libvintf` had already
cached the manifest fragments at boot time. Pushing new XML mid-session doesn't take effect.

### Fix (Part 2)

Reboot the device. After reboot, `libvintf` reads the fresh fragment. Also fixed the manifest
format to match the working thermalcontrol manifest:

```xml
<!-- WRONG: -->
<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>com.myoem.pirdetector</name>
        <version>1</version>                    ← causes lookup mismatch
        <fqname>IPirDetectorService/default</fqname>
    </hal>
</manifest>

<!-- CORRECT (matches thermalcontrol pattern): -->
<manifest version="8.0" type="device">
    <hal format="aidl">
        <name>com.myoem.pirdetector</name>
        <fqname>IPirDetectorService/default</fqname>
    </hal>
</manifest>
```

**Key insight**: The `<version>` tag inside `<hal>` is for HIDL interface versions.
AIDL services declare their version implicitly via the interface descriptor — the XML
`<version>` tag causes a lookup mismatch and must be omitted.

**Commands used:**
```bash
# Check servicemanager rejection reason (most useful command for -3 errors)
adb logcat -s servicemanager -d | grep -E "pirdetector|VINTF"

# Verify VINTF manifest is present on device
adb shell ls /vendor/etc/vintf/manifest/
adb shell cat /vendor/etc/vintf/manifest/pirdetector.xml

# After reboot, check registration
adb shell service list | grep pirdetector
```

---

## Error 6 — `getInterfaceHash()` Not Overridden in Java Stub

### The Error

```
vendor/myoem/libs/pirdetector/java/com/myoem/pirdetector/PirDetectorManager.java:83:
error: <anonymous com.myoem.pirdetector.PirDetectorManager$1> is not abstract and does
not override abstract method getInterfaceHash() in IPirDetectorCallback
    private final IPirDetectorCallback.Stub mCallback = new IPirDetectorCallback.Stub() {
```

**Build command:**
```bash
m pirdetector-manager PirDetectorApp
```

### Root Cause

With `frozen: true` + `versions_with_info`, the generated Java stubs add two abstract methods
to every `Stub` class:
- `getInterfaceVersion()` — returns the frozen version number
- `getInterfaceHash()` — returns the API hash for runtime compatibility checks

Any anonymous class that extends `IPirDetectorCallback.Stub` must implement both.

### Fix

In `PirDetectorManager.java`, add the two overrides to the anonymous Stub:

```java
private final IPirDetectorCallback.Stub mCallback = new IPirDetectorCallback.Stub() {
    // Required by versioned (frozen) AIDL stubs.
    @Override public int getInterfaceVersion() { return IPirDetectorCallback.VERSION; }
    @Override public String getInterfaceHash() { return IPirDetectorCallback.HASH; }

    @Override
    public void onMotionEvent(MotionEvent event) {
        // ... existing implementation
    }
};
```

The constants `VERSION` and `HASH` are generated by the AIDL compiler into the
`IPirDetectorCallback` interface class.

---

## Error 7 — `Theme.DeviceDefault.DayNight.NoActionBar` Not Found

### The Error

```
out/.../AndroidManifest.xml:27: error: resource
android:style/Theme.DeviceDefault.DayNight.NoActionBar not found.
error: failed processing manifest.
```

**Build command:**
```bash
m PirDetectorApp
```

### Root Cause

`Theme.DeviceDefault.DayNight.NoActionBar` does not exist in AOSP 15's framework resources.
Only `Theme.DeviceDefault.DayNight` exists (without the `.NoActionBar` suffix).

### How We Diagnosed It

Cross-referenced with `SafeModeDemo/AndroidManifest.xml` which works:
```bash
grep "android:theme" vendor/myoem/apps/SafeModeDemo/AndroidManifest.xml
# → android:theme="@android:style/Theme.DeviceDefault.DayNight"
```

### Fix

In `AndroidManifest.xml`:
```xml
<!-- WRONG: -->
android:theme="@android:style/Theme.DeviceDefault.DayNight.NoActionBar"

<!-- CORRECT: -->
android:theme="@android:style/Theme.DeviceDefault.DayNight"
```

---

## Error 8 — SELinux: `logd_writer` Macro Not Defined

### The Error

```
vendor/myoem/services/pirdetector/sepolicy/private/pirdetectord.te:80:
ERROR 'syntax error' at token 'logd_writer' on line 22755:
logd_writer(pirdetectord)
checkpolicy: error(s) encountered while parsing configuration
```

**Build command:**
```bash
make bootimage systemimage vendorimage -j$(nproc)
```

### Root Cause

`logd_writer()` is an AOSP SELinux macro defined in the platform's `te_macros` file.
It grants a domain permission to write to the Android log daemon (logd). However, this
macro is **not available in vendor SELinux policy** on AOSP 15. Vendor `.te` files only
have access to macros defined in the versioned public vendor policy.

### How We Diagnosed It

Checked `thermalcontrold.te` (a working service in the same project):
```bash
grep -n "logd\|log" vendor/myoem/services/thermalcontrol/sepolicy/private/thermalcontrold.te
# → (no output — thermalcontrol never calls logd_writer)
```

`thermalcontrold` still produces logcat output, meaning `init_daemon_domain()` already
grants sufficient logd access. `logd_writer()` was redundant.

### Fix

Remove `logd_writer(pirdetectord)` from `pirdetectord.te`. No replacement needed —
`init_daemon_domain()` includes logd write access.

---

## Error 9 — SELinux: `pirdetector_service` Type Undefined

### The Error

```
vendor/myoem/services/pirdetector/sepolicy/private/pirdetectord.te:52:
ERROR 'unknown type pirdetector_service' at token ';' on line 22727:
allow pirdetectord pirdetector_service:service_manager { add find };
```

### Root Cause

`pirdetector_service` was used in `pirdetectord.te` but never declared. In AOSP SELinux,
every type must be declared with a `type` statement before it can appear in an `allow` rule.
Service types are conventionally declared in a separate `service.te` file.

### How We Diagnosed It

Checked thermalcontrol's working sepolicy structure:
```bash
ls vendor/myoem/services/thermalcontrol/sepolicy/private/
# → file_contexts  service.te  service_contexts  thermalcontrold.te

cat vendor/myoem/services/thermalcontrol/sepolicy/private/service.te
# → type thermalcontrold_service, app_api_service, service_manager_type;
```

The pirdetector `sepolicy/private/` was missing `service.te` entirely.

### Fix

Created `vendor/myoem/services/pirdetector/sepolicy/private/service.te`:
```te
type pirdetector_service, app_api_service, service_manager_type;
```

Also fixed `service_contexts` — the service name must include `/default`:
```
# WRONG:
com.myoem.pirdetector.IPirDetectorService    u:object_r:pirdetector_service:s0

# CORRECT (matches what AServiceManager_addService registers):
com.myoem.pirdetector.IPirDetectorService/default    u:object_r:pirdetector_service:s0
```

---

## Error 10 — SELinux: `gpio_device` Type Undefined

### The Error

```
vendor/myoem/services/pirdetector/sepolicy/private/pirdetectord.te:74:
ERROR 'unknown type gpio_device' at token ';' on line 22749:
allow pirdetectord gpio_device:chr_file { open read ioctl };
```

### Root Cause

`gpio_device` is an SELinux type defined in some Android distributions (especially those
with Qualcomm GPIO support). It does **not exist** in AOSP 15's base vendor policy on RPi5.

### How We Diagnosed It

```bash
# Check actual SELinux label of the gpiochip device
adb shell ls -laZ /dev/gpiochip0
# → crw------- 1 root root u:object_r:device:s0
```

The label is `device` — the generic catch-all type. However, `device` is blocked by an
AOSP neverallow rule:
```
neverallow domain device:chr_file { read write open };
```

So we can't use `device` either. The solution is to create a custom type and label
`/dev/gpiochipN` with it via `file_contexts`.

### Fix

**Step 1:** Declare a custom type in `pirdetectord.te`:
```te
# dev_type is the correct attribute for device nodes in /dev/
type pirdetectord_gpio_device, dev_type;
```

**Step 2:** Label `/dev/gpiochip*` in `file_contexts`:
```
# Labels all gpiochip character devices with our custom type.
# Avoids neverallow: "neverallow domain device:chr_file { read write open }"
/dev/gpiochip[0-9]*    u:object_r:pirdetectord_gpio_device:s0
```

**Step 3:** Allow pirdetectord to access the custom type:
```te
allow pirdetectord pirdetectord_gpio_device:chr_file { open read ioctl };
```

---

---

## Error 11 — App Shows "PIR Service Unavailable" After Full Image Flash

### The Symptom

After flashing a full image (boot + system + vendor), the app showed "PIR Service unavailable".
The daemon was crash-looping every 5 seconds in dmesg:

```
init: Service 'pirdetectord' (pid 2392) exited with status 1
init: starting service 'pirdetectord'...
init: Service 'pirdetectord' (pid 2396) exited with status 1
...
init: process with updatable components 'pirdetectord' exited 4 times in 4 minutes
```

### Debugging Commands Used

```bash
# Step 1: Is the daemon running?
adb shell ps -eZ | grep pirdetectord
# → (empty) — not running

# Step 2: What does logcat say?
adb logcat -s pirdetectord -d | tail -10
# → E pirdetectord: GpioHal::init() failed — pirdetectord cannot start

# Step 3: Is it an SELinux denial?
adb shell getenforce
# → Permissive (so not SELinux)
adb shell dmesg | grep "avc.*pirdetectord"
# → (empty) — no denials

# Step 4: Check the actual device permissions
adb shell ls -laZ /dev/gpiochip0
# → crw------- 1 root root u:object_r:pirdetectord_gpio_device:s0
#   ↑ root:root 0600 — group 'system' has NO permission
```

### Root Cause

`/dev/gpiochip0` is created by the kernel as `root:root 0600` (owner-read/write only).
`pirdetectord` runs as `user=system` (UID 1000). When it calls `open("/dev/gpiochip0", O_RDONLY)`,
the kernel rejects it — `system` is not `root` and the file has no group or world permissions.
`GpioHal::init()` returns `false`, the daemon exits with status 1, init restarts it — crash loop.

This was **not caught during dev iteration** because we were using `adb root` and running
the daemon manually as root. After a full image flash with the RC file starting the daemon
as `user system`, the permission mismatch became visible.

### Fix

Add an `on boot` section to `pirdetectord.rc` to `chown` and `chmod` the device before the
daemon starts:

```bash
# In vendor/myoem/services/pirdetector/pirdetectord.rc:
on boot
    chown root system /dev/gpiochip0
    chmod 0660 /dev/gpiochip0

service pirdetectord /vendor/bin/pirdetectord
    class main
    user system
    group system
```

`class main` starts after `on boot` triggers, so by the time the daemon runs, the permissions
are already set.

### Quick Fix Without Rebuild

For immediate testing without reflashing:
```bash
adb root
adb shell "chown root:system /dev/gpiochip0 && chmod 0660 /dev/gpiochip0"
adb shell "stop pirdetectord && sleep 1 && start pirdetectord"
adb logcat -s pirdetectord -d | tail -5
# Expected: "pirdetectord registered as '...IPirDetectorService/default'"
```

Push the RC file fix for persistence across reboots:
```bash
adb shell mount -o remount,rw /vendor
adb push vendor/myoem/services/pirdetector/pirdetectord.rc /vendor/etc/init/pirdetectord.rc
```

Then rebuild the vendor image to bake it in permanently:
```bash
make vendorimage -j$(nproc)
```

---

## Error 12 — App Shows "No Object Present" Even When Object Is in Front of Sensor

### The Symptom

After fixing the "PIR Service unavailable" banner (Error 11), the app connected successfully
but always showed "No Object Present" — even when a hand was held directly in front of the PIR
sensor. The daemon was running and registered. No crash, no error banner.

### Debugging Commands Used

```bash
# Step 1: Is the daemon running?
adb shell ps -eZ | grep pirdetectord
# → u:r:pirdetectord:s0  system  497  pirdetectord  ✓ running
# → u:r:kernel:s0  root  530  [irq/187-pirdetectord]  ✓ IRQ thread exists

# Step 2: Are GPIO edges being detected at the HAL level?
adb logcat -s GpioHal -d
# → D GpioHal: GpioHal: edge=FALLING  ts=316084315626 ns  ✓ HAL sees edges

# Step 3: Are edges reaching the service layer?
adb logcat -s pirdetectord -d
# → I pirdetectord: GPIO edge: FALLING (motion ended) → motionState=0  ✓

# Step 4: Is the callback reaching the Java manager?
adb logcat -s PirDetectorManager -d
# → (only "PirDetectorManager created" and "registerListener" — no onMotionEvent)  ✗

# Step 5: SELinux denial?
adb shell dmesg | grep "avc.*denied" | grep -v "flags_health\|apexd\|aac"
# → avc: denied { call } for comm="binder:497_2"
#     scontext=u:r:pirdetectord:s0
#     tcontext=u:r:platform_app:s0:c512,c768
#     tclass=binder permissive=1
```

### Root Cause

**SELinux was silently dropping the oneway Binder callbacks.**

The service fires `onMotionEvent()` on the registered `IPirDetectorCallback`. That callback
object lives in the app's process (`platform_app` domain). For the service to invoke it,
the Binder runtime must perform a `binder:call` from `pirdetectord` → `platform_app`.

The SELinux policy had rules allowing:
- `pirdetectord → servicemanager` (to register the service)
- `platform_app → pirdetectord` (for the app to call `getCurrentState`, `registerCallback`)

But it was **missing** the reverse direction:
- `pirdetectord → platform_app` (for the service to fire callbacks back)

Without this rule, every `onMotionEvent()` call was silently denied. Because the callback
is `oneway`, the service doesn't block waiting for a return — it fires and forgets. The
denial happened invisibly with no error log in the service. The only evidence was the
`avc: denied { call }` line in dmesg.

The `permissive=1` suffix meant SELinux was in permissive mode during our dev-iteration
runs (we had run `setenforce 0`), so the denial was logged but not enforced. After the
full image flash restored enforcing mode, the callbacks were hard-blocked.

### Key Insight: `oneway` callbacks require a reverse binder_call rule

With normal (two-way) Binder calls:
```
Client → Service:  needs  allow client_domain service_domain:binder { call transfer };
```

With `oneway` callbacks (service calls back into client):
```
Client → Service:  needs  allow client_domain service_domain:binder { call transfer };
Service → Client:  needs  allow service_domain client_domain:binder { call transfer };
```

The `oneway` direction is **always missed** during development because the service runs as
root (via manual `adb shell`) when SELinux is permissive. It only becomes visible after
a proper image flash with enforcing mode.

### How the Denial Was Found

The `permissive=1` flag in the dmesg AVC denial is the critical clue:
```
avc: denied { call } for comm="binder:497_2"
    scontext=u:r:pirdetectord:s0 tcontext=u:r:platform_app:s0
    tclass=binder permissive=1
```
- `scontext=u:r:pirdetectord:s0` — the binder call is coming FROM the pirdetectord domain
- `tcontext=u:r:platform_app:s0` — it is targeting the platform_app domain
- `tclass=binder { call }` — it is a Binder call (the oneway callback delivery)
- `permissive=1` — currently allowed (permissive mode) but would be denied in enforcing mode

### Fix

Add `binder_call(pirdetectord, platform_app)` to `pirdetectord.te`:

```te
# Allow pirdetectord to invoke Binder callbacks into registered client apps.
# The oneway IPirDetectorCallback.onMotionEvent() call goes FROM pirdetectord
# INTO the platform_app domain. Without this rule, callbacks are silently dropped.
binder_call(pirdetectord, platform_app)
```

`binder_call(A, B)` is an AOSP macro that expands to:
```te
allow A B:binder { call transfer };
allow A servicemanager:binder transfer;
```

### Build and Deploy After Fix

```bash
# Rebuild just the vendor image (SELinux policy is in vendor)
make vendorimage -j$(nproc)

# Flash vendor image to SD card, or for quick testing:
# SELinux in permissive mode means callbacks already work — verify with logcat:
adb logcat -s pirdetectord,GpioHal,PirDetectorManager
# Wave hand — expected output:
# D GpioHal   : GpioHal: edge=RISING  ts=... ns
# I pirdetectord: GPIO edge: RISING (motion detected) → motionState=1
# (app UI should flip to "Object Present")
```

### The General Rule

> Every `oneway` callback interface requires a **reverse** `binder_call` rule in SELinux.
> Always scan dmesg for `avc: denied ... tclass=binder` after functional testing, even in
> permissive mode — `permissive=1` denials become hard failures after flashing a full image.

---

## Full Build and Test Sequence (Reproduced Successfully)

Here is the complete sequence of commands to build and deploy the project from scratch,
incorporating all fixes above.

### Phase 1 — AIDL API Snapshot Bootstrap

```bash
# One-time: run after aidl_api/ directory exists to let Soong canonicalize the snapshot
m pirdetectorservice-aidl-update-api
```

### Phase 2 — Build Daemon and Client

```bash
m pirdetectord pirdetector_client
```

### Phase 3 — Build Java Manager and App

```bash
m pirdetector-manager PirDetectorApp
```

### Phase 4 — Deploy to Device (Dev Iteration Without Full Flash)

```bash
# Remount vendor and system partitions
adb root
adb shell mount -o remount,rw /vendor
adb shell mount -o remount,rw /         # system lives on root on this build

# Push daemon and client
adb push out/target/product/rpi5/vendor/bin/pirdetectord /vendor/bin/pirdetectord
adb push out/target/product/rpi5/vendor/bin/pirdetector_client /vendor/bin/pirdetector_client

# Push RC file (needs reboot to take effect — init reads RC only at boot)
adb push out/target/product/rpi5/vendor/etc/init/pirdetectord.rc /vendor/etc/init/pirdetectord.rc

# Push VINTF manifest (needs reboot — libvintf caches at boot)
adb push vendor/myoem/services/pirdetector/vintf/pirdetector.xml \
    /vendor/etc/vintf/manifest/pirdetector.xml

# Push app (can hot-install without reboot)
adb shell mkdir -p /system/app/PirDetectorApp
adb push out/target/product/rpi5/system/app/PirDetectorApp/PirDetectorApp.apk \
    /system/app/PirDetectorApp/PirDetectorApp.apk
adb shell pm install --dont-kill -r /system/app/PirDetectorApp/PirDetectorApp.apk

# Reboot for RC + VINTF to take effect
adb reboot
adb wait-for-device && sleep 15 && adb shell getprop sys.boot_completed
```

### Phase 5 — Verify After Reboot

```bash
# Confirm daemon is running (init should start it via RC file)
adb shell ps -eZ | grep pirdetectord

# Confirm it registered in ServiceManager
adb shell service list | grep pirdetector

# Check daemon logs
adb logcat -s pirdetectord -d

# Run the CLI client to test callbacks (wave hand in front of PIR sensor)
adb shell /vendor/bin/pirdetector_client

# Watch motion events in logcat
adb logcat -s pirdetectord -d | grep "GPIO edge"

# Launch the app
adb shell am start -n com.myoem.pirdetectorapp/.MainActivity
```

### Phase 6 — Full Image Build (Production)

```bash
make bootimage systemimage vendorimage -j$(nproc)
```

Flash to SD card:
```bash
sudo dd if=out/target/product/rpi5/rpiboot/boot.img of=/dev/sdX bs=4M status=progress
```

### Debugging Commands Reference

```bash
# ── gpiochip discovery ─────────────────────────────────────────────────────────
# List all gpiochip character devices
adb shell ls -la /dev/gpiochip*

# Find which gpiochip is the RP1 40-pin header controller
adb shell "for c in /sys/class/gpio/gpiochip*; do \
  echo -n \"\$(basename \$c) (ngpio=\$(cat \$c/ngpio)): \"; cat \$c/label; done"

# Map sysfs class number to /dev/ node
adb shell "ls /sys/class/gpio/gpiochip569/device/"
# Look for "gpiochipN" symlink — that N is the /dev/ number

# ── SELinux diagnosis ──────────────────────────────────────────────────────────
# Check actual label of a file/device
adb shell ls -laZ /dev/gpiochip0
adb shell ls -laZ /vendor/bin/pirdetectord

# Check daemon's running domain
adb shell ps -eZ | grep pirdetectord

# Check for SELinux denials (run AFTER triggering the action you're testing)
adb logcat -d | grep "avc: denied" | grep pirdetect
adb shell dmesg | grep -E "avc.*pirdetect"

# ── ServiceManager diagnosis ───────────────────────────────────────────────────
# Most useful: see WHY addService() was rejected
adb logcat -s servicemanager -d | grep -E "pirdetector|VINTF"

# List all registered AIDL services
adb shell service list | grep pirdetector

# Check VINTF manifest parsing
adb shell ls /vendor/etc/vintf/manifest/
adb shell cat /vendor/etc/vintf/manifest/pirdetector.xml

# ── Daemon lifecycle ───────────────────────────────────────────────────────────
adb logcat -s pirdetectord -d
adb logcat -s pirdetectord        # live follow
adb shell logcat -s servicemanager -d | tail -20

# ── App + Manager diagnostics ──────────────────────────────────────────────────
adb logcat -s pirdetectord,PirDetectorManager,PirDetectorApp -d
```

---

## Summary: Lessons Learned from Errors

| # | Error | Root Cause | Fix |
|---|-------|-----------|-----|
| 1 | Soong panic: `vendor:true` + `platform_apis:true` | Incompatible combination — hidden APIs only on system partition | Remove `vendor:true` from app and library |
| 2 | AIDL `-V1-java` undefined | AIDL snapshot bootstrap deadlock | Manually create `aidl_api/1/` + correct hash, `frozen:true`, run `update-api` |
| 3 | Linker: `setPirDetectorServiceRef` undefined | Function defined inside namespace, called from global scope | Move function definition outside namespace braces |
| 4 | `GpioHal::init()` failed | Wrong gpiochip path `/dev/gpiochip4` (doesn't exist) | Discover real path via sysfs: `/dev/gpiochip0` on this build |
| 5a | `addService` status -3 | Service name missing `/default` suffix | Use `type/instance` format: `...IPirDetectorService/default` |
| 5b | `addService` status -3 | VINTF manifest not read (cached at boot) | Reboot after pushing manifest; fix `<version>` tag in XML |
| 6 | `getInterfaceHash()` not overridden | Frozen stubs add abstract version/hash methods | Add `getInterfaceVersion()` and `getInterfaceHash()` overrides |
| 7 | Theme not found | `NoActionBar` theme variant doesn't exist in AOSP | Use `Theme.DeviceDefault.DayNight` without `.NoActionBar` |
| 8 | SELinux: `logd_writer` syntax error | Macro not available in vendor policy | Remove it — `init_daemon_domain()` already grants logd access |
| 9 | SELinux: `pirdetector_service` undefined | Missing `service.te` file | Create `service.te` with type declaration + fix `service_contexts` |
| 10 | SELinux: `gpio_device` undefined | Type doesn't exist in AOSP 15 base policy | Create custom `pirdetectord_gpio_device` type + label in `file_contexts` |
| 11 | App shows "PIR Service unavailable" after full flash | `/dev/gpiochip0` is `root:root 0600`; daemon runs as `system` and can't open it | Add `chown`/`chmod` for `/dev/gpiochip0` in RC `on boot` section |
| 12 | App shows "No Object Present" even when object is in front of sensor | SELinux denies `pirdetectord → platform_app` binder call; oneway callbacks silently dropped | Add `binder_call(pirdetectord, platform_app)` to `pirdetectord.te` |
