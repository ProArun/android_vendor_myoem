# AOSP Vendor Tools — What They Are, When to Write Them, and a Deep Dive into the SafeMode Simulator

**A complete engineering guide to the `vendor/myoem/tools/` pattern**

---

## Table of Contents

1. [What Is a Tool in AOSP?](#part-1)
2. [When Should You Write a Tool?](#part-2)
3. [When Should You NOT Write a Tool?](#part-3)
4. [Types of Tools in AOSP Vendor Development](#part-4)
5. [The SafeMode Simulator — Overview](#part-5)
6. [Why This Simulator Exists — The Problem It Solves](#part-6)
7. [Directory Structure — Every File Explained](#part-7)
8. [Data Files — The Input to Every Script](#part-8)
9. [simulate_speed.py — Detailed Walkthrough](#part-9)
10. [simulate_gear.py — Detailed Walkthrough](#part-10)
11. [simulate_fuel.py — Detailed Walkthrough](#part-11)
12. [simulate_all.py — The Combined Orchestrator](#part-12)
13. [full_pipeline_test.sh — Automated End-to-End Validation](#part-13)
14. [The Two Modes: File-Based vs inject-vhal-event](#part-14)
15. [Which Script Do I Run and When?](#part-15)
16. [Design Decisions — Why It Was Built This Way](#part-16)
17. [Moving From Dev Tools to Production — The Clean Exit](#part-17)

---

<a name="part-1"></a>
# Part 1: What Is a Tool in AOSP?

In the context of AOSP OEM vendor development, a **tool** is any script, binary, or
program that is NOT part of your product's shipping software, but exists to help you
**develop, test, debug, or validate** that software.

Tools live outside the normal build product. They are for developers, not end users.

```
vendor/myoem/
├── services/safemode/      ← SHIPPING code — goes into /vendor/bin/safemoded
├── libs/safemode/          ← SHIPPING code — bundled into SafeModeDemo.apk
├── apps/SafeModeDemo/      ← SHIPPING code — goes into /system/app/
│
└── tools/                  ← NOT SHIPPING — developer utilities only
    └── safemode-simulator/ ← This tool: injects vehicle data for dev/test
```

The key distinction:

| | Shipping Code | Tool |
|---|---|---|
| Installed on device | Yes (built into image) | No (run from host or pushed manually) |
| End user sees it | Yes | Never |
| Built by default | Yes (in PRODUCT_PACKAGES) | Usually not (run directly) |
| Language choice | C++, Kotlin, Java | Anything: Python, Bash, Go, etc. |
| Quality bar | Production grade | Good enough for devs to use reliably |
| Depends on hardware | Must work on real device | Can depend on adb, host Python, etc. |

## Tools Are First-Class Citizens

A common mistake in embedded/automotive development is to treat test scripts as
throwaway hacks — a one-liner in the terminal that you'll never write down. This
causes two problems:

1. **Every team member reinvents the same incantation.** How do I inject a speed value?
   What's the VHAL property ID for gear? Which `adb shell` command changes the fuel level?
   Without a tool, this knowledge lives only in someone's shell history.

2. **Manual testing is not reproducible.** "I tested it at 15 km/h" means nothing if
   you can't repeat that exact test reliably next week, after a code change, or on a new
   device.

A well-written tool solves both: it documents the knowledge in code, and it makes the
test repeatable with a single command.

---

<a name="part-2"></a>
# Part 2: When Should You Write a Tool?

Write a tool when you find yourself doing any of the following more than twice:

## Scenario 1: You Are Manually Sending Test Data to a Service

```bash
# You keep typing this by hand, varying the value each time:
adb shell cmd car_service inject-vhal-event 0x11600207 0 3.0
adb shell cmd car_service inject-vhal-event 0x11600207 0 5.0
adb shell cmd car_service inject-vhal-event 0x11600207 0 12.5
```

**Write a tool when:** You need to send a sequence of values, need to repeat the
sequence, need to time the injection, or need another developer to do the same thing.

## Scenario 2: You Are Testing Across a Real-World Scenario

You don't just want to test "speed = 5.0". You want to test a full drive cycle:
car starts parked, accelerates through urban speeds, reaches highway speed, then slows
down and parks. That scenario has 20 data points across 3 signals (speed, gear, fuel)
coordinated in time.

**Write a tool when:** Your test scenario has more than 2-3 states, involves timing,
or involves multiple signals changing together.

## Scenario 3: You Have a Hardware Constraint That Blocks Real-World Testing

On RPi5, you cannot turn a steering wheel or press a gas pedal. There is no CAN bus,
no physical vehicle, no real VHAL sensor data. Without a tool, your service is
completely untestable until you have real hardware.

**Write a tool when:** The real data source is unavailable during development.
A simulator tool unblocks you.

## Scenario 4: You Need to Validate a Multi-Layer Stack

Your system has 4 layers (VHAL → service → library → app). Testing them all
together manually requires opening 4 terminals, running 4 commands, watching 4 log
streams, and keeping track of timing. You will make mistakes. You will miss events.

**Write a tool when:** Validating a stack requires orchestrating multiple components
in a specific sequence. Automation prevents human error in the test itself.

## Scenario 5: A New Team Member Would Take Hours to Get Unblocked

If someone joins the team and asks "how do I test the SafeMode service?", what do you
tell them? If the answer is a paragraph of commands, property IDs, timing notes, and
caveats — that's a tool waiting to be written.

**Write a tool when:** Onboarding someone to your component requires explaining more
than two manual steps.

---

<a name="part-3"></a>
# Part 3: When Should You NOT Write a Tool?

Equally important: knowing when a tool is overkill.

## Don't Write a Tool for a One-Off Debug Command

```bash
adb logcat -s safemoded | head -50
```

This is a one-liner you'll run once during a debugging session. It does not need to
be a script.

## Don't Write a Tool When the Platform Already Has One

AOSP already provides:
- `adb logcat` — log reading
- `adb shell service call` — raw Binder calls
- `adb shell cmd car_service` — AAOS-specific commands
- `adb shell dumpsys` — service state dumps
- `adb shell ps -eZ` — process + SELinux domain listing

Use these directly. Don't wrap them in a script that adds no value.

## Don't Write a Tool Before You Understand What You're Testing

A simulator written before you understand the data model will have wrong assumptions
baked in. Write the service first, understand the data (property IDs, units, ranges,
change modes), then write the simulator with full knowledge.

## Don't Write a Tool That Only You Can Run

If your tool requires a specific Python version pinned to your laptop, or hardcodes
a device serial number, or only works on macOS — it is not a team tool. Keep tools
portable: standard Python 3, detect the device automatically, use relative paths.

---

<a name="part-4"></a>
# Part 4: Types of Tools in AOSP Vendor Development

Tools in `vendor/<oem>/tools/` generally fall into these categories:

| Type | What It Does | Examples |
|---|---|---|
| **Simulator** | Injects synthetic data when real hardware is unavailable | safemode-simulator, fake VHAL |
| **Test Client** | Calls into a service and displays results | `safemode_client`, `thermalcontrol_client` |
| **Flasher/Pusher** | Automates pushing binaries to device | push_all.sh |
| **Log Analyzer** | Parses logcat for specific patterns | grep_avc.sh, parse_crashes.py |
| **Pipeline Validator** | Tests the full stack end-to-end automatically | full_pipeline_test.sh |
| **Data Generator** | Creates synthetic input data sets | generate_drive_cycle.py |

The SafeMode simulator combines three of these: it is a **simulator**, a **data generator**,
and a **pipeline validator** all in one directory.

---

<a name="part-5"></a>
# Part 5: The SafeMode Simulator — Overview

```
vendor/myoem/tools/safemode-simulator/
├── simulate_speed.py      ← inject only PERF_VEHICLE_SPEED (20 values)
├── simulate_gear.py       ← inject only CURRENT_GEAR (20 values)
├── simulate_fuel.py       ← inject only FUEL_LEVEL (20 values)
├── simulate_all.py        ← inject all 3 signals together (the main tool)
├── full_pipeline_test.sh  ← automated PASS/FAIL validation of the full stack
├── speed_data.txt         ← (root level copy — legacy) 20 speed values
└── data/
    ├── speed_data.txt     ← 20 speed values in m/s (canonical location)
    ├── gear_data.txt      ← 20 gear codes (VehicleGear bitmask integers)
    └── fuel_data.txt      ← 20 fuel levels in millilitres
```

**What the simulator does at the highest level:**

1. Reads pre-defined vehicle data from text files (speed/gear/fuel)
2. Sends each value to the running safemoded service on the device
3. safemoded processes the value exactly as it would a real VHAL event
4. The update propagates up: service → library → app UI

The simulator makes it possible to test the entire SafeMode stack on an RPi5 without
any physical vehicle hardware, CAN bus, or real sensor data.

---

<a name="part-6"></a>
# Part 6: Why This Simulator Exists — The Problem It Solves

## The Hardware Gap

In a real production AAOS vehicle, the data flow looks like this:

```
Physical sensors (CAN bus)
       ↓
VHAL implementation (reads CAN, publishes properties)
       ↓
safemoded (subscribes to IVehicle::subscribe())
       ↓
safemode_library (polls safemoded via getCurrentData())
       ↓
SafeModeDemo app (Compose UI updates)
```

On RPi5 during development, there is no CAN bus and no real vehicle. The VHAL
emulator (`android.hardware.automotive.vehicle`) runs but publishes mostly static
default values — speed stays at 0, gear stays at PARK.

## The inject-vhal-event Dead End

The first instinct is to use AOSP's built-in `inject-vhal-event` command:

```bash
adb shell cmd car_service inject-vhal-event 0x11600207 0 5.0
```

This works for testing `CarPropertyService` and apps that use `Car.createCar()` and
`CarPropertyManager`. **But it does NOT work for safemoded.**

`inject-vhal-event` writes directly into `CarPropertyService`'s internal property cache.
It never calls `IVehicle::setValues()` at the HAL layer. safemoded connects directly to
`IVehicle::subscribe()` — it completely bypasses `CarPropertyService`. So injected events
never reach safemoded.

```
inject-vhal-event
       ↓
  CarPropertyService cache  ← DEAD END for safemoded
       ✗ (never reaches IVehicle HAL layer)

safemoded ← IVehicle::subscribe() ← VHAL HAL ← (needs real sensor or direct file)
```

## The SAFEMODE_SIM_MODE Solution

Instead of trying to reach safemoded through `inject-vhal-event`, we bring the data
directly to safemoded. safemoded is compiled with `#ifdef SAFEMODE_SIM_MODE` which
activates a simulator thread that reads from a file:

```
/data/local/tmp/safemode_sim.txt
```

The Python simulator writes one line to this file:
```
<speed_ms> <gear_int> <fuel_ml>
```

safemoded's simulator thread polls the file every 500ms, parses the values, updates
`mCurrentData`, and calls `dispatchToCallbacks()` — the exact same path as a real VHAL
event. The data then flows up to the library and app as normal.

This is the cleanest possible design: the simulator is a data source, not a mock.

---

<a name="part-7"></a>
# Part 7: Directory Structure — Every File Explained

## simulate_all.py — The Primary Tool

**Use this for:** Normal development and demonstration. Sends all three signals
(speed + gear + fuel) as a coordinated unit, cycling through all 20 steps of the
drive cycle. This is the tool you run to see the app UI change.

```bash
python3 simulate_all.py             # 20 steps × 5s = 100s total
python3 simulate_all.py --loop      # repeat continuously until Ctrl+C
python3 simulate_all.py --interval 2  # faster cycle (2s per step)
```

## simulate_speed.py — Single-Signal Speed Tool

**Use this for:** Testing the SafeMode state machine in isolation. When you only want
to vary speed and hold gear/fuel constant. Also useful for testing the speed threshold
boundaries precisely (exactly 1.389 m/s = 5 km/h, exactly 4.167 m/s = 15 km/h).

Uses `inject-vhal-event` via `cmd car_service` — note this reaches `CarPropertyService`,
not safemoded directly. Useful for testing the CarPropertyService layer separately.

```bash
python3 simulate_speed.py --interval 2.0
python3 simulate_speed.py --device 192.168.1.100:5555  # wireless adb
```

## simulate_gear.py — Single-Signal Gear Tool

**Use this for:** Testing how the UI displays gear changes (PARK/NEUTRAL/REVERSE/DRIVE).
The Gear display in the app should update immediately when the simulator runs.

Correctly handles `CURRENT_GEAR`'s `ON_CHANGE` semantics: logs a note when the same
gear is injected twice in a row (no VHAL event will fire for the duplicate).

```bash
python3 simulate_gear.py --interval 3.0
```

## simulate_fuel.py — Single-Signal Fuel Tool

**Use this for:** Testing the fuel display card in the app. The RPi5 VHAL emulator
does not support `FUEL_LEVEL` (returns `INVALID_ARG`), so safemoded always shows 0.
This script can send fuel values through CarPropertyService if needed for UI testing.

Includes fuel level thresholds (LOW_FUEL at 5L, CRITICAL at 2L) displayed in the output.

```bash
python3 simulate_fuel.py --interval 2.0
```

## full_pipeline_test.sh — Automated Validation Script

**Use this for:** Verifying the full VHAL→safemoded→callback pipeline is working after
a new build or code change. Runs all three simulators in sequence while `safemode_client`
listens in subscribe mode, then checks that at least 5 callbacks were received and that
`HARD_SAFE_MODE` was triggered. Prints PASS/FAIL for each stage.

```bash
chmod +x full_pipeline_test.sh
./full_pipeline_test.sh                        # first connected device
./full_pipeline_test.sh 192.168.1.100:5555    # specific device
```

## data/ directory — The Data Files

The canonical data files used by `simulate_all.py`. Each has 20 values, one per line,
with `#` comments explaining the format and thresholds.

The three files are designed to be **synchronized**: index 0 across all three files
represents the same moment in the drive cycle. Step 1 is parked with full tank at 0 km/h.
Step 9 is highway speed in DRIVE with tank at 80%.

---

<a name="part-8"></a>
# Part 8: Data Files — The Input to Every Script

## speed_data.txt — The Drive Cycle

```
# Vehicle speed values in metres per second (m/s)
# SafeModeState thresholds:
#   NO_SAFE_MODE:     speed < 1.389 m/s  (< 5 km/h)
#   NORMAL_SAFE_MODE: 1.389 – 4.167 m/s  (5 – 15 km/h)
#   HARD_SAFE_MODE:   speed > 4.167 m/s  (> 15 km/h)
```

| Step | m/s | km/h | SafeMode Zone |
|------|-----|------|---------------|
| 01 | 0.00 | 0.0 | NO_SAFE_MODE |
| 02 | 0.50 | 1.8 | NO_SAFE_MODE |
| 03 | 1.00 | 3.6 | NO_SAFE_MODE |
| 04 | 1.39 | 5.0 | NORMAL_SAFE_MODE (threshold) |
| 05 | 2.00 | 7.2 | NORMAL_SAFE_MODE |
| 06 | 2.78 | 10.0 | NORMAL_SAFE_MODE |
| 07 | 3.50 | 12.6 | NORMAL_SAFE_MODE |
| 08 | 4.17 | 15.0 | HARD_SAFE_MODE (threshold) |
| 09 | 5.00 | 18.0 | HARD_SAFE_MODE |
| 10 | 6.94 | 25.0 | HARD_SAFE_MODE |
| 11 | 8.33 | 30.0 | HARD_SAFE_MODE |
| 12 | 9.72 | 35.0 | HARD_SAFE_MODE |
| 13 | 11.11 | 40.0 | HARD_SAFE_MODE |
| 14 | 12.50 | 45.0 | HARD_SAFE_MODE |
| 15 | 13.89 | 50.0 | HARD_SAFE_MODE (peak) |
| 16 | 11.11 | 40.0 | HARD_SAFE_MODE |
| 17 | 8.33 | 30.0 | HARD_SAFE_MODE |
| 18 | 5.56 | 20.0 | HARD_SAFE_MODE |
| 19 | 2.78 | 10.0 | NORMAL_SAFE_MODE |
| 20 | 0.00 | 0.0 | NO_SAFE_MODE |

**Why this sequence?** It tests all three state transitions in both directions:
- NO → NORMAL (step 3→4)
- NORMAL → HARD (step 7→8)
- HARD → NORMAL (step 18→19)
- NORMAL → NO (step 19→20)

A sequence that only goes up would miss the deceleration path.

**Units are m/s, not km/h.** This matches the VHAL `PERF_VEHICLE_SPEED` property type
(FLOAT, m/s) and the `VehicleData.speedMs` field in the AIDL. Conversions to km/h
happen in the library layer (`VehicleInfo.speedKmh = speedMs * 3.6f`).

## gear_data.txt — VehicleGear Bitmask Integers

```
# Gear position values (VehicleGear bitmask integers)
# Constants (from android.hardware.automotive.vehicle.VehicleGear):
#   1 = NEUTRAL   2 = REVERSE   4 = PARK   8 = DRIVE
```

The 20-step gear sequence mirrors a realistic drive cycle:

```
4 4   → parked (steps 1-2)
1     → neutral (step 3, shifting)
2 2   → reverse (steps 4-5, backing out)
1     → neutral (step 6, shifting)
8 × 12 → drive (steps 7-18, driving)
1     → neutral (step 19, approaching park)
4 4   → park (steps 20, parked again)
```

**Why bitmask integers?** VehicleGear in VHAL is a bitmask enum defined in
`hardware/interfaces/automotive/vehicle/aidl/.../VehicleGear.aidl`. The values are:

```java
GEAR_UNKNOWN = 0x0000,  // 0
GEAR_NEUTRAL = 0x0001,  // 1
GEAR_REVERSE = 0x0002,  // 2
GEAR_PARK    = 0x0004,  // 4
GEAR_DRIVE   = 0x0008,  // 8
GEAR_1       = 0x0010,  // 16
GEAR_2       = 0x0020,  // 32
// ...
```

These are the integer values injected via `inject-vhal-event` and stored in
`VehicleData.gear`. `VehicleGear.name(gear)` in the library maps them to
human-readable strings (PARK, DRIVE, etc.) for display.

## fuel_data.txt — Millilitres

```
# Fuel level in millilitres (full tank = 50000 ml = 50 litres)
# Gradually decreasing across the 20 steps of the drive cycle.
# Percentage = (value / 50000) * 100
```

20 values from 50000 ml (full) down to 31000 ml (62%), decreasing by 1000 ml per step.
This models fuel consumption during the drive cycle.

**Unit is millilitres** — matching the `FUEL_LEVEL` VHAL property definition which uses
millilitres (not litres or percentage). The library converts to litres:
`VehicleInfo.fuelLevelL = fuelLevelMl / 1000f`.

---

<a name="part-9"></a>
# Part 9: simulate_speed.py — Detailed Walkthrough

## What It Does

Reads `data/speed_data.txt` line by line and for each value:
1. Calculates the km/h equivalent
2. Determines the expected SafeMode state (mirrors `SafeModeState.kt` thresholds)
3. Injects the value via `adb shell cmd car_service inject-vhal-event`
4. Prints a formatted status line
5. Sleeps `--interval` seconds (default 2.0s)

## Key Constants

```python
PROP_SPEED = "0x11600207"   # PERF_VEHICLE_SPEED — hex property ID
AREA_ID    = "0"            # global area (not per-wheel or per-seat)
```

These constants come directly from the VHAL AIDL definition. `0x11600207` is the
stable ID across all AOSP versions for `PERF_VEHICLE_SPEED`. Using the hex ID directly
avoids having to generate or include `VehicleProperty.h`.

## The inject_vhal_event() Function

```python
def inject_vhal_event(prop, area, value, device):
    cmd = ["adb"]
    if device:
        cmd += ["-s", device]
    cmd += ["shell", "cmd", "car_service", "inject-vhal-event", prop, area, value]
    result = subprocess.run(cmd, capture_output=True, text=True)
    ...
```

This wraps `adb shell cmd car_service inject-vhal-event`. The function:
- Builds the command as a list (not a string) — avoids shell injection
- Supports a `--device` serial for multi-device setups
- Captures stdout/stderr to detect silent failures
- Returns True/False — the caller decides whether to abort

## The get_state_label() Function

```python
def get_state_label(speed_ms: float) -> str:
    kmh = speed_ms * 3.6
    if kmh >= 15.0:
        return "HARD_SAFE_MODE   🔴"
    elif kmh >= 5.0:
        return "NORMAL_SAFE_MODE 🟡"
    else:
        return "NO_SAFE_MODE     🟢"
```

This **mirrors** the logic in `SafeModeState.kt`:

```kotlin
fun fromVehicleInfo(info: VehicleInfo): SafeModeState {
    val kmh = info.speedMs * 3.6f
    return when {
        kmh >= HARD_KMH   -> HARD_SAFE_MODE    // 15 km/h
        kmh >= NORMAL_KMH -> NORMAL_SAFE_MODE  //  5 km/h
        else              -> NO_SAFE_MODE
    }
}
```

**Why duplicate this logic in the simulator?** So the terminal output shows what
state the app *should* display. If the simulator says `HARD_SAFE_MODE` but the
app shows `NORMAL_SAFE_MODE`, there is a bug to investigate. The simulator output
is your ground truth.

## Sample Output

```
Simulating 20 speed values at 2.0s interval
Property: PERF_VEHICLE_SPEED (0x11600207), Area: 0
------------------------------------------------------------
[01/20] Injecting   0.00 m/s =   0.00 km/h  →  NO_SAFE_MODE     🟢
[02/20] Injecting   0.50 m/s =   1.80 km/h  →  NO_SAFE_MODE     🟢
[03/20] Injecting   1.00 m/s =   3.60 km/h  →  NO_SAFE_MODE     🟢
[04/20] Injecting   1.39 m/s =   5.00 km/h  →  NORMAL_SAFE_MODE 🟡
[05/20] Injecting   2.00 m/s =   7.20 km/h  →  NORMAL_SAFE_MODE 🟡
[08/20] Injecting   4.17 m/s =  15.01 km/h  →  HARD_SAFE_MODE   🔴
...
[15/20] Injecting  13.89 m/s =  50.00 km/h  →  HARD_SAFE_MODE   🔴
...
[20/20] Injecting   0.00 m/s =   0.00 km/h  →  NO_SAFE_MODE     🟢
------------------------------------------------------------
Speed simulation complete.
```

---

<a name="part-10"></a>
# Part 10: simulate_gear.py — Detailed Walkthrough

## What Makes Gear Different: ON_CHANGE Semantics

Speed (`PERF_VEHICLE_SPEED`) is a CONTINUOUS property — VHAL publishes it at a fixed
rate (typically 1 Hz) regardless of whether the value changed. Gear (`CURRENT_GEAR`)
is ON_CHANGE — VHAL only publishes an event when the gear actually changes.

This has an important consequence for the simulator: **injecting the same gear value
twice will not fire a second VHAL event**. The VHAL emulator deduplicates consecutive
identical values for ON_CHANGE properties.

`simulate_gear.py` tracks this:

```python
prev_gear = None
for i, gear_code in enumerate(gears, start=1):
    changed = (gear_code != prev_gear)
    change_note = "" if changed else " (same as previous — no VHAL event expected)"
    print(f"[{i:02d}/{len(gears)}] Gear: {gear_code:5d}  ({name}){change_note}")
    inject_vhal_event(...)
    prev_gear = gear_code
```

This gives you an accurate picture: if you see "no VHAL event expected" in the output
and the safemode_client shows no corresponding callback — that's correct behavior,
not a bug.

## The GEAR_NAMES Dictionary

```python
GEAR_NAMES = {
    1:    "NEUTRAL",
    2:    "REVERSE",
    4:    "PARK",
    8:    "DRIVE",
    16:   "GEAR_1",
    32:   "GEAR_2",
    64:   "GEAR_3",
    # ...
}
```

This mirrors the `VehicleGear` constants from:
```
hardware/interfaces/automotive/vehicle/aidl/android/hardware/automotive/vehicle/VehicleGear.aidl
```

Duplicating the constants in the simulator is intentional — the tool must not depend on
building or parsing AIDL files. The constants are stable across AOSP versions.

---

<a name="part-11"></a>
# Part 11: simulate_fuel.py — Detailed Walkthrough

## The RPi5 VHAL Limitation

The RPi5 VHAL emulator does not support `FUEL_LEVEL` (property ID `0x45100004`). When
safemoded tries to subscribe to it during `connectToVhal()`, the emulator returns
`INVALID_ARG`.

This is why safemoded marks `FUEL_LEVEL` as `required = false` in its subscription loop:

```cpp
PropSubscription props[] = {
    { "SPEED", speedOpts, true  },   // fatal if VHAL rejects
    { "GEAR",  gearOpts,  true  },   // fatal if VHAL rejects
    { "FUEL",  fuelOpts,  false },   // optional — RPi5 doesn't support this
};
```

What does `simulate_fuel.py` actually test then? It tests that `CarPropertyService`
correctly receives and caches fuel level updates. If you have an app layer that reads
fuel via `CarPropertyManager` (instead of through safemoded), this simulator is useful
for that layer. For safemoded specifically, fuel data arrives only through the
SAFEMODE_SIM_MODE file mechanism.

## Fuel Level Thresholds in the Tool

```python
TANK_CAPACITY_ML  = 50_000.0   # 50 litres
LOW_FUEL_ML       =  5_000.0   # 5 litres
CRITICAL_FUEL_ML  =  2_000.0   # 2 litres
```

These are informational — the service itself does not implement fuel warnings. But
displaying them in the simulator output makes it easy to see when the test scenario
crosses a threshold that a future fuel-warning feature would care about.

---

<a name="part-12"></a>
# Part 12: simulate_all.py — The Combined Orchestrator

`simulate_all.py` is the **primary tool** — the one you use day-to-day.

## What It Does Differently From the Single-Signal Scripts

The three single-signal scripts (`simulate_speed.py`, `simulate_gear.py`,
`simulate_fuel.py`) inject one signal at a time using `inject-vhal-event`. They
reach `CarPropertyService` but NOT safemoded directly.

`simulate_all.py` takes a completely different approach: it writes one line containing
all three values to the sim file:

```bash
adb shell "echo '4.17 8 45000' > /data/local/tmp/safemode_sim.txt"
```

safemoded's `runSimulatorThread()` reads this file every 500ms and updates
`mCurrentData` atomically for all three fields. This is the path that actually
reaches safemoded and propagates to the library and app.

## Coordinated vs Independent Signals

With the single-signal scripts, you might have:
- Speed = 15 km/h (from simulate_speed.py)
- Gear = PARK (still at default, not updated)

This is an impossible vehicle state (driving at 15 km/h in PARK). It can confuse
the UI and generate misleading test data.

With `simulate_all.py`, all three signals update together in lockstep — step N
has a consistent speed, gear, and fuel level that represents a coherent vehicle state.

## The Output Table

```
  Step   Speed      Gear      Fuel%    SafeModeState
  --------------------------------------------------------
  [01/20]   0.0 km/h  PARK      100.0%  → NO_SAFE_MODE
  [02/20]   1.8 km/h  PARK       98.0%  → NO_SAFE_MODE
  [03/20]   3.6 km/h  NEUTRAL    96.0%  → NO_SAFE_MODE
  [04/20]   5.0 km/h  REVERSE    94.0%  → NORMAL_SAFE_MODE
  ...
  [09/20]  18.0 km/h  DRIVE      80.0%  → HARD_SAFE_MODE
  ...
  [20/20]   0.0 km/h  PARK       62.0%  → NO_SAFE_MODE
```

Each column maps to a UI element in `SafeModeDemo`:
- **Speed** → SpeedCard value
- **Gear** → GearCard value
- **Fuel%** → FuelCard value (as litres in the app)
- **SafeModeState** → the big animated card (green/yellow/red)

This output IS your test oracle: what the terminal shows is exactly what the app
should display, with a latency of at most 200ms (SafeModeManager's poll interval).

## Ctrl+C Behavior — Safety Reset

```python
except KeyboardInterrupt:
    print("\nStopped. Resetting to PARK, 0 km/h, full tank.")
    subprocess.run(
        ["adb", "shell", f"echo '0.0 4 50000' > {SIM_FILE}"],
        capture_output=True
    )
```

When you press Ctrl+C mid-simulation, the app might be showing `HARD_SAFE_MODE`
(red card). The tool resets to parked + 0 speed + full tank so the app returns to
`NO_SAFE_MODE` (green). Without this, the device would be stuck showing a red card
until the next simulator run.

---

<a name="part-13"></a>
# Part 13: full_pipeline_test.sh — Automated End-to-End Validation

## Purpose

This script is used to validate that the complete VHAL→safemoded→callback pipeline
is functioning after:
- A new build and flash
- A code change to safemoded
- A new device being set up
- After debugging a connection issue

## The 10-Step Test Plan

```
Step 0: ADB connectivity check
Step 1: Verify safemoded process is running
Step 2: Verify service registered in ServiceManager
Step 3: Test getCurrentData() synchronous call (snapshot mode)
Step 4: Start safemode_client in subscribe mode (background listener)
Step 5: Run simulate_speed.py (speed injection)
Step 6: Run simulate_gear.py (gear injection)
Step 7: Run simulate_fuel.py (fuel injection)
Step 8: Read callback log — count events received
Step 9: Verify HARD_SAFE_MODE was triggered
Step 10: Show recent safemoded logcat
```

Each step prints `✔ PASS:` or `✘ FAIL:` in color. If any step fails, the script
exits immediately with a non-zero return code — making it suitable for CI pipelines.

## Reading the Test Output

```bash
./full_pipeline_test.sh

━━ 0. ADB connectivity check
✔ PASS: Device reachable

━━ 1. Verify safemoded is running
✔ PASS: safemoded process found

━━ 2. Verify service is registered with ServiceManager
✔ PASS: Service registered in ServiceManager

━━ 3. Snapshot test — synchronous getCurrentData()
Running: safemode_client snapshot
Current data: Speed=0.00 m/s  Gear=4  Fuel=50000.0 ml
✔ PASS: getCurrentData() returned vehicle data

━━ 4. Subscribe mode — open callback listener in background
Starting safemode_client in subscribe mode for 90 seconds...

━━ 5. Inject speed values (all three SafeMode zones)
[01/20] Injecting 0.00 m/s ... → NO_SAFE_MODE 🟢
...
✔ PASS: Speed injection complete

...

━━ 8. Read callback log — verify events arrived
Events received: 18
✔ PASS: Received 18 callback events (expected ≥ 5)

━━ 9. SafeMode state transitions
✔ PASS: HARD_SAFE_MODE state observed in callbacks

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
  Full pipeline test PASSED
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

## When Step 9 Is Expected to Not Detect HARD_SAFE_MODE

In `SAFEMODE_SIM_MODE`, speed data flows through the file, not through
`inject-vhal-event`. So `full_pipeline_test.sh` (which uses the inject-based
single-signal scripts) will NOT trigger callbacks in safemoded. In this case:
- Steps 1–3 will pass (service is up, snapshot works)
- Steps 5–7 will show success from inject-vhal-event
- Steps 8–9 may show 0 callback events from safemode_client

This is expected and correct — it confirms the inject path works for CarPropertyService,
while the file-based path (via `simulate_all.py`) works for safemoded.

Use `full_pipeline_test.sh` to validate the service connectivity. Use `simulate_all.py`
to validate the full end-to-end stack including the app UI.

---

<a name="part-14"></a>
# Part 14: The Two Modes — File-Based vs inject-vhal-event

Understanding which tool reaches which layer is critical:

## Mode 1: File-Based Simulator (SAFEMODE_SIM_MODE)

**Tool:** `simulate_all.py`

**Data path:**
```
simulate_all.py
  → writes to /data/local/tmp/safemode_sim.txt (via adb shell echo)
    → safemoded runSimulatorThread() reads file every 500ms
      → mCurrentData updated
        → dispatchToCallbacks() called
          → safemode_client callbacks fire
            → SafeModeManager polling reads getCurrentData() every 200ms
              → SafeModeDemo app UI updates
```

**Reaches:** safemoded and everything above it (library, app)

**Requires:** safemoded compiled with `-DSAFEMODE_SIM_MODE` in Android.bp cflags

**Does NOT reach:** CarPropertyService, CarDrivingStateService, other AAOS components

## Mode 2: inject-vhal-event (CarPropertyService path)

**Tools:** `simulate_speed.py`, `simulate_gear.py`, `simulate_fuel.py`

**Data path:**
```
simulate_speed.py
  → adb shell cmd car_service inject-vhal-event 0x11600207 0 5.0
    → CarPropertyService internal cache updated
      → CarDrivingStateService reads speed → updates driving state
        → CarPackageManagerService may block/unblock apps
          → Apps using CarPropertyManager get updates
```

**Reaches:** CarPropertyService and everything that subscribes to it (NOT safemoded)

**Requires:** AAOS car lunch target (`myoem_rpi5_car`), CarService running

**Does NOT reach:** safemoded's VHAL subscriber (it bypasses IVehicle HAL layer)

## Quick Reference

| What you want to test | Tool to use |
|---|---|
| SafeModeDemo app UI changes color | `simulate_all.py` |
| SafeModeManager poll log shows state changes | `simulate_all.py` |
| safemode_client receives callbacks from safemoded | `simulate_all.py` |
| safemoded processes data correctly | `simulate_all.py` |
| CarPropertyService receives speed update | `simulate_speed.py` |
| CarDrivingStateService changes driving state | `simulate_speed.py` |
| AAOS UX restrictions trigger/release | `simulate_speed.py` |
| Full automated go/no-go after a new build | `full_pipeline_test.sh` |
| Only vary speed, hold gear/fuel fixed | `simulate_speed.py` |
| Test gear display in the app UI | `simulate_all.py` (with SAFEMODE_SIM_MODE) |

---

<a name="part-15"></a>
# Part 15: Which Script Do I Run and When?

## Daily Development Workflow

```bash
# 1. Start the device and confirm safemoded is running
adb shell ps -eZ | grep safemoded

# 2. Open two terminals:

# Terminal 1 — watch logs
adb logcat -s SafeModeManager safemoded -v time

# Terminal 2 — run the simulator
cd vendor/myoem/tools/safemode-simulator
python3 simulate_all.py --interval 3 --loop
# (Ctrl+C to stop)
```

## Testing a Specific Speed Threshold

```bash
# Test exactly at the NORMAL/HARD boundary (15 km/h = 4.167 m/s)
# Write it directly to the sim file:
adb shell "echo '4.17 8 45000' > /data/local/tmp/safemode_sim.txt"
sleep 1
# App should show HARD_SAFE_MODE (red)

adb shell "echo '4.16 8 45000' > /data/local/tmp/safemode_sim.txt"
sleep 1
# App should show NORMAL_SAFE_MODE (yellow)
```

## After a New Build (Validate Everything Works)

```bash
# Push new binaries
adb push out/target/product/rpi5/vendor/bin/safemoded /vendor/bin/safemoded
adb shell stop safemoded && adb shell start safemoded

# Run the automated test
./full_pipeline_test.sh
```

## Demonstrating the App to Someone

```bash
# Full loop, slow pace, so they can see each state
python3 simulate_all.py --loop --interval 5
```

## Isolating a Bug in One Layer

```bash
# Bug: "app UI is not updating" — is it the service or the library?

# Step 1: Test service directly (bypass library and app)
adb shell /vendor/bin/safemode_client subscribe 60
# In another terminal:
adb shell "echo '5.0 8 45000' > /data/local/tmp/safemode_sim.txt"
# safemode_client should print a callback event

# If safemode_client gets events but app doesn't → bug is in the library
# If safemode_client gets no events → bug is in safemoded or the sim file
```

---

<a name="part-16"></a>
# Part 16: Design Decisions — Why It Was Built This Way

## Why Python Instead of Shell Scripts?

Shell scripts become hard to read quickly when they need:
- Argument parsing (`--interval`, `--device`)
- Number arithmetic (convert m/s to km/h)
- Readable formatted output
- Error handling that distinguishes success from different failure modes

Python handles all of these cleanly. It is available on every developer machine
(macOS, Linux, Windows WSL) without installation. The scripts use only the standard
library (`subprocess`, `time`, `argparse`, `os`, `sys`) — no pip installs required.

## Why Text Data Files Instead of Hardcoded Values?

```python
speeds = load_data("data/speed_data.txt")
```

Rather than:
```python
speeds = [0.0, 0.5, 1.0, 1.39, 2.0, 2.78, 3.5, ...]  # hardcoded
```

Text files separate **what to test** from **how to test**. Benefits:
- You can change the test scenario without touching code
- You can have multiple data files for different test scenarios
  (e.g., `speed_highway.txt`, `speed_urban.txt`, `speed_parking.txt`)
- Non-programmers can edit data files to create new test scenarios
- Data files have `#` comment support so you can annotate what each value represents

## Why Skip FUEL_LEVEL Gracefully Instead of Removing It?

The `FUEL_LEVEL` property exists in the production VHAL spec. The RPi5 emulator doesn't
support it, but a real vehicle will. The simulator was designed to work in both contexts:

- On RPi5 dev: safemoded skips FUEL subscription (`required=false`), simulator sends
  fuel via the file. Service compiles with FUEL handling.
- In production: safemoded subscribes to real FUEL_LEVEL from VHAL. No simulator needed.

Removing FUEL from the simulator would make the test unrealistic. Skipping gracefully
keeps the dev and production code paths aligned.

## Why Does simulate_all.py Use `adb shell echo` Instead of adb push?

```python
result = subprocess.run(
    ["adb", "shell", f"echo '{line}' > {SIM_FILE}"],
    ...
)
```

`adb push` transfers a file from the host to the device. For this simulator, we
generate the line content on the fly (from the data files on the host) and write
it directly to the device in one command — no local temp file needed. This is
also faster for a one-line write.

`adb push` would be appropriate if the data file was large or binary. For a single
text line every 5 seconds, `adb shell echo` is cleaner.

---

<a name="part-17"></a>
# Part 17: Moving From Dev Tools to Production — The Clean Exit

The entire simulator design is built around a single principle:

> **The dev path and the production path run identical code. Only the data source changes.**

## The Compile-Time Switch

```cpp
// In vendor/myoem/services/safemode/src/main.cpp:

#ifdef SAFEMODE_SIM_MODE
    service->startSimulator();   // reads /data/local/tmp/safemode_sim.txt
#else
    service->connectToVhal();    // subscribes to IVehicle::subscribe()
#endif
```

Both paths call the same `mCurrentData` update and `dispatchToCallbacks()` code.
The service behavior is identical to the library and app — they cannot tell which
data source is active.

## Removing the Flag — The One-Line Production Migration

```bp
// vendor/myoem/services/safemode/Android.bp
cc_binary {
    name: "safemoded",
    cflags: [
        "-Wall", "-Wextra", "-Werror",
        // RPi5 dev mode: REMOVE this line for production
        "-DSAFEMODE_SIM_MODE",
    ],
}
```

To go to production:
1. Delete the line `"-DSAFEMODE_SIM_MODE",` from Android.bp
2. Run `m safemoded`
3. Flash or push to device

That is the entire migration. No other files change.

## What Stays and What Goes

| Component | In dev | In production |
|---|---|---|
| `simulate_all.py` | Used daily | Not needed |
| `full_pipeline_test.sh` | Used after builds | Can be adapted for CI |
| `data/*.txt` | Used by simulator | Not needed on device |
| `-DSAFEMODE_SIM_MODE` flag | In Android.bp | Removed |
| `startSimulator()` / `runSimulatorThread()` | Compiled in | Compiled out (ifdef) |
| `connectToVhal()` | Compiled in (not called) | Called at startup |
| Everything else | Unchanged | Unchanged |

The tools directory itself is never installed on the device. It lives only in the
source tree and is never referenced in `PRODUCT_PACKAGES`. It is invisible to the
build system unless you explicitly run a script from it.

---

## Summary: The safemode-simulator at a Glance

```
vendor/myoem/tools/safemode-simulator/
│
├── simulate_all.py        ← USE THIS daily. Writes to safemode_sim.txt.
│                            All 3 signals together. Reaches safemoded → lib → app.
│
├── simulate_speed.py      ← Use when testing CarPropertyService speed layer only.
├── simulate_gear.py       ← Use when testing ON_CHANGE gear events via CarService.
├── simulate_fuel.py       ← Use when testing fuel display via CarPropertyService.
│
├── full_pipeline_test.sh  ← Run after every build. Automated PASS/FAIL.
│
└── data/
    ├── speed_data.txt     ← 20 m/s values. Full drive cycle. All 3 state zones.
    ├── gear_data.txt      ← 20 VehicleGear codes. Mirrors speed sequence.
    └── fuel_data.txt      ← 20 fuel levels in ml. Decreasing. 50L → 31L.
```

**One command to test the full stack:**
```bash
python3 vendor/myoem/tools/safemode-simulator/simulate_all.py --loop --interval 3
```

**One command to validate after a new build:**
```bash
vendor/myoem/tools/safemode-simulator/full_pipeline_test.sh
```

---

*This article covers `vendor/myoem/tools/safemode-simulator/` as of Android 15 (`android-15.0.0_r14`) on Raspberry Pi 5 AAOS.*
*The tool pattern described here is applicable to any AOSP vendor component that needs hardware-free development testing.*
