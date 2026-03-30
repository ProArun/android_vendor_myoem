# Building a Physical Volume Knob for AOSP on Raspberry Pi 5
## From a Potentiometer on a Breadboard to Android System Volume

**A Complete Engineering Journal**

---

> **About this article**: This is a real engineering journal of building a hardware-to-Android
> bridge entirely inside `vendor/myoem/` — no framework changes, no app, no AIDL service.
> A physical rotary potentiometer connected to an MCP3008 SPI ADC chip controls the Android
> media volume in real time. Every hardware decision, every C++ design choice, every SELinux
> rule, and every build command is documented exactly as it was written.
>
> **Status**: All build errors resolved and documented. Parts 11 and 12 updated
> with all real errors encountered during development. Hardware runtime testing pending.

---

## Table of Contents

1. [The Idea — A Physical Volume Knob for Android](#part-1)
2. [Hardware Foundations — SPI, ADC, and the MCP3008](#part-2)
3. [Architecture — Why No AIDL and No App?](#part-3)
4. [Phase 1 — SpiReader: Talking to Hardware from C++](#part-4)
5. [Phase 2 — AdcFilter: Taming Electrical Noise](#part-5)
6. [Phase 3 — VolumeMapper and VolumeController: Reaching Android Audio](#part-6)
7. [Phase 4 — main.cpp, RC File, and the Boot Sequence](#part-7)
8. [SELinux Policy — The Hidden Wall Between Vendor and Audio](#part-8)
9. [Build System — Android.bp and myoem_base.mk](#part-9)
10. [Build, Push, and First Boot](#part-10)
11. [Debugging Guide — Layer by Layer](#part-11)
12. [Lessons Learned](#part-12)

---

<a name="part-1"></a>
# Part 1: The Idea — A Physical Volume Knob for Android

## The Problem

Modern Android setups — especially on a single-board computer like the Raspberry Pi 5 —
often lack physical hardware controls. Changing volume requires touching the screen or
pressing software buttons. For a learning project, the more interesting question is:
**can we add a real physical knob that controls Android volume, without modifying a single
line of the Android framework?**

The answer is yes. And the path is entirely through the vendor layer.

## The Components

The project uses three pieces of hardware:

- A **rotary potentiometer** — a variable resistor with a knob. Turning it changes the
  resistance between its wiper pin and its endpoints, which changes the voltage on the
  wiper from 0V to 3.3V.
- An **MCP3008** — a small SPI-connected ADC (Analog-to-Digital Converter) chip. It reads
  the analog voltage from the potentiometer and converts it to a 10-bit digital number
  (0 to 1023) that a microprocessor can read.
- The **Raspberry Pi 5** itself — which has a hardware SPI controller and runs AOSP Android 15.

The RPi5 has no built-in ADC. This is why the MCP3008 is needed: the Pi can only read
digital signals (0 or 1), not analog voltages. The MCP3008 bridges that gap.

## The Goal

```
Turn knob left  →  lower Android media volume
Turn knob right →  raise Android media volume
Idle (no movement) →  volume stays put, no flicker
```

This is implemented as a native C++ daemon called `potvolumed` that lives in
`vendor/myoem/services/potvolumed/`. It starts at boot, polls the hardware every 50ms,
and calls Android's audio API when the knob moves.

---

<a name="part-2"></a>
# Part 2: Hardware Foundations — SPI, ADC, and the MCP3008

## What Is SPI?

SPI (Serial Peripheral Interface) is a communication protocol used between a master
device (the RPi5) and one or more peripheral chips (our MCP3008). It uses four wires:

| Wire | Direction | Purpose |
|------|-----------|---------|
| SCLK | Master → Slave | Clock signal — master drives this at fixed frequency |
| MOSI | Master → Slave | Master Out Slave In — data sent FROM the Pi |
| MISO | Master → Slave | Master In Slave Out — data received BY the Pi |
| CS   | Master → Slave | Chip Select — pulled LOW to select the MCP3008 |

For every clock pulse, one bit goes out on MOSI and one bit comes back on MISO
simultaneously (full duplex). The master controls the clock; the slave responds in sync.

On the RPi5, the kernel SPI master driver (`spi-bcm2835`) exposes SPI bus 0 chip-select 0
as a spidev character device. Our daemon talks to the MCP3008 using the standard
Linux `ioctl` interface on this file.

> **RPi5 AOSP Quirk — Unexpected Bus Number**: You might expect the device node to be
> `/dev/spidev0.0` (SPI bus 0). On this specific RPi5 AOSP 15 build, the kernel
> registers `spi-bcm2835` as `spi_master` index **10**, producing `/dev/spidev10.0`.
> This is not a hardware change — it is a kernel enumeration artifact. Always verify
> with `adb shell ls /dev/spidev*` on the actual device before hardcoding any path.
> We learned this the hard way (see Part 11, Bug #4).

## What Is the MCP3008?

The MCP3008 is an 8-channel, 10-bit SPI ADC made by Microchip. Key facts:

- **10-bit**: outputs values from 0 to 1023 (2¹⁰ − 1)
- **8 channels**: can read up to 8 different analog signals (CH0–CH7)
- **Reference voltage = VDD**: with VDD = 3.3V, a 3.3V input reads as 1023, 0V reads as 0
- **SPI Mode 0,0**: clock idle low, data sampled on rising edge

We use channel CH0 (pin 1) for the potentiometer wiper.

## The MCP3008 SPI Transaction

Every reading requires a 3-byte (24 clock cycle) SPI transaction:

```
BYTES SENT (MOSI):
  [0]: 0x01                     ← Start bit — MCP3008 waits for this '1'
  [1]: 0x80 | (channel << 4)    ← SGL=1 (single-ended), channel select
       CH0: 1000_0000 = 0x80
       CH1: 1001_0000 = 0x90
       CH2: 1010_0000 = 0xA0 ... etc.
  [2]: 0x00                     ← Don't care — just clocking out the result

BYTES RECEIVED (MISO):
  [0]: ignored                  ← MCP3008 hasn't seen start bit yet
  [1]: 0b??????ab               ← bits [1:0] = ADC result bits [9:8]
  [2]: 0bbbbbbbbb               ← bits [7:0] = ADC result bits [7:0]

DECODE:
  value = ((received[1] & 0x03) << 8) | received[2]
  Range: 0 (0V) to 1023 (3.3V)
```

## Wiring the Hardware

```
┌──────────────────────────────────────────────────────────────────┐
│                    BREADBOARD WIRING                             │
│                                                                  │
│   Raspberry Pi 5 (40-pin header)     MCP3008 (DIP-16)           │
│   ──────────────────────────         ──────────────────          │
│                                                                  │
│   3.3V  [Pin 1]  ─────────────────── VDD  [Pin 16]              │
│   3.3V  [Pin 1]  ─────────────────── VREF [Pin 15]  ← ref       │
│   GND   [Pin 6]  ─────────────────── AGND [Pin 14]              │
│   SCLK  [Pin 23] ─────────────────── CLK  [Pin 13]              │
│   MISO  [Pin 21] ─────────────────── DOUT [Pin 12]              │
│   MOSI  [Pin 19] ─────────────────── DIN  [Pin 11]              │
│   CE0   [Pin 24] ─────────────────── CS   [Pin 10]              │
│   GND   [Pin 6]  ─────────────────── DGND [Pin 9]               │
│                                                                  │
│                       MCP3008 CH0 [Pin 1] ──→ Pot Wiper         │
│                                                                  │
│   Potentiometer (3 terminals):                                   │
│     Left end   ──→ GND                                          │
│     Wiper      ──→ MCP3008 CH0 (pin 1)                          │
│     Right end  ──→ 3.3V                                         │
└──────────────────────────────────────────────────────────────────┘
```

## Verifying the Hardware Exists

Before writing any code, verify that the SPI device is visible in AOSP:

```bash
# Does the kernel expose the SPI device?
adb shell ls -la /dev/spidev*
# Expected: crw-rw---- root root ... /dev/spidev0.0
# If missing: SPI is not enabled in the device tree (see Part 10)

# What SELinux label does it have?
adb shell ls -laZ /dev/spidev*
# Expected: u:object_r:spidev_device:s0
# If label is different: update potvolumed.te accordingly
```

**Key insight**: The SPI device file exists independently of whether any hardware is
physically wired. The kernel creates `/dev/spidev0.0` as soon as the SPI controller is
enabled in the device tree. You can run Steps 1–3 (build, push, SELinux fix) with no
hardware connected at all.

---

<a name="part-3"></a>
# Part 3: Architecture — Why No AIDL and No App?

## Comparing to Previous Projects

The previous projects in this vendor tree (ThermalControl, SafeMode) both follow a
multi-layer stack: HAL → Binder Service → Java Manager → Kotlin App. Why does
`potvolumed` look so different?

```
ThermalControl / SafeMode pattern:
  Hardware → HAL → AIDL Service → Java Library → Kotlin App
  (5 layers, multiple binaries, AIDL interface)

potvolumed pattern:
  Hardware → single C++ daemon → Android Audio API
  (1 layer, 1 binary, no AIDL)
```

The difference comes from the **data flow direction**:

- ThermalControl exposes data **to** other apps (temperature, fan state). Any app might
  want to display or control it. A Binder service is the right pattern.
- `potvolumed` is a **sink** — it takes a hardware input and drives a system output.
  No other process needs to query it or subscribe to it. A Binder service would be
  over-engineering. A simple poll-and-set loop is the right pattern.

## The Design Rule: Simplest Thing That Works

```
Complexity Budget:
  Needed: read SPI, map to volume, set volume
  NOT needed: expose Binder service, AIDL interface,
              Java library, Kotlin app, callbacks,
              subscription model
```

Adding an AIDL service "in case" some future app wants to read the knob position would
add ~300 lines of boilerplate for zero present benefit. We chose not to.

## Full Stack Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│  HARDWARE LAYER                                                  │
│  Potentiometer ──analog voltage──▶ MCP3008 ──SPI──▶ RPi5 GPIO   │
└───────────────────────────────────────────┬──────────────────────┘
                                            │ /dev/spidev0.0
                                            │ (Linux character device)
┌───────────────────────────────────────────▼──────────────────────┐
│  VENDOR DAEMON: potvolumed  (vendor/myoem/services/potvolumed/)  │
│                                                                  │
│  SpiReader          reads raw 10-bit ADC value (0–1023)         │
│      │                                                           │
│  AdcFilter          EMA noise smoothing + dead zone             │
│      │                                                           │
│  VolumeMapper       0–1023 → 0–15 Android volume index         │
│      │                                                           │
│  VolumeController   AudioSystem::setStreamVolumeIndex()         │
└───────────────────────────────────────────┬──────────────────────┘
                                            │ Binder IPC
                                            │ (libaudioclient LLNDK)
┌───────────────────────────────────────────▼──────────────────────┐
│  ANDROID FRAMEWORK (system partition — unmodified)               │
│  media.audio_policy → AudioPolicyService → AudioService          │
│                              │                                   │
│                      AUDIO_STREAM_MUSIC volume 0–15             │
└──────────────────────────────────────────────────────────────────┘
```

## File Structure

```
vendor/myoem/services/potvolumed/
├── Android.bp                    ← cc_binary, links libaudioclient (LLNDK)
├── potvolumed.rc                 ← init: chown spidev, start service
├── src/
│   ├── main.cpp                  ← poll loop: SPI → filter → map → volume
│   ├── SpiReader.h / .cpp        ← Linux spidev ioctl wrapper
│   ├── AdcFilter.h / .cpp        ← EMA low-pass + dead zone filter
│   ├── VolumeMapper.h / .cpp     ← 0–1023 → 0–15 linear mapping
│   └── VolumeController.h / .cpp ← AudioSystem::setStreamVolumeIndex()
└── sepolicy/private/
    ├── potvolumed.te             ← domain policy: spidev + audioserver
    ├── file_contexts             ← labels /vendor/bin/potvolumed
    └── service_contexts          ← empty (no named Binder service)
```

---

<a name="part-4"></a>
# Part 4: Phase 1 — SpiReader: Talking to Hardware from C++

## The Linux spidev Interface

The Linux kernel provides a standard userspace SPI interface through the `spidev` module.
Any process that can open `/dev/spidev0.0` can perform SPI transactions using `ioctl`.
No custom driver is needed.

The key header is `<linux/spi/spidev.h>`, which defines:
- `SPI_IOC_WR_MODE` — set SPI mode (clock polarity + phase)
- `SPI_IOC_WR_MAX_SPEED_HZ` — set clock frequency
- `SPI_IOC_WR_BITS_PER_WORD` — set word size (always 8 for us)
- `SPI_IOC_MESSAGE(n)` — perform n transfer segments atomically

## Opening and Configuring the Device

```cpp
// SpiReader.cpp

// Open the character device
mFd = ::open("/dev/spidev0.0", O_RDWR);

// Set SPI Mode 0: CPOL=0 (clock idle low), CPHA=0 (sample on rising edge)
// MCP3008 datasheet specifies SPI mode 0,0 or 1,1. We use 0,0.
uint8_t mode = SPI_MODE_0;
ioctl(mFd, SPI_IOC_WR_MODE, &mode);

// Set clock speed to 1 MHz (MCP3008 max is 3.6 MHz at 3.3V — we're conservative)
uint32_t speed = 1'000'000;
ioctl(mFd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

// 8-bit words — standard for byte-oriented protocols
uint8_t bits = 8;
ioctl(mFd, SPI_IOC_WR_BITS_PER_WORD, &bits);
```

## Performing the 3-Byte Transaction

```cpp
// Build the transmit buffer
uint8_t tx[3] = {
    0x01,                                         // Start bit
    static_cast<uint8_t>(0x80 | (channel << 4)), // SGL=1, CH0=0x80
    0x00                                          // Don't care
};
uint8_t rx[3] = {0, 0, 0};

// Describe the transfer to the kernel
struct spi_ioc_transfer tr{};
tr.tx_buf        = reinterpret_cast<unsigned long>(tx);
tr.rx_buf        = reinterpret_cast<unsigned long>(rx);
tr.len           = 3;
tr.speed_hz      = 1'000'000;
tr.bits_per_word = 8;
tr.delay_usecs   = 0;

// Execute: kernel asserts CS, clocks 3 bytes, de-asserts CS
ioctl(mFd, SPI_IOC_MESSAGE(1), &tr);

// Decode the result from the receive buffer
int value = ((rx[1] & 0x03) << 8) | rx[2];
// value: 0 = knob fully left (0V), 1023 = knob fully right (3.3V)
```

## The Channel Byte Explained

The second byte sent to the MCP3008 is the configuration byte. Its bit layout is:

```
Bit position: 7    6    5    4    3    2    1    0
              SGL  D2   D1   D0   X    X    X    X

SGL = 1 → single-ended mode (read channel vs GND, not differential)
D2:D0 → channel number (000 = CH0, 001 = CH1, 010 = CH2 ... 111 = CH7)
X     → don't care (these bit positions are used for the result bits on MISO)

For CH0 single-ended: SGL=1, D2=0, D1=0, D0=0
  = 1000_0000b = 0x80

For CH1 single-ended: SGL=1, D2=0, D1=0, D0=1
  = 1001_0000b = 0x90

General formula: 0x80 | (channel << 4)
```

## Why `reinterpret_cast<unsigned long>` for tx_buf?

The `spi_ioc_transfer` struct defines `tx_buf` and `rx_buf` as `__u64` (64-bit unsigned
integers) even on 32-bit systems, because the struct is designed to be stable across
kernel versions and architectures. We store the **pointer** to our arrays as integers
in these fields. The `reinterpret_cast<unsigned long>` converts the pointer to an integer
without losing bits — the kernel then uses this integer as a pointer inside its driver.

---

<a name="part-5"></a>
# Part 5: Phase 2 — AdcFilter: Taming Electrical Noise

## The Problem: Noise on Real Hardware

When you first connect a potentiometer to an ADC and print the values, you will see
something like this even when the knob is completely still:

```
raw=512 raw=514 raw=511 raw=513 raw=512 raw=510 raw=514 ...
```

The value oscillates by 2–5 counts constantly. Sources of this noise:

1. **Switching noise**: The RPi5's CPU and power management generate high-frequency
   electrical interference that couples into the analog wires
2. **Contact resistance**: Inside the potentiometer, the wiper slides over a resistive
   track. Near the current position, micro-vibrations cause tiny resistance changes
3. **ADC quantization**: A continuous voltage that sits between two ADC steps toggles
   back and forth between them

Without filtering, these oscillations would cause the daemon to call
`setStreamVolumeIndex()` hundreds of times per second even when you're not
touching the knob. Each call is a cross-partition Binder IPC to `audioserver`.

## Stage 1: Exponential Moving Average

An EMA (Exponential Moving Average) is a digital low-pass filter that smooths rapid
fluctuations while still responding to genuine changes:

```
EMA_new = α × raw_new + (1 − α) × EMA_old
```

Where `α` (alpha) is the smoothing factor:
- `α = 1.0` → no smoothing, instant response (raw pass-through)
- `α = 0.0` → infinite smoothing, never responds to anything
- `α = 0.2` → responds in ~5 samples to a stable new value (5 × 50ms = 250ms)

We chose `α = 0.2`. This means when you turn the knob, the filtered value reaches
the new stable position in about 250ms — fast enough to feel responsive.

```cpp
// AdcFilter.cpp

// First call: seed directly (don't ramp from -1 to actual value)
if (mEmaValue < 0.0f) {
    mEmaValue = static_cast<float>(rawValue);
    mLastStable = rawValue;
    *changed = true;
    return mLastStable;
}

// EMA formula: each new sample contributes 20%, history contributes 80%
mEmaValue = 0.2f * static_cast<float>(rawValue) + 0.8f * mEmaValue;
int filtered = static_cast<int>(std::round(mEmaValue));
```

## Stage 2: Dead Zone

Even after EMA, the filtered value may still oscillate by ±1 count near a stable
resting position. We suppress small changes using a **dead zone**:

```cpp
// Only notify caller if the change is >= 8 ADC counts
if (std::abs(filtered - mLastStable) >= 8) {
    mLastStable = filtered;
    *changed = true;
}
```

With a dead zone of 8 counts out of 1023 total, each of our 16 volume steps covers
~64 ADC counts — meaning the dead zone (8 counts) is 12.5% of one volume step.
This eliminates idle flicker completely while still responding to genuine knob movement.

## Why We Track `mLastStable` Separately from `mEmaValue`

```
Without separate tracking (wrong):
  mEmaValue oscillates: 512.1 → 511.9 → 512.3 → 511.8
  |filtered - mEmaValue| = 0.1 → 0.3 → 0.5 ...
  Never triggers dead zone because we're comparing to ourselves

With separate tracking (correct):
  mLastStable stays at 512 (the last value we emitted)
  When a real movement happens: mEmaValue reaches 590
  |590 - 512| = 78 >> dead zone → emit once, update mLastStable to 590
  Then value settles: oscillates around 590 → |591-590|=1 < dead zone → no more emissions
```

---

<a name="part-6"></a>
# Part 6: Phase 3 — VolumeMapper and VolumeController

## VolumeMapper: Linear Interpolation

Android's `AUDIO_STREAM_MUSIC` has 16 discrete volume steps: index 0 (mute) to
index 15 (maximum). The mapping from 10-bit ADC (0–1023) to volume index (0–15)
is a simple linear interpolation:

```
ratio        = adcValue / 1023.0          → 0.0 to 1.0
volume_index = round(ratio × 15)          → 0 to 15
```

Using `round()` instead of integer division (truncation) makes the mapping symmetric:

```
ADC=0    → ratio=0.000 → index=0   (mute)
ADC=34   → ratio=0.033 → index=1
ADC=102  → ratio=0.100 → index=2  (approximately)
ADC=512  → ratio=0.500 → index=8  (midpoint → midpoint ✓)
ADC=989  → ratio=0.967 → index=14
ADC=1023 → ratio=1.000 → index=15 (max)
```

Each volume step spans approximately 64 ADC counts (1023 / 15 ≈ 68).

## VolumeController: Reaching Android Audio from Vendor Code

This is the most architecturally interesting piece — and the one that required the
biggest design pivot during the build. Our daemon is a vendor process. Setting Android
volume requires communicating with `AudioPolicyService` in the system partition.

### Method 1 (Original Plan): libaudioclient / LLNDK

The first design used `libaudioclient`, an LLNDK library that provides
`AudioSystem::setStreamVolumeIndex()` — the exact call the Settings app uses.

**Advantages of Method 1:**
- Direct API — set volume to index 7 → volume is exactly 7. No intermediate steps.
- No key repeat delay or input event routing
- Clean semantics: "set to absolute position" rather than "step up/down"

**Disadvantages of Method 1:**
- Requires `libaudioclient`, `libutils`, and Binder rules for audioserver in SELinux
- More complex Android.bp and SELinux policy
- Depends on LLNDK availability of libaudioclient

The original `Android.bp` had:
```bp
shared_libs: [
    "libaudioclient",   // LLNDK — AudioSystem::setStreamVolumeIndex()
    "libutils",         // status_t
    "liblog",
],
```

And the original `VolumeController.cpp` used:
```cpp
#include <media/AudioSystem.h>
android::AudioSystem::setStreamVolumeIndex(
    AUDIO_STREAM_MUSIC, volumeIndex, AUDIO_DEVICE_OUT_DEFAULT);
```

### Build Error #1 — libaudioclient Has No Vendor Variant in AOSP 15

When running `m potvolumed`, the build immediately failed:

```
error: vendor/myoem/services/potvolumed/Android.bp:
    dependency "libaudioclient" missing variant:
        "os:android,image:vendor,link:shared"
    available variants:
        "os:android,image:core,link:shared"
        "os:android,image:system,link:shared"
```

**Root cause:** In AOSP 15, the VNDK sunset removed the `image:vendor` build variant
from `libaudioclient`. The library exists only for `image:system`. Even though
`libaudioclient` was historically considered LLNDK, this AOSP 15 build does not
provide a vendor-accessible variant for it.

This is a common AOSP pitfall: documentation or older examples may list a library as
LLNDK, but the *actual build configuration* may not include the vendor variant.
The build error message is definitive — if `image:vendor` is not in the available
variants list, the library cannot be used from vendor code.

**Fix: Switch to Method 2 — Linux uinput virtual input device.**

---

### Method 2 (Implemented): Linux uinput Virtual Keyboard

Instead of calling AudioPolicyService directly, we create a virtual input device
that injects `KEY_VOLUMEUP` and `KEY_VOLUMEDOWN` key events — the same events
produced by physical hardware volume buttons on a phone.

**How the key event reaches Android audio:**
```
VolumeController::setVolume()
    │  write uinput_event{EV_KEY, KEY_VOLUMEUP, 1}
    ▼
/dev/uinput (Linux kernel)
    │  kernel delivers event to InputReader
    ▼
Android InputReader (frameworks/native/services/inputflinger)
    │  routes key event to focused window / system
    ▼
PhoneWindowManager.interceptKeyBeforeDispatching()
    │  KEY_VOLUMEUP → adjustStreamVolume(STREAM_MUSIC, ADJUST_RAISE)
    ▼
AudioManager.adjustStreamVolume()  [Java]
    │  → AudioService → AudioPolicyService
    ▼
Hardware volume changes + UI volume bar updates
```

**Advantages of Method 2:**
- Zero system library dependencies — only `liblog` needed
- Uses the same path as real hardware volume buttons (tested, proven)
- No LLNDK/VNDK issues — `/dev/uinput` is a pure kernel interface
- SELinux policy is simpler (no Binder to audioserver)

**Disadvantages of Method 2:**
- Delta-based rather than absolute: we track the last index and send N key events
- Volume step size is fixed by Android system settings (we can't jump 5 steps with
  one event; we must send 5 separate key events)
- Requires the display/audio focus to handle volume keys normally

### uinput Implementation: Creating a Virtual Keyboard

```cpp
// VolumeController.cpp — the open() method

// 1. Open the uinput device node
mFd = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK);

// 2. Declare the event types we will produce
ioctl(mFd, UI_SET_EVBIT, EV_KEY);   // we produce key events
ioctl(mFd, UI_SET_EVBIT, EV_SYN);   // we produce sync events

// 3. Declare which specific keys we will use
ioctl(mFd, UI_SET_KEYBIT, KEY_VOLUMEUP);
ioctl(mFd, UI_SET_KEYBIT, KEY_VOLUMEDOWN);

// 4. Describe the virtual device to the kernel
struct uinput_setup usetup{};
strncpy(usetup.name, "PotVolume Knob", UINPUT_MAX_NAME_SIZE - 1);
usetup.id.bustype = BUS_USB;
ioctl(mFd, UI_DEV_SETUP, &usetup);

// 5. Create — after this, InputReader sees a new input device
ioctl(mFd, UI_DEV_CREATE);
```

### uinput Implementation: Sending Key Events

```cpp
// VolumeController::setVolume(int volumeIndex)

// On first call, set mLastIndex = current value as reference. No events sent.
if (mLastIndex < 0) {
    mLastIndex = volumeIndex;
    return true;
}

int delta = volumeIndex - mLastIndex;
if (delta == 0) return true;   // nothing to do

int keyCode = (delta > 0) ? KEY_VOLUMEUP : KEY_VOLUMEDOWN;

// Send abs(delta) key press+release pairs
for (int i = 0; i < std::abs(delta); i++) {
    sendKeyEvent(keyCode, 1);   // key down
    sendSyncEvent();
    sendKeyEvent(keyCode, 0);   // key up
    sendSyncEvent();
}

mLastIndex = volumeIndex;
```

Each `sendKeyEvent` writes a `struct input_event` to the uinput fd:

```cpp
struct input_event ev{};
ev.type  = EV_KEY;
ev.code  = static_cast<uint16_t>(keyCode);
ev.value = value;   // 1=down, 0=up
write(mFd, &ev, sizeof(ev));
```

### Delta-Based Volume: Why the First Call Is Special

The first `setVolume()` call after boot establishes the reference. Without a known
starting index, any index would generate a delta from zero to the current ADC position,
producing a burst of key events at startup (possibly changing volume without the user
touching the knob). The `mLastIndex = -1` sentinel prevents this.

### Throttling: Avoid Redundant Key Events

The `AdcFilter` dead zone ensures `setVolume()` is only called when the stable index
changes. But as a second guard, `VolumeController` tracks `mLastIndex` and skips the
loop if `delta == 0`. This handles the boot-reference case cleanly.

---

<a name="part-7"></a>
# Part 7: Phase 4 — main.cpp, RC File, and the Boot Sequence

## main.cpp: The Poll Loop

The main function is deliberately simple. It owns the lifecycle of all components:

```cpp
// NOTE: RPi5 AOSP enumerates the SPI0 hardware controller as bus 10.
// The actual device node is /dev/spidev10.0, NOT /dev/spidev0.0.
// Verified with: adb shell ls /dev/spidev*
static constexpr char kSpiDevice[]    = "/dev/spidev10.0";
static constexpr int  kAdcChannel     = 0;
static constexpr int  kPollIntervalMs = 50;   // 20 Hz

int main() {
    ALOGI("potvolumed starting (spi=%s ch=%d poll=%dms)",
          kSpiDevice, kAdcChannel, kPollIntervalMs);

    signal(SIGTERM, onSignal);   // catch init's "stop" command for clean shutdown
    signal(SIGINT,  onSignal);

    SpiReader        spi(kSpiDevice, kAdcChannel);
    AdcFilter        filter;
    VolumeMapper     mapper;
    VolumeController controller;

    if (!spi.open())        { return 1; }   // fail fast: SPI not accessible
    if (!controller.open()) { return 1; }   // fail fast: uinput not accessible

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
```

Why 50ms poll interval?
- A full knob sweep from min to max takes at least 300–500ms for most people
- 50ms (20Hz) gives ~6–10 samples per sweep — more than enough for smooth response
- No more than 1 key event burst per 50ms cycle — negligible load on InputReader

## The RC File: Service Definition and Device Permission Fixup

```ini
# potvolumed.rc

on boot
    # The kernel creates /dev/spidev10.0 as root:root 0600.
    # Our daemon runs as 'system' user — it needs group write access.
    # init runs as root at boot time, so it can chown/chmod before the service starts.
    #
    # NOTE: RPi5 AOSP enumerates SPI0 as bus 10 → /dev/spidev10.0 (not spidev0.0)
    chown root system /dev/spidev10.0
    chmod 0660 /dev/spidev10.0

service potvolumed /vendor/bin/potvolumed
    class main    # starts after 'core' — InputReader, audioserver already running
    user system   # UID 1000 — needed to open /dev/uinput and reach system services
    group system  # GID 1000 — matches the chown above for /dev/spidev10.0
```

### Why `class main` Matters

Android init starts services in phases:

```
Phase "core":  servicemanager, logd, vold, keymaster
Phase "main":  audioserver, surfaceflinger, mediaserver, potvolumed ← us
Phase "late_start": user apps
```

If we used `class core`, we'd start before `InputReader` and before the Android input
subsystem is ready to receive events. The uinput virtual device would be created but
key events injected into it might be lost.

Using `class main` guarantees that `InputReader` and `AudioService` are both running
before our first key event is injected.

### SIGTERM and Clean Shutdown

When `init` receives `stop potvolumed` (e.g., `adb shell stop potvolumed`), it sends
`SIGTERM` to the process. If we don't handle it, the process is killed mid-ioctl,
which can leave the SPI device in an undefined state.

Our signal handler sets `gRunning = false`. The main loop checks this flag on every
iteration, so the daemon exits cleanly after at most one more 50ms sleep, properly
closing the SPI file descriptor via `SpiReader`'s destructor.

---

<a name="part-8"></a>
# Part 8: SELinux Policy — The Hidden Wall Between Vendor and Audio

## Why SELinux Is Not Optional

Android runs in Enforcing SELinux mode. This means every single system call made by
a process must be explicitly allowed by a policy rule. Without policy:

- `init_daemon_domain()` won't create the domain transition → daemon runs in `init`'s
  domain (wrong) or the exec is denied entirely
- `open("/dev/spidev0.0")` → `avc: denied { open } for ... tclass=chr_file`
- `ioctl(fd, SPI_IOC_MESSAGE, ...)` → `avc: denied { ioctl } for ...`
- Binder call to audioserver → `avc: denied { call } for ... tclass=binder`

Every one of these shows up as an `avc: denied` line in logcat. The approach is:
start with `setenforce 0` (permissive — denials are logged but not enforced), collect
all denials, then write exact policy rules for each.

## The Four Policy Files

### `potvolumed.te` — Type Enforcement Rules (Final Correct State)

> **Note:** This final policy looks simple. It took four separate debugging rounds to
> get here — each wrong assumption produced a different build error or runtime failure.
> See Part 11 for the full debugging story.

```
# ── Type declarations ─────────────────────────────────────────────────────────

type potvolumed,      domain;
type potvolumed_exec, exec_type, vendor_file_type, file_type;

# spidev_device: our own type for the SPI character device.
# AOSP 15 base policy does NOT define spidev_device — we define it here.
# The matching file_contexts entry labels /dev/spidev* with this type at boot.
type spidev_device, dev_type;

# ── Domain transition ─────────────────────────────────────────────────────────
# Allows init to exec /vendor/bin/potvolumed and transition into potvolumed domain.
init_daemon_domain(potvolumed)

# ── SPI device access ─────────────────────────────────────────────────────────
# /dev/spidev10.0 — SPI character device for MCP3008 ADC communication.
# Labeled spidev_device via our file_contexts entry.
allow potvolumed spidev_device:chr_file { open read write ioctl };

# ── uinput device access ──────────────────────────────────────────────────────
# /dev/uinput — verified label on RPi5 AOSP 15: uhid_device (NOT uinput_device).
# AOSP 15 labels both /dev/uhid and /dev/uinput with the uhid_device type.
allow potvolumed uhid_device:chr_file { open read write ioctl };

# ── Logging ───────────────────────────────────────────────────────────────────
# ALOGI/ALOGE send log records to logd via Unix datagram socket.
# /dev/log* does NOT exist on AOSP 15 — all logging is socket-based.
allow potvolumed logd:unix_dgram_socket sendto;
```

**What was removed and why** (see Part 11 for full details):

| Removed rule | Reason |
|---|---|
| `binder_call(potvolumed, audioserver)` | Switched from libaudioclient to uinput — no Binder calls |
| `allow potvolumed audioserver_service:service_manager find` | Same — no Binder |
| `get_prop(potvolumed, audio_prop)` | No longer needed without libaudioclient |
| `allow potvolumed uinput_device:chr_file ...` | `uinput_device` type does not exist in AOSP 15 |
| `allow potvolumed log_device:chr_file ...` | `log_device` type does not exist in AOSP 15 |

### `file_contexts` — Binary and Device Labels

```
# Label the daemon executable so init_daemon_domain() works
/vendor/bin/potvolumed              u:object_r:potvolumed_exec:s0

# Label all spidev device nodes with our custom type
/dev/spidev[0-9]+\.[0-9]+          u:object_r:spidev_device:s0
```

Without the binary entry, `/vendor/bin/potvolumed` gets the default `vendor_file` label.
`init_daemon_domain()` requires the `potvolumed_exec` type. If the label is wrong, init's
`selabel_lookup()` at RC parse time silently drops the entire service definition — the
service never appears in `getprop init.svc.*` at all (see Part 11, Error #2).

The `/dev/spidev*` entry creates the `spidev_device` label we defined in `.te`. Without
it, the device node gets the generic `device` label at runtime and our allow rule would
not match.

### `service_contexts` — Empty

`potvolumed` does not register any Binder service. No entry needed, but the file must
exist or the build system may warn.

## The Three SELinux Types That Don't Exist in AOSP 15

This is the most common surprise when writing vendor policy on AOSP 15. Three types
that appear in online examples, older AOSP versions, or seem logical do not exist:

| Assumed type | Real situation | Fix |
|---|---|---|
| `spidev_device` | Not in AOSP 15 base policy | Define `type spidev_device, dev_type;` ourselves |
| `uinput_device` | Not in AOSP 15 base policy | `/dev/uinput` is actually labeled `uhid_device` |
| `log_device` | `/dev/log*` does not exist | AOSP 15 logging is all via `logd` socket |

**Always verify labels on the actual device before writing policy:**

```bash
adb shell ls -laZ /dev/spidev*
# u:object_r:device:s0   ← generic, not spidev_device

adb shell ls -laZ /dev/uinput /dev/uhid
# u:object_r:uhid_device:s0   ← BOTH get this label

adb shell ls -laZ /dev/log*
# No such file              ← /dev/log* does not exist in AOSP 15
```

## Development Workflow for SELinux

```bash
# Step 1: Build and push with permissive mode
adb root
adb shell setenforce 0

# Step 2: Start daemon and watch logs
adb shell start potvolumed
adb logcat | grep -E "avc: denied|potvolumed"

# Step 3: Each "avc: denied" line tells you exactly what to add
# Example: avc: denied { ioctl } for name="spidev0.0" dev="tmpfs"
#          scontext=u:r:potvolumed:s0 tcontext=u:object_r:spidev_device:s0
#          tclass=chr_file permissive=0
# → add: allow potvolumed spidev_device:chr_file ioctl;

# Step 4: After adding all rules, rebuild and test with enforcing
adb shell setenforce 1
adb shell stop potvolumed && adb shell start potvolumed
adb logcat | grep "avc: denied"  # should be empty
```

---

<a name="part-9"></a>
# Part 9: Build System — Android.bp and myoem_base.mk

## Android.bp

```bp
soong_namespace {}   // required for every directory under vendor/myoem/

cc_binary {
    name: "potvolumed",
    vendor: true,               // installs to /vendor/bin/
    init_rc: ["potvolumed.rc"], // packaged alongside the binary

    srcs: [
        "src/main.cpp",
        "src/SpiReader.cpp",
        "src/AdcFilter.cpp",
        "src/VolumeMapper.cpp",
        "src/VolumeController.cpp",
    ],

    local_include_dirs: ["src"],  // so files can #include "SpiReader.h" directly

    shared_libs: [
        // liblog: ALOGI, ALOGE macros → logcat
        // This is the ONLY non-kernel shared library we need.
        // All other functionality uses Linux kernel headers only:
        //   <linux/spi/spidev.h>  for SPI
        //   <linux/uinput.h> + <linux/input.h>  for uinput
        "liblog",
    ],

    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

> **Why the dependency list is so short:** The original plan included `libaudioclient`
> and `libutils`. Both were removed after Build Error #1 (see Part 11) when we switched
> from the Binder-based approach to the uinput approach. The uinput interface is a pure
> Linux kernel ABI — it requires only kernel headers, not any Android shared library.
> This makes the binary maximally portable and free of VNDK/LLNDK issues entirely.

### Key Decision: No AIDL Block, No `libbinder_ndk`

The `thermalcontrold` Android.bp has an `aidl_interface` block and uses
`libbinder_ndk`. `potvolumed` has neither. Why?

- No `aidl_interface`: we are not exposing a service. No interface to define.
- No `libbinder_ndk`: we are not implementing a Binder service. We inject uinput
  events into the kernel — no Binder is involved at all.

The rule from CLAUDE.md — "always use `libbinder_ndk` in vendor services" — applies
to services that **expose** a Binder interface. A daemon that is a pure hardware input
bridge needs neither.

## myoem_base.mk Changes

Three additions to `vendor/myoem/common/myoem_base.mk`:

```makefile
# 1. Register the Soong namespace
PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/potvolumed

# 2. Include the binary in the product image
PRODUCT_PACKAGES += potvolumed

# 3. Include the SELinux policy directory
PRODUCT_PRIVATE_SEPOLICY_DIRS += vendor/myoem/services/potvolumed/sepolicy/private
```

Every new module under `vendor/myoem/` requires all three. Missing any one causes:
- Missing namespace → `Android.bp` module not found during build
- Missing PACKAGES → binary not included in the final image
- Missing SEPOLICY → SELinux types undefined → policy compilation error

---

<a name="part-10"></a>
# Part 10: Build, Push, and First Boot

## Step 1: Verify SPI Is Enabled

Before building, confirm the SPI device node exists. On RPi5 AOSP, SPI may need
to be enabled in the boot configuration:

```bash
# Check if spidev node exists
adb shell ls -la /dev/spidev*

# If nothing is returned, SPI is disabled.
# On RPi5, check /boot/config.txt or the equivalent in your device tree:
# Look for: dtparam=spi=on
# If missing, add it and reboot.
```

## Step 2: Build

```bash
cd /home/arun/aosp-rpi
source build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug

# Build only the daemon (fast)
m potvolumed

# Or rebuild the entire image
m
```

## Step 3: Dev Iteration Without Reflashing

For faster iteration during development, push the binary directly without rebuilding
the full image.

> **Important — correct output path:** The product output directory is named after
> `PRODUCT_DEVICE`, not `PRODUCT_NAME`. For this build: `PRODUCT_DEVICE := rpi5`,
> so the output is in `out/target/product/rpi5/`, NOT `out/target/product/myoem_rpi5/`.
> This was a real mistake made during development (see Part 11, Dev Iteration section).

```bash
# 1. Remount vendor partition read-write
adb root
adb shell mount -o remount,rw /vendor

# 2. Push the newly built binary
# NOTE: output dir is rpi5/, not myoem_rpi5/
adb push out/target/product/rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed

# 3. Fix permissions (adb push preserves host permissions, not Android ones)
adb shell chmod 755 /vendor/bin/potvolumed

# 4. Push SELinux policy files (CRITICAL — must do this or service won't start)
# These are separate from the binary and require m selinux_policy
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts \
         /vendor/etc/selinux/vendor_file_contexts
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil \
         /vendor/etc/selinux/vendor_sepolicy.cil
adb push out/target/product/rpi5/vendor/etc/selinux/precompiled_sepolicy \
         /vendor/etc/selinux/precompiled_sepolicy

# 5. Restart the daemon
adb shell stop potvolumed
adb shell start potvolumed

# 6. Watch logs
adb logcat -s potvolumed
```

## Step 4: Verify the Daemon Started

```bash
# Is the process running?
adb shell ps -eZ | grep potvolumed
# Expected: u:r:potvolumed:s0   system  ... /vendor/bin/potvolumed

# What does init think?
adb shell getprop init.svc.potvolumed
# Expected: "running"
# If empty string: service not parsed — SELinux policy files not pushed (see Part 11 Error #2)
# If "stopped": daemon exited — check logcat for ALOGE messages

# Check the binary's SELinux label
adb shell ls -laZ /vendor/bin/potvolumed
# Expected: u:object_r:potvolumed_exec:s0
# If "vendor_file": SELinux policy not pushed → init silently ignores the service

# Watch logs for startup message
adb logcat -s potvolumed
# Expected first lines:
#   potvolumed: potvolumed starting (spi=/dev/spidev10.0 ch=0 poll=50ms)
#   potvolumed: SPI and uinput ready. Entering poll loop.
```

## Step 5: Test Volume Control

```bash
# Before turning knob: check current volume
adb shell dumpsys audio | grep -A3 "STREAM_MUSIC"

# Turn knob slowly and watch logs
adb logcat -s potvolumed
# Expected when you turn:
#   potvolumed: AdcFilter: raw=680 ema=623.4 stable=512→620 (Δ=108)
#   potvolumed: VolumeMapper: adc=620 → ratio=0.606 → index=9/15
#   potvolumed: Volume: index 7 → 9 / 15 (STREAM_MUSIC)

# Verify volume changed in Android
adb shell dumpsys audio | grep -A3 "STREAM_MUSIC"
```

---

<a name="part-11"></a>
# Part 11: Real Errors Encountered — A Complete Debugging Journal

This section documents every real error hit during the build and bring-up of
`potvolumed`, in the order they were encountered. Each entry includes the exact
error message, the root cause, the diagnosis steps, and the fix applied.

---

## Error #1 — Build Failure: libaudioclient Has No Vendor Variant

**When:** First `m potvolumed` run after wiring up the initial `Android.bp` with
`libaudioclient` and `libutils`.

**Error message:**
```
error: vendor/myoem/services/potvolumed/Android.bp:
    dependency "libaudioclient" missing variant:
        "os:android,image:vendor,link:shared"
    available variants:
        "os:android,image:core,link:shared"
        "os:android,image:system,link:shared"
```

**Root cause:** In AOSP 15, the VNDK sunset removed the `image:vendor` build variant
from `libaudioclient`. The library exists only as a system image library. Even though
older AOSP versions or documentation may list it as LLNDK-accessible from vendor code,
AOSP 15 simply does not build a vendor-accessible variant.

The error is a Soong dependency resolver failure. It means: "you asked for
`libaudioclient` with the vendor image flag, but only system image variants exist."

**Diagnosis:**
```bash
# The error message itself is definitive — no additional diag needed.
# If uncertain, check what image variants a library has:
grep -r "libaudioclient" build/make/core/vndk/ 2>/dev/null
# or check Android.bp of frameworks/av/media/libaudioclient/Android.bp
# and look for: llndk: { symbol_file: ... } or vendor_available: true
```

**Fix:** Replaced `libaudioclient` with the Linux uinput virtual input device approach.
`VolumeController.cpp` was rewritten completely. `Android.bp` simplified to:
```bp
shared_libs: ["liblog"],  // only library needed — everything else is kernel headers
```
The SELinux policy was also simplified — no more Binder rules for audioserver.

**Lesson:** Never assume a library is vendor-accessible just because it's "system" or
"LLNDK". The Soong build error `missing variant: image:vendor` is definitive. When
you hit it, you have three options: (1) find a vendor-accessible alternative, (2) use
a pure kernel ABI instead, (3) write a HIDL/AIDL HAL. For uinput, option 2 was perfect.

---

## Error #2 — Runtime: Service Not Parsed by Init (getprop Returns Empty)

**When:** After first successful binary build and push to the device.

**Symptoms:**
```bash
adb shell getprop init.svc.potvolumed
# (empty — no output at all)

adb shell start potvolumed
# Unable to start service 'potvolumed': No such file or directory

adb shell ps -eZ | grep potvolumed
# (nothing — process never started)
```

The binary file was confirmed present: `adb shell ls /vendor/bin/potvolumed` worked.
The RC file was confirmed present: `adb shell ls /vendor/etc/init/potvolumed.rc` worked.
Even after `adb reboot`, the service still didn't appear in `getprop` output.

**Root cause:** Two separate targets, not one.

Running `m potvolumed` builds the binary and RC file, but does **NOT** rebuild the
SELinux policy files (`vendor_file_contexts`, `vendor_sepolicy.cil`, `precompiled_sepolicy`).

Android init parses `.rc` files at boot and calls `selabel_lookup()` for each service's
binary path to verify its SELinux label. If the binary is not in the compiled
`vendor_file_contexts`, the path returns the generic `vendor_file` type — but
`init_daemon_domain()` requires the binary to have `potvolumed_exec`.

When init sees a binary that doesn't have the expected exec type, it **silently drops
the entire service definition** from its service table. There is no error in logcat.
The service simply never exists.

**Diagnosis:**
```bash
# Check the binary's actual SELinux label
adb shell ls -laZ /vendor/bin/potvolumed
# Result: u:object_r:vendor_file:s0   ← WRONG — should be potvolumed_exec:s0
# This immediately tells you: file_contexts was not rebuilt/pushed
```

**Fix:**
```bash
# Step 1: Build SELinux policy (separate from the binary)
m selinux_policy

# Step 2: Push the three SELinux files
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts \
         /vendor/etc/selinux/vendor_file_contexts
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil \
         /vendor/etc/selinux/vendor_sepolicy.cil
adb push out/target/product/rpi5/vendor/etc/selinux/precompiled_sepolicy \
         /vendor/etc/selinux/precompiled_sepolicy

# Step 3: Reboot (init must re-read the new policy and re-parse .rc files)
adb reboot

# Step 4: Verify
adb shell ls -laZ /vendor/bin/potvolumed
# Now: u:object_r:potvolumed_exec:s0  ← correct

adb shell getprop init.svc.potvolumed
# Now: "running" or "stopped" (not empty!)
```

**Lesson:** `m potvolumed` ≠ `m selinux_policy`. These are two separate build targets
that produce different output files. For any new vendor service, you must run both
and push both. The failure mode (silent service disappearance) is particularly
confusing because there is no error anywhere — init just quietly drops the service.

---

## Error #3 — Build Failure: Unknown Type `spidev_device`

**When:** Running `m selinux_policy` for the first time after adding the initial
`potvolumed.te` file.

**Error message:**
```
out/host/linux-x86/bin/checkpolicy: error(s) encountered while parsing configuration
vendor/myoem/services/potvolumed/sepolicy/private/potvolumed.te:33:ERROR
'unknown type spidev_device' at token ';'
allow potvolumed spidev_device:chr_file { open read write ioctl };
```

**Root cause:** `spidev_device` is not defined in AOSP 15's base policy. Some AOSP
repositories (older versions, device-specific policy) define it. But the AOSP 15
base policy for RPi5 does not. The SELinux compiler (`checkpolicy`) fails because
we're referencing an undefined type.

**Diagnosis:**
```bash
# Check what type /dev/spidev* actually has on the running device
adb shell ls -laZ /dev/spidev*
# Result: /dev/spidev10.0: u:object_r:device:s0
# The kernel assigned the generic "device" type, not "spidev_device"

# Search AOSP base policy for spidev_device
grep -r "spidev_device" system/sepolicy/
# (no results — it doesn't exist in base policy)
```

**Fix:** Define the type ourselves in `potvolumed.te`, and add a `file_contexts` entry
to label the device nodes with this type at boot:

In `potvolumed.te`, add before the allow rule:
```
type spidev_device, dev_type;
```

In `sepolicy/private/file_contexts`, add:
```
/dev/spidev[0-9]+\.[0-9]+    u:object_r:spidev_device:s0
```

This pattern — define a missing type yourself + label devices via file_contexts —
is the standard AOSP approach for any device node that doesn't have a vendor-specific
type in the base policy.

**Lesson:** Don't assume SELinux types exist. Always grep the base policy first.
AOSP 15 base policy has fewer device-specific types than older versions due to
the reduction of board-specific code in base policy. When a type is missing, the
correct fix is to define it yourself rather than using a broader existing type
(using `device` directly would grant less precise policy control).

---

## Error #4 — Wrong SPI Device Path

**When:** After all build errors were fixed and the daemon started for the first
time, the service immediately stopped with `SPI device cannot open` in logcat.

**Symptom:**
```bash
adb logcat -s potvolumed
# potvolumed: Cannot open SPI device /dev/spidev0.0 — daemon cannot start

adb shell getprop init.svc.potvolumed
# stopped
```

**Root cause:** `main.cpp` was hardcoded with `/dev/spidev0.0`, which seemed natural
for "SPI bus 0, chip select 0". But on this RPi5 AOSP 15 build, the kernel's
`spi-bcm2835` driver registers `spi_master` at index 10 rather than 0.

This is a kernel enumeration artifact. The hardware SPI0 controller maps to
`spi_master` index 10 on this specific build's bus numbering. There is no documentation
for this — it can only be discovered empirically.

**Diagnosis:**
```bash
adb shell ls /dev/spidev*
# /dev/spidev10.0   ← major 153, minor 0
# Not /dev/spidev0.0 — that file doesn't exist at all
```

One command, definitive answer. This is why you always run `ls /dev/spidev*` on the
actual device before hardcoding any path.

**Fix:** Changed three files:

1. `src/main.cpp`:
```cpp
// BEFORE:
static constexpr char kSpiDevice[] = "/dev/spidev0.0";
// AFTER:
static constexpr char kSpiDevice[] = "/dev/spidev10.0";
```

2. `potvolumed.rc` (chown/chmod on boot):
```ini
# BEFORE:
chown root system /dev/spidev0.0
chmod 0660 /dev/spidev0.0
# AFTER:
chown root system /dev/spidev10.0
chmod 0660 /dev/spidev10.0
```

3. The comment in `file_contexts` (path pattern covers both — regex already correct).

**Lesson:** Hardware enumeration in AOSP does not follow expected numbering. SPI bus 0
became bus 10. The fix is always to verify with `adb shell ls /dev/spidev*` before
writing code. Same applies to I2C (`/dev/i2c-*`), UART (`/dev/ttyAMA*`), and any other
peripheral — the kernel-assigned index may not match the hardware schematic number.

---

## Additional Discovery: Incorrect SELinux Types for uinput and Logging

While debugging Error #3, we also audited all other types used in `potvolumed.te`
by checking the actual device labels:

```bash
adb shell ls -laZ /dev/uinput /dev/uhid
# /dev/uinput: u:object_r:uhid_device:s0
# /dev/uhid:   u:object_r:uhid_device:s0

adb shell ls -laZ /dev/log*
# ls: /dev/log*: No such file or directory
```

**Finding 1 — `/dev/uinput` is labeled `uhid_device`, not `uinput_device`:**

The initial `.te` had `allow potvolumed uinput_device:chr_file ...`. This type does
not exist in AOSP 15. The actual type for `/dev/uinput` is `uhid_device` (the same
type used for `/dev/uhid`, the Human Interface Device userspace driver). The fix:
```
# BEFORE (wrong type — doesn't exist):
allow potvolumed uinput_device:chr_file { open read write ioctl };
# AFTER (real label on this device):
allow potvolumed uhid_device:chr_file { open read write ioctl };
```

**Finding 2 — `/dev/log*` does not exist in AOSP 15:**

Older Android versions used `/dev/log/main`, `/dev/log/radio`, etc. as character
devices for logging. AOSP 15 removed these completely — all logging goes through
the `logd` Unix datagram socket. The fix: remove the `log_device` rule and keep
only the socket rule:
```
# REMOVE (type doesn't exist, file doesn't exist):
allow potvolumed log_device:chr_file { open read write };
# KEEP (correct for AOSP 15):
allow potvolumed logd:unix_dgram_socket sendto;
```

---

## Dev Iteration Mistake: Wrong Push Path

**When:** Pushing the binary for the first time after build.

**Mistake:**
```bash
# Attempted (wrong product name in path):
adb push out/target/product/myoem_rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed
# adb: error: cannot stat 'out/target/product/myoem_rpi5/vendor/bin/potvolumed':
#              No such file or directory
```

**Root cause:** The output directory is named after `PRODUCT_DEVICE`, not
`PRODUCT_NAME`. In `products/myoem_rpi5.mk`:
```makefile
# PRODUCT_DEVICE controls the output directory name:
PRODUCT_DEVICE := rpi5
# PRODUCT_NAME is the lunch target prefix:
PRODUCT_NAME := myoem_rpi5
```

So the correct path is `out/target/product/rpi5/`, not `out/target/product/myoem_rpi5/`.

**Fix:**
```bash
adb push out/target/product/rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed
```

---

## General Debugging Checklist (Hardware Layers)

**Problem**: `/dev/spidev*` does not exist

```bash
adb shell ls /dev/spidev*
# No such file
```

Cause: SPI is not enabled in the RPi5 device tree or `config.txt`.

Diagnosis:
```bash
# Check if spidev module is loaded
adb shell lsmod | grep spi

# Check kernel boot log for SPI messages
adb logcat -b kernel | grep -i spi
```

Fix: On RPi5, SPI is enabled via `config.txt` or the device tree overlay. Add
`dtparam=spi=on` to the boot config and reflash. This is a device-level change
outside of `vendor/myoem/`.

---

**Problem**: SPI reads return all zeros or garbage

```bash
# In potvolumed logs:
potvolumed: SPI raw: tx=[01 80 00] rx=[00 00 00] value=0
```

Causes:
1. MISO wire not connected — MCP3008 can't send data back
2. VDD/VREF not connected — MCP3008 unpowered
3. Wrong SPI mode — clock polarity/phase mismatch

Diagnosis: Verify wiring step by step against the diagram in Part 2.

---

## Layer 2: Linux Permissions

**Problem**: `open(/dev/spidev0.0) failed: Permission denied`

```bash
adb logcat -s potvolumed
# potvolumed: open(/dev/spidev0.0) failed: Permission denied
```

Cause: The `on boot` chown/chmod in `potvolumed.rc` didn't run, or ran after the
service started.

Diagnosis:
```bash
# Check current permissions
adb shell ls -la /dev/spidev0.0
# If still root:root 0600: the on boot block didn't execute

# Check RC file was installed
adb shell ls /vendor/etc/init/potvolumed.rc
```

Fix: Ensure the RC file is packaged. Also verify the `on boot` block runs before
`class main` starts (it does by design — `on boot` triggers before service classes
are started).

---

## Layer 3: SELinux

**Problem**: Daemon starts but SPI reads silently fail

```bash
adb logcat | grep "avc: denied"
# avc: denied { open } for pid=1234 comm="potvolumed"
#   name="spidev0.0" dev="tmpfs" ino=...
#   scontext=u:r:potvolumed:s0 tcontext=u:object_r:spidev_device:s0
#   tclass=chr_file permissive=0
```

Cause: SELinux policy missing for the SPI device.

Fix:
```bash
# Step 1: Go permissive to collect all denials at once
adb root && adb shell setenforce 0

# Step 2: Run daemon and collect all denials
adb shell stop potvolumed && adb shell start potvolumed
adb logcat | grep "avc: denied" > /tmp/denials.txt

# Step 3: For each denial, add the corresponding allow rule to potvolumed.te

# Step 4: Rebuild and test in enforcing mode
m potvolumed
adb push ... (dev iteration steps)
adb shell setenforce 1
```

**Common denials and their fixes on AOSP 15:**

| Denial | Fix in potvolumed.te |
|--------|---------------------|
| `{ open } tclass=chr_file tcontext=spidev_device` | `allow potvolumed spidev_device:chr_file { open read write ioctl };` |
| `{ open } tclass=chr_file tcontext=uhid_device` | `allow potvolumed uhid_device:chr_file { open read write ioctl };` |
| `{ sendto } tclass=unix_dgram_socket tcontext=logd` | `allow potvolumed logd:unix_dgram_socket sendto;` |

> **Note on AOSP 15:** The `uinput_device` and `log_device` types do not exist.
> The `binder_call(potvolumed, audioserver)` rules are not needed since we use
> uinput instead of libaudioclient. See Error #3 and the type audit above.

---

<a name="part-12"></a>
# Part 12: Lessons Learned

These lessons are drawn directly from the real errors encountered during development,
not from theoretical knowledge. Each one cost real debugging time.

## 1. VNDK Sunset in AOSP 15 Blocks Many Assumed Libraries

The original plan used `libaudioclient`. It was considered LLNDK in earlier AOSP
versions. AOSP 15 removed the `image:vendor` build variant — the Soong error
`missing variant: os:android,image:vendor` is the tell.

**Rule:** When building vendor code in AOSP 15, run `m <module>` with the initial
dependency list first, before writing any logic. If a library fails immediately with
"missing variant", find the alternative before investing in the implementation. The
earlier you discover this, the less code you throw away.

For audio specifically: **uinput is the correct vendor approach** in AOSP 15. It
uses only kernel ABIs, requires zero VNDK/LLNDK dependencies, and follows the same
path real hardware volume buttons use.

## 2. `m potvolumed` and `m selinux_policy` Are Two Separate Operations

Building the binary does not rebuild SELinux policy. They are separate Soong targets
that produce different output files in different directories. When iterating on a new
vendor service, the correct workflow is:

```bash
# For binary changes only:
m potvolumed && push binary

# For SELinux changes only:
m selinux_policy && push selinux files

# For new service (first bring-up):
m potvolumed selinux_policy && push both
```

The failure mode when you forget `selinux_policy` is maximally confusing: the binary
is present on device, the RC file is present, but `getprop init.svc.<name>` returns
empty. No error in logcat. Init silently drops the service.

## 3. Never Assume SELinux Type Existence — Always Verify on Device

Three types that seemed natural were either wrong or nonexistent in AOSP 15:

| Expected type | Reality |
|---|---|
| `spidev_device` | Does not exist in AOSP 15 base policy |
| `uinput_device` | Does not exist; `/dev/uinput` labeled `uhid_device` |
| `log_device` | `/dev/log*` files do not exist in AOSP 15 |

The verification command is quick and definitive:
```bash
adb shell ls -laZ /dev/<device_name>
```
Run this for every device node before writing any SELinux `allow` rules.
An allow rule for a nonexistent type is a build error (`unknown type`).
An allow rule for the wrong type is a runtime denial (or worse, silent failure).

## 4. Verify Hardware Device Paths on the Real Device Before Hardcoding

`/dev/spidev0.0` was the assumed path. The real path was `/dev/spidev10.0`. This was
not documented anywhere — it was discovered only by running `adb shell ls /dev/spidev*`
on the booted device.

Kernel bus numbering in AOSP does not necessarily match hardware schematic numbering.
The `spi-bcm2835` driver chose bus index 10 for SPI0 on this build. Nothing in the
product's documentation mentioned this.

**Rule:** Before hardcoding any `/dev/` path, run `ls /dev/` on the real device.
This applies equally to I2C (`/dev/i2c-*`), UART (`/dev/ttyAMA*`), GPIO, and others.

## 5. Output Directory Is `PRODUCT_DEVICE`, Not `PRODUCT_NAME`

The lunch target is `myoem_rpi5-trunk_staging-userdebug`, so `PRODUCT_NAME=myoem_rpi5`.
But the build output directory is controlled by `PRODUCT_DEVICE=rpi5`. The correct
push path is `out/target/product/rpi5/`, not `out/target/product/myoem_rpi5/`.

This is a consistent rule across all AOSP products: build output directory = `PRODUCT_DEVICE`.

## 6. Not Every Vendor Module Needs a Binder Service

After building ThermalControl (Binder service + AIDL) and SafeMode (Binder service +
AIDL + Java manager), the instinct is to reach for Binder for every new module.
`potvolumed` demonstrates the opposite: a pure hardware input that maps to a system
input event is cleanest as a poll daemon with no service interface.

**Design principle:** Add a Binder service only when other processes need to connect to
you. A one-way pipe (hardware → kernel → Android input subsystem) has no callers and
needs no interface.

## 7. The RC File `on boot` Is the Right Place for Device Permission Setup

The kernel creates `/dev/spidev10.0` as `root:root 0600`. Using `on boot` in the RC
file to do `chown/chmod` before the service starts is the correct AOSP pattern —
identical to how `thermalcontrold.rc` handles hwmon permissions. Init runs as root
during boot, the chown runs before `class main` starts, and the pattern is idiomatic.

## 8. ADC Noise Is Always Worse Than Expected on a Breadboard

A 10kΩ potentiometer on a clean 3.3V rail should theoretically give steady readings.
In practice, the switching noise from the RPi5's PMIC, USB-C power circuit, and
breadboard wire inductance causes ±5 count oscillations continuously. The two-stage
filter (EMA α=0.2 + dead zone ±8) is the minimum for stable behavior.

For hardware testing, if readings oscillate more than expected, a 100nF capacitor
between CH0 and GND on the MCP3008, as close to the IC as possible, will help
significantly. This is the standard decoupling technique for ADC input signals.

---

## Reference: Quick Command Cheatsheet

```bash
# ── Hardware — Verify BEFORE hardcoding any paths ──────────────────
adb shell ls /dev/spidev*               # find actual SPI device node (might be spidev10.0!)
adb shell ls -laZ /dev/spidev*          # check SELinux label — use this in .te
adb shell ls -laZ /dev/uinput           # verify label is uhid_device, not uinput_device

# ── Build ─────────────────────────────────────────────────────────
m potvolumed                            # build binary + RC file only
m selinux_policy                        # build SELinux policy files (separate target!)
# For first bring-up, run BOTH before pushing

# ── Deploy (output dir is rpi5/, not myoem_rpi5/) ─────────────────
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed
adb shell chmod 755 /vendor/bin/potvolumed
# Also push SELinux files after m selinux_policy:
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts \
         /vendor/etc/selinux/vendor_file_contexts
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil \
         /vendor/etc/selinux/vendor_sepolicy.cil
adb push out/target/product/rpi5/vendor/etc/selinux/precompiled_sepolicy \
         /vendor/etc/selinux/precompiled_sepolicy

# ── Service Control ────────────────────────────────────────────────
adb shell stop potvolumed
adb shell start potvolumed
adb shell getprop init.svc.potvolumed   # "running" / "stopped" / empty (SELinux not pushed)
adb shell ls -laZ /vendor/bin/potvolumed  # must be potvolumed_exec:s0, not vendor_file:s0

# ── Logs ──────────────────────────────────────────────────────────
adb logcat -s potvolumed                # daemon output only
adb logcat | grep "avc: denied"         # SELinux denials
adb logcat -b kernel | grep -i spi      # kernel SPI driver messages

# ── SELinux ───────────────────────────────────────────────────────
adb root && adb shell setenforce 0      # permissive mode (dev only — collect all denials)
adb shell getenforce                    # Enforcing / Permissive

# ── Volume Verification ────────────────────────────────────────────
adb shell dumpsys audio | grep -A5 "STREAM_MUSIC"
# uinput key events can be tested manually:
# adb shell sendevent /dev/uinput 1 115 1  # EV_KEY KEY_VOLUMEUP down
# adb shell sendevent /dev/uinput 0 0 0    # EV_SYN
# adb shell sendevent /dev/uinput 1 115 0  # EV_KEY KEY_VOLUMEUP up
# adb shell sendevent /dev/uinput 0 0 0    # EV_SYN
```

---

# Part 13 — The SELinux Build System Debugging Session

This part documents a real debugging session that occurred after the daemon was
building and running (Parts 1–12 complete). The service appeared to start during
development iteration, but when the SELinux policy was rebuilt and pushed to the
device, the binary kept the wrong label and init refused to start the daemon. This
section traces the full root-cause chain and the fixes.

---

## Error #5 — PRODUCT_PRIVATE_SEPOLICY_DIRS Is Wrong for Vendor Services

**When:** After building `m selinux_policy` and pushing the policy files, the binary
still had the wrong label `vendor_file:s0` instead of `potvolumed_exec:s0`, and
`init` refused to start the service.

**Symptom:**
```bash
adb shell ls -laZ /vendor/bin/potvolumed
# u:object_r:vendor_file:s0   ← WRONG (should be potvolumed_exec:s0)

adb shell getprop init.svc.potvolumed
# (empty — init doesn't know the service exists)

adb logcat | grep potvolumed
# init: starting service 'potvolumed' failed — incorrect label
# (or: no domain transition from u:r:init:s0)
```

**Diagnosis — check if the policy was even built:**
```bash
# On the device — is our file_contexts entry there?
adb shell grep potvolumed /vendor/etc/selinux/vendor_file_contexts
# (no output — entry is missing)

# In the build output — is it there after m selinux_policy?
grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts
# (no output — our policy never made it in)
```

The file_contexts entry was absent from both the device and the build output.
This meant the issue was in the BUILD SYSTEM configuration, not in the `.te` file.

**Root cause — the wrong make variable:**

`myoem_base.mk` was using `PRODUCT_PRIVATE_SEPOLICY_DIRS`:
```makefile
# WRONG — this feeds product_sepolicy.cil (product partition), not vendor:
PRODUCT_PRIVATE_SEPOLICY_DIRS += \
    vendor/myoem/services/potvolumed/sepolicy/private
```

In AOSP 15, the SELinux build pipeline has two separate paths:

| Make variable | Feeds | Used for |
|---|---|---|
| `PRODUCT_PRIVATE_SEPOLICY_DIRS` | `product_sepolicy.cil` | `/product/` partition |
| `BOARD_VENDOR_SEPOLICY_DIRS` | `vendor_sepolicy.cil` | `/vendor/` partition |

`vendor_sepolicy.cil` and `vendor_file_contexts` are assembled from sources tagged
`.vendor` in Soong's build file system. That tag is populated only from
`BoardVendorSepolicyDirs` (set by `BOARD_VENDOR_SEPOLICY_DIRS`). Using
`PRODUCT_PRIVATE_SEPOLICY_DIRS` means the policy goes into the product partition —
completely ignored when building vendor SELinux policy.

**How to confirm this in AOSP source:**

In `system/sepolicy/Android.bp`, the `vendor_sepolicy.conf` target includes:
```
se_build_files { .plat_vendor, .vendor }
```
In `system/sepolicy/build/soong/build_files.go`:
```go
// Line ~91:
b.srcs[".product_private"] = b.findSrcsInDirs(ctx, ctx.Config().ProductPrivateSepolicyDirs()...)
b.srcs[".vendor"]          = b.findSrcsInDirs(ctx, ctx.DeviceConfig().VendorSepolicyDirs()...)
```
`.product_private` feeds `product_sepolicy.conf`. `.vendor` feeds `vendor_sepolicy.conf`.
They are completely separate compilation units.

**Fix — change ALL vendor service sepolicy to BOARD_VENDOR_SEPOLICY_DIRS:**
```makefile
# vendor/myoem/common/myoem_base.mk

# BEFORE (wrong for vendor services):
PRODUCT_PRIVATE_SEPOLICY_DIRS += \
    vendor/myoem/services/calculator/sepolicy/private \
    vendor/myoem/services/bmi/sepolicy/private \
    vendor/myoem/services/thermalcontrol/sepolicy/private \
    vendor/myoem/services/safemode/sepolicy/private \
    vendor/myoem/services/potvolumed/sepolicy/private

# AFTER (correct — feeds vendor_sepolicy.cil and vendor_file_contexts):
# Vendor services must use BOARD_VENDOR_SEPOLICY_DIRS, not PRODUCT_PRIVATE_SEPOLICY_DIRS.
# vendor_sepolicy.cil and vendor_file_contexts only pull from BoardVendorSepolicyDirs (.vendor tag).
# PRODUCT_PRIVATE_SEPOLICY_DIRS feeds product_sepolicy.cil (product partition) — wrong for /vendor/bin/* services.
BOARD_VENDOR_SEPOLICY_DIRS += \
    vendor/myoem/services/calculator/sepolicy/private \
    vendor/myoem/services/bmi/sepolicy/private \
    vendor/myoem/services/thermalcontrol/sepolicy/private \
    vendor/myoem/services/safemode/sepolicy/private \
    vendor/myoem/services/potvolumed/sepolicy/private
```

**Verification after fix:**
```bash
grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts
# /vendor/bin/potvolumed    u:object_r:potvolumed_exec:s0   ← now present

grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil
# (17 entries — type declarations + allow rules all compiled in)
```

**Rule: Every service whose binary lives in `/vendor/bin/` MUST use `BOARD_VENDOR_SEPOLICY_DIRS`.
`PRODUCT_PRIVATE_SEPOLICY_DIRS` is for apps and libraries on the product partition.**

---

## Error #6 — Soong Ninja Cache Not Invalidated by soong.variables Changes

**When:** After fixing `myoem_base.mk` (Error #5), running `m selinux_policy` still
produced output that didn't include the potvolumed policy. The fix appeared to have
no effect.

**Symptom:**
```bash
# After editing myoem_base.mk to use BOARD_VENDOR_SEPOLICY_DIRS:
m selinux_policy
# ninja: no work to do.   ← or only 4/150 actions, stale output

grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts
# (still empty)
```

**Diagnosis — compare timestamps:**
```bash
ls -la out/soong/build.myoem_rpi5.ninja
# -rw-r--r-- ... Mar 27 01:11 build.myoem_rpi5.ninja

ls -la out/soong/soong.myoem_rpi5.variables
# -rw-r--r-- ... Mar 27 11:47 soong.myoem_rpi5.variables
```

The ninja file was from `01:11` — before the `11:47` edit to `myoem_base.mk`. But
ninja is using the old file, so it "sees" the old configuration.

**Root cause — Soong's bootstrap dependency chain:**

Soong's build system has a two-level cache:
1. `out/soong/soong.myoem_rpi5.variables` — updated immediately when any `.mk` file changes
2. `out/soong/build.myoem_rpi5.ninja` — the actual build graph, generated by `soong_build`

The bootstrap rule (`out/soong/bootstrap.ninja`) only re-runs `soong_build` when:
- `build.myoem_rpi5.ninja.glob_results` changes (glob results for Android.bp files), OR
- The `soong_build` binary itself changes (recompiled)

**It does NOT watch `soong.variables` for changes.**

This means: editing a `.mk` file that changes `BOARD_VENDOR_SEPOLICY_DIRS` (or
`PRODUCT_SOONG_NAMESPACES`, or any other Soong variable) updates `soong.variables`
immediately, but `soong_build` is NOT re-run because the glob results haven't changed.
The ninja file stays stale with the old variable values baked in.

**Fix — delete the stale ninja file to force regeneration:**
```bash
rm -f out/soong/build.myoem_rpi5.ninja
m selinux_policy
# soong_build runs for ~1-2 min, regenerates the full ninja graph with new variables
```

After this, the build correctly picks up `BOARD_VENDOR_SEPOLICY_DIRS` and includes
the potvolumed policy in `vendor_sepolicy.cil`.

**Confirmation:**
```bash
# Check that soong_build ran (ninja file timestamp updated):
ls -la out/soong/build.myoem_rpi5.ninja
# Mar 27 11:55 build.myoem_rpi5.ninja   ← now recent

# Check variables were applied:
grep -A10 "BoardVendorSepolicyDirs" out/soong/soong.myoem_rpi5.variables
# "BoardVendorSepolicyDirs": ["vendor/myoem/services/calculator/sepolicy/private",
#   "vendor/myoem/services/bmi/sepolicy/private", ...potvolumed...]
```

**Rule: When you change any AOSP make variable that feeds Soong (PRODUCT_SOONG_NAMESPACES,
BOARD_VENDOR_SEPOLICY_DIRS, PRODUCT_PRIVATE_SEPOLICY_DIRS, etc.), delete
`out/soong/build.<product>.ninja` before rebuilding. Otherwise the stale ninja graph
silently ignores your change.**

---

## Operational Lesson — restorecon and adb root After Reboot

**Context:** After building and pushing the correct SELinux policy files and binary,
the label was still wrong on the device (`vendor_file:s0` instead of `potvolumed_exec:s0`).

### Part A — Files pushed via adb push get default labels

`adb push` uses the raw filesystem — it doesn't apply SELinux labels from
`vendor_file_contexts`. The pushed binary gets the parent directory's default label
(`vendor_file` for files in `/vendor/bin/`).

**Fix: run `restorecon` after every push:**
```bash
adb shell restorecon /vendor/bin/potvolumed
adb shell ls -laZ /vendor/bin/potvolumed
# u:object_r:potvolumed_exec:s0   ← correct
```

`restorecon` reads the loaded `vendor_file_contexts` and reapplies the correct label.
No reboot needed — the label change takes effect immediately.

**Note:** `restorecon` only works if the policy was already pushed AND `/vendor` is
mounted rw. Push the SELinux files first (`vendor_file_contexts`, `vendor_sepolicy.cil`),
then the binary, then `restorecon`.

### Part B — adb root reverts to non-root after every reboot

`adb root` switches adbd to run as root. This state does NOT survive a reboot. After
every reboot, adbd reverts to the standard non-root mode.

**Symptom:**
```bash
# After reboot, trying to remount /vendor:
adb shell mount -o remount,rw /vendor
# mount: '/vendor' not in /proc/mounts  ← or: Permission denied
```

**Fix: always `adb root` first, wait for reconnect, then remount:**
```bash
adb root                          # switches adbd to root; device reconnects
# wait ~3 seconds for adb reconnect
adb shell mount -o remount,rw /vendor
adb shell restorecon /vendor/bin/potvolumed
```

**Complete dev iteration sequence (after ANY reboot):**
```bash
adb root
adb shell mount -o remount,rw /vendor
# push binary:
adb push out/target/product/rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed
adb shell chmod 755 /vendor/bin/potvolumed
adb shell restorecon /vendor/bin/potvolumed
# push SELinux (only needed when policy changed):
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts \
         /vendor/etc/selinux/vendor_file_contexts
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil \
         /vendor/etc/selinux/vendor_sepolicy.cil
# verify and start:
adb shell ls -laZ /vendor/bin/potvolumed
adb shell start potvolumed
adb shell getprop init.svc.potvolumed
```

---

## SPI Enable Procedure — Editing config.txt on RPi5

**Context:** `/dev/spidev*` does not exist on a fresh RPi5 AOSP build. SPI must be
enabled via `dtparam=spi=on` in the RPi boot config file (`config.txt`).

**The problem with the obvious approach:**
```bash
adb shell mount -o remount,rw /boot
# mount: '/boot' not in /proc/mounts
```

Android on RPi5 does NOT mount the boot partition. The RPi5 boot partition
(`/dev/block/mmcblk0p1`, vfat, labeled "BOOT") is entirely invisible to the Android
mount namespace — it's not listed in `fstab` and not automounted.

**How to find the boot partition:**
```bash
adb shell ls -la /dev/block/mmcblk0*
# mmcblk0p1 — vfat "BOOT" partition (config.txt lives here)
# mmcblk0p2 — ext4 system/vendor partition
# ...

adb shell cat /proc/partitions
# shows mmcblk0p1 size ~256MB — consistent with RPi boot partition
```

**Fix — manually mount the boot partition:**
```bash
# Mount the boot partition to a temp directory
adb shell mkdir -p /mnt/boot_tmp
adb shell mount -t vfat /dev/block/mmcblk0p1 /mnt/boot_tmp

# Verify config.txt is there
adb shell ls /mnt/boot_tmp
# config.txt  cmdline.txt  overlays/  ...

# Add SPI enable (check first whether it's already there)
adb shell grep spi /mnt/boot_tmp/config.txt

# If not present, append it:
adb shell "echo 'dtparam=spi=on' >> /mnt/boot_tmp/config.txt"

# Verify
adb shell grep spi /mnt/boot_tmp/config.txt
# dtparam=spi=on

# Unmount cleanly
adb shell umount /mnt/boot_tmp

# Reboot to apply device tree change
adb reboot
```

**After reboot, verify SPI was enabled:**
```bash
adb shell ls /dev/spidev*
# /dev/spidev10.0   ← SPI enabled; kernel assigned bus index 10 (see Error #4)

adb logcat -b kernel | grep -i spi
# [    1.234567] spi-bcm2835 ...spi0: registered master spi10
```

**Note:** The `dtparam=spi=on` change persists across reboots (it's on the SD card).
It does not need to be re-applied unless the SD card is reflashed.

---

## Lessons Learned (continued from Part 12)

## 9. BOARD_VENDOR_SEPOLICY_DIRS Is the Only Correct Variable for Vendor Services

Any SELinux policy for a service that runs from `/vendor/bin/` must be in a directory
listed under `BOARD_VENDOR_SEPOLICY_DIRS`. Using `PRODUCT_PRIVATE_SEPOLICY_DIRS` —
which looks plausible and was used as a copy-paste from product-partition service
examples — silently routes the policy to `product_sepolicy.cil`. The vendor
file_contexts never gets the entry, the binary keeps its generic label, and init
refuses to start the service.

**Diagnostic shortcut:** If `getprop init.svc.<name>` returns empty AND the binary's
SELinux label is generic (`vendor_file:s0`), check the vendor file_contexts first:
```bash
adb shell grep <service_name> /vendor/etc/selinux/vendor_file_contexts
```
If that returns empty, the issue is in which make variable is feeding the SELinux
build pipeline — not in the `.te` file itself.

## 10. Soong Variables and the Ninja Cache Are Not the Same Thing

`soong.variables` is updated immediately when any `.mk` file is parsed. But
`build.<product>.ninja` is regenerated only when glob results change — NOT when
variables change. This creates a silent inconsistency window: the variables file
reflects your latest edit, but the ninja graph is stale and ignores it.

The symptom is subtle: `m selinux_policy` runs but reports only a few actions
(or "no work to do"), and the built output is identical to what it was before your
`.mk` edit. The fix is `rm -f out/soong/build.<product>.ninja`.

**When to delete the ninja file:**
- After changing `BOARD_VENDOR_SEPOLICY_DIRS`
- After changing `PRODUCT_SOONG_NAMESPACES`
- After changing `PRODUCT_PRIVATE_SEPOLICY_DIRS`
- After adding or removing entries from any Soong variable in `.mk` files
- In general: any time a `.mk` change doesn't seem to take effect in the build

## 11. The RPi5 Boot Partition Is Not Mounted by Android — Mount It Manually

Android on RPi5 has no knowledge of `/boot` as a mount point. If you need to edit
`config.txt` (for SPI, I2C, UART, HDMI, or any other overlay), you must manually
mount `/dev/block/mmcblk0p1` as vfat. Attempting `mount -o remount,rw /boot` fails
because `/boot` is simply not in the Android fstab or mount namespace.

This is not a bug — it's by design. The boot partition is owned by the bootloader
(U-Boot / RPi firmware), not by Android. Android mounts only the partitions it needs.

---

## Reference: Quick Command Cheatsheet (updated)

```bash
# ── Hardware — Verify BEFORE hardcoding any paths ──────────────────
adb shell ls /dev/spidev*               # find actual SPI device node (might be spidev10.0!)
adb shell ls -laZ /dev/spidev*          # check SELinux label — use this in .te
adb shell ls -laZ /dev/uinput           # verify label is uhid_device, not uinput_device

# ── Build ─────────────────────────────────────────────────────────
m potvolumed                            # build binary + RC file only
m selinux_policy                        # build SELinux policy files (separate target!)
# IMPORTANT: if .mk variables changed, delete stale ninja first:
rm -f out/soong/build.myoem_rpi5.ninja && m selinux_policy

# ── Deploy (output dir is rpi5/, not myoem_rpi5/) ─────────────────
adb root                                # MUST do this after every reboot
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed
adb shell chmod 755 /vendor/bin/potvolumed
adb shell restorecon /vendor/bin/potvolumed   # MUST do after every push
# SELinux policy files (only when policy changed):
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts \
         /vendor/etc/selinux/vendor_file_contexts
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil \
         /vendor/etc/selinux/vendor_sepolicy.cil
adb push out/target/product/rpi5/vendor/etc/selinux/precompiled_sepolicy \
         /vendor/etc/selinux/precompiled_sepolicy

# ── SELinux policy verification (after build, before push) ─────────
grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts
grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil
# Must return results — if empty, policy not compiled in (check BOARD_VENDOR_SEPOLICY_DIRS)

# ── SELinux policy verification (on device, after push) ────────────
adb shell grep potvolumed /vendor/etc/selinux/vendor_file_contexts
adb shell ls -laZ /vendor/bin/potvolumed   # must be potvolumed_exec:s0

# ── Service Control ────────────────────────────────────────────────
adb shell stop potvolumed
adb shell start potvolumed
adb shell getprop init.svc.potvolumed   # "running" / "restarting" / "stopped" / empty
# empty → policy not pushed / wrong label → check vendor_file_contexts + restorecon

# ── Logs ──────────────────────────────────────────────────────────
adb logcat -s potvolumed                # daemon output only
adb logcat | grep "avc: denied"         # SELinux denials
adb logcat -b kernel | grep -i spi      # kernel SPI driver messages

# ── SELinux ───────────────────────────────────────────────────────
adb root && adb shell setenforce 0      # permissive mode (dev only — collect all denials)
adb shell getenforce                    # Enforcing / Permissive

# ── SPI enable via config.txt (one-time, persists across reboots) ──
adb shell mkdir -p /mnt/boot_tmp
adb shell mount -t vfat /dev/block/mmcblk0p1 /mnt/boot_tmp
adb shell grep spi /mnt/boot_tmp/config.txt           # check if already enabled
adb shell "echo 'dtparam=spi=on' >> /mnt/boot_tmp/config.txt"
adb shell umount /mnt/boot_tmp
adb reboot
# After reboot: adb shell ls /dev/spidev*  →  /dev/spidev10.0

# ── Volume Verification ────────────────────────────────────────────
adb shell dumpsys audio | grep -A5 "STREAM_MUSIC"
# uinput key events can be tested manually:
# adb shell sendevent /dev/uinput 1 115 1  # EV_KEY KEY_VOLUMEUP down
# adb shell sendevent /dev/uinput 0 0 0    # EV_SYN
# adb shell sendevent /dev/uinput 1 115 0  # EV_KEY KEY_VOLUMEUP up
# adb shell sendevent /dev/uinput 0 0 0    # EV_SYN
```

---

# Part 14 — SPI Enable, uinput Permission, and Hardware Bring-Up

This part documents what happened immediately after the SELinux policy was fixed
(Part 13). The daemon could now start, SPI was not yet enabled on the device, and
a second permission error appeared. This section covers all of that plus the
hardware wiring for the MCP3008 potentiometer circuit.

---

## SPI Enable — Why /dev/spidev* Didn't Exist Initially

After the SELinux fix, the daemon started but immediately crashed:

```
potvolumed: Cannot open SPI device /dev/spidev0.0 — daemon cannot start
```

Checking the device:
```bash
adb shell ls /dev/spidev*
# ls: /dev/spidev*: No such file or directory
```

**Root cause:** SPI is a hardware peripheral that is OFF by default on the RPi5.
The Linux kernel only creates `/dev/spidev*` device nodes when the SPI controller is
activated by the device tree. On RPi, device tree overlays are controlled via
`config.txt` in the boot partition.

The comment in `config.txt` said:
```
#dtparam=spi=on
```
That `#` means it is commented out — SPI disabled.

**Why the boot partition isn't just `/boot`:**

The natural instinct is:
```bash
adb shell mount -o remount,rw /boot
# mount: '/boot' not in /proc/mounts   ← fails
```

Android on RPi5 does NOT mount the RPi boot partition. It's a vfat partition
(`/dev/block/mmcblk0p1`, LABEL="BOOT") that belongs to the RPi firmware/bootloader.
Android's `fstab` doesn't include it — Android only mounts the partitions it needs
(`system`, `vendor`, `data`, etc.). The boot partition is invisible to the Android
mount namespace.

**Fix — manually mount the boot partition from a root shell:**

```bash
adb root                                         # need root to mount
adb shell mkdir -p /mnt/boot_tmp                 # create a temporary mount point
adb shell mount -t vfat /dev/block/mmcblk0p1 /mnt/boot_tmp

# Verify the files are there
adb shell ls /mnt/boot_tmp
# Image  bcm2712-rpi-5-b.dtb  cmdline.txt  config.txt  overlays  ramdisk.img  ...

# Check current SPI state (the line is commented out)
adb shell grep spi /mnt/boot_tmp/config.txt
# #dtparam=spi=on

# Append the active line (>> appends, doesn't overwrite)
adb shell "echo 'dtparam=spi=on' >> /mnt/boot_tmp/config.txt"

# Verify both lines now exist — the commented original + our active line
adb shell grep spi /mnt/boot_tmp/config.txt
# #dtparam=spi=on
# dtparam=spi=on

# Unmount cleanly before reboot
adb shell umount /mnt/boot_tmp
adb reboot
```

**After reboot — what actually appeared:**

Before enabling SPI, `ls /dev/spidev*` returned nothing.
After enabling SPI and rebooting:
```bash
adb shell ls /dev/spidev*
# /dev/spidev0.0
# /dev/spidev0.1
# /dev/spidev10.0
```

Three device nodes appeared:
- `/dev/spidev0.0` — SPI bus 0, chip select 0 (CE0, GPIO8) — **this is SPI0**
- `/dev/spidev0.1` — SPI bus 0, chip select 1 (CE1, GPIO7)
- `/dev/spidev10.0` — same SPI0 hardware, registered at kernel bus index 10

Wait — why is bus 10 still there alongside bus 0?

**Why three nodes instead of one:**

The RPi5's SPI0 hardware controller can appear at different kernel bus indices
depending on how the device tree is interpreted. With `dtparam=spi=on`, the RPi
firmware creates the standard `spidev0.0` and `spidev0.1`. The bus 10 index
(`spidev10.0`) was already present from before — it's the same physical hardware
just registered with a different index by the `spi-bcm2835` driver depending on
build configuration.

The daemon was hardcoded to `/dev/spidev0.0` in `main.cpp`. With SPI enabled, this
node now exists and the daemon opens it successfully:
```
potvolumed: SPI device /dev/spidev0.0 opened: mode=0 bits=8 speed=1000000Hz
```

SPI problem solved. A new error appeared immediately.

---

## Error #7 — uinput Opens with Permission Denied

**Symptom:** Even after SPI opened successfully, the daemon still crashed:

```
I potvolumed: SPI device /dev/spidev0.0 opened: mode=0 bits=8 speed=1000000Hz
E potvolumed: open(/dev/uinput) failed: Permission denied
E potvolumed: Cannot open uinput device — daemon cannot start
```

The daemon gets past SPI (good) but immediately fails on uinput (bad).

**Diagnosis — check the raw Linux permissions on /dev/uinput:**

```bash
adb shell ls -la /dev/uinput
# crw------- 1 root root 10, 223 ... /dev/uinput
# permissions: 0600 (owner=root, group=root, others=none)
```

The daemon runs as user `system` (UID 1000), not `root`. With permissions `0600`,
only `root` can open the file. `system` user gets `Permission denied`.

**This is NOT a SELinux error.** SELinux is in permissive mode (denials are logged
but not enforced). This is a plain Linux filesystem permission error — the kind you'd
see on any Linux system with a restricted device node.

**Root cause — the RC file was incomplete:**

`potvolumed.rc` had an `on boot` block that fixed the SPI device permissions, but
it never mentioned `/dev/uinput`:

```ini
# BEFORE (missing uinput):
on boot
    chown root system /dev/spidev10.0
    chmod 0660 /dev/spidev10.0
    # ← /dev/uinput never chowned or chmoded
```

The kernel creates `/dev/uinput` as `root:root 0600` at every boot. Without an
`on boot` fixup, it stays inaccessible to any non-root process.

**Why this wasn't caught earlier:**

During earlier development iterations the daemon was crashing before it even reached
the `open(/dev/uinput)` call — it was dying at `open(/dev/spidev0.0)` first, because
SPI wasn't enabled. Once SPI was enabled and the SPI open succeeded, uinput became
the next failure point.

This is a common pattern in bring-up: fixing one error reveals the next one that was
always there but was never reached.

**Fix — add uinput and all spidev nodes to the RC file `on boot` block:**

```ini
# vendor/myoem/services/potvolumed/potvolumed.rc

on boot
    # SPI device — kernel creates as root:root 0600.
    # With dtparam=spi=on, RPi5 exposes spidev0.0 (CE0) and spidev0.1 (CE1).
    # Bus index 10 (spidev10.0) also appears; cover all three.
    chown root system /dev/spidev0.0
    chmod 0660 /dev/spidev0.0
    chown root system /dev/spidev0.1
    chmod 0660 /dev/spidev0.1
    chown root system /dev/spidev10.0
    chmod 0660 /dev/spidev10.0

    # uinput device — kernel creates as root:root 0600.
    # potvolumed writes key events (KEY_VOLUMEUP/DOWN) via uinput.
    chown root system /dev/uinput
    chmod 0660 /dev/uinput

service potvolumed /vendor/bin/potvolumed
    class main
    user system
    group system
```

**Why `on boot` is the right place for this:**

`init` runs as root. The `on boot` trigger fires before `class main` services
are started. This guarantees:
1. init can `chown/chmod` (needs root — init has it)
2. The permissions are set before potvolumed's `open()` calls happen
3. The pattern is identical to `thermalcontrold.rc` and all other AOSP vendor services

**Apply the fix without a full rebuild (for dev iteration):**

The `on boot` block only runs once at boot via init — you can't just restart the
service to re-run it. But you can apply the same permissions manually from a root
shell right now without rebooting:

```bash
adb root
adb shell chown root:system /dev/uinput
adb shell chmod 0660 /dev/uinput
adb shell chown root:system /dev/spidev0.0
adb shell chmod 0660 /dev/spidev0.0

# Stop/start the service to pick up the new permissions
adb shell stop potvolumed
adb shell start potvolumed

# Check it's running
adb shell getprop init.svc.potvolumed
# running  ← or "restarting" if hardware not connected yet
```

Push the updated RC file so the fix is permanent after the next reboot:
```bash
adb shell mount -o remount,rw /vendor
adb push vendor/myoem/services/potvolumed/potvolumed.rc /vendor/etc/init/potvolumed.rc
```

---

## SELinux Denials During Testing (permissive — informational only)

While fixing the uinput issue, the logcat showed AVC denial messages alongside the
permission error:

```
I potvolumed: type=1400 audit(0.0:41): avc: denied { read write } for
    name="spidev0.0" dev="tmpfs" ino=736
    scontext=u:r:potvolumed:s0 tcontext=u:object_r:device:s0
    tclass=chr_file permissive=1
```

**What this means:**

- `avc: denied` — SELinux would deny this if enforcing
- `permissive=1` — but SELinux is in permissive mode, so it only logs, doesn't block
- `tcontext=u:object_r:device:s0` — `/dev/spidev0.0` has the generic `device` type,
  not `spidev_device`

**Why `spidev0.0` has the generic `device` type:**

Our `file_contexts` entry uses a regex pattern:
```
/dev/spidev[0-9]+\.[0-9]+    u:object_r:spidev_device:s0
```

This pattern covers `spidev0.0`. BUT — `restorecon` only runs when init processes
`file_contexts` at boot. The nodes `/dev/spidev0.0` and `/dev/spidev0.1` are
**newly created** by this boot (they didn't exist before `dtparam=spi=on`). The
timing of when init labels them vs when they appear depends on the kernel driver
init ordering.

In permissive mode this doesn't block anything. For a production enforcing build,
you would want to verify with `adb shell ls -laZ /dev/spidev*` and run
`adb shell restorecon /dev/spidev0.0` if the label is still `device:s0`.

**The critical observation:** The actual blocking error was the Linux permission
(`Permission denied` from `open()`), NOT the SELinux denial. The SELinux denial
is secondary — if the file can't even be opened due to permissions, SELinux never
gets a chance to block it anyway. Always read the error message text, not just
the AVC audit logs.

---

## Hardware Wiring — MCP3008 to RPi5

Once the daemon is running without crashing (you see log output instead of
`Cannot open uinput device`), the next step is connecting the physical hardware.

### What You Need

| Component | Quantity | Purpose |
|-----------|----------|---------|
| MCP3008 ADC | 1 | Converts potentiometer analog voltage → 10-bit digital |
| 10kΩ potentiometer | 1 | Variable voltage divider (0V → 3.3V as you turn it) |
| Breadboard | 1 | No-solder prototyping |
| Jumper wires | ~10 | Male-to-female (PC to breadboard) or M-M |
| 100nF capacitor | 1 | Optional but recommended: noise filter on ADC input |

### Understanding the Signal Path

```
You turn the knob
        ↓
Potentiometer wiper voltage changes (0V to 3.3V)
        ↓
MCP3008 CH0 samples the analog voltage
        ↓
MCP3008 encodes it as a 10-bit number (0–1023)
        ↓
SPI transaction: RPi5 sends 3 bytes, MCP3008 sends back 3 bytes with the value
        ↓
potvolumed reads the value every 50ms
        ↓
EMA filter + dead zone — suppresses noise, prevents jitter
        ↓
VolumeMapper maps 0–1023 → KEY_VOLUMEDOWN or KEY_VOLUMEUP events
        ↓
/dev/uinput → Android InputReader → PhoneWindowManager → AudioManager
        ↓
STREAM_MUSIC volume changes
```

### MCP3008 Pin Layout

The MCP3008 is a 16-pin DIP chip. Looking at it from above with the notch at top:

```
       ┌──────────────────┐
CH0  1 │●                 │ 16  VDD   ← 3.3V power
CH1  2 │                  │ 15  VREF  ← 3.3V reference voltage
CH2  3 │                  │ 14  AGND  ← analog ground
CH3  4 │    MCP3008        │ 13  CLK   ← SPI clock
CH4  5 │                  │ 12  DOUT  ← SPI MISO (data out to RPi)
CH5  6 │                  │ 11  DIN   ← SPI MOSI (data in from RPi)
CH6  7 │                  │ 10  CS    ← chip select (CE0)
CH7  8 │                  │  9  DGND  ← digital ground
       └──────────────────┘
```

We only use CH0 (pin 1) for the potentiometer. Pins CH1–CH7 are unused.

### RPi5 40-Pin GPIO Header (relevant pins)

```
Physical pin layout (odd on left, even on right):

 3.3V  │  1 ●  ●  2 │ 5V
       │  3 ●  ●  4 │ 5V
       │  5 ●  ●  6 │ GND   ← use this GND
       │  7 ●  ●  8 │
GND    │  9 ●  ● 10 │
       │ 11 ●  ● 12 │
       │ 13 ●  ● 14 │ GND
       │ 15 ●  ● 16 │
3.3V   │ 17 ●  ● 18 │
MOSI   │ 19 ●  ● 20 │ GND
MISO   │ 21 ●  ● 22 │
SCLK   │ 23 ●  ● 24 │ CE0   ← chip select for spidev0.0
GND    │ 25 ●  ● 26 │ CE1   (not used)
```

### Complete Wiring Table

| MCP3008 Pin | MCP3008 Name | RPi5 Pin | RPi5 Name | Notes |
|-------------|--------------|----------|-----------|-------|
| 16 | VDD | 1 | 3.3V | Power for the ADC chip |
| 15 | VREF | 1 | 3.3V | Reference voltage (sets 0–3.3V input range) |
| 14 | AGND | 6 | GND | Analog ground |
| 9 | DGND | 6 | GND | Digital ground (share with AGND on breadboard) |
| 13 | CLK | 23 | SCLK (GPIO11) | SPI clock |
| 12 | DOUT | 21 | MISO (GPIO9) | MCP3008 sends data to RPi |
| 11 | DIN | 19 | MOSI (GPIO10) | RPi sends commands to MCP3008 |
| 10 | CS/SHDN | 24 | CE0 (GPIO8) | Chip select — active LOW, selects spidev0.0 |
| 1 | CH0 | — | — | Connect to potentiometer wiper |

**Potentiometer connections:**

| Pot pin | Connect to |
|---------|-----------|
| Left leg | GND (any GND pin on RPi5 or breadboard rail) |
| Right leg | 3.3V (RPi5 pin 1 or 17) |
| Middle leg (wiper) | MCP3008 CH0 (pin 1) |

The direction (which leg is GND vs 3.3V) only affects whether turning left or right
increases volume. Swap them if you want the opposite behavior.

### Optional: Noise Filter Capacitor

Add a 100nF ceramic capacitor between MCP3008 CH0 (pin 1) and GND, as close to the
MCP3008 as possible. This filters high-frequency switching noise from the RPi5's PMIC
and power supply that couples into the ADC input, reducing the ±5 count oscillation
described in Lesson 8. Without it the EMA + dead zone filter in `AdcFilter.cpp`
handles the noise in software, but a hardware filter always helps.

### Wiring Diagram (ASCII)

```
RPi5                            MCP3008 (16-pin DIP)
Pin 1 (3.3V) ──────────────────┬─ pin16 VDD
                                └─ pin15 VREF
Pin 6 (GND)  ──────────────────┬─ pin14 AGND
                                └─ pin 9 DGND
Pin 23 (SCLK)──────────────────── pin13 CLK
Pin 21 (MISO)──────────────────── pin12 DOUT
Pin 19 (MOSI)──────────────────── pin11 DIN
Pin 24 (CE0) ──────────────────── pin10 CS

                  pin 1 CH0 ─────────── Pot wiper (middle)

Potentiometer:
  Left  leg ── GND
  Right leg ── 3.3V
  Wiper    ── MCP3008 CH0 (+ optional 100nF cap to GND)
```

### Verifying the Wiring Without Physical Hardware

Before connecting anything, confirm the daemon is running and logging:
```bash
adb logcat -s potvolumed
# Should see SPI poll lines every 50ms:
# I potvolumed: SPI raw: tx=[01 80 00] rx=[00 XX YY] value=NNN
```

If you see `value=0` continuously with no hardware connected — that's expected.
The MCP3008 input floats to 0V when nothing is connected (no current path to supply).

Once the potentiometer is wired, you should see `value` change as you turn the knob
(0 at one extreme, ~1023 at the other). The daemon then injects volume key events
based on the direction and speed of change.

### Verifying Volume Events Reach Android

```bash
# Watch audio system volume changes
adb shell dumpsys audio | grep -A3 "STREAM_MUSIC"

# Or watch for input events in real time
adb shell getevent -l | grep KEY_VOLUME

# Manual test (no hardware needed — inject events directly):
adb shell sendevent /dev/uinput 1 115 1   # EV_KEY KEY_VOLUMEUP press
adb shell sendevent /dev/uinput 0 0 0     # EV_SYN sync
adb shell sendevent /dev/uinput 1 115 0   # EV_KEY KEY_VOLUMEUP release
adb shell sendevent /dev/uinput 0 0 0     # EV_SYN sync
```

---

## Lessons Learned (continued)

## 12. One Error Masks the Next — Fix the First, Find the Second

The uinput permission error was always present in the code. It was never seen before
because the daemon crashed at `open(/dev/spidev0.0)` first — a crash that happens
before the uinput open call is ever reached.

When you fix the SPI open (by enabling `dtparam=spi=on`), the daemon advances past
that point and immediately hits the next error: uinput Permission Denied.

**Pattern:** In bring-up debugging, each fix reveals the next problem. The order in
which errors appear is determined by execution order, not by importance. Don't assume
a module is correct just because its error hasn't appeared yet — it may simply not
have been reached.

## 13. RC File `on boot` Must Cover ALL Devices the Service Will Open

If your service opens N device nodes, the `on boot` block must `chown/chmod` all N
of them. The kernel creates device nodes with `root:root 0600` by default. Any device
your service touches that isn't fixed up will cause `Permission denied` when the
service runs as a non-root user.

The way to audit this: look at every `open()` call in your service code and trace
which device it targets. Then make sure each one has a `chown/chmod` in `on boot`.
For `potvolumed`: `/dev/spidev0.0` (SPI read) and `/dev/uinput` (key event inject).

## 14. Linux Permission Denied vs SELinux Denied Are Two Different Things

Both produce errors that look similar at first glance:

| Type | Log output | Blocking? | Fix |
|------|-----------|-----------|-----|
| Linux permission | `open() failed: Permission denied` | Always — regardless of SELinux mode | `chown/chmod` in RC `on boot` |
| SELinux denial | `avc: denied ... permissive=1` | Only in enforcing mode | Add `allow` rule in `.te` file |

If you see `open() failed: Permission denied` in logcat AND `avc: denied permissive=1`
for the same file, the Linux permission error is the actual blocker. SELinux is just
logging an audit trail (permissive mode). Fix the Linux permissions first —
`chown/chmod` in the RC file. The SELinux denials can then be addressed separately
for enforcing mode.

A quick way to distinguish: if `permissive=1` appears in the AVC log, SELinux is NOT
blocking. If the syscall still fails, the blocking cause is Linux DAC (owner/group/mode).

---

## Reference: Quick Command Cheatsheet (updated)

```bash
# ── Hardware — Verify BEFORE hardcoding any paths ──────────────────
adb shell ls /dev/spidev*               # find actual SPI device node
adb shell ls -laZ /dev/spidev*          # check SELinux label
adb shell ls -laZ /dev/uinput           # verify label (uhid_device on AOSP 15)
adb shell ls -la /dev/uinput            # check Linux permissions (should be 0660 after boot)
adb shell ls -la /dev/spidev0.0         # check Linux permissions

# ── Build ─────────────────────────────────────────────────────────
m potvolumed                            # build binary + RC file
m selinux_policy                        # build SELinux policy files
# If .mk variables changed, delete stale ninja first:
rm -f out/soong/build.myoem_rpi5.ninja && m selinux_policy

# ── Deploy (output dir is rpi5/, not myoem_rpi5/) ─────────────────
adb root                                # MUST do this after every reboot
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/potvolumed /vendor/bin/potvolumed
adb shell chmod 755 /vendor/bin/potvolumed
adb shell restorecon /vendor/bin/potvolumed        # apply correct SELinux label
adb push vendor/myoem/services/potvolumed/potvolumed.rc /vendor/etc/init/potvolumed.rc

# ── Fix device permissions without reboot (dev iteration) ──────────
adb root
adb shell chown root:system /dev/uinput && adb shell chmod 0660 /dev/uinput
adb shell chown root:system /dev/spidev0.0 && adb shell chmod 0660 /dev/spidev0.0
adb shell stop potvolumed && adb shell start potvolumed

# ── SELinux policy files (only when policy changed) ────────────────
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts \
         /vendor/etc/selinux/vendor_file_contexts
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil \
         /vendor/etc/selinux/vendor_sepolicy.cil
adb push out/target/product/rpi5/vendor/etc/selinux/precompiled_sepolicy \
         /vendor/etc/selinux/precompiled_sepolicy

# ── SELinux policy verification (build output) ──────────────────────
grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_file_contexts
grep potvolumed out/target/product/rpi5/vendor/etc/selinux/vendor_sepolicy.cil
# If empty → BOARD_VENDOR_SEPOLICY_DIRS not set, or stale ninja (rm build.ninja)

# ── Service Control ────────────────────────────────────────────────
adb shell getprop init.svc.potvolumed   # running / restarting / stopped / (empty)
adb shell stop potvolumed
adb shell start potvolumed

# ── Logs ──────────────────────────────────────────────────────────
adb logcat -s potvolumed                # daemon output only
adb logcat | grep "avc: denied"         # SELinux denials (informational in permissive)
adb logcat -b kernel | grep -i spi      # kernel SPI driver messages

# ── SPI enable via config.txt (one-time) ───────────────────────────
adb root
adb shell mkdir -p /mnt/boot_tmp
adb shell mount -t vfat /dev/block/mmcblk0p1 /mnt/boot_tmp
adb shell grep spi /mnt/boot_tmp/config.txt           # check current state
adb shell "echo 'dtparam=spi=on' >> /mnt/boot_tmp/config.txt"
adb shell umount /mnt/boot_tmp && adb reboot
# After reboot: adb shell ls /dev/spidev*  →  /dev/spidev0.0  spidev0.1  spidev10.0

# ── Volume Verification ────────────────────────────────────────────
adb shell dumpsys audio | grep -A3 "STREAM_MUSIC"
adb shell getevent -l | grep KEY_VOLUME            # watch for real-time events
# Manual inject test (no hardware needed):
adb shell sendevent /dev/uinput 1 115 1  # KEY_VOLUMEUP press
adb shell sendevent /dev/uinput 0 0 0    # EV_SYN
adb shell sendevent /dev/uinput 1 115 0  # KEY_VOLUMEUP release
adb shell sendevent /dev/uinput 0 0 0    # EV_SYN
```

---

# Part 15 — Hardware Bring-Up: spidev10.0 vs spidev0.0, and Final Success

This part documents the final hardware debugging session. After all software
issues were resolved (SELinux, uinput permissions, SPI enable), the daemon was
running and polling but the knob had zero effect. This section traces exactly
why, and records the moment everything started working.

---

## Error #8 — ADC Always Returns 1023 Regardless of Knob Position

**When:** After enabling `dtparam=spi=on` and fixing the uinput permission, the
daemon ran successfully but `AdcFilter: seeded at raw=1023` appeared at every
startup, and the value never changed when turning the potentiometer.

**Symptom:**
```
I potvolumed: SPI device /dev/spidev10.0 opened
I potvolumed: SPI and uinput ready. Entering poll loop.
D potvolumed: AdcFilter: seeded at raw=1023
D potvolumed: VolumeMapper: adc=1023 → ratio=1.000 → index=15/15
I potvolumed: VolumeController: boot reference set to index=15

# Turn the knob fully left, fully right — nothing changes in logcat
```

**What 1023 means physically:**

In the MCP3008 SPI protocol, the RPi5 sends 3 bytes and reads 3 bytes back.
The ADC value is decoded from the response bytes:
```cpp
value = ((rx[1] & 0x03) << 8) | rx[2]
```
If MISO (Master In Slave Out — the wire carrying data from MCP3008 back to RPi5)
is not being driven by the chip, it floats. The RPi5's SPI MISO pin has an internal
pull-up resistor that keeps it HIGH. Every bit received is 1, so:
- `rx[1] = 0xFF` → `(0xFF & 0x03) << 8 = 0x300`
- `rx[2] = 0xFF` → `0x300 | 0xFF = 0x3FF = 1023`

**1023 = MISO is floating = MCP3008 is not driving the line.**

**Root cause — wrong SPI controller:**

The daemon was hardcoded to `/dev/spidev10.0`. This device existed BEFORE
`dtparam=spi=on` was enabled. The standard GPIO header SPI0 pins are:

```
Physical pin 19 → GPIO10 (MOSI)
Physical pin 21 → GPIO9  (MISO)
Physical pin 23 → GPIO11 (SCLK)
Physical pin 24 → GPIO8  (CE0)
```

These GPIO pins belong to `/dev/spidev0.0` — the standard SPI0 controller
enabled by `dtparam=spi=on`.

`/dev/spidev10.0` is a DIFFERENT SPI controller registered by the AOSP RPi5
device tree at kernel bus index 10. It uses DIFFERENT GPIO pins — not the
standard header SPI0 pins. Because the MCP3008 was wired to the standard
GPIO header pins (19/21/23/24), it was connected to `spidev0.0` but the daemon
was talking to `spidev10.0`. The two controllers are completely independent.

The wiring was correct. The device path was wrong.

**Diagnosis — confirm MISO is floating:**

If `raw=1023` appears and never changes when the knob moves:
```bash
adb logcat -s potvolumed | grep "raw="
# If always raw=1023 → MISO floating → wrong controller or disconnected wire
# If always raw=0    → MOSI/CS not working → chip never selected
# If raw changes     → SPI working but some other issue
```

**Fix — change `kSpiDevice` in `main.cpp`:**

```cpp
// BEFORE (wrong controller — not connected to GPIO header SPI0):
static constexpr char kSpiDevice[] = "/dev/spidev10.0";

// AFTER (correct — SPI0 on GPIO header pins 19/21/23/24):
// spidev0.0 = CLK=GPIO11(pin23), MOSI=GPIO10(pin19),
//             MISO=GPIO9(pin21), CE0=GPIO8(pin24)
// dtparam=spi=on in /boot/config.txt must be active for this to appear.
static constexpr char kSpiDevice[] = "/dev/spidev0.0";
```

**Rule: Before hardcoding any `/dev/spidevX.Y` path, verify it maps to the
GPIO pins your hardware is wired to. `ls /dev/spidev*` shows all available
devices but doesn't tell you which GPIO pins they use. Check the RPi5 device
tree or verify empirically by checking when each node appears vs disappears.**

---

## Error #9 — config.txt Change Does Not Survive Reflash

**When:** After fixing the device path to `spidev0.0`, the daemon crashed:
```
E potvolumed: open(/dev/spidev0.0) failed: No such file or directory
```

Checking:
```bash
adb shell ls /dev/spidev*
# /dev/spidev10.0   ← only this one

adb shell mount -t vfat /dev/block/mmcblk0p1 /mnt/boot_tmp
adb shell grep spi /mnt/boot_tmp/config.txt
# #dtparam=spi=on   ← commented out — our change is gone
```

**Root cause:** The SD card had been reflashed (a new AOSP image written via
`dd`). The reflash overwrites the entire boot partition, including `config.txt`,
restoring it to the original state with SPI disabled.

The `dtparam=spi=on` line we appended in a previous session was wiped when the
image was re-flashed.

**This is expected behavior.** `dd` writes a complete image — it doesn't merge
individual file edits. Every reflash resets `config.txt` to the factory defaults
of the image.

**Fix — re-add `dtparam=spi=on` after every reflash:**

```bash
adb root
adb shell mkdir -p /mnt/boot_tmp
adb shell mount -t vfat /dev/block/mmcblk0p1 /mnt/boot_tmp
adb shell "echo 'dtparam=spi=on' >> /mnt/boot_tmp/config.txt"
adb shell grep spi /mnt/boot_tmp/config.txt
# #dtparam=spi=on   ← original (commented)
# dtparam=spi=on    ← our addition (active)
adb shell umount /mnt/boot_tmp
adb reboot
```

**Better long-term fix — add `dtparam=spi=on` to the AOSP image itself:**

If you want SPI enabled in the flashed image so it survives reflash without
manual intervention, you need to embed it in the build. One approach:

```makefile
# In vendor/myoem/common/myoem_base.mk, add a post-install hook
# OR edit the AOSP RPi5 config.txt source in device/brcm/rpi5/
# This is a device-level change and touches device/brcm/rpi5/ — outside
# vendor/myoem/ — but it's the correct production approach.
```

For development purposes, re-adding after reflash is fine.

**Rule: Any `config.txt` edit done via `adb shell` is a live-system change on
the current SD card. A `dd` reflash will wipe it. Either re-apply after every
reflash OR bake it into the AOSP image at build time.**

---

## Final Verification — It Works

After:
1. `dtparam=spi=on` added to `config.txt` → reboot → `/dev/spidev0.0` appears
2. Daemon changed to use `/dev/spidev0.0`
3. Permissions set: `chown root:system /dev/spidev0.0 && chmod 0660 /dev/spidev0.0`
4. Daemon restarted

The logcat showed the knob working:
```
I potvolumed: SPI device /dev/spidev0.0 opened: mode=0 bits=8 speed=1000000Hz
I potvolumed: Virtual input device 'PotVolume Knob' created on /dev/uinput
I potvolumed: SPI and uinput ready. Entering poll loop.
D potvolumed: AdcFilter: seeded at raw=1023         ← knob at max
I potvolumed: VolumeController: boot reference set to index=15

# Turn knob counterclockwise:
D potvolumed: AdcFilter: raw=900 ema=980.2 stable=1023→900 (Δ=-123)
D potvolumed: VolumeMapper: adc=900 → ratio=0.880 → index=13/15
I potvolumed: Volume: index 15 → 13 (Δ=-2, 2 × KEY_VOLUMEDOWN)

# Turn knob to middle:
D potvolumed: AdcFilter: raw=512 ema=712.1 stable=900→512 (Δ=-388)
D potvolumed: VolumeMapper: adc=512 → ratio=0.500 → index=8/15
I potvolumed: Volume: index 13 → 8 (Δ=-5, 5 × KEY_VOLUMEDOWN)

# Turn knob fully left:
D potvolumed: AdcFilter: raw=3 ema=102.4 stable=512→3 (Δ=-509)
D potvolumed: VolumeMapper: adc=3 → ratio=0.003 → index=0/15
I potvolumed: Volume: index 8 → 0 (Δ=-8, 8 × KEY_VOLUMEDOWN)
```

Android volume changed in response to every knob turn. The project is complete.

---

## Lessons Learned (continued)

## 15. spidev Node Number Does Not Indicate GPIO Pins

`/dev/spidev0.0` means "SPI bus 0, chip select 0" — but "bus 0" is a kernel
enumeration number, not a GPIO header bus number. The RPi5 AOSP kernel may
register SPI controllers at unexpected bus indices (e.g., `spidev10.0` for
hardware SPI0).

After enabling `dtparam=spi=on`, the RPi firmware's standard SPI0 appears at
the expected index `spidev0.0`. But another controller (`spidev10.0`) was
already registered by the AOSP device tree at index 10 — for a different
hardware SPI peripheral.

**Always verify which `/dev/spidevX.Y` corresponds to the GPIO pins you're
using. The clearest method: check which node APPEARS when you enable
`dtparam=spi=on` and was NOT present before. That new node is the one
mapped to the standard GPIO header SPI0.**

## 16. config.txt Changes Are Wiped on Reflash

Any edit made to the RPi5 boot partition via `adb shell` lives on the current
SD card. `dd` reflash overwrites the entire partition. This means:

- SPI enable: `dtparam=spi=on` must be re-added after every reflash
- Any other device tree customizations: same
- All AOSP partition changes (vendor, system): also wiped by reflash, but
  those are rebuilt from source — config.txt is not

For production/permanent setup, embed config.txt customizations in the AOSP
build (in `device/brcm/rpi5/`) so they survive reflash automatically.

---

## Complete Working State — Summary

After all debugging sessions, the final working configuration is:

| Item | Final State |
|------|------------|
| `kSpiDevice` in `main.cpp` | `/dev/spidev0.0` |
| `config.txt` | `dtparam=spi=on` (must re-add after reflash) |
| RC file `on boot` | `chown/chmod` for `spidev0.0`, `spidev0.1`, `spidev10.0`, `uinput` |
| SELinux | permissive mode (`androidboot.selinux=permissive`) |
| MCP3008 wiring | GPIO header pins 19/21/23/24 (SPI0) |
| Potentiometer | CH0 (pin 1) of MCP3008, left→GND, right→3.3V |
| Volume range | 0–15 (STREAM_MUSIC), controlled by knob position |
| Key events | `KEY_VOLUMEUP` / `KEY_VOLUMEDOWN` via `/dev/uinput` |
| Daemon status | `getprop init.svc.potvolumed` = `running` |

---

*Article for: `vendor/myoem/services/potvolumed`*
*Platform: Raspberry Pi 5 · AOSP Android 15 (`android-15.0.0_r14`)*
*Related: `ThermalControl_Article_Series.md`, `SafeMode_Article_Series.md`*
*Plan file: `POTVOLUME_PLAN.md`*
