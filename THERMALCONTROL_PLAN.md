# ThermalControl Feature — Complete Implementation Plan

**Project:** `vendor/myoem/thermalcontrol`
**Target:** Raspberry Pi 5 · AOSP Android 15 (`android-15.0.0_r14`)
**Features:** CPU temperature display, fan speed display, fan on/off control, fan speed control
**Date:** March 2026

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Android App (Kotlin/Compose)                  │
│                    vendor/myoem/apps/ThermalMonitor              │
└────────────────────────────┬────────────────────────────────────┘
                             │ uses ThermalControlManager (Java)
┌────────────────────────────▼────────────────────────────────────┐
│              ThermalControlManager.java (Java Manager)           │
│              vendor/myoem/libs/thermalcontrol                    │
└────────────────────────────┬────────────────────────────────────┘
                             │ AIDL binder IPC (/dev/binder)
                             │ libbinder_ndk · LLNDK
┌────────────────────────────▼────────────────────────────────────┐
│            thermalcontrold (Binder Service daemon)               │
│            vendor/myoem/services/thermalcontrol                  │
└────────────────────────────┬────────────────────────────────────┘
                             │ C++ function calls (linked library)
┌────────────────────────────▼────────────────────────────────────┐
│         ThermalControlHal (C++ Shared Library)                   │
│         vendor/myoem/hal/thermalcontrol                          │
└────────────────────────────┬────────────────────────────────────┘
                             │ sysfs read / write
┌────────────────────────────▼────────────────────────────────────┐
│                  Linux Kernel (thermal + pwm-fan)                │
│  /sys/class/thermal/thermal_zone0/temp                           │
│  /sys/class/hwmon/hwmon<N>/pwm1          (0–255 PWM value)       │
│  /sys/class/hwmon/hwmon<N>/pwm1_enable   (0=off 1=manual 2=auto)│
│  /sys/class/hwmon/hwmon<N>/fan1_input    (RPM)                   │
└────────────────────────────┬────────────────────────────────────┘
                             │
                    CPU sensor · Fan hardware
```

### Architectural Decision: HAL as Shared Library

The HAL is implemented as a **C++ shared library** (`libthermalcontrolhal`) rather than a
separate daemon process. This is a valid, production-used pattern (called "passthrough mode"
in HIDL, and still applicable for custom OEM HALs in AIDL era).

**Why not a separate HAL daemon?**
- The hardware interface is simple sysfs reads/writes — no hardware multiplexing needed.
- A separate process would add latency, IPC overhead, and another SELinux domain for
  no real benefit at this complexity level.
- All real work (sysfs I/O) happens in the service process, which already runs as `system`.

**The "HAL layer" is still a real architectural boundary:**
- Defined by a pure virtual C++ interface (`IThermalControlHal.h`)
- Completely swappable (e.g., for a different board with different sysfs paths)
- Unit-testable independently with mock substitution
- The service knows nothing about sysfs — it only calls the HAL interface

---

## Directory Structure (Final State)

```
vendor/myoem/
├── hal/
│   └── thermalcontrol/
│       ├── Android.bp
│       ├── include/
│       │   └── thermalcontrol/
│       │       └── IThermalControlHal.h        ← Pure virtual C++ HAL interface
│       └── src/
│           ├── ThermalControlHal.h             ← Concrete implementation header
│           ├── ThermalControlHal.cpp           ← Reads/writes sysfs
│           └── SysfsHelper.cpp                 ← Low-level sysfs file I/O
│
├── services/
│   └── thermalcontrol/
│       ├── Android.bp
│       ├── thermalcontrold.rc
│       ├── aidl/com/myoem/thermalcontrol/
│       │   └── IThermalControlService.aidl     ← Service AIDL interface
│       ├── src/
│       │   ├── ThermalControlService.h
│       │   ├── ThermalControlService.cpp       ← Calls HAL, implements AIDL
│       │   └── main.cpp
│       ├── test/
│       │   └── thermalcontrol_client.cpp       ← CLI test client
│       └── sepolicy/private/
│           ├── thermalcontrold.te
│           ├── service.te
│           ├── file_contexts
│           └── service_contexts
│
├── libs/
│   └── thermalcontrol/
│       ├── Android.bp
│       └── java/com/myoem/thermalcontrol/
│           └── ThermalControlManager.java      ← Java wrapper for apps
│
└── apps/
    └── ThermalMonitor/
        ├── Android.bp
        ├── AndroidManifest.xml
        └── src/main/
            ├── kotlin/com/myoem/thermalmonitor/
            │   ├── MainActivity.kt             ← Single-activity Compose host
            │   ├── ThermalViewModel.kt         ← StateFlow-based ViewModel
            │   └── ui/
            │       ├── ThermalScreen.kt        ← Main composable
            │       └── theme/
            │           └── Theme.kt
            └── res/
                └── values/
                    └── strings.xml
```

---

## AIDL Interface Design

### `IThermalControlService.aidl`

```aidl
package com.myoem.thermalcontrol;

interface IThermalControlService {
    // ── Read operations ──────────────────────────────────────────────
    /** Returns CPU temperature in Celsius (e.g. 42.5) */
    float getCpuTemperatureCelsius();

    /** Returns current fan RPM. Returns -1 if tachometer not available. */
    int getFanSpeedRpm();

    /** Returns true if fan is currently running (PWM > 0). */
    boolean isFanRunning();

    /** Returns current fan speed as percentage 0–100. */
    int getFanSpeedPercent();

    /** Returns true if fan is in automatic (kernel-controlled) mode. */
    boolean isFanAutoMode();

    // ── Write operations ─────────────────────────────────────────────
    /** Turn fan fully on (100%) or fully off (0%), exits auto mode. */
    void setFanEnabled(boolean enabled);

    /**
     * Set fan speed manually. speedPercent: 0–100.
     * Exits auto mode and applies the specified PWM duty cycle.
     */
    void setFanSpeed(int speedPercent);

    /**
     * Hand control back to the kernel thermal governor.
     * In auto mode, setFanEnabled/setFanSpeed calls are ignored.
     */
    void setFanAutoMode(boolean autoMode);

    // ── Error codes ───────────────────────────────────────────────────
    const int ERROR_HAL_UNAVAILABLE  = 1; // sysfs path not found
    const int ERROR_INVALID_SPEED    = 2; // speedPercent out of 0–100
    const int ERROR_SYSFS_WRITE      = 3; // write to sysfs failed
    const int ERROR_IN_AUTO_MODE     = 4; // attempted manual control in auto mode
}
```

### `IThermalControlHal.h` (C++ pure virtual interface)

```cpp
#pragma once
#include <cstdint>

namespace myoem::thermalcontrol {

class IThermalControlHal {
public:
    virtual ~IThermalControlHal() = default;

    virtual float   getCpuTemperatureCelsius() = 0;
    virtual int32_t getFanSpeedRpm()           = 0;   // -1 if unavailable
    virtual int32_t getFanSpeedPercent()       = 0;   // 0–100
    virtual bool    isFanRunning()             = 0;
    virtual bool    isAutoMode()               = 0;

    virtual bool setFanEnabled(bool enabled)      = 0;
    virtual bool setFanSpeed(int32_t percent)     = 0;
    virtual bool setAutoMode(bool autoMode)       = 0;
};

} // namespace myoem::thermalcontrol
```

---

## sysfs Reference (Raspberry Pi 5)

| Property         | sysfs Path                                    | Notes                          |
|------------------|-----------------------------------------------|--------------------------------|
| CPU temperature  | `/sys/class/thermal/thermal_zone0/temp`       | millidegrees → divide by 1000  |
| Fan PWM value    | `/sys/class/hwmon/hwmon<N>/pwm1`              | 0–255; N discovered at runtime |
| Fan mode         | `/sys/class/hwmon/hwmon<N>/pwm1_enable`       | 0=off, 1=manual, 2=auto        |
| Fan RPM          | `/sys/class/hwmon/hwmon<N>/fan1_input`        | −1 if not wired                |

**hwmon index discovery:** The HAL scans `/sys/class/hwmon/hwmon*/` at init, looking for
the first entry that contains a `pwm1` file. This avoids hardcoding the index, which can
change across kernel versions or boot order.

---

## myoem_base.mk Additions Required

```makefile
# HAL shared library
PRODUCT_SOONG_NAMESPACES += vendor/myoem/hal/thermalcontrol

# Service daemon + test client
PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/thermalcontrol
PRODUCT_PACKAGES          += thermalcontrold thermalcontrol_client

# Java Manager
PRODUCT_SOONG_NAMESPACES += vendor/myoem/libs/thermalcontrol
PRODUCT_PACKAGES          += thermalcontrol-manager

# App
PRODUCT_SOONG_NAMESPACES += vendor/myoem/apps/ThermalMonitor
PRODUCT_PACKAGES          += ThermalMonitor

# SELinux
PRODUCT_PRIVATE_SEPOLICY_DIRS += vendor/myoem/services/thermalcontrol/sepolicy/private
```

---

## SELinux Policy Summary

```
# thermalcontrold.te
type thermalcontrold,      domain;
type thermalcontrold_exec, exec_type, vendor_file_type, file_type;

init_daemon_domain(thermalcontrold)
binder_use(thermalcontrold)
binder_service(thermalcontrold)
add_service(thermalcontrold, thermalcontrold_service)

# Allow reading CPU temperature sysfs
allow thermalcontrold sysfs_thermal:file r_file_perms;

# Allow reading/writing fan hwmon sysfs
allow thermalcontrold sysfs_hwmon:file rw_file_perms;
allow thermalcontrold sysfs_hwmon:dir  r_dir_perms;

# service.te
type thermalcontrold_service, app_api_service, service_manager_type;
```

> **Note:** `sysfs_thermal` and `sysfs_hwmon` are existing SELinux types in AOSP.
> If denials appear for a new type name, check with `adb logcat | grep "avc: denied"` and
> use the exact `tcontext` type shown in the denial to write the correct `allow` rule.

---

## Implementation Plan (Phase by Phase)

---

### Phase 1 — HAL Layer (`vendor/myoem/hal/thermalcontrol/`)

#### Checklist

- [x] **1.1** Create directory `vendor/myoem/hal/thermalcontrol/`
- [x] **1.2** Write `include/thermalcontrol/IThermalControlHal.h` — pure virtual C++ interface
- [x] **1.3** Write `src/SysfsHelper.h` + `src/SysfsHelper.cpp` — generic sysfs read/write helpers
  - `sysfsReadInt(path)` → reads integer from file, returns -1 on error
  - `sysfsReadFloat(path)` → reads float from file
  - `sysfsWriteInt(path, value)` → writes integer to file, returns bool success
  - `discoverHwmonPath()` → scans `/sys/class/hwmon/hwmon*/pwm1`, returns the dir path
- [x] **1.4** Write `src/ThermalControlHal.h` / `ThermalControlHal.cpp`
  - Constructor calls `discoverHwmonPath()`, stores result as member
  - All methods return safe defaults (`-1`, `false`) if path not found
  - `getCpuTemperatureCelsius()` → reads `thermal_zone0/temp`, divides by 1000.0f
  - `getFanSpeedPercent()` → reads `pwm1`, converts 0–255 → 0–100
  - `setFanSpeed(percent)` → converts 0–100 → 0–255, writes to `pwm1`, also writes `1` to `pwm1_enable`
  - `setAutoMode(true)` → writes `2` to `pwm1_enable`
  - `setFanEnabled(false)` → writes `0` to `pwm1`, writes `1` to `pwm1_enable`
- [x] **1.5** Write `Android.bp` for `cc_library_shared` named `libthermalcontrolhal`
  - `vendor: true`
  - `export_include_dirs: ["include"]`
  - No binder dependency (pure sysfs)

#### Test Strategy — Phase 1 (Before building anything)

```bash
# On device via adb shell, manually verify sysfs paths exist and work:
adb shell

# 1. CPU Temperature
cat /sys/class/thermal/thermal_zone0/temp
# Expected: a number like 45000 (= 45.0 °C)

# 2. Discover hwmon index
ls /sys/class/hwmon/
# Note which hwmon<N> has pwm1:
ls /sys/class/hwmon/hwmon0/
ls /sys/class/hwmon/hwmon1/   # try both

# 3. Read current fan mode
cat /sys/class/hwmon/hwmon0/pwm1_enable
# 2 = auto mode (kernel controls)

# 4. Read current PWM value
cat /sys/class/hwmon/hwmon0/pwm1
# e.g. 64 (= 25%)

# 5. Read fan RPM (if tachometer wired)
cat /sys/class/hwmon/hwmon0/fan1_input
# e.g. 1200  or  "no such file" if not wired

# 6. Manual fan control test
echo 1 > /sys/class/hwmon/hwmon0/pwm1_enable   # switch to manual
echo 255 > /sys/class/hwmon/hwmon0/pwm1          # fan full speed
echo 128 > /sys/class/hwmon/hwmon0/pwm1          # fan 50%
echo 0 > /sys/class/hwmon/hwmon0/pwm1            # fan off
echo 2 > /sys/class/hwmon/hwmon0/pwm1_enable   # back to auto
```

> If step 6 is denied by SELinux: `adb shell setenforce 0` temporarily for testing.
> The service SELinux policy will grant these permissions properly.

---

### Phase 2 — Binder Service (`vendor/myoem/services/thermalcontrol/`)

#### Checklist

- [x] **2.1** Create directory `vendor/myoem/services/thermalcontrol/`
- [x] **2.2** Write `aidl/com/myoem/thermalcontrol/IThermalControlService.aidl`
  - All 7 methods from the AIDL design above
  - 3 error constants (ERROR_HAL_UNAVAILABLE, ERROR_INVALID_SPEED, ERROR_SYSFS_WRITE)
- [x] **2.3** Write `Android.bp`
  - `aidl_interface` with `ndk` backend only (`cpp: false`)
  - `cc_binary` `thermalcontrold` with `vendor: true`, links `libthermalcontrolhal`
  - `cc_binary` `thermalcontrol_client` for testing
  - `soong_namespace` imports `vendor/myoem/hal/thermalcontrol`
- [x] **2.4** Write `src/ThermalControlService.h` — inherits `BnThermalControlService`
- [x] **2.5** Write `src/ThermalControlService.cpp`
  - Constructor creates HAL via `createThermalControlHal()` factory
  - Each AIDL method delegates to HAL, converts errors to `ScopedAStatus::fromServiceSpecificError`
  - `setFanSpeed`: validates 0–100, returns `ERROR_INVALID_SPEED` if bad
- [x] **2.6** Write `src/main.cpp` — standard 5-step binder service skeleton
  - Service name: `com.myoem.thermalcontrol.IThermalControlService`
- [x] **2.7** Write `thermalcontrold.rc` — `user system`, `group system`, `class main`
- [x] **2.8** Write SELinux policy (4 files in `sepolicy/private/`)
  - `thermalcontrold.te`: domain, exec, sysfs_thermal + sysfs_hwmon permissions
  - `service.te`: `app_api_service, service_manager_type`
  - `file_contexts`: `/vendor/bin/thermalcontrold`
  - `service_contexts`: `com.myoem.thermalcontrol.IThermalControlService`
- [x] **2.9** Write `test/thermalcontrol_client.cpp`
  - Commands: `temp`, `rpm`, `percent`, `running`, `auto_status`, `on`, `off`, `speed <N>`, `auto`, `manual`
  - Prints human-readable output with units and error names
- [x] **2.10** Add all three entries to `myoem_base.mk` (namespaces, packages, sepolicy)
- [x] **HAL factory** Added `createThermalControlHal()` to `IThermalControlHal.h` so service never needs concrete header

#### Build Commands

```bash
source build/envsetup.sh && lunch myoem_rpi5-trunk_staging-userdebug

# Build HAL library first
m libthermalcontrolhal

# Build service and client
m thermalcontrold thermalcontrol_client

# Flash only vendor partition
adb reboot bootloader
fastboot flash vendor out/target/product/rpi5/vendor.img
fastboot reboot
```

#### Test Strategy — Phase 2

```bash
# 1. Verify process is running
adb shell ps -e | grep thermalcontrol

# 2. Verify SELinux domain (must show u:r:thermalcontrold:s0, NOT init)
adb shell ps -eZ | grep thermalcontrold

# 3. Verify binary label
adb shell ls -laZ /vendor/bin/thermalcontrold

# 4. Verify service is registered
adb shell service list | grep thermalcontrol

# 5. Watch startup logs
adb logcat -s thermalcontrold

# 6. Run test client
adb shell thermalcontrol_client temp           # Get CPU temp
adb shell thermalcontrol_client rpm            # Get fan RPM
adb shell thermalcontrol_client speed 50       # Set fan to 50%
adb shell thermalcontrol_client on             # Fan full on
adb shell thermalcontrol_client off            # Fan off
adb shell thermalcontrol_client auto           # Return to auto
adb shell thermalcontrol_client speed 150      # Should print ERROR_INVALID_SPEED

# 7. Check SELinux denials
adb logcat | grep "avc: denied" | grep thermalcontrol
```

---

### Phase 3 — Java Manager (`vendor/myoem/libs/thermalcontrol/`)

#### Checklist

- [x] **3.1** Create directory structure `vendor/myoem/libs/thermalcontrol/java/com/myoem/thermalcontrol/`
- [x] **3.2** Write `Android.bp` as `java_library` (vendor:true, sdk_version:system_current)
- [x] **3.3** Write `ThermalControlManager.java`
  - `SERVICE_NAME = "com.myoem.thermalcontrol.IThermalControlService"`
  - Constructor: `ServiceManager.checkService(SERVICE_NAME)` → `IThermalControlService.Stub.asInterface()`
  - `isAvailable()` → returns false if binder is null (graceful degradation)
  - All 7 public methods mirroring the AIDL interface
  - Each method catches `RemoteException` and returns a safe sentinel (`-1`, `false`)
  - Static constants: `ERROR_HAL_UNAVAILABLE`, `ERROR_INVALID_SPEED`, `ERROR_SYSFS_WRITE`
  - Static utility: `categorizeTemperature(float celsius)` → returns `"Cool"/"Warm"/"Hot"/"Critical"`
  - Static utility: `temperatureColor(float celsius)` → returns Material color int
- [x] **3.4** Add namespace + packages to `myoem_base.mk`

#### Test Strategy — Phase 3

```bash
# Quick Java smoke test via adb shell (using service call)
adb shell service call com.myoem.thermalcontrol.IThermalControlService 1
# Parcel should show float value (CPU temperature as IEEE 754 float)

# Check logcat for any RemoteException from Manager side
adb logcat | grep ThermalControlManager
```

---

### Phase 4 — Android App (`vendor/myoem/apps/ThermalMonitor/`)

#### Checklist

- [x] **4.1** Create directory structure `vendor/myoem/apps/ThermalMonitor/`
- [x] **4.2** Write `Android.bp` as `android_app` — platform_apis, privileged, certificate:platform
- [x] **4.3** Write `AndroidManifest.xml` — package com.myoem.thermalmonitor, minSdk 34
- [x] **4.4** Write `ThermalViewModel.kt` — UiState, StateFlow, 2s poll loop, control methods, factory
- [x] **4.5** Write `ThermalScreen.kt` — Scaffold+TopAppBar, TemperatureCard, FanStatusCard, FanControlCard, AUTO badge
- [x] **4.6** Write `MainActivity.kt` — ServiceManager lookup, ViewModel factory, setContent
- [x] **4.7** Write `Theme.kt` — Material3 dark color scheme
- [x] **4.8** Write `strings.xml` + `styles.xml`
- [x] **4.9** Add to `myoem_base.mk`

#### App UI Layout Sketch

```
┌──────────────────────────────────┐
│  ThermalMonitor          [AUTO]  │
├──────────────────────────────────┤
│  ┌────────────────────────────┐  │
│  │       CPU Temperature      │  │
│  │         42.5 °C            │  │
│  │    ● Normal                │  │
│  └────────────────────────────┘  │
│  ┌────────────────────────────┐  │
│  │         Fan Status         │  │
│  │  Speed: 35%   RPM: 1200   │  │
│  └────────────────────────────┘  │
│  ┌────────────────────────────┐  │
│  │       Fan Control          │  │
│  │  [Turn On] [Turn Off] [Auto] │
│  │                            │  │
│  │  Set Speed:                │  │
│  │  ←────────●──────────→     │  │
│  │       35%                  │  │
│  │  [____35____] [Apply]      │  │
│  └────────────────────────────┘  │
└──────────────────────────────────┘
```

#### Build & Install App

```bash
m ThermalMonitor
adb reboot bootloader
fastboot flash vendor out/target/product/rpi5/vendor.img
fastboot reboot

# Or push directly without reflash (if partition is writable):
adb push out/target/product/rpi5/system/priv-app/ThermalMonitor/ThermalMonitor.apk \
    /system/priv-app/ThermalMonitor/
adb shell am force-stop com.myoem.thermalmonitor
adb shell pm install -r /system/priv-app/ThermalMonitor/ThermalMonitor.apk
adb shell am start -n com.myoem.thermalmonitor/.MainActivity
```

#### Test Strategy — Phase 4

```bash
# 1. Launch the app
adb shell am start -n com.myoem.thermalmonitor/.MainActivity

# 2. Verify it connects to service (check logcat)
adb logcat | grep -E "ThermalControlManager|thermalcontrold"

# 3. Test fan control from app
#    - Press "Turn On" → watch `thermalcontrol_client rpm` change
#    - Drag slider to 50% → adb shell thermalcontrol_client rpm should show change
#    - Press "Auto" → kernel resumes control

# 4. Check for crashes
adb logcat -s AndroidRuntime | grep -E "FATAL|ThermalMonitor"

# 5. Verify UI updates on temperature change
#    Heat CPU (run a benchmark or wait) and watch temperature card update
adb shell stress-ng --cpu 4 --timeout 30   # if stress-ng is installed
# Or simply:
adb shell while true; do echo "hello" > /dev/null; done &
```

---

## Integration Test (End-to-End)

```bash
# Run the complete stack validation in order:

echo "=== 1. HAL sysfs ==="
adb shell cat /sys/class/thermal/thermal_zone0/temp

echo "=== 2. Service registration ==="
adb shell service list | grep thermalcontrol

echo "=== 3. Temperature via service ==="
adb shell thermalcontrol_client temp

echo "=== 4. Fan control via service ==="
adb shell thermalcontrol_client speed 80
sleep 2
adb shell thermalcontrol_client rpm
adb shell thermalcontrol_client auto

echo "=== 5. App UI ==="
adb shell am start -n com.myoem.thermalmonitor/.MainActivity

echo "=== 6. No SELinux denials? ==="
adb logcat -d | grep "avc: denied" | grep thermalcontrol
```

---

## Common Pitfalls & Mitigations

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| hwmon index varies across boots | HAL can't find pwm1 | Dynamic discovery in `discoverHwmonPath()` |
| Fan tachometer not wired | `fan1_input` missing | `getFanSpeedRpm()` returns -1, UI shows "N/A" |
| SELinux blocks sysfs write | `avc: denied { write }` on `sysfs_hwmon` | Add `allow thermalcontrold sysfs_hwmon:file w` |
| Auto mode blocks manual writes | Kernel overrides PWM value | Service tracks mode; returns `ERROR_IN_AUTO_MODE` |
| `LOG_TAG` macro redefinition | Build error `-Werror` | Ensure `#define LOG_TAG` is first line in every `.cpp` |
| App crashes if service not running | `NullPointerException` in Manager | `isAvailable()` check before every call |
| Wrong service name string | `addService` returns -129 | Verify `main.cpp` == `service_contexts` == `Manager.java` |

---

## Build Order

```
Phase 1 → libthermalcontrolhal       (no dependencies)
Phase 2 → thermalcontrold            (depends on libthermalcontrolhal)
           thermalcontrol_client      (depends on service AIDL)
Phase 3 → thermalcontrol-manager     (depends on service AIDL java backend)
Phase 4 → ThermalMonitor             (depends on thermalcontrol-manager)
```

---

## Overall Progress

- [x] **Phase 1** — HAL Layer complete
- [x] **Phase 2** — Binder Service complete
- [x] **Phase 3** — Java Manager complete ✓ built successfully
- [ ] **Phase 4** — Android App complete
- [ ] **Integration** — End-to-end test passing

---

*AOSP android-15.0.0_r14 · Target: rpi5 · GitHub: ProArun · March 2026*
