# Building a Full-Stack AOSP Thermal Control System on Raspberry Pi 5
## From "Why Is My Fan Running?" to Jetpack Compose App

**A 10-Part Engineering Series**

---

> **About this series**: This is a real engineering journal of building a complete
> vendor-only AOSP stack — HAL → Binder Service → Java Manager → Kotlin App —
> on a Raspberry Pi 5 running Android 15 (android-15.0.0_r14). Every failed
> approach, every error message, and every debugging command is documented exactly
> as it happened.

---

## Table of Contents

1. [Why Is My Fan Running? — Hardware Foundations](#part-1)
2. [The Architecture — Planning a Full-Stack Vendor Module](#part-2)
3. [Phase 1 — The HAL Layer in C++](#part-3)
4. [Phase 2 — The Binder Service (thermalcontrold)](#part-4)
5. [Phase 2 Debugging — SELinux, sysfs Permissions, and No Fastboot](#part-5)
6. [Phase 3 — Java Manager Library and Soong Namespaces](#part-6)
7. [Phase 3 Debugging — The @hide ServiceManager Problem](#part-7)
8. [Phase 4 — Kotlin/Jetpack Compose App](#part-8)
9. [Phase 4 Debugging — The AIDL Stability Crisis and VINTF](#part-9)
10. [Lessons Learned — AOSP Development on RPi5](#part-10)

---

<a name="part-1"></a>
# Part 1: Why Is My Fan Running? — Hardware Foundations

## The Starting Question

The project began with a simple observation: the Raspberry Pi 5 inside its case
has a small fan attached, and it was spinning. The question was: **who is
controlling it?** Is it Android? The kernel? Some hardware circuit?

Understanding the answer fully is the foundation for everything that came after.

## The Linux Thermal Framework

The RPi5 runs a Linux kernel underneath Android. Linux has a subsystem called the
**thermal framework** (`drivers/thermal/`) that manages temperature-sensitive
components. It works in three layers:

```
┌──────────────────────────────────────────────┐
│           Thermal Zone (sensor)              │
│   /sys/class/thermal/thermal_zone0/          │
│   temp: reports CPU temp in millidegrees     │
└────────────────────┬─────────────────────────┘
                     │ triggers
                     ▼
┌──────────────────────────────────────────────┐
│           Thermal Governor                   │
│   (e.g. "step_wise" — built into kernel)    │
│   Reads temp, computes required cooling      │
└────────────────────┬─────────────────────────┘
                     │ sends commands to
                     ▼
┌──────────────────────────────────────────────┐
│           Cooling Device / pwm-fan driver    │
│   /sys/class/hwmon/hwmon*/pwm1               │
│   Accepts PWM duty cycle 0–255               │
└──────────────────────────────────────────────┘
```

On RPi5 with the official case fan, the kernel driver is `pwm-fan`. It receives
a PWM (Pulse Width Modulation) value between 0 (fully off) and 255 (fully on)
and drives the fan accordingly.

## Discovering the sysfs Paths

The first real work was exploring what sysfs files exist. We used `adb shell` to
poke around:

```bash
# Read CPU temperature (reported in millidegrees Celsius)
adb shell cat /sys/class/thermal/thermal_zone0/temp
# Output: 45000  →  means 45.0°C

# Find which hwmon device is the fan
adb shell ls /sys/class/hwmon/
# Output: hwmon0  hwmon1  hwmon2

# Explore each one
adb shell ls /sys/class/hwmon/hwmon0/name
# Output: cpu_thermal
adb shell ls /sys/class/hwmon/hwmon2/
# Output: pwm1  pwm1_enable  fan1_input  device  name  ...
adb shell cat /sys/class/hwmon/hwmon2/name
# Output: pwm-fan

# Read current fan state
adb shell cat /sys/class/hwmon/hwmon2/pwm1         # PWM value: 0-255
adb shell cat /sys/class/hwmon/hwmon2/pwm1_enable  # 1=manual, 2=auto
adb shell cat /sys/class/hwmon/hwmon2/fan1_input   # RPM (if tachometer wired)
```

**Key insight**: `hwmon2` is the fan. The files that matter are:

| File | Purpose | Values |
|------|---------|--------|
| `pwm1` | Fan speed duty cycle | 0–255 |
| `pwm1_enable` | Control mode | 1=manual, 2=kernel auto |
| `fan1_input` | Tachometer RPM | integer RPM, or -1 if unwired |

## Writing to sysfs — First Attempt

```bash
# Try to manually turn the fan off
adb shell "echo 0 > /sys/class/hwmon/hwmon2/pwm1"
# Permission denied!

# Check who owns the file
adb shell ls -la /sys/class/hwmon/hwmon2/pwm1
# -rw-r--r-- root root  ...
```

The file is `root:root 0644` — readable by everyone, writable only by root.
This immediately told us that any Android service wanting to write to the fan
would need special handling. We bookmarked that problem and moved on to design.

## Why "Auto" Mode Matters

The `pwm1_enable` file is crucial. When set to `2` (auto), the kernel's thermal
governor controls the fan. When set to `1` (manual), your writes to `pwm1` stick.
If you don't switch to manual mode first, the governor will immediately override
whatever you write to `pwm1`.

```bash
# Wrong approach (governor overrides immediately):
echo 128 > /sys/class/hwmon/hwmon2/pwm1

# Correct approach:
echo 1 > /sys/class/hwmon/hwmon2/pwm1_enable  # switch to manual first
echo 128 > /sys/class/hwmon/hwmon2/pwm1        # now set speed
```

---

<a name="part-2"></a>
# Part 2: The Architecture — Planning a Full-Stack Vendor Module

## Design Goals

Before writing a single line of code, we laid out the constraints:

1. **Everything in `vendor/myoem/`** — no changes to `frameworks/`, `device/`,
   or any existing AOSP code
2. **Production-quality Android patterns** — proper HAL, proper Binder IPC,
   proper AIDL, not just a shell script
3. **Full UI** — a Jetpack Compose app a real user could interact with
4. **Education** — document every architectural decision so the code teaches
   how AOSP vendor development works

## The Stack

```
┌─────────────────────────────────────────────┐
│         ThermalMonitor.apk                  │  Kotlin / Jetpack Compose
│         (system priv-app)                   │  StateFlow + ViewModel
└─────────────────────┬───────────────────────┘
                      │ Java API calls
┌─────────────────────▼───────────────────────┐
│         ThermalControlManager.java          │  Java wrapper library
│         (vendor java_library)               │  Hides AIDL complexity
└─────────────────────┬───────────────────────┘
                      │ AIDL Binder IPC
┌─────────────────────▼───────────────────────┐
│         thermalcontrold                     │  C++ daemon
│         (vendor cc_binary)                  │  Registered with ServiceManager
└─────────────────────┬───────────────────────┘
                      │ C++ function calls
┌─────────────────────▼───────────────────────┐
│         libthermalcontrolhal.so             │  C++ shared library
│         (vendor cc_library_shared)          │  Reads/writes sysfs directly
└─────────────────────┬───────────────────────┘
                      │ sysfs file I/O
┌─────────────────────▼───────────────────────┐
│         Linux kernel (pwm-fan driver)       │
│         /sys/class/hwmon/hwmon2/            │
└─────────────────────────────────────────────┘
```

## Why a Separate HAL Library?

We could have put the sysfs reading directly inside `thermalcontrold`. But
separating it into `libthermalcontrolhal.so` gives several benefits:

- **Testability**: the HAL can be unit-tested without the binder service
- **Replaceability**: swap the HAL implementation without touching the service
- **AOSP pattern matching**: real Android HALs work exactly this way

The HAL exposes a pure virtual C++ interface — `IThermalControlHal` — and the
service only ever sees the interface, never the concrete implementation. This is
enforced by a factory function:

```cpp
// In IThermalControlHal.h — the only header the service includes
std::unique_ptr<IThermalControlHal> createThermalControlHal();
```

The service calls `createThermalControlHal()` and gets back a
`unique_ptr<IThermalControlHal>`. It never knows whether the implementation
reads real sysfs, a mock file, or returns hardcoded values.

## HIDL vs AIDL

Pre-Android 11, vendor HALs used HIDL (Hardware Interface Definition Language).
Android 11+ introduced AIDL HALs, and by Android 13 HIDL was deprecated. Since
we're on Android 15, we chose **AIDL** with the **NDK backend**.

Why NDK backend (not Java or C++ AIDL backend)?

- **C++ backend** uses `libbinder` which is a VNDK library. VNDK cannot be used
  in vendor binaries that need to cross the system/vendor boundary on binder.
  It also carries the older `BpInterface`/`BnInterface` style.
- **NDK backend** uses `libbinder_ndk` which is an LLNDK library (stable across
  Android versions). It's the correct choice for vendor services.
- **Java backend** is needed for the Java Manager on the system side.

We enabled both NDK and Java backends:

```
backend: {
    cpp: { enabled: false },  // Wrong for vendor — uses VNDK libbinder
    java: { enabled: true },  // For the Java Manager library
    ndk: { enabled: true },   // For the C++ service daemon — uses LLNDK
    rust: { enabled: false },
},
```

## File Layout Plan

```
vendor/myoem/
├── hal/thermalcontrol/
│   ├── Android.bp
│   ├── include/thermalcontrol/
│   │   └── IThermalControlHal.h
│   └── src/
│       ├── SysfsHelper.h
│       ├── SysfsHelper.cpp
│       ├── ThermalControlHal.h
│       └── ThermalControlHal.cpp
├── services/thermalcontrol/
│   ├── Android.bp
│   ├── thermalcontrold.rc
│   ├── vintf/thermalcontrol.xml
│   ├── aidl/com/myoem/thermalcontrol/
│   │   └── IThermalControlService.aidl
│   ├── src/
│   │   ├── ThermalControlService.h
│   │   ├── ThermalControlService.cpp
│   │   └── main.cpp
│   ├── test/
│   │   └── thermalcontrol_client.cpp
│   └── sepolicy/private/
│       ├── thermalcontrold.te
│       ├── service.te
│       ├── file_contexts
│       └── service_contexts
├── libs/thermalcontrol/
│   ├── Android.bp
│   └── java/com/myoem/thermalcontrol/
│       └── ThermalControlManager.java
└── apps/ThermalMonitor/
    ├── Android.bp
    └── src/main/kotlin/com/myoem/thermalmonitor/
        ├── MainActivity.kt
        ├── ThermalViewModel.kt
        └── ui/ThermalScreen.kt
```

---

<a name="part-3"></a>
# Part 3: Phase 1 — The HAL Layer in C++

## The Interface

The HAL interface (`IThermalControlHal.h`) is a pure virtual C++ class that the
service uses. The key design decision: include the factory function declaration
here so the service never needs to include `ThermalControlHal.h` (the concrete
class):

```cpp
// vendor/myoem/hal/thermalcontrol/include/thermalcontrol/IThermalControlHal.h

#pragma once
#include <cstdint>
#include <memory>

namespace myoem::thermalcontrol {

class IThermalControlHal {
public:
    virtual ~IThermalControlHal() = default;

    virtual float   getCpuTemperatureCelsius() = 0;
    virtual int32_t getFanSpeedRpm()           = 0;
    virtual int32_t getFanSpeedPercent()        = 0;
    virtual bool    isFanRunning()             = 0;
    virtual bool    isAutoMode()               = 0;

    virtual bool setFanEnabled(bool enabled)     = 0;
    virtual bool setFanSpeed(int32_t percent)    = 0;
    virtual bool setAutoMode(bool autoMode)      = 0;
};

// Factory function — only this declaration goes into the exported header.
// The service calls this; it never sees ThermalControlHal (the concrete class).
std::unique_ptr<IThermalControlHal> createThermalControlHal();

}  // namespace myoem::thermalcontrol
```

## sysfs Helper Utilities

Reading and writing sysfs is repetitive. We extracted it into a private helper:

```cpp
// SysfsHelper.cpp — internal to the HAL library

int sysfsReadInt(const std::string& path, int defaultVal) {
    std::ifstream f(path);
    if (!f.is_open()) return defaultVal;
    int val;
    f >> val;
    return f ? val : defaultVal;
}

bool sysfsWriteInt(const std::string& path, int value) {
    std::ofstream f(path);
    if (!f.is_open()) {
        ALOGE("sysfsWriteInt: cannot open '%s'", path.c_str());
        return false;
    }
    f << value << std::endl;
    return f.good();
}
```

## Discovering hwmon at Runtime

Rather than hardcoding `/sys/class/hwmon/hwmon2/`, we discover it at startup by
scanning all hwmon entries for the `pwm-fan` driver name:

```cpp
static std::string discoverHwmonPath() {
    for (int i = 0; i < 10; ++i) {
        std::string base = "/sys/class/hwmon/hwmon" + std::to_string(i);
        std::string nameFile = base + "/name";
        std::ifstream f(nameFile);
        if (!f.is_open()) continue;
        std::string name;
        f >> name;
        // RPi5 case fan uses the pwm-fan kernel driver
        if (name == "pwm-fan") return base;
    }
    return {};  // not found
}
```

Why bother? On different RPi5 configurations the hwmon index can vary
(e.g., hwmon1 vs hwmon2 depending on what other sensors are loaded). Scanning
makes the code resilient to index changes across boots.

## PWM Arithmetic

The kernel expresses PWM in the range 0–255. Users think in 0–100%.
Two conversions are needed:

```cpp
// Percent → PWM (for writing)
int pwmVal = (percent * 255) / 100;

// PWM → Percent (for reading — round up to avoid reporting 0 when fan spins)
int percent = (pwm * 100 + 255 - 1) / 255;
```

The ceiling division in the read path is important: a PWM of 1 should report
1%, not 0%.

## The Build File

```
// vendor/myoem/hal/thermalcontrol/Android.bp

soong_namespace {}  // declares this directory as a Soong namespace

cc_library_shared {
    name: "libthermalcontrolhal",
    vendor: true,           // installs to /vendor/lib64/

    srcs: [
        "src/SysfsHelper.cpp",
        "src/ThermalControlHal.cpp",
    ],

    export_include_dirs: ["include"],  // IThermalControlHal.h is public
    local_include_dirs:  ["src"],      // ThermalControlHal.h is private

    shared_libs: ["liblog"],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

**`soong_namespace {}`** is mandatory here because this module is referenced
from another directory (`vendor/myoem/services/thermalcontrol`) by name.
Soong namespaces prevent module name collisions across the entire AOSP tree.

## Building and Testing Phase 1

```bash
# Build only the HAL library
m libthermalcontrolhal

# Check it landed in the right place
ls out/target/product/rpi5/vendor/lib64/libthermalcontrolhal.so

# Push to device (remount vendor first)
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/lib64/libthermalcontrolhal.so \
         /vendor/lib64/libthermalcontrolhal.so
```

At this stage there's nothing to test at runtime — the library has no main()
and no client. We verified it compiled and linked correctly and moved to Phase 2.

---

<a name="part-4"></a>
# Part 4: Phase 2 — The Binder Service (thermalcontrold)

## The AIDL Interface

The AIDL file defines the IPC contract. It lives under
`aidl/com/myoem/thermalcontrol/IThermalControlService.aidl`:

```aidl
package com.myoem.thermalcontrol;

@VintfStability
interface IThermalControlService {

    float   getCpuTemperatureCelsius();
    int     getFanSpeedRpm();
    boolean isFanRunning();
    int     getFanSpeedPercent();
    boolean isFanAutoMode();

    void setFanEnabled(boolean enabled);
    void setFanSpeed(int speedPercent);   // throws if out of 0–100
    void setFanAutoMode(boolean autoMode);

    const int ERROR_HAL_UNAVAILABLE = 1;
    const int ERROR_INVALID_SPEED   = 2;
    const int ERROR_SYSFS_WRITE     = 3;
}
```

> **Note on `@VintfStability`**: this annotation was NOT in the original design.
> It was added later as a fix to a crash, and its full story is in Part 9.
> The initial version used `unstable: true` without any annotation.

## The Service Class

The service inherits from the AIDL-generated `BnThermalControlService` and
holds the HAL via the interface:

```cpp
// ThermalControlService.h
namespace aidl::com::myoem::thermalcontrol {

class ThermalControlService : public BnThermalControlService {
public:
    ThermalControlService();

    // AIDL methods
    ndk::ScopedAStatus getCpuTemperatureCelsius(float* _aidl_return) override;
    ndk::ScopedAStatus getFanSpeedRpm(int32_t* _aidl_return) override;
    ndk::ScopedAStatus isFanRunning(bool* _aidl_return) override;
    ndk::ScopedAStatus getFanSpeedPercent(int32_t* _aidl_return) override;
    ndk::ScopedAStatus isFanAutoMode(bool* _aidl_return) override;
    ndk::ScopedAStatus setFanEnabled(bool enabled) override;
    ndk::ScopedAStatus setFanSpeed(int32_t speedPercent) override;
    ndk::ScopedAStatus setFanAutoMode(bool autoMode) override;

private:
    std::unique_ptr<::myoem::thermalcontrol::IThermalControlHal> mHal;
};

}  // namespace aidl::com::myoem::thermalcontrol
```

The constructor creates the HAL via the factory function:

```cpp
ThermalControlService::ThermalControlService()
    : mHal(::myoem::thermalcontrol::createThermalControlHal()) {
    ALOGI("ThermalControlService created");
}
```

Error handling for write operations uses `ServiceSpecificException`:

```cpp
ndk::ScopedAStatus ThermalControlService::setFanSpeed(int32_t speedPercent) {
    if (speedPercent < 0 || speedPercent > 100) {
        return ndk::ScopedAStatus::fromServiceSpecificError(
                IThermalControlService::ERROR_INVALID_SPEED);
    }
    if (!mHal->setFanSpeed(speedPercent)) {
        return ndk::ScopedAStatus::fromServiceSpecificError(
                IThermalControlService::ERROR_SYSFS_WRITE);
    }
    return ndk::ScopedAStatus::ok();
}
```

## main.cpp — Service Registration

Service registration follows a 5-step pattern standard for all Android NDK
binder services:

```cpp
int main() {
    ALOGI("thermalcontrold starting");

    // 1. Size the thread pool
    ABinderProcess_setThreadPoolMaxThreadCount(4);

    // 2. Start worker threads
    ABinderProcess_startThreadPool();

    // 3. Create service instance
    auto service = ndk::SharedRefBase::make<
            aidl::com::myoem::thermalcontrol::ThermalControlService>();

    // 4. Register with ServiceManager
    // VINTF AIDL format: <interface_descriptor>/<instance>
    static constexpr const char* kServiceName =
        "com.myoem.thermalcontrol.IThermalControlService/default";

    binder_status_t status = AServiceManager_addService(
            service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("addService('%s') failed: %d", kServiceName, status);
        return 1;
    }

    ALOGI("thermalcontrold registered as '%s'", kServiceName);

    // 5. Block main thread — workers handle calls
    ABinderProcess_joinThreadPool();
    return 0;
}
```

## The Init RC File

Android services are started by `init`. The RC file has two sections:

```rc
# thermalcontrold.rc

# Fix hwmon sysfs permissions at boot.
# The pwm-fan driver creates pwm1/pwm1_enable as root:root 0644.
# Init runs as root and runs this before thermalcontrold starts.
on boot
    chown root system /sys/class/hwmon/hwmon2/pwm1
    chown root system /sys/class/hwmon/hwmon2/pwm1_enable
    chmod 0664 /sys/class/hwmon/hwmon2/pwm1
    chmod 0664 /sys/class/hwmon/hwmon2/pwm1_enable

service thermalcontrold /vendor/bin/thermalcontrold
    class main
    user system
    group system
```

> **Why `chown root system` BEFORE `chmod 0664`?**
> This ordering matters. See Part 5 for the full debugging story.

## The Android.bp

```
soong_namespace {
    imports: ["vendor/myoem/hal/thermalcontrol"],
}

aidl_interface {
    name: "thermalcontrolservice-aidl",
    owner: "myoem",
    vendor_available: true,
    stability: "vintf",
    srcs: ["aidl/com/myoem/thermalcontrol/IThermalControlService.aidl"],
    local_include_dir: "aidl",
    versions_with_info: [{ version: "1", imports: [] }],
    backend: {
        cpp: { enabled: false },
        java: { enabled: true },
        ndk:  { enabled: true },
        rust: { enabled: false },
    },
}

cc_binary {
    name: "thermalcontrold",
    vendor: true,
    init_rc: ["thermalcontrold.rc"],
    srcs: ["src/ThermalControlService.cpp", "src/main.cpp"],
    shared_libs: ["libbinder_ndk", "liblog", "libthermalcontrolhal"],
    static_libs: ["thermalcontrolservice-aidl-V1-ndk"],
}
```

Notice **`static_libs`** for the AIDL NDK stubs, not `shared_libs`. The generated
AIDL NDK stubs are always static-linked into the binary.

---

<a name="part-5"></a>
# Part 5: Phase 2 Debugging — SELinux, sysfs Permissions, and No Fastboot

Phase 2 had three separate bugs, each requiring a different kind of debugging.
This part documents them in chronological order.

## Bug 1: `sysfs_hwmon` Does Not Exist

**Error during build:**
```
error: vendor/myoem/services/thermalcontrol/sepolicy/private/thermalcontrold.te:16:0:
type sysfs_hwmon is not defined
```

**Original policy attempt:**
```
allow thermalcontrold sysfs_hwmon:file rw_file_perms;  # WRONG
```

**Root cause**: Looking at real AOSP hardware HALs, we assumed `sysfs_hwmon`
would be a defined SELinux type — but it doesn't exist in the AOSP 15 base
policy. AOSP does define `sysfs_thermal` (for `/sys/class/thermal/*`), but hwmon
files just carry the generic `sysfs` label on this build.

**Verification:**
```bash
adb shell ls -laZ /sys/class/hwmon/hwmon2/pwm1
# -rw-r--r-- root root u:object_r:sysfs:s0 pwm1
#                       ^^^^^^^^ generic sysfs, not sysfs_hwmon
```

**Fix:**
```
# thermalcontrold.te — correct policy
allow thermalcontrold sysfs_thermal:file r_file_perms;  # for temp reads
allow thermalcontrold sysfs_thermal:dir  r_dir_perms;
allow thermalcontrold sysfs:file rw_file_perms;          # for hwmon writes
allow thermalcontrold sysfs:dir  r_dir_perms;
```

**Lesson**: Never assume an SELinux type exists just because the filesystem
category sounds like it should have one. Always check with `ls -laZ` first.

## Bug 2: RPi5 Has No Fastboot

After fixing the SELinux policy, we needed to flash the new image. The standard
AOSP workflow is:

```bash
adb reboot bootloader
fastboot flashall
```

**What actually happened:**
```bash
adb reboot bootloader
# Device rebooted but came back to Android, not fastboot mode

fastboot devices
# (nothing listed)
```

The RPi5 does not have a dedicated bootloader that supports fastboot protocol.
`adb reboot bootloader` just reboots Android normally.

**Alternative approaches tried:**
```bash
adb disable-verity
# error: disable-verity only works for userdebug builds with dm-verity
# (even though this IS userdebug — verity was compiled in differently)
```

**The working approach — adb push for development iteration:**

```bash
# For vendor partition changes (binary + libraries):
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/thermalcontrold /vendor/bin/
adb push out/target/product/rpi5/vendor/lib64/libthermalcontrolhal.so \
         /vendor/lib64/

# For system partition changes (APKs):
adb shell mount -o remount,rw /
# OR find the block device:
adb shell "mount | grep /system"
# /dev/block/mmcblk0p2 on / type ext4 ...
adb shell mount -o remount,rw /dev/block/mmcblk0p2 /
adb push ThermalMonitor.apk /system/priv-app/ThermalMonitor/

# SELinux policy changes REQUIRE a full image rebuild:
make vendorimage
# Then flash via SD card or full image copy
```

**For SELinux changes specifically**, there's no way around a full image rebuild.
The compiled policy is baked into the image at build time. For development
iteration on non-SELinux changes (binary logic, Java code, etc.), `adb push`
is fast and effective.

**Lesson**: On RPi5 AOSP, think of development as two workflows:
- **Fast iteration** (logic changes): mount + adb push + adb shell stop/start service
- **Policy changes** (SELinux, RC files, VINTF): full `make vendorimage`

## Bug 3: Permission Denied Writing to sysfs (The Tricky One)

After getting the service running (SELinux fixed, binary pushed), we tested
with the CLI client:

```bash
adb shell thermalcontrol_client temp
# 47.3°C  ✓ temperature works

adb shell thermalcontrol_client on
# ERROR_SYSFS_WRITE (code 3)  ✗
```

**First investigation — was it SELinux?**

```bash
adb shell dmesg | grep avc
# (no denials)

adb shell ps -eZ | grep thermalcontrold
# u:r:thermalcontrold:s0  root  754  ...
# ↑ correct domain
```

No SELinux denials. So what was blocking the write?

**Checking the file permissions on device:**
```bash
adb shell ls -la /sys/class/hwmon/hwmon2/pwm1
# -rw-r--r-- root root  ...
```

**Root:root 0644!** The service runs as `user system` (uid 1000). Unix
file permissions: owner=root (rw), group=root (r), others=r. System user is
in neither root's owner nor root's group — it's "others", which has only
read permission.

**First fix attempt — just chmod:**
```bash
# In thermalcontrold.rc on boot:
chmod 0664 /sys/class/hwmon/hwmon2/pwm1
chmod 0664 /sys/class/hwmon/hwmon2/pwm1_enable
```

Push the RC file, reboot, check:
```bash
adb shell ls -la /sys/class/hwmon/hwmon2/pwm1
# -rw-rw-r-- root root  ...
```

Permissions changed to 664, but **group is still `root`**. The system user
(uid=1000, gid=1000) is NOT in the root group. The group-write permission is
for the `root` group, not `system`. Still fails.

**The debugging aha moment**: `chmod` changes the permission bits but not the
ownership. `0664` gives group-write permission, but if the group is still
`root`, only members of `root` can use it.

**Correct fix — chown THEN chmod:**
```rc
on boot
    chown root system /sys/class/hwmon/hwmon2/pwm1
    chown root system /sys/class/hwmon/hwmon2/pwm1_enable
    chmod 0664 /sys/class/hwmon/hwmon2/pwm1
    chmod 0664 /sys/class/hwmon/hwmon2/pwm1_enable
```

`chown root system` makes root the owner and **system the group**. Then
`chmod 0664` gives group-write permission — and now the system group can write.

**After fix:**
```bash
adb shell ls -la /sys/class/hwmon/hwmon2/pwm1
# -rw-rw-r-- root system  ...   ✓ group is now system

adb shell thermalcontrol_client on
# OK  ✓

adb shell thermalcontrol_client speed 75
# OK  ✓

adb shell thermalcontrol_client temp
# 44.2°C  ✓
```

**A critical deployment pitfall**: When we initially tried to push the RC file,
we pushed the build output:

```bash
adb push out/target/product/rpi5/vendor/etc/init/thermalcontrold.rc \
         /vendor/etc/init/thermalcontrold.rc
```

But the build output was **stale** — it reflected an older version that didn't
have the `chown` lines yet. The build system hadn't regenerated it because we'd
only edited the source and not rebuilt. Always push the **source file** directly,
or rebuild first:

```bash
# Push source directly (safer during development):
adb push vendor/myoem/services/thermalcontrol/thermalcontrold.rc \
         /vendor/etc/init/thermalcontrold.rc
```

---

<a name="part-6"></a>
# Part 6: Phase 3 — Java Manager Library and Soong Namespaces

## Why a Java Manager Layer?

We could have had the app call AIDL directly. But a manager layer brings:

1. **Exception insulation**: AIDL throws `RemoteException`; the manager catches
   it and returns safe sentinel values (0.0f, -1, false)
2. **Utility methods**: `categorizeTemperature()` and `temperatureColor()` live
   here, shared between any future apps
3. **API stability**: apps don't change if the AIDL interface evolves

## The Manager Design

```java
// ThermalControlManager.java
public class ThermalControlManager {

    public static final String SERVICE_NAME =
        "com.myoem.thermalcontrol.IThermalControlService/default";

    private final IThermalControlService mService;

    // Constructor takes an IBinder, not a Context or ServiceManager reference.
    // Reason: ServiceManager.checkService() is @hide — unavailable in vendor
    // SDK builds. The app (platform_apis:true) does the lookup and passes
    // the binder here.
    public ThermalControlManager(IBinder binder) {
        if (binder != null) {
            mService = IThermalControlService.Stub.asInterface(binder);
        } else {
            mService = null;
        }
    }

    public float getCpuTemperatureCelsius() {
        if (mService == null) return 0.0f;
        try {
            return mService.getCpuTemperatureCelsius();
        } catch (RemoteException e) {
            return 0.0f;  // safe sentinel
        }
    }

    // ... other methods follow same pattern ...

    public static String categorizeTemperature(float celsius) {
        if (celsius < 50f) return "Cool";
        if (celsius < 70f) return "Warm";
        if (celsius < 85f) return "Hot";
        return "Critical";
    }

    public static int temperatureColor(float celsius) {
        if (celsius < 50f) return 0xFF4CAF50;  // Material Green 500
        if (celsius < 70f) return 0xFFFFC107;  // Material Amber 500
        if (celsius < 85f) return 0xFFFF5722;  // Material Deep Orange 500
        return 0xFFF44336;                       // Material Red 500
    }
}
```

## The Build File

```
// vendor/myoem/libs/thermalcontrol/Android.bp

soong_namespace {
    imports: ["vendor/myoem/services/thermalcontrol"],
}

java_library {
    name: "thermalcontrol-manager",
    vendor: true,
    srcs: ["java/**/*.java"],
    libs: ["thermalcontrolservice-aidl-V1-java"],
    sdk_version: "system_current",
}
```

Two things to call out:

**`soong_namespace { imports: [...] }`**: Without this import, Soong cannot
resolve `thermalcontrolservice-aidl-V1-java` because that module lives in a
different namespace (`vendor/myoem/services/thermalcontrol`). Soong namespaces
are like package scopes — modules are only visible within their own namespace
unless explicitly imported.

**`sdk_version: "system_current"`**: The manager uses types from the Android
system API surface. `system_current` grants access to `@SystemApi` classes.
We cannot use `platform_apis: true` in a vendor `java_library` — that's only
valid for system apps. And we cannot use `sdk_version: "current"` because
`IThermalControlService` (the AIDL-generated Java class) compiles against
system API types.

## Testing Phase 3

At this stage there's no standalone test for the manager. We verified that
`thermalcontrolservice-aidl-V1-java` and `thermalcontrol-manager` compiled
without errors:

```bash
m thermalcontrol-manager

# Check what was generated
ls out/soong/.intermediates/vendor/myoem/libs/thermalcontrol/thermalcontrol-manager/
# android_common_vendor/javac/thermalcontrol-manager.jar
```

We also verified the service could be called from the command line as a sanity
check before building the app:

```bash
# Check service is registered
adb shell service list | grep thermalcontrol
# com.myoem.thermalcontrol.IThermalControlService/default: ...

# Manually invoke via CLI client
adb shell thermalcontrol_client temp     # 46.1°C
adb shell thermalcontrol_client percent  # 35%
adb shell thermalcontrol_client auto     # auto mode ON
```

---

<a name="part-7"></a>
# Part 7: Phase 3 Debugging — The @hide ServiceManager Problem

Phase 3 had three distinct build failures, each teaching a key lesson about
AOSP's vendor SDK rules.

## Build Failure 1: Soong Namespace — Module Not Found

**Error:**
```
error: vendor/myoem/libs/thermalcontrol/Android.bp:9:1:
"thermalcontrol-manager" depends on undefined module "thermalcontrolservice-aidl-java".
```

We referenced `thermalcontrolservice-aidl-java` in `libs`, but Soong couldn't
find it because the module lives in a different namespace
(`vendor/myoem/services/thermalcontrol`) and we hadn't imported that namespace.

**Fix:** Add `imports` to `soong_namespace`:
```
soong_namespace {
    imports: ["vendor/myoem/services/thermalcontrol"],
}
```

**Lesson**: In a namespaced build, inter-directory dependencies require explicit
`imports` in `soong_namespace`. Just because you can see the directory on disk
doesn't mean Soong can see its modules from another namespace.

## Build Failure 2: `platform_apis: true` in Vendor — Forbidden

**Error:**
```
error: vendor/myoem/libs/thermalcontrol/Android.bp:9:1:
"thermalcontrol-manager" is a vendor module, but platform_apis is true
```

Our initial `Android.bp` had `platform_apis: true` because we wanted to use
`ServiceManager.checkService()`. AOSP's build rules forbid this combination.

**Why?** Vendor modules must declare which SDK surface they compile against.
`platform_apis: true` means "I use private platform internals" — which is only
allowed for platform code and privileged system apps, not vendor libraries.
The whole point of vendor/system separation is that vendor code shouldn't
reach into platform internals.

**First fix attempt:** Change to `sdk_version: "current"`.

**New error:**
```
error: "thermalcontrol-manager" compiles against Android API but its dependency
"thermalcontrolservice-aidl-V1-java" compiles against system API
```

An SDK level mismatch: the AIDL-generated Java stubs use system API types
(because they're built with system visibility), but our module declared
`sdk_version: "current"` (only public API). We needed to match.

**Fix:** Change to `sdk_version: "system_current"`.

## Build Failure 3: The @hide ServiceManager Problem

Even with `sdk_version: "system_current"`, the build failed:

```
error: cannot find symbol
    symbol:   class ServiceManager
    location: class ThermalControlManager
```

`ServiceManager.checkService()` is annotated `@hide` in AOSP — it's not part of
any public, system, or test API surface. The only way to call it is with
`platform_apis: true`. But `platform_apis: true` is forbidden in vendor modules.

**The dead end:**
- The manager needs `ServiceManager` to find the service
- `ServiceManager` requires `platform_apis: true`
- `platform_apis: true` is forbidden in vendor `java_library`
- There is NO combination of `sdk_version` that exposes `ServiceManager`

**The architectural fix:**

Move `ServiceManager` usage out of the library and into the **app**.

The app (`android_app`) can have `platform_apis: true` — that's allowed for
privileged system apps. So we changed the design:

```
BEFORE:
  Manager constructor: ServiceManager.checkService() → wraps binder
  App: new ThermalControlManager(context)

AFTER:
  App: IBinder binder = ServiceManager.checkService(SERVICE_NAME)
  App: new ThermalControlManager(binder)
  Manager constructor: just wraps the provided binder
```

This keeps the manager free of `@hide` dependencies. The manager never calls
`ServiceManager` — it takes an `IBinder` directly. The app, which has
`platform_apis: true`, does the service lookup.

```java
// In MainActivity.kt (platform_apis: true — this @hide call is allowed here)
val binder = ServiceManager.checkService(ThermalControlManager.SERVICE_NAME)

// Manager constructor just wraps the binder
val manager = ThermalControlManager(binder)
```

**Lesson**: When you hit a `@hide` API you need, ask "who in the stack can
legitimately call this?" and push the call there, passing the result down.

---

<a name="part-8"></a>
# Part 8: Phase 4 — Kotlin/Jetpack Compose App

## Architecture

The app follows modern Android architecture:

```
MainActivity (ComponentActivity)
    ├── Creates ThermalControlManager (service lookup here)
    ├── Creates ThermalViewModel (via factory)
    └── setContent { ThermalScreen(viewModel) }

ThermalViewModel
    ├── UiState (StateFlow) — single source of truth
    ├── Polling coroutine: fetchData() every 2 seconds on Dispatchers.IO
    └── Commands: turnFanOn(), turnFanOff(), setFanSpeed(), setAutoMode()

ThermalScreen (Compose)
    ├── TemperatureCard — 48sp temp text, color-coded
    ├── FanStatusCard — Speed%, RPM, Running
    └── FanControlCard — On/Off/Auto buttons + Slider + TextField
```

## UiState — Single Source of Truth

```kotlin
data class UiState(
    val cpuTempCelsius: Float = 0f,
    val fanRpm: Int = -1,
    val fanSpeedPercent: Int = 0,
    val isFanRunning: Boolean = false,
    val isAutoMode: Boolean = true,
    val serviceAvailable: Boolean = false,
    val errorMessage: String? = null
)
```

All UI state lives in one immutable data class. The ViewModel exposes it as
a `StateFlow<UiState>`. Compose observes it with `collectAsState()`.

## The Polling ViewModel

```kotlin
class ThermalViewModel(private val manager: ThermalControlManager) : ViewModel() {

    private val _uiState = MutableStateFlow(UiState())
    val uiState: StateFlow<UiState> = _uiState.asStateFlow()

    init {
        // Poll every 2 seconds on IO thread — binder calls are blocking
        viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                fetchData()
                delay(2_000)
            }
        }
    }

    private fun fetchData() {
        if (!manager.isAvailable()) {
            _uiState.update {
                it.copy(serviceAvailable = false,
                        errorMessage = "thermalcontrold not running")
            }
            return
        }
        _uiState.update {
            it.copy(
                cpuTempCelsius   = manager.getCpuTemperatureCelsius(),
                fanRpm           = manager.getFanSpeedRpm(),
                fanSpeedPercent  = manager.getFanSpeedPercent(),
                isFanRunning     = manager.isFanRunning(),
                isAutoMode       = manager.isFanAutoMode(),
                serviceAvailable = true,
                errorMessage     = null
            )
        }
    }
}
```

The key design decision: binder IPC calls are synchronous and can block.
Running the polling on `Dispatchers.IO` keeps the main thread free for UI.

## The Compose UI

The fan control card uses local state for the slider/text field to prevent
the 2-second poll from overwriting a value the user is actively editing:

```kotlin
@Composable
private fun FanControlCard(state: UiState, viewModel: ThermalViewModel) {
    // Local state — NOT from UiState. This prevents the 2-second poll
    // from resetting the slider while the user is dragging it.
    var sliderValue by remember { mutableFloatStateOf(state.fanSpeedPercent.toFloat()) }
    var textValue   by remember { mutableStateOf(state.fanSpeedPercent.toString()) }

    // Bidirectional sync between slider and text field
    Slider(
        value = sliderValue,
        onValueChange = { newVal ->
            sliderValue = newVal
            textValue   = newVal.toInt().toString()  // text follows slider
        },
        valueRange = 0f..100f, steps = 99
    )
    OutlinedTextField(
        value = textValue,
        onValueChange = { raw ->
            textValue = raw
            raw.toIntOrNull()?.coerceIn(0, 100)?.let { pct ->
                sliderValue = pct.toFloat()  // slider follows text
            }
        },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number)
    )
}
```

## The Android.bp

```
android_app {
    name: "ThermalMonitor",
    platform_apis: true,   // required for ServiceManager.checkService() (@hide)
    privileged: true,      // installs to /system/priv-app/ (not /system/app/)
    certificate: "platform",

    srcs: ["src/main/kotlin/**/*.kt"],

    static_libs: [
        "thermalcontrol-manager",
        "thermalcontrolservice-aidl-V1-java",
        "kotlin-stdlib",
        "kotlinx-coroutines-android",
        "androidx.core_core-ktx",
        "androidx.activity_activity-compose",
        "androidx.compose.runtime_runtime",
        "androidx.compose.ui_ui",
        "androidx.compose.foundation_foundation",
        "androidx.compose.material3_material3",
        "androidx.lifecycle_lifecycle-viewmodel-ktx",
        "androidx.lifecycle_lifecycle-viewmodel-compose",
        "androidx.lifecycle_lifecycle-runtime-compose",
    ],
}
```

**`privileged: true`** installs to `/system/priv-app/` which grants access
to `android.permission.BIND_*` and other privileged permissions.

**`certificate: "platform"`** signs the APK with the platform key, required
for apps that use `platform_apis: true` and need to interact with protected
system services.

## Build Failure: Theme Not Found

**Error:**
```
error: resource style/android:Theme.Material.NoTitleBar not found
```

Our `styles.xml` initially had:
```xml
<style name="AppTheme" parent="android:Theme.Material.NoTitleBar">
```

`Theme.Material.NoTitleBar` does not exist in AOSP 15's theme set.
Available themes in AOSP 15:
- `android:Theme.DeviceDefault` — standard AOSP DeviceDefault
- `android:Theme.DeviceDefault.NoActionBar` — the one we need
- `android:Theme.Material` — basic Material (no action bar variant)

**Fix:**
```xml
<style name="AppTheme" parent="android:Theme.DeviceDefault.NoActionBar">
```

---

<a name="part-9"></a>
# Part 9: Phase 4 Debugging — The AIDL Stability Crisis and VINTF

This was the most complex debugging session of the entire project. It exposed
a fundamental Android architectural concept that is rarely explained clearly
outside of Google's internal documentation.

## The Crash

The app launched, `ThermalControlManager` logged "Connected to service", and
then immediately crashed:

```
03-17 19:26:36.816  E AndroidRuntime: FATAL EXCEPTION: DefaultDispatcher-worker-1
03-17 19:26:36.816  E AndroidRuntime: java.lang.IllegalArgumentException
03-17 19:26:36.816  E AndroidRuntime:     at android.os.BinderProxy.transactNative(Native Method)
03-17 19:26:36.816  E AndroidRuntime:     at android.os.BinderProxy.transact(BinderProxy.java:586)
03-17 19:26:36.816  E AndroidRuntime:     at com.myoem.thermalcontrol.IThermalControlService
                                               $Stub$Proxy.getCpuTemperatureCelsius(IThermalControlService.java:195)
```

The exception was thrown **inside** `transactNative` — inside the JNI binder
call, before our Java code even got a chance to run.

## Understanding Binder Stability

Android's binder framework has a concept called **parcel stability** that was
added in Android 12 to enforce partition boundaries. Every binder object and
every parcel has a declared stability level:

| Stability Level | Meaning |
|----------------|---------|
| `LOCAL` | Used only within one partition (e.g., vendor-to-vendor) |
| `SYSTEM` | Used within the system partition |
| `VINTF` | Declared in the VINTF manifest — safe across partitions |

When a client's parcel stability doesn't match what the service's binder
object declares, the kernel binder driver rejects the transaction with
`BAD_VALUE` (-22). In Java, `BAD_VALUE` from `IBinder::transact()` maps to
`IllegalArgumentException`.

**Our situation:**
- `thermalcontrold` was built with `unstable: true` → binder objects have
  `LOCAL` stability (vendor-local)
- `ThermalMonitor` is a system app (in `/system/priv-app/`) → its parcels
  carry `SYSTEM` stability
- When `SYSTEM` stability parcel meets `LOCAL` stability binder: **rejected**

## The Root Cause in Code

In `Stability.cpp` (Android binder internals):

```cpp
bool Stability::check(int32_t provided, Level required) {
    bool stable = (provided & required) == required;
    if (!stable) {
        ALOGE("Parcel stability mismatch: provided %d required %d",
              provided, required);
    }
    return stable;
}
```

The LOCAL level doesn't satisfy the SYSTEM requirement. The transaction fails
before any of our code runs.

## The Fix: `stability: "vintf"` and `@VintfStability`

The solution is `VINTF` stability, which is the cross-partition level. Both the
service and the app must agree to use it. There are two components:

**1. AIDL interface annotation:**
```aidl
@VintfStability           // ← add this
interface IThermalControlService {
    ...
}
```

**2. Build system declaration:**
```
// In aidl_interface block (replacing unstable: true)
stability: "vintf",
owner: "myoem",           // required when in a soong_namespace
versions_with_info: [{ version: "1", imports: [] }],  // required by vintf
```

## The Cascade of New Build Errors

Switching to `stability: "vintf"` triggered a cascade of errors because VINTF
AIDL has stricter requirements than unstable AIDL.

### Error 1: `owner` Required

```
error: module "thermalcontrolservice-aidl_interface":
aidl_interface in a soong_namespace must have the 'owner' property set.
```

Any `aidl_interface` inside a `soong_namespace` requires an `owner` property
when `stability: "vintf"` is used. Added `owner: "myoem"`.

### Error 2: Module Names Changed

```
error: "thermalcontrol-manager" depends on undefined module
"thermalcontrolservice-aidl-java".
Did you mean ["thermalcontrolservice-aidl-V1-java" ...]?
```

With `unstable: true`, AIDL generates `thermalcontrolservice-aidl-java`.
With `stability: "vintf"` and versioning, it becomes `thermalcontrolservice-aidl-V1-java`.

Updated all references:
- `libs/thermalcontrol/Android.bp`: `thermalcontrolservice-aidl-java` → `thermalcontrolservice-aidl-V1-java`
- `apps/ThermalMonitor/Android.bp`: same change
- `services/thermalcontrol/Android.bp`: `thermalcontrolservice-aidl-ndk` → `thermalcontrolservice-aidl-V1-ndk`

### Error 3: API Snapshot Required (`.hash` file)

```
error: A frozen aidl_interface must have '.hash' file, but
thermalcontrolservice-aidl-V1 doesn't have it.
Use the command below to generate a hash:
(croot && system/tools/aidl/build/hash_gen.sh
  vendor/myoem/services/thermalcontrol/aidl_api/thermalcontrolservice-aidl/1
  latest-version
  vendor/myoem/services/thermalcontrol/aidl_api/thermalcontrolservice-aidl/1/.hash)
```

VINTF interfaces are versioned and frozen. The build system requires a snapshot
of the API and a hash to detect changes. We had created the snapshot directory
(`aidl_api/thermalcontrolservice-aidl/1/`) but not the hash.

The error message told us exactly what to run — we ran it:

```bash
(croot && system/tools/aidl/build/hash_gen.sh \
  vendor/myoem/services/thermalcontrol/aidl_api/thermalcontrolservice-aidl/1 \
  latest-version \
  vendor/myoem/services/thermalcontrol/aidl_api/thermalcontrolservice-aidl/1/.hash)
```

### Error 4: `@VintfStability` Annotation Missing in AIDL File

```
ERROR: vendor/myoem/services/thermalcontrol/aidl/com/myoem/thermalcontrol/
IThermalControlService.aidl:6.1-10:
com.myoem.thermalcontrol.IThermalControlService does not have VINTF level
stability (marked @VintfStability), but this interface requires it in java
```

The `stability: "vintf"` build declaration requires the AIDL file itself to
have the `@VintfStability` annotation. Both the source file AND the snapshot
file need it:

```aidl
// IThermalControlService.aidl — add @VintfStability
@VintfStability
interface IThermalControlService { ... }
```

Applied to both:
- `aidl/com/myoem/thermalcontrol/IThermalControlService.aidl`
- `aidl_api/thermalcontrolservice-aidl/1/com/myoem/thermalcontrol/IThermalControlService.aidl`
- `aidl_api/thermalcontrolservice-aidl/current/com/myoem/thermalcontrol/IThermalControlService.aidl`

## The VINTF Manifest Requirement

After fixing all build errors, the service crashed on registration:

```
03-17 21:40:45.131  E thermalcontrold: addService(
    'com.myoem.thermalcontrol.IThermalControlService/default') failed: -3
```

Error code `-3` is `EX_ILLEGAL_ARGUMENT` in Android binder exception codes.

Looking at the Android service manager source (`ServiceManager.cpp`):

```cpp
Status ServiceManager::addService(const std::string& name,
                                   const sp<IBinder>& service, ...) {
    // For VINTF-stable binders, verify VINTF manifest declaration
    if (Stability::getStability(service.get()) == Stability::Level::kVintf) {
        if (!isVintfDeclared(name)) {
            return Status::fromExceptionCode(Status::EX_ILLEGAL_ARGUMENT,
                "Service is not declared in VINTF manifest");
        }
    }
    // ...
}
```

The service manager **explicitly checks** the VINTF manifest for any service
with VINTF stability. If the service isn't declared there, registration is
rejected. This is by design — it prevents unauthorized services from masquerading
as standard HALs.

## Creating the VINTF Manifest Fragment

The fix: declare the service in a VINTF manifest fragment. This can be done
from `vendor/myoem/` without touching `device/`:

```xml
<!-- vendor/myoem/services/thermalcontrol/vintf/thermalcontrol.xml -->
<manifest version="1.0" type="device">
    <hal format="aidl">
        <name>com.myoem.thermalcontrol</name>
        <version>1</version>
        <interface>
            <name>IThermalControlService</name>
            <instance>default</instance>
        </interface>
    </hal>
</manifest>
```

Install it via a `vintf_fragment` Soong module:

```
// In services/thermalcontrol/Android.bp
vintf_fragment {
    name: "thermalcontrol-vintf-fragment",
    src: "vintf/thermalcontrol.xml",
    vendor: true,  // installs to /vendor/etc/vintf/manifest/
}
```

Add to product packages:
```makefile
# myoem_base.mk
PRODUCT_PACKAGES += thermalcontrol-vintf-fragment
```

## Service Name Format for VINTF AIDL

VINTF AIDL services use a specific name format: `<interface_descriptor>/<instance>`.

This is different from the old `unstable` format. Changes required everywhere
the service name appears:

**In main.cpp:**
```cpp
// Old (unstable):
static constexpr const char* kServiceName =
    "com.myoem.thermalcontrol.IThermalControlService";

// New (VINTF):
static constexpr const char* kServiceName =
    "com.myoem.thermalcontrol.IThermalControlService/default";
```

**In ThermalControlManager.java:**
```java
// Old:
public static final String SERVICE_NAME =
    "com.myoem.thermalcontrol.IThermalControlService";

// New:
public static final String SERVICE_NAME =
    "com.myoem.thermalcontrol.IThermalControlService/default";
```

**In sepolicy/private/service_contexts:**
```
# Old:
com.myoem.thermalcontrol.IThermalControlService u:object_r:thermalcontrold_service:s0

# New:
com.myoem.thermalcontrol.IThermalControlService/default u:object_r:thermalcontrold_service:s0
```

**In thermalcontrol_client.cpp** — every `AServiceManager_checkService` call
needed the updated name.

## Deployment Sequence for VINTF Changes

VINTF manifest and SELinux changes both require a full image rebuild:

```bash
# Build the full vendor image
make vendorimage -j$(nproc)

# After flashing (or using a pre-built image):
# For development iteration, push binaries + manifest manually:
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/thermalcontrold /vendor/bin/
adb push out/target/product/rpi5/vendor/bin/thermalcontrol_client /vendor/bin/
adb push out/target/product/rpi5/vendor/etc/vintf/manifest/thermalcontrol.xml \
         /vendor/etc/vintf/manifest/thermalcontrol.xml
adb push out/target/product/rpi5/vendor/etc/selinux/vendor_service_contexts \
         /vendor/etc/selinux/vendor_service_contexts

# System APK needs system partition write access:
adb shell mount -o remount,rw /
adb push out/target/product/rpi5/system/priv-app/ThermalMonitor/ThermalMonitor.apk \
         /system/priv-app/ThermalMonitor/ThermalMonitor.apk

adb reboot
```

## Summary of All Stability-Related Bugs

| # | Error | Root Cause | Fix |
|---|-------|-----------|-----|
| 1 | `IllegalArgumentException` at `transactNative` | Parcel stability mismatch: SYSTEM app → LOCAL vendor binder | Add `stability: "vintf"` |
| 2 | `owner` property required | `aidl_interface` in namespace without owner | Add `owner: "myoem"` |
| 3 | Module name `thermalcontrolservice-aidl-java` not found | Versioned AIDL uses `-V1-` infix | Update all references to `-V1-java`, `-V1-ndk` |
| 4 | `.hash` file missing | Frozen AIDL version requires hash snapshot | Run `hash_gen.sh` |
| 5 | `@VintfStability` annotation missing | VINTF stability requires annotation in AIDL file | Add `@VintfStability` to `.aidl` and snapshot files |
| 6 | `addService` fails: `-3` (`EX_ILLEGAL_ARGUMENT`) | VINTF service not in device VINTF manifest | Add `vintf_fragment` module + manifest XML |
| 7 | Service name not found | Name format changed for VINTF: add `/default` suffix | Update service name everywhere |

---

<a name="part-10"></a>
# Part 10: Lessons Learned — AOSP Development on RPi5

## The Big Picture — What We Built

```
5,000+ lines of code across:
├── 4 C++ source files (HAL + service)
├── 1 AIDL interface (versioned, VINTF-stable)
├── 1 Java library (ThermalControlManager)
├── 3 Kotlin files (MainActivity, ViewModel, ThermalScreen)
├── 4 SELinux policy files
├── 1 VINTF manifest fragment
├── 1 init RC file
└── 6 Android.bp build files
```

All in `vendor/myoem/`. Zero changes to `frameworks/`, `device/`, or any
existing AOSP module.

## Key Lessons by Category

### sysfs and Linux Thermal

1. **CPU temperature** is at `/sys/class/thermal/thermal_zone0/temp` in
   millidegrees — always divide by 1000.

2. **Fan hwmon index** is not stable across boots. Always discover it by
   scanning `hwmon*/name` for your driver name (`pwm-fan`).

3. **Switch to manual mode before writing PWM**. If you write `pwm1` while in
   auto mode (`pwm1_enable = 2`), the thermal governor immediately overrides it.

4. **sysfs write permissions**: kernel drivers typically create sysfs files as
   `root:root 0644`. Use `chown root system` + `chmod 0664` in the init RC
   `on boot` trigger — **in that order**. `chmod` alone changes permission bits
   but if the group is still `root`, your service (running as `system`) still
   can't write.

### Soong Build System

5. **`soong_namespace {}`** is required in any directory that is a target of
   `PRODUCT_SOONG_NAMESPACES`. Modules in different namespaces are invisible to
   each other unless explicitly imported with `imports: [...]`.

6. **AIDL module name suffixes** change with stability:
   - `unstable: true` → `thermalcontrolservice-aidl-java`, `-ndk`
   - `stability: "vintf"` → `thermalcontrolservice-aidl-V1-java`, `-V1-ndk`

7. **`static_libs` for AIDL stubs**, not `shared_libs`. AIDL-generated NDK
   and Java stubs are always statically linked.

8. **`sdk_version: "system_current"`** for vendor Java libraries that use
   system API types. `"current"` (public API) is insufficient if your
   dependencies use system API.

### Binder IPC

9. **Use `libbinder_ndk`** (LLNDK), never `libbinder` (VNDK), for vendor
   services that need to cross the system/vendor binder boundary. `libbinder`
   is the old VNDK path that doesn't work for cross-partition IPC in Android 13+.

10. **`@VintfStability` + VINTF manifest** is required for any vendor service
    that is called from a system partition app. This is not optional — the
    service manager actively enforces it.

11. **Service name format** for VINTF AIDL: `<package>.<Interface>/<instance>`,
    e.g., `com.myoem.thermalcontrol.IThermalControlService/default`.

### SELinux

12. **Check types before using them**: `ls -laZ /path/to/file` shows the actual
    SELinux label. Don't assume types like `sysfs_hwmon` exist.

13. **`service_contexts`** must match the exact service name string you register
    with `AServiceManager_addService`. If the name includes `/default`, the
    contexts entry must also include `/default`.

14. **SELinux policy changes require full image rebuild**. No workaround.
    Binary/library/APK changes can use `adb push`.

### AIDL Design

15. **Error codes as AIDL constants** (`const int ERROR_SYSFS_WRITE = 3`) are
    better than magic numbers — they're accessible in both C++ and Java.

16. **`ServiceSpecificException`** (C++) / `ServiceSpecificException` (Java)
    is the correct mechanism for returning domain errors over AIDL, not return
    codes mixed with data values.

17. **Java manager pattern**: if you need `@hide` APIs (like `ServiceManager`),
    put them in the `platform_apis: true` app and pass the result (an `IBinder`)
    down to the manager library. This lets the library stay at
    `sdk_version: "system_current"`.

### Android App Architecture

18. **ViewModel + StateFlow** is the correct Compose architecture. Never
    access binder services from the main thread — use `Dispatchers.IO`.

19. **Local Compose state for UI controls** (sliders, text fields) that can
    receive both user input and server updates. Use `remember { }` for local
    state that shouldn't be overwritten by polling, separate from the ViewModel's
    `UiState`.

20. **AOSP Compose theming**: use `android:Theme.DeviceDefault.NoActionBar`,
    not Material or AppCompat themes — those don't exist in AOSP builds without
    Google's extras.

### RPi5 / AOSP Deployment

21. **RPi5 has no fastboot**. `adb reboot bootloader` just reboots normally.
    The entire fastboot workflow doesn't apply.

22. **Remounting partitions for development:**
    ```bash
    adb root
    adb shell mount -o remount,rw /vendor   # for vendor changes
    adb shell mount -o remount,rw /         # for system changes
    ```

23. **VINTF manifest fragments** can be installed from `vendor/myoem/` using
    `vintf_fragment { vendor: true }`. They land in `/vendor/etc/vintf/manifest/`
    and the service manager reads them at runtime. No need to touch `device/`.

24. **Always push source RC files**, not build output RC files, during
    development. Build output can be stale if you didn't rebuild.

## The Final Stack — What It Feels Like to Use

When everything works:

```
[RPi5 display shows ThermalMonitor app]

┌─────────────────────────────────┐
│ ThermalMonitor           [AUTO] │
├─────────────────────────────────┤
│        CPU Temperature          │
│           47.3 °C               │
│         ● Warm                  │
├─────────────────────────────────┤
│ Fan Status                      │
│  35%      1850 RPM    Yes       │
│ Speed     RPM      Running      │
├─────────────────────────────────┤
│ Fan Control                     │
│ [Turn On] [Turn Off] [Auto]     │
│                                 │
│ Set Speed:                      │
│ ▓▓▓▓▓▓▓▓▓░░░░░░░░░░░░░░░░░░░   │
│ [35    ] [Apply]                │
└─────────────────────────────────┘
```

Tapping "Turn Off" → ViewModel calls `manager.setFanEnabled(false)` → Java AIDL
proxy sends binder transaction → `thermalcontrold` receives it → calls
`mHal->setFanEnabled(false)` → writes `1` to `pwm1_enable` then `0` to `pwm1` →
fan stops. The next 2-second poll updates the display.

## Appendix: Quick Command Reference

### Discovery
```bash
# Find CPU temp
adb shell cat /sys/class/thermal/thermal_zone0/temp

# Find fan hwmon
adb shell grep -r "pwm-fan" /sys/class/hwmon/*/name

# Check sysfs labels
adb shell ls -laZ /sys/class/hwmon/hwmon2/

# Check service registration
adb shell service list | grep thermalcontrol
```

### Development Workflow
```bash
# Rebuild service + client + app
m thermalcontrold thermalcontrol_client ThermalMonitor

# Push to device (no reboot needed for binary changes)
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/thermalcontrold /vendor/bin/
adb shell stop thermalcontrold
adb shell start thermalcontrold

# For APK updates
adb shell mount -o remount,rw /
adb push ThermalMonitor.apk /system/priv-app/ThermalMonitor/ThermalMonitor.apk
adb reboot
```

### Debugging
```bash
# Watch service logs
adb logcat -s thermalcontrold ThermalControlManager ThermalMonitor

# Check SELinux denials
adb shell dmesg | grep avc

# Verify service domain
adb shell ps -eZ | grep thermalcontrold

# Manually test without app
adb shell thermalcontrol_client temp
adb shell thermalcontrol_client on
adb shell thermalcontrol_client speed 50
adb shell thermalcontrol_client auto

# Check VINTF manifest is loaded
adb shell cat /vendor/etc/vintf/manifest/thermalcontrol.xml
```

### Full Image Build
```bash
. build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug

# Specific targets (faster than full build)
m thermalcontrold thermalcontrol_client ThermalMonitor thermalcontrol-vintf-fragment

# Full vendor image (needed for SELinux changes)
make vendorimage -j$(nproc)
```

---

*End of Series*

**Total project scope**: 4 development phases, 3 major debugging crises,
1 architectural redesign (ServiceManager relocation), and 7 VINTF/stability
errors — all resolved by understanding how Android's partition isolation model
actually works at the binder level.
