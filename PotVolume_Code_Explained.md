# PotVolume — Complete Code Explanation

**Project:** `vendor/myoem/services/potvolumed`
**Platform:** Raspberry Pi 5 · AOSP Android 15 (`android-15.0.0_r14`)
**Purpose:** This document explains every source file, every design decision,
and the complete data flow of the `potvolumed` daemon. Intended as a study
reference after the project is working.

---

## Table of Contents

1. [What the Project Does — One Paragraph](#what-the-project-does)
2. [Full System Flow Diagram](#full-system-flow-diagram)
3. [File Map](#file-map)
4. [Android.bp — Build Rules](#androidbp--build-rules)
5. [potvolumed.rc — Service Startup](#potvolumedrc--service-startup)
6. [main.cpp — Entry Point and Poll Loop](#maincpp--entry-point-and-poll-loop)
7. [SpiReader — Talking to the MCP3008](#spireader--talking-to-the-mcp3008)
8. [AdcFilter — Noise Removal](#adcfilter--noise-removal)
9. [VolumeMapper — ADC to Volume Index](#volumemapper--adc-to-volume-index)
10. [VolumeController — Injecting Key Events](#volumecontroller--injecting-key-events)
11. [SELinux Policy](#selinux-policy)
12. [Key Design Decisions and Why](#key-design-decisions-and-why)
13. [How Each Component Was Tested](#how-each-component-was-tested)

---

## What the Project Does

You turn a physical potentiometer knob. Android's media volume changes.

Between the knob and the volume change, there are six components working in
a chain: the potentiometer produces a voltage, the MCP3008 ADC converts it to
a number, the `potvolumed` daemon reads the number over SPI, a filter removes
noise, a mapper converts the number to a volume step, and a controller injects
keyboard events that Android's input system interprets as volume button presses.

No Android framework code was modified. The daemon lives entirely in
`vendor/myoem/` and interacts with Android using only the kernel's standard
input device interface (`/dev/uinput`).

---

## Full System Flow Diagram

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         HARDWARE LAYER                                   │
│                                                                          │
│   You turn the knob                                                      │
│         │                                                                │
│         ▼                                                                │
│   ┌─────────────────┐   analog voltage    ┌──────────────────────────┐  │
│   │  Potentiometer  │ ──0V to 3.3V──────▶ │      MCP3008 ADC         │  │
│   │  (10kΩ linear)  │                     │  10-bit SPI ADC          │  │
│   └─────────────────┘                     │  converts voltage → 0-1023│ │
│                                           └────────────┬─────────────┘  │
│                                               4 wires  │                │
│                                           MOSI/MISO/CLK/CS              │
│                                                        │                │
│                                           ┌────────────▼─────────────┐  │
│                                           │  RPi5 GPIO Header SPI0   │  │
│                                           │  /dev/spidev0.0          │  │
│                                           └────────────┬─────────────┘  │
└────────────────────────────────────────────────────────┼────────────────┘
                                                         │ ioctl(SPI_IOC_MESSAGE)
┌────────────────────────────────────────────────────────▼────────────────┐
│                   VENDOR DAEMON: potvolumed (C++)                        │
│                   /vendor/bin/potvolumed                                 │
│                                                                          │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐                │
│  │  SpiReader  │────▶│  AdcFilter  │────▶│ VolumeMapper│                │
│  │             │     │             │     │             │                │
│  │ Sends 3-byte│     │ EMA filter  │     │ 0-1023      │                │
│  │ SPI frame   │     │ α=0.2       │     │   →  0-15   │                │
│  │ every 50ms  │     │ Dead zone±8 │     │ (linear)    │                │
│  │ Returns 0-  │     │ Suppresses  │     │             │                │
│  │ 1023        │     │ jitter      │     │             │                │
│  └─────────────┘     └─────────────┘     └──────┬──────┘                │
│                                                 │                       │
│                                    ┌────────────▼─────────────┐         │
│                                    │   VolumeController       │         │
│                                    │                          │         │
│                                    │ Tracks last index        │         │
│                                    │ Computes delta           │         │
│                                    │ Sends N key press cycles │         │
│                                    └────────────┬─────────────┘         │
└─────────────────────────────────────────────────┼───────────────────────┘
                                                  │ write(input_event)
┌─────────────────────────────────────────────────▼───────────────────────┐
│                        LINUX KERNEL INPUT SUBSYSTEM                      │
│                                                                          │
│   /dev/uinput → virtual input device "PotVolume Knob"                   │
│                                                                          │
│   Kernel delivers: KEY_VOLUMEUP / KEY_VOLUMEDOWN events                 │
│                                                                          │
└─────────────────────────────────────────────────┬───────────────────────┘
                                                  │ InputReader
┌─────────────────────────────────────────────────▼───────────────────────┐
│                      ANDROID FRAMEWORK                                   │
│                                                                          │
│   InputReader (system_server)                                            │
│       │                                                                  │
│       ▼                                                                  │
│   PhoneWindowManager.handleKeyEvent()                                    │
│       │  sees KEY_VOLUMEUP → calls AudioManager.adjustStreamVolume()     │
│       ▼                                                                  │
│   AudioManager → AudioService (Binder IPC)                              │
│       │                                                                  │
│       ▼                                                                  │
│   STREAM_MUSIC volume: 0-15                                             │
│   On-screen volume indicator appears                                     │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## File Map

```
vendor/myoem/services/potvolumed/
│
├── Android.bp                      Build rules (cc_binary)
├── potvolumed.rc                   Init service definition + device permissions
│
├── src/
│   ├── main.cpp                    Entry point: open devices, run poll loop
│   ├── SpiReader.h / .cpp          SPI ioctl wrapper for MCP3008
│   ├── AdcFilter.h / .cpp          EMA low-pass filter + dead zone
│   ├── VolumeMapper.h / .cpp       0-1023 → 0-15 linear mapping
│   └── VolumeController.h / .cpp   uinput virtual device + key event injection
│
└── sepolicy/private/
    ├── potvolumed.te               SELinux type enforcement rules
    └── file_contexts               Binary and device node labels
```

---

## Android.bp — Build Rules

```bp
cc_binary {
    name: "potvolumed",
    vendor: true,              // installs to /vendor/bin/potvolumed
    init_rc: ["potvolumed.rc"],
    srcs: [ "src/main.cpp", "src/SpiReader.cpp", "src/AdcFilter.cpp",
            "src/VolumeMapper.cpp", "src/VolumeController.cpp" ],
    local_include_dirs: ["src"],
    shared_libs: [ "liblog" ],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

**Key decisions explained:**

**`vendor: true`**
This single field determines where the binary is installed and which
set of libraries it can link against. `vendor: true` means:
- Binary goes to `/vendor/bin/` (not `/system/bin/`)
- Can only use VNDK or LLNDK libraries (not system-only libraries)
- Runs in the vendor SELinux domain after init transition

**`shared_libs: ["liblog"]` — only one library**

The original plan used `libaudioclient` to call `AudioSystem::setStreamVolumeIndex()`
directly. That failed at build time:
```
dependency "libaudioclient" missing variant: os:android,image:vendor
```
In AOSP 15, `libaudioclient` is a system-only library — vendor code cannot
link against it. The fix: use `uinput` instead. Since `uinput` is a Linux
kernel interface accessed via `ioctl()` and `write()`, it needs only:
- Standard Linux kernel headers (no library — headers are in the sysroot)
- `liblog` for `ALOGI`/`ALOGE` macros

No `libbinder`, no `libaudioclient`, no VNDK/LLNDK complications.

**`cflags: ["-Wall", "-Wextra", "-Werror"]`**

Every warning is a build error. This forces clean code with no implicit
conversions, unused variables, or signed/unsigned mismatches. Common issues
this catches in embedded C++: `int` vs `uint8_t` comparisons, unused parameters,
missing return paths.

**`init_rc: ["potvolumed.rc"]`**

Tells Soong to package `potvolumed.rc` alongside the binary and install it to
`/vendor/etc/init/potvolumed.rc`. Android init scans all `.rc` files in that
directory at boot. Without this, the daemon would never start automatically.

---

## potvolumed.rc — Service Startup

```ini
on boot
    chown root system /dev/spidev0.0
    chmod 0660 /dev/spidev0.0
    chown root system /dev/spidev0.1
    chmod 0660 /dev/spidev0.1
    chown root system /dev/spidev10.0
    chmod 0660 /dev/spidev10.0
    chown root system /dev/uinput
    chmod 0660 /dev/uinput

service potvolumed /vendor/bin/potvolumed
    class main
    user system
    group system
```

**Why `on boot` instead of inside the service:**

The Linux kernel creates `/dev/spidev*` and `/dev/uinput` as `root:root 0600`.
The daemon runs as `user system` (UID 1000), which cannot open `0600` files.
The fix must happen before the daemon starts — `on boot` runs as root and
fires before `class main` services are started. The daemon then opens devices
that are already `root:system 0660`.

**Why all three spidev nodes (0.0, 0.1, 10.0):**

After enabling `dtparam=spi=on`, three nodes appear. We don't know which one
the kernel will assign at runtime, and covering all three costs nothing. If the
device path ever changes, the permissions are already set.

**`class main`:**

Android init starts services in class order: `core` → `main` → `late_start`.
`class main` starts after `servicemanager`, `audioserver`, and other core
services are running. This matters because the daemon ultimately triggers
audio system calls through Android's input pipeline — those systems must
be alive before key events reach them.

**`user system` / `group system`:**

`system` (UID 1000) is the standard user for vendor services that need to
interact with Android framework services. It has Binder access to most
system services. It matches the `group system` ownership set in `on boot`.

---

## main.cpp — Entry Point and Poll Loop

```cpp
static constexpr char kSpiDevice[]    = "/dev/spidev0.0";
static constexpr int  kAdcChannel     = 0;
static constexpr int  kPollIntervalMs = 50;   // 20 Hz

int main() {
    signal(SIGTERM, onSignal);
    signal(SIGINT,  onSignal);

    SpiReader        spi(kSpiDevice, kAdcChannel);
    AdcFilter        filter;
    VolumeMapper     mapper;
    VolumeController controller;

    if (!spi.open())        return 1;
    if (!controller.open()) return 1;

    while (gRunning) {
        int raw    = spi.read();
        bool changed = false;
        int stable = filter.update(raw, &changed);
        if (changed) {
            int index = mapper.toVolumeIndex(stable);
            controller.setVolume(index);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
    }
    return 0;
}
```

**Design: the pipeline pattern**

`main.cpp` owns no logic. It creates four objects and chains them:
```
spi.read() → filter.update() → mapper.toVolumeIndex() → controller.setVolume()
```
Each component has one job. `main.cpp` just drives the loop. This makes each
component independently testable and replaceable.

**Why 50ms poll interval (20 Hz):**

Human fingers cannot turn a knob faster than a few hundred ADC counts per second.
At 20 Hz, we sample 20 times per second. The EMA filter has time to stabilize
between readings. Faster polling (e.g., 10ms = 100 Hz) increases CPU wake-ups
for no perceptible benefit — the audio system processes key events in ~100ms anyway.

**`#define LOG_TAG` must be first:**

Every `.cpp` file starts with `#define LOG_TAG "potvolumed"` BEFORE any `#include`.
The Android logging macros (`ALOGI`, `ALOGE`, etc.) from `<log/log.h>` embed
`LOG_TAG` as a string literal at compile time. If `LOG_TAG` is defined after
`<log/log.h>` is included, the preprocessor has already expanded it with
whatever default value was there — usually an empty string or a compile warning.

**Signal handling:**

`SIGTERM` is sent by `adb shell stop potvolumed` (init kills the service).
`SIGINT` is sent by Ctrl+C in development. The signal handler sets `gRunning = false`,
which causes the main loop to exit cleanly on the next iteration. The destructor of
`VolumeController` then calls `UI_DEV_DESTROY` to remove the virtual input device
from the kernel cleanly — no dangling devices left after shutdown.

---

## SpiReader — Talking to the MCP3008

### What SPI Is

SPI (Serial Peripheral Interface) is a 4-wire synchronous communication protocol:
- **SCLK** (clock): RPi5 drives this. Every rising edge clocks one bit.
- **MOSI** (Master Out Slave In): RPi5 sends bits to MCP3008 on this wire.
- **MISO** (Master In Slave Out): MCP3008 sends bits back to RPi5 on this wire.
- **CS/CE** (Chip Select): RPi5 pulls this LOW to select the MCP3008 for a transaction.

The transfer is always simultaneous — while the master sends bytes, it also
receives bytes. There is no "wait for response" step; both happen at the same time
(the slave's response is clocked out as the master clocks in its command).

### The MCP3008 3-Byte Protocol

The MCP3008 uses a specific 3-byte SPI frame to return an ADC reading:

```
Byte you send → What it does
─────────────────────────────────────────────────────────────────
Byte 0: 0x01   Start bit. MCP3008 ignores all bits until it sees
               the '1' start bit. Sending 0x01 puts the start bit
               in the LSB, leaving 7 leading zeros for the chip to
               synchronize on.

Byte 1: 0x80   Configuration byte.
               Bit 7 (SGL=1): single-ended mode (measure CH0 vs GND,
               not as differential pair).
               Bits 6:4 (D2:D0 = 000): select channel 0 (CH0).
               Bits 3:0: don't-care — just clocking.
               → 1000 0000 = 0x80 for CH0
               → 1001 0000 = 0x90 for CH1
               → 1010 0000 = 0xA0 for CH2, etc.

Byte 2: 0x00   Padding. MCP3008 shifts out the remaining 8 bits of
               result on MISO while we send 0s on MOSI. Content of
               what we send doesn't matter.
```

What comes back on MISO:
```
rx[0]: undefined (received while sending start bit — chip not ready)
rx[1]: bits 9:8 of ADC result in positions [1:0]
       bits [7:2] are leading zeros from chip's null bit
rx[2]: bits 7:0 of ADC result

Decode: value = ((rx[1] & 0x03) << 8) | rx[2]
```

If the potentiometer wiper is at 0V: `value = 0`
If the potentiometer wiper is at 3.3V: `value = 1023`
If the wiper is at mid-point 1.65V: `value ≈ 512`

### The Linux SPI ioctl

The kernel exposes SPI via `ioctl()` on `/dev/spidevX.Y`. The key call is:

```cpp
struct spi_ioc_transfer tr{};
tr.tx_buf        = (unsigned long)tx;   // what we send (3 bytes)
tr.rx_buf        = (unsigned long)rx;   // where to store what we receive
tr.len           = 3;
tr.speed_hz      = 1000000;             // 1 MHz clock
tr.bits_per_word = 8;
tr.delay_usecs   = 0;

ioctl(mFd, SPI_IOC_MESSAGE(1), &tr);
```

`SPI_IOC_MESSAGE(1)` is a single full-duplex transfer:
- Kernel asserts CS (pulls GPIO8/CE0 low)
- Clocks 24 bits (3 bytes) on SCLK
- Simultaneously shifts out `tx[]` on MOSI and receives on MISO into `rx[]`
- Kernel de-asserts CS (pulls GPIO8/CE0 high)

The `open()` sequence sets SPI mode, bits per word, and speed:
```cpp
ioctl(mFd, SPI_IOC_WR_MODE,         &kSpiMode);       // MODE_0: CPOL=0,CPHA=0
ioctl(mFd, SPI_IOC_WR_BITS_PER_WORD, &kSpiBitsPerWord); // 8 bits
ioctl(mFd, SPI_IOC_WR_MAX_SPEED_HZ,  &kSpiSpeedHz);    // 1 MHz
```

**Why SPI_MODE_0 (CPOL=0, CPHA=0):**
MCP3008 datasheet specifies it works with CPOL=0,CPHA=0 or CPOL=1,CPHA=1
(modes 0 and 1,1). We use mode 0: clock idles LOW, data sampled on rising edge.

**Why 1 MHz:**
MCP3008 maximum SPI speed at 3.3V is 3.6 MHz. 1 MHz is conservative and works
reliably with breadboard wire inductance and capacitance (long wires reduce maximum
reliable frequency).

**The raw bytes log (ALOGV level):**

```cpp
ALOGV("SPI raw: tx=[%02X %02X %02X] rx=[%02X %02X %02X] value=%d",
      tx[0], tx[1], tx[2], rx[0], rx[1], rx[2], value);
```

This uses `ALOGV` (verbose) — the lowest log level. In AOSP, verbose logs are
stripped out in release builds and suppressed by default even in debug builds
unless explicitly enabled. To see them during development:
```bash
adb shell setprop log.tag.potvolumed V
```

---

## AdcFilter — Noise Removal

### Why Raw ADC Readings Are Noisy

Even when the potentiometer knob is perfectly still, the ADC reading fluctuates:
- The RPi5's switching power supply generates high-frequency noise that couples
  onto the 3.3V rail and into the MCP3008 reference voltage
- The potentiometer wiper makes resistive contact with a carbon/cermet track —
  at the microscopic level this contact is never perfectly clean
- 10-bit ADC quantization means even a stable 1.65V toggles between count 511
  and 512 depending on the exact voltage at the sampling moment

Without filtering, `setVolume()` would be called constantly even at rest, causing
visible UI flicker and unnecessary Binder IPC calls.

### Stage 1: Exponential Moving Average (EMA)

```cpp
static constexpr float kAlpha = 0.2f;
mEmaValue = kAlpha * rawValue + (1.0f - kAlpha) * mEmaValue;
```

EMA is a single-pole IIR (Infinite Impulse Response) low-pass filter. It is
mathematically equivalent to a simple RC circuit (resistor-capacitor):
- High-frequency noise (fast oscillations) → attenuated
- Slow changes (turning the knob) → pass through with slight delay

`α = 0.2` means each new sample contributes 20% and history 80%.

**Effect on response time:**

If you turn the knob instantly from 0 to 1023, how many samples does the EMA
take to reach 90% of the final value?

```
After 1 sample:  0.2 × 1023 = 204
After 2 samples: 0.2×1023 + 0.8×204 = 367
After 10 samples: ~1023×(1 - 0.8^10) ≈ 893  (≈87%)
After 11 samples: ≈910  (≈89%)
```

At 50ms per sample, 11 samples = 550ms. This is a slight lag when you spin
the knob very fast — acceptable for a volume control.

**Why float, not integer:**

EMA on integers would accumulate rounding errors. Imagine the true EMA is 511.6.
With integer math: `int(0.2×512 + 0.8×511) = int(102.4 + 408.8) = int(511.2) = 511`.
The 0.2 and 0.6 parts are lost every cycle. Over thousands of iterations, this
biases the filter. Float accumulator avoids this.

**The seeding problem:**

On first call, `mEmaValue = -1` (sentinel). If we ran the EMA normally:
```
After call 1: 0.2×real + 0.8×(-1) = real×0.2 - 0.8
```
That's wrong — the EMA would slowly ramp from near -1 to the real value over
many seconds, causing a spurious volume sweep at boot. The fix: seed directly:
```cpp
if (mEmaValue < 0.0f) {
    mEmaValue = static_cast<float>(rawValue);  // jump straight to real value
    mLastStable = rawValue;
    *changed = true;  // emit once so VolumeController can set boot reference
}
```

### Stage 2: Dead Zone

```cpp
static constexpr int kDeadZone = 8;

if (std::abs(filtered - mLastStable) >= kDeadZone) {
    mLastStable = filtered;
    *changed = true;
}
```

After EMA, the filtered value may still oscillate by ±1–2 counts when at rest
(EMA doesn't completely eliminate noise, it just reduces it). The dead zone
ignores changes smaller than 8 counts.

**Key subtlety:** The dead zone is measured against `mLastStable` (last emitted
value), NOT against the running EMA. This matters:
- If the value drifts slowly across the ±8 boundary due to noise, `mLastStable`
  stays fixed until the filtered value moves definitively past ±8
- Without this, the filter could oscillate: emit change at +8, then emit back
  at -8, then +8 again — all from noise

**Dead zone coverage:**

8 counts out of 1023 = 0.78% of full scale. The volume has 16 steps (0–15),
each covering 1023/15 ≈ 68 ADC counts. The dead zone (8 counts) is about 12%
of one volume step — tight enough to be responsive, wide enough to reject noise.

---

## VolumeMapper — ADC to Volume Index

```cpp
float ratio = static_cast<float>(adcValue) / static_cast<float>(kAdcMax);
int index   = static_cast<int>(std::round(ratio * static_cast<float>(kVolumeMax)));
```

Simple linear interpolation: `index = round(adcValue / 1023.0 × 15)`

**Examples:**
```
adc=0    → ratio=0.000 → index=round(0.00)  = 0   (mute)
adc=68   → ratio=0.067 → index=round(1.00)  = 1
adc=512  → ratio=0.500 → index=round(7.50)  = 8   (midpoint)
adc=955  → ratio=0.934 → index=round(14.01) = 14
adc=1023 → ratio=1.000 → index=round(15.00) = 15  (max)
```

**Why `round()` instead of integer division:**

`(512 * 15) / 1023 = 7680 / 1023 = 7` (truncates — biased toward lower volumes)
`round(512.0 / 1023.0 * 15) = round(7.5) = 8` (symmetric, unbiased)

`round()` distributes the 16 volume steps evenly across the 1024 ADC counts.
Each step covers approximately `1023/15 ≈ 68` counts, with a sharp transition
exactly at the midpoint of each interval.

**Where the volume max (15) comes from:**

`AUDIO_STREAM_MUSIC` in AOSP Android 15 has 16 steps indexed 0–15. This is
queried at runtime by `AudioSystem::getStreamMaxVolume()` in a full implementation.
Here it is hardcoded to 15 as a constant — safe for this platform.

---

## VolumeController — Injecting Key Events

### Why uinput Instead of libaudioclient

The original plan called `AudioSystem::setStreamVolumeIndex()` from `libaudioclient`.
This failed because `libaudioclient` has no `image:vendor` variant in AOSP 15 —
it's a system-only library. The Soong build refused to link it into a vendor binary.

The alternative is `uinput` — a standard Linux kernel interface that lets any
process create a virtual input device. The kernel then delivers events from that
device to Android's `InputReader` exactly as if a physical button was pressed.

This is literally how volume buttons on real Android phones work:
```
Physical button pressed → GPIO interrupt → kernel key event → InputReader →
PhoneWindowManager → AudioManager.adjustStreamVolume(STREAM_MUSIC, ±1)
```
Our uinput device inserts into the same pipeline at the kernel input event level.

### uinput Setup Sequence

```cpp
// Step 1: open the device
mFd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

// Step 2: declare what event types we produce
ioctl(mFd, UI_SET_EVBIT, EV_KEY);   // we send key events
ioctl(mFd, UI_SET_EVBIT, EV_SYN);   // we send sync events (required)

// Step 3: register which keys
ioctl(mFd, UI_SET_KEYBIT, KEY_VOLUMEUP);
ioctl(mFd, UI_SET_KEYBIT, KEY_VOLUMEDOWN);

// Step 4: describe the device
struct uinput_setup usetup{};
strncpy(usetup.name, "PotVolume Knob", UINPUT_MAX_NAME_SIZE - 1);
usetup.id.bustype = BUS_USB;   // conventional for virtual devices
ioctl(mFd, UI_DEV_SETUP, &usetup);

// Step 5: publish — after this, /dev/input/eventN appears
ioctl(mFd, UI_DEV_CREATE);
```

Once `UI_DEV_CREATE` succeeds, Android's `InputReader` sees a new input device
and starts monitoring it for events.

### Sending a Key Press

One "key press" that Android sees as one volume step:
```cpp
// Key down
struct input_event ev{};
ev.type  = EV_KEY;
ev.code  = KEY_VOLUMEUP;  // or KEY_VOLUMEDOWN
ev.value = 1;             // 1 = pressed
write(mFd, &ev, sizeof(ev));

// Sync — flush to InputReader
ev.type  = EV_SYN;
ev.code  = SYN_REPORT;
ev.value = 0;
write(mFd, &ev, sizeof(ev));

// Key up
ev.type  = EV_KEY;
ev.code  = KEY_VOLUMEUP;
ev.value = 0;             // 0 = released
write(mFd, &ev, sizeof(ev));

// Sync again
ev.type  = EV_SYN;
ev.code  = SYN_REPORT;
ev.value = 0;
write(mFd, &ev, sizeof(ev));
```

`EV_SYN / SYN_REPORT` is mandatory. Android's `InputReader` buffers events and
does not dispatch them until it sees an `EV_SYN`. Sending `EV_KEY` without
following it with `EV_SYN` means Android never processes the key event.

### Delta-Based Control

uinput sends discrete events — there is no "set volume to absolute index N".
Instead, we track the last index we set and compute how many steps to move:

```cpp
int delta   = volumeIndex - mLastIndex;
int keyCode = (delta > 0) ? KEY_VOLUMEUP : KEY_VOLUMEDOWN;
int steps   = std::abs(delta);

for (int i = 0; i < steps; i++) {
    sendKeyEvent(keyCode, 1);   // down
    sendSyncEvent();
    sendKeyEvent(keyCode, 0);   // up
    sendSyncEvent();
}
```

If you move the knob from index 5 to index 12 in one poll cycle (large fast
movement), `delta=7` and we send 7 KEY_VOLUMEUP presses. Each press increments
Android's volume by 1, so after 7 presses the volume is at index 12.

### Boot Reference — No Volume Jump at Startup

On the very first `setVolume()` call, `mLastIndex = -1` (sentinel). If we
computed the delta normally: `delta = currentIndex - (-1) = currentIndex + 1`.
That would be wrong — we'd send phantom volume-up presses at boot.

Instead:
```cpp
if (mLastIndex < 0) {
    mLastIndex = volumeIndex;  // just record where we are — send nothing
    return true;
}
```

The first reading silently becomes the reference. The knob must move from its
boot position to trigger any volume event. This prevents Android's volume from
jumping at startup.

### Clean Shutdown

The destructor calls `UI_DEV_DESTROY`:
```cpp
VolumeController::~VolumeController() { close(); }

void VolumeController::close() {
    if (mFd >= 0) {
        ioctl(mFd, UI_DEV_DESTROY);  // removes /dev/input/eventN from kernel
        ::close(mFd);
        mFd = -1;
    }
}
```

Without `UI_DEV_DESTROY`, the virtual device lingers in the kernel's input
system. Android would still list it as a connected device. On next daemon start,
a second virtual device would be created — two "PotVolume Knob" devices, both
sending events. Multiply restarts → multiple ghost devices.

---

## SELinux Policy

```
# potvolumed.te

type potvolumed,      domain;
type potvolumed_exec, exec_type, vendor_file_type, file_type;
type spidev_device,   dev_type;

init_daemon_domain(potvolumed)

allow potvolumed spidev_device:chr_file { open read write ioctl };
allow potvolumed uhid_device:chr_file   { open read write ioctl };
allow potvolumed logd:unix_dgram_socket sendto;
```

**Type declarations:**

`type potvolumed, domain` — declares the SELinux domain the daemon runs in.
Every process in Android SELinux runs in a domain.

`type potvolumed_exec, exec_type, vendor_file_type, file_type` — declares the
type for the binary file itself. `exec_type` means it can be used as a domain
transition entry point. `vendor_file_type` means it lives in the vendor partition.

`type spidev_device, dev_type` — AOSP 15 base policy does NOT define this type.
There is no built-in `spidev_device` for SPI character devices. We define our own.
`dev_type` is the base type for all device nodes. The matching `file_contexts`
entry labels `/dev/spidev*` with this type.

**`init_daemon_domain(potvolumed)`:**

This macro (defined in AOSP base policy) expands to a set of rules that allow
`init` to execute `/vendor/bin/potvolumed` and transition the new process into
the `potvolumed` domain. Without this, init cannot start the daemon.

**`allow potvolumed spidev_device:chr_file { open read write ioctl }`:**

- `open`: needed to call `open("/dev/spidev0.0", ...)`
- `read` / `write`: SPI `ioctl` uses both directions internally
- `ioctl`: the actual `SPI_IOC_MESSAGE` call that performs the SPI transfer

**`allow potvolumed uhid_device:chr_file { open read write ioctl }`:**

On RPi5 AOSP 15, `/dev/uinput` is labeled `uhid_device` — NOT `uinput_device`.
The type `uinput_device` does not exist in AOSP 15 base policy. Verified with
`adb shell ls -laZ /dev/uinput`. This was discovered by checking the actual
device label on the running device, not from documentation.

**`allow potvolumed logd:unix_dgram_socket sendto`:**

AOSP 15 removed `/dev/log*` character devices. All logging goes through `logd`
via a Unix datagram socket at `/dev/socket/logd`. The `ALOGI`/`ALOGE` macros
from `liblog` do this automatically. The rule grants permission to send to
that socket.

**`file_contexts`:**

```
/vendor/bin/potvolumed       u:object_r:potvolumed_exec:s0
/dev/spidev[0-9]+\.[0-9]+   u:object_r:spidev_device:s0
```

The regex `/dev/spidev[0-9]+\.[0-9]+` matches all SPI device nodes:
`spidev0.0`, `spidev0.1`, `spidev10.0`, etc. This is correct — all SPI devices
should use `spidev_device` type regardless of bus/CS index.

---

## Key Design Decisions and Why

### Decision 1: uinput over libaudioclient

**Why:** `libaudioclient` is not accessible from vendor code in AOSP 15.
`uinput` is a kernel interface — no library dependency, no VNDK issue, and
it follows the exact same code path as physical hardware volume buttons.

**Trade-off:** uinput is delta-based (relative), not absolute. If Android's
volume is manually changed by another source (e.g., user taps on-screen volume
bar), the daemon's `mLastIndex` becomes stale. The next knob movement will
compute a wrong delta. In practice this is acceptable for a development demo —
a production system would re-sync `mLastIndex` from `AudioManager.getStreamVolume()`.

### Decision 2: Two-stage filter (EMA + dead zone)

**Why:** EMA alone reduces high-frequency noise but the output still drifts
by ±1 at rest. Dead zone alone would ignore small real movements. Combined:
EMA smooths, dead zone suppresses the residual EMA noise. The two stages
handle different frequency bands of noise.

**Why α=0.2 and dead zone=8:**
- α=0.2: heavy enough to suppress switching noise, fast enough to feel responsive
- 8 counts: 12% of one volume step, empirically determined to eliminate jitter
  on a breadboard without losing responsiveness to slow knob turns

### Decision 3: 20 Hz polling (50ms interval)

**Why:** Volume controls don't need high frequency. The audio system processes
events in ~100ms. The human hand cannot turn a knob faster than meaningful ADC
change rate. 20 Hz is conservative and well-understood — predictable timing
for the EMA filter.

### Decision 4: Single-file components with clear interfaces

Each component (`SpiReader`, `AdcFilter`, `VolumeMapper`, `VolumeController`)
is a single `.h`/`.cpp` pair with a minimal public interface. This allows:
- Independent testing of each stage
- Easy replacement (e.g., swap `SpiReader` for a mock in a test binary)
- Clear separation of kernel-level concerns (SPI, uinput) from algorithmic
  concerns (filtering, mapping)

### Decision 5: `BOARD_VENDOR_SEPOLICY_DIRS` not `PRODUCT_PRIVATE_SEPOLICY_DIRS`

**Why:** All vendor services must use `BOARD_VENDOR_SEPOLICY_DIRS`. This feeds
`vendor_sepolicy.cil` and `vendor_file_contexts`. Using `PRODUCT_PRIVATE_SEPOLICY_DIRS`
feeds `product_sepolicy.cil` (product partition) — completely wrong for
`/vendor/bin/*` services. The binary stays with label `vendor_file:s0` and init
refuses to start it. This was the hardest bug in the project.

---

## How Each Component Was Tested

### SpiReader
**Symptom-based test:** `adb logcat -s potvolumed | grep "raw="` — value must
change when knob moves. Value stuck at 1023 → MISO floating (wrong SPI controller
or disconnected wire). Value stuck at 0 → CS or MOSI issue.

### AdcFilter
**Log observation:** The `ALOGD` lines show the EMA value alongside the raw value.
With hardware connected, the EMA should smoothly track the raw value with slight
lag when moving fast, and stay stable when the knob is still.

### VolumeMapper
**Log observation:** `VolumeMapper: adc=NNN → ratio=X.XXX → index=Y/15` appears
every time a stable change is detected. Verify the index matches expected — adc=0
→ index=0, adc=1023 → index=15, adc=512 → index=8.

### VolumeController
**Manual test (no hardware needed):**
```bash
adb shell sendevent /dev/uinput 1 115 1   # KEY_VOLUMEUP press
adb shell sendevent /dev/uinput 0 0 0     # EV_SYN
adb shell sendevent /dev/uinput 1 115 0   # KEY_VOLUMEUP release
adb shell sendevent /dev/uinput 0 0 0     # EV_SYN
```
If Android volume changes, uinput is working.

**End-to-end:** `adb shell dumpsys audio | grep -A2 "STREAM_MUSIC"` shows current
volume index. Turn the knob and re-run to verify it changed.

---

*Code explanation for: `vendor/myoem/services/potvolumed`*
*Platform: Raspberry Pi 5 · AOSP Android 15 (`android-15.0.0_r14`)*
*See also: `PotVolume_Article_Series.md` (debugging journal)*
*Plan file: `POTVOLUME_PLAN.md`*
