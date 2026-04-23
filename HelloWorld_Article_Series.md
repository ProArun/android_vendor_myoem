# Building a Native Daemon in AOSP — From Zero to `atest` Green
## The Simplest Possible Vendor Service: No Binder, No AIDL, Just a Loop

**A Complete Engineering Journal**

---

> **About this article**: Every AOSP vendor service we have built so far — calculator,
> BMI, ThermalControl, PIR Detector — starts with Binder IPC and AIDL. Before you
> can understand any of that, there is a simpler question: what is a native daemon,
> and how does it run at all?
>
> This article builds the absolute minimum: a C++ binary that prints `Hello World`
> to the Android logger every 30 seconds. No Binder, no AIDL, no HAL. Just a loop.
>
> That simplicity is the point. Every concept — `cc_binary`, `init_rc`, SELinux
> domain transitions, `vendor: true`, VTS testing — appears here in its purest form,
> with nothing else in the way. Everything more complex you build afterward is this
> foundation, extended.
>
> **Platform**: Raspberry Pi 5 · Android 15 · `android-15.0.0_r14`
> **Location**: `vendor/myoem/services/helloworld/`

---

## Table of Contents

1. [What Is a Native Daemon?](#part-1)
2. [The Architecture of Zero IPC](#part-2)
3. [The C++ Source — main.cpp](#part-3)
4. [The Build System — Android.bp](#part-4)
5. [The RC File — How Init Launches Your Daemon](#part-5)
6. [SELinux — Domain Transitions for a Simple Daemon](#part-6)
7. [Wiring It Into the Product — myoem_base.mk](#part-7)
8. [Build, Flash, and Verify](#part-8)
9. [CTS vs VTS — Why the Test Type Matters](#part-9)
10. [Writing the VTS Test](#part-10)
11. [Running the Test with atest](#part-11)
12. [Debugging Guide](#part-12)
13. [Lessons Learned — The Foundation Everything Else Builds On](#part-13)

---

<a name="part-1"></a>
# Part 1: What Is a Native Daemon?

## The Definition

A native daemon is a C or C++ binary that:

1. Starts automatically at boot (managed by Android's `init` process)
2. Runs continuously in the background (infinite loop or blocking call)
3. Lives on the vendor partition (`/vendor/bin/`)
4. Has its own SELinux security domain

That's it. There is no magic. A daemon is just a long-running process that `init`
spawns and keeps alive.

## Where Native Daemons Live in the AOSP Stack

```
┌─────────────────────────────────────────────────────────────────────┐
│                         USER APPS                                   │
│  Kotlin / Jetpack Compose — installed to /data/app/                 │
├─────────────────────────────────────────────────────────────────────┤
│                     JAVA FRAMEWORK                                  │
│  system_server (Java daemons — ActivityManagerService, etc.)        │
│  Lives on: /system/                                                 │
├─────────────────────────────────────────────────────────────────────┤
│                    NATIVE FRAMEWORK                                 │
│  mediaserver, audioserver, surfaceflinger (C++ daemons)             │
│  Lives on: /system/bin/                                             │
├═════════════════════════════════════════════════════════════════════╡  ← Treble line
│                     VENDOR DAEMONS  ← we work here                 │
│  thermalcontrold, calculatord, helloworldd                          │
│  Lives on: /vendor/bin/                                             │
├─────────────────────────────────────────────────────────────────────┤
│                        KERNEL                                       │
│  Linux — sysfs, GPIO, hwmon, drivers                                │
└─────────────────────────────────────────────────────────────────────┘
```

The thick line (`═`) is the **Treble partition boundary**. Everything above it
is the Android system partition — updated by Google OTAs. Everything below is
the vendor partition — controlled by the OEM (us). A native daemon lives below
that line: it is vendor code, independent of Android system updates.

## When Would You Write a Pure Daemon (No Binder)?

Most vendor daemons eventually expose a Binder service so apps can call them.
But there are legitimate cases for a pure loop daemon:

| Use Case | Why No Binder Needed |
|----------|---------------------|
| Watchdog / health monitor | Logs system state continuously, no client needs to query it |
| Hardware initializer | Runs once at boot, configures hardware, exits (or sleeps forever) |
| Telemetry agent | Collects metrics and writes to a file or socket, no IPC |
| Prototype / learning | You want to understand the daemon lifecycle before adding Binder |

Our `helloworldd` is the prototype case. The pattern is identical to a real
watchdog or telemetry daemon — it just does less work inside the loop.

---

<a name="part-2"></a>
# Part 2: The Architecture of Zero IPC

## Why This Is Simpler Than Any Other Project

The calculator, BMI, and thermal services all require:

```
AIDL interface    →  3 generated libraries (cpp / ndk / java stubs)
VINTF manifest    →  1 XML fragment for service registration
service_contexts  →  SELinux labels for the ServiceManager entry
service.te        →  SELinux type for the ServiceManager entry
main.cpp          →  AServiceManager_addService() + thread pool
```

The HelloWorld daemon requires none of this because it never registers with
`ServiceManager`. It does not receive IPC calls. It does not need a thread pool.

```
helloworldd
│
├── main.cpp           (30 lines)
├── Android.bp         (25 lines)
├── helloworld.rc      (5 lines)
└── sepolicy/private/
    ├── helloworldd.te  (5 lines — no binder rules)
    └── file_contexts   (3 lines)
```

Total: ~68 lines to define a complete, production-quality vendor daemon.

## The Execution Model

```
[Boot]
  │
  ▼
init reads /vendor/etc/init/helloworld.rc
  │
  ▼
init forks helloworldd process
  │
  ▼
SELinux transitions: init domain → helloworldd domain
  │
  ▼
main() runs:
  ALOGI("HelloWorld daemon started")
  while(true):
    ALOGI("Hello World")
    sleep(30)
```

The process never exits. `init` monitors it — if it crashes, `init` will
restart it based on the service definition in the RC file.

## The Logging Architecture

`ALOGI()` does not write to a file. It writes to `logd` — Android's log daemon.
The path is:

```
helloworldd                     logd
   │                              │
   │   (unix domain socket)       │
   ├──── /dev/socket/logd ───────►│
   │                              │   ──► /dev/log/main (ring buffer)
   │                              │   ──► adb logcat (client reads ring buffer)
```

The `LOG_TAG` macro defined at the top of `main.cpp` becomes the tag column in
`logcat` output. Filtering by `adb logcat -s HelloWorldDaemon:*` shows only
lines from our daemon.

---

<a name="part-3"></a>
# Part 3: The C++ Source — main.cpp

```cpp
// vendor/myoem/services/helloworld/src/main.cpp

// LOG_TAG must be defined before any #include.
// The ALOGI/ALOGE macros use the preprocessor to embed this tag at compile time.
// If any #include appears first and it transitively includes <log/log.h>,
// you get a compiler warning and potentially the wrong tag.
#define LOG_TAG "HelloWorldDaemon"

#include <log/log.h>
#include <unistd.h>

static constexpr unsigned int kPrintIntervalSeconds = 30;

int main() {
    ALOGI("HelloWorld daemon started");

    while (true) {
        ALOGI("Hello World");
        sleep(kPrintIntervalSeconds);
    }

    return 0;
}
```

## Every Line Explained

### `#define LOG_TAG "HelloWorldDaemon"`

This is the **first rule of AOSP C++ logging**: define `LOG_TAG` before any
`#include`. The `ALOGI`, `ALOGE`, and other macros from `<log/log.h>` use
`LOG_TAG` at the preprocessor level to set the tag. If you put an `#include`
first, and that header transitively includes `<log/log.h>`, the tag gets set
to the default (`"unknown"`) and your log lines will be untaggable in `logcat`.

### `#include <log/log.h>`

This is the Android logging header from `liblog`. It provides:

| Macro | Level | When to use |
|-------|-------|-------------|
| `ALOGV` | Verbose | Extremely frequent messages (disabled in release builds by default) |
| `ALOGD` | Debug | Debug information, disabled by default on user builds |
| `ALOGI` | Info | Normal operational events — service started, periodic status |
| `ALOGW` | Warning | Unexpected but recoverable conditions |
| `ALOGE` | Error | Errors that require attention but don't crash the process |

We use `ALOGI` because "Hello World" is an informational message. It will appear
in logcat on all build variants (debug, userdebug, user).

### `#include <unistd.h>`

Standard POSIX header. Provides `sleep(seconds)` which we use to pause the loop.
`sleep()` is a blocking call that relinquishes the CPU — the kernel removes the
process from the run queue for the specified time. It does not busy-wait.

### `static constexpr unsigned int kPrintIntervalSeconds = 30`

The 30-second interval as a named constant. `static constexpr` means:
- `constexpr`: evaluated at compile time, not runtime
- `static`: function-local linkage (no symbol exported)

This is the AOSP/LLVM preferred style for integer constants. Avoid `#define NUM 30`
— that's a C macro, not a typed constant.

### `ALOGI("HelloWorld daemon started")`

One-time startup message. When you run `adb logcat -s HelloWorldDaemon:*`, this
message tells you exactly when the daemon started. It's also how you verify the
daemon launched at all before waiting 30 seconds for the first "Hello World".

### `while (true) { ALOGI("Hello World"); sleep(kPrintIntervalSeconds); }`

The main loop. It runs indefinitely:

1. Log "Hello World" to logd
2. Sleep for 30 seconds (CPU is idle during this time)
3. Repeat

A real daemon would replace `ALOGI("Hello World")` with meaningful work:
reading a sensor, checking a hardware state, collecting metrics. The structure
is identical.

### `return 0;`

Unreachable, but required by C++ to satisfy `int main()`. The compiler would
warn without it on some configurations.

---

<a name="part-4"></a>
# Part 4: The Build System — Android.bp

```bp
// vendor/myoem/services/helloworld/Android.bp

soong_namespace {}

package {
    default_applicable_licenses: ["Android-Apache-2.0"],
}

cc_binary {
    name: "helloworldd",
    vendor: true,
    init_rc: ["helloworld.rc"],

    srcs: ["src/main.cpp"],

    shared_libs: [
        "liblog",
    ],

    cflags: [
        "-Wall",
        "-Wextra",
        "-Werror",
    ],
}
```

## Each Field Explained

### `soong_namespace {}`

Declares a Soong namespace for this directory. Required by any directory listed
in `PRODUCT_SOONG_NAMESPACES`. Without this, the Soong build system cannot
locate the `Android.bp` file when resolving module dependencies.

An empty `soong_namespace {}` means this directory has no namespace **imports**
(it does not depend on modules from other namespaces). If we depended on a HAL
from another namespace (like `libthermalcontrolhal`), we would write:
```bp
soong_namespace {
    imports: ["vendor/myoem/hal/thermalcontrol"],
}
```

### `cc_binary`

Compiles a C++ executable. The key Soong module types and what they produce:

| Module Type | Output | Installed To |
|-------------|--------|-------------|
| `cc_binary` | ELF executable | `/vendor/bin/` (with `vendor: true`) |
| `cc_library_shared` | `.so` shared library | `/vendor/lib64/` |
| `cc_library_static` | `.a` static library | linked into binary at build time |
| `cc_test` | ELF test binary | pushed to device for testing |

### `name: "helloworldd"`

The module name. This is also the output binary name: the build produces
`/vendor/bin/helloworldd`. By convention:
- Daemon names end in `d` (from the Unix tradition: `httpd`, `sshd`, `crond`)
- This makes them distinguishable from their client binaries in `ps` output

### `vendor: true`

**The most important flag for vendor binaries.**

Without `vendor: true`, Soong would install the binary to `/system/bin/` and
link it against system partition libraries. With `vendor: true`:

1. Binary installs to `/vendor/bin/`
2. Only VNDK and LLNDK libraries are available for linking
3. The binary is subject to vendor SELinux policies
4. The binary is part of the vendor image, not the system image

**Why does this matter?** The vendor partition and system partition are separate
disk partitions. They can be updated independently (Project Treble). If your
binary ends up on the wrong partition, it either won't be found at runtime or
it will be updated/wiped by a system OTA.

### `init_rc: ["helloworld.rc"]`

Tells the build system to install `helloworld.rc` to `/vendor/etc/init/`.
Android's `init` process scans all files in `/vendor/etc/init/` at boot and
processes them. This is how `init` learns about our daemon — we don't modify
any global `init.rc` file. Each module carries its own RC fragment.

### `shared_libs: ["liblog"]`

The only dependency: `liblog`, Android's logging library. It provides the
`ALOGI`, `ALOGE`, etc. macros. `liblog` is an LLNDK library — it is available
to vendor code, crosses the Treble partition boundary, and is stable across
Android versions.

**LLNDK vs VNDK:**
| Type | Full Name | Available In | Notes |
|------|-----------|-------------|-------|
| LLNDK | Low-Level NDK | `/vendor/` | Stable ABI, usable from vendor code |
| VNDK | Vendor NDK | `/vendor/` | Vendor-specific, may change across Android versions |

`liblog` is LLNDK. For a real binder daemon you would also add `libbinder_ndk`
(also LLNDK). You would **never** add `libbinder` (plain binder) in vendor code
— that is a system partition library.

### `cflags: ["-Wall", "-Wextra", "-Werror"]`

Standard AOSP warning flags:
- `-Wall`: enable common warnings
- `-Wextra`: enable extra warnings
- `-Werror`: treat warnings as errors — ensures the code is warning-clean

This is standard across all modules in `vendor/myoem/`. Keeping warnings as
errors prevents warnings from accumulating silently.

---

<a name="part-5"></a>
# Part 5: The RC File — How Init Launches Your Daemon

```
# vendor/myoem/services/helloworld/helloworld.rc

service helloworldd /vendor/bin/helloworldd
    class main
    user system
    group system
```

## How Init Works

Android's `init` process (PID 1) is the first userspace process the kernel
starts. It reads RC files from:
- `/init.rc` (core system)
- `/system/etc/init/*.rc` (system partition services)
- `/vendor/etc/init/*.rc` (vendor partition services — our file goes here)

The RC file language is a simple declarative language. Our file defines one
service with three properties.

## Line by Line

### `service helloworldd /vendor/bin/helloworldd`

Syntax: `service <name> <path> [args...]`

- `helloworldd` — the service name. `init` uses this to track the process
  internally. Must be unique across all RC files on the system. You refer
  to this name in `start helloworldd` / `stop helloworldd` commands.
- `/vendor/bin/helloworldd` — absolute path to the executable. Must exactly
  match where Soong installs the binary (determined by `vendor: true`).

### `class main`

Services are grouped into classes. The Android boot sequence starts classes in
order:
```
core    → critical system services (logd, servicemanager, vold)
hal     → HAL services (vendor HALs that hardware depends on)
main    → application-layer services ← helloworldd starts here
late_start → services that need the framework to be running first
```

`class main` is the right class for a daemon that:
- Does not provide a HAL interface that other services depend on
- Does not need the Java framework to be running
- Should start alongside other application-level vendor services

### `user system`

The Unix user ID the daemon runs as. Options for vendor daemons:

| User | UID | When to Use |
|------|-----|-------------|
| `root` | 0 | Avoid — only if truly needed (kernel driver init) |
| `system` | 1000 | Trusted system service, common for vendor daemons |
| `nobody` | 65534 | Untrusted, minimal privilege |

We use `system` (UID 1000) because it is the standard user for trusted vendor
services. This user can interact with ServiceManager, write to system properties,
and access most system resources a daemon needs. For a pure logging daemon, even
`nobody` would work — but `system` is the convention.

### `group system`

The Unix group IDs the daemon's process is a member of. A daemon can be in
multiple groups: `group system audio net_admin`. Groups determine which
supplementary resources the process can access.

For `helloworldd`, `group system` is sufficient — we only write to logd, and
that is allowed by the base domain policy.

## What Happens at Boot

```
1. Kernel starts init (PID 1)
2. init reads all RC files including /vendor/etc/init/helloworld.rc
3. init parses "service helloworldd /vendor/bin/helloworldd"
4. When class "main" is started (during the boot sequence),
   init forks a child process:
     - forks
     - sets UID/GID to system/system
     - SELinux transitions from init to helloworldd domain
     - exec /vendor/bin/helloworldd
5. Our main() runs
6. init watches the PID — if the process dies, init restarts it
   (unless "oneshot" is in the service definition)
```

## The `oneshot` Modifier

Our daemon does not use `oneshot`. What it does:

```
# With oneshot: init runs the binary once, does NOT restart on exit
service myinitscript /vendor/bin/hw_init
    class hal
    oneshot

# Without oneshot (our case): init restarts the binary if it dies
service helloworldd /vendor/bin/helloworldd
    class main
    user system
    group system
```

For a daemon with a `while(true)` loop, omitting `oneshot` is correct — if
the daemon crashes, `init` will restart it. For a one-shot hardware
initializer that exits after completing, use `oneshot`.

---

<a name="part-6"></a>
# Part 6: SELinux — Domain Transitions for a Simple Daemon

SELinux is mandatory in Android. Every process runs in a **domain** (a set of
permissions). Without a domain policy, `init` cannot start your process.

## The Two Files

### `helloworldd.te` — The Policy

```
# vendor/myoem/services/helloworld/sepolicy/private/helloworldd.te

type helloworldd, domain;
type helloworldd_exec, exec_type, vendor_file_type, file_type;

init_daemon_domain(helloworldd)
```

**Line 1: `type helloworldd, domain;`**

Declares a new SELinux type named `helloworldd` and assigns it to the attribute
`domain`. Every process domain must have the `domain` attribute — it is what
makes SELinux recognize this type as a process domain (as opposed to a file
type).

**Line 2: `type helloworldd_exec, exec_type, vendor_file_type, file_type;`**

Declares a type for the **executable file** on disk. This is separate from the
process domain because SELinux labels files and processes independently.

- `exec_type` — attribute required for any executable file type
- `vendor_file_type` — attribute required for files on the vendor partition
- `file_type` — attribute required for all file types

**Line 3: `init_daemon_domain(helloworldd)`**

This is an M4 macro from AOSP's base SELinux policy. It expands to:

```
# Allow init to fork, exec, and transition into the helloworldd domain
allow init helloworldd_exec:file { read open execute };
allow init helloworldd:process transition;
allow helloworldd helloworldd_exec:file { entrypoint read execute open };

# Grant basic permissions every daemon needs:
# - stdin/stdout/stderr file descriptors
# - /proc/self reads (for stack unwinding, crash reports)
# - Signal sending/receiving
# - Basic memory operations
allow helloworldd self:process { fork signal sigchld ... };
# ... and more
```

The macro exists to avoid copy-pasting 30+ SELinux allow rules into every
daemon's `.te` file. It encodes the invariant: "init launches this, it runs
as this domain, it gets basic process permissions."

## The Binder Rules We Did NOT Need

Compare to `calculatord.te` which needs additional rules:

```
# calculatord.te (for reference — NOT in helloworldd.te)
binder_use(calculatord)        ← opens /dev/binder
binder_service(calculatord)    ← can be found by other processes via Binder
add_service(calculatord, calculatord_service)  ← can register with ServiceManager
```

`helloworldd` needs none of these because it never touches `/dev/binder` or
`ServiceManager`. The minimal policy is correct and intentional — the principle
of least privilege.

### `file_contexts` — The File Label

```
# vendor/myoem/services/helloworld/sepolicy/private/file_contexts

/vendor/bin/helloworldd    u:object_r:helloworldd_exec:s0
```

This file tells the SELinux labeling system: when the filesystem is built (or
when `restorecon` runs), label `/vendor/bin/helloworldd` with
`u:object_r:helloworldd_exec:s0`.

The format `u:object_r:<type>:s0`:
- `u` — user: always `u` in Android
- `object_r` — role: always `object_r` for files
- `helloworldd_exec` — the type we declared in `helloworldd.te`
- `s0` — sensitivity level: always `s0` (MLS is not used in Android)

**Why This File Is Critical**

Without the `file_contexts` entry, the binary file gets the default label for
vendor executables: `u:object_r:vendor_file:s0`. The `init_daemon_domain` macro
grants transition permission specifically from `helloworldd_exec` — not from
`vendor_file`. If the file has the wrong label, `init` cannot transition into
the `helloworldd` domain and the process is either blocked or runs in the wrong
domain.

## The `BOARD_VENDOR_SEPOLICY_DIRS` Requirement

In `myoem_base.mk`:

```makefile
BOARD_VENDOR_SEPOLICY_DIRS += vendor/myoem/services/helloworld/sepolicy/private
```

**Why `BOARD_VENDOR_SEPOLICY_DIRS` and not `PRODUCT_PRIVATE_SEPOLICY_DIRS`?**

This is a common mistake. The two variables feed different policy compilation
paths:

| Variable | Feeds | Compiled Into | Installed To |
|----------|-------|--------------|-------------|
| `BOARD_VENDOR_SEPOLICY_DIRS` | `vendor_sepolicy.cil` | Vendor policy | `/vendor/etc/selinux/` |
| `PRODUCT_PRIVATE_SEPOLICY_DIRS` | `product_sepolicy.cil` | Product policy | `/product/etc/selinux/` |

Vendor binaries (`/vendor/bin/helloworldd`) are governed by the vendor SELinux
policy. Their domain types and file labels must be in `vendor_sepolicy.cil`.
If you put them in `PRODUCT_PRIVATE_SEPOLICY_DIRS`, the types exist in a
different policy blob and the `file_contexts` match will fail — `init` won't
find the domain transition rule.

---

<a name="part-7"></a>
# Part 7: Wiring It Into the Product — myoem_base.mk

Three additions to `vendor/myoem/common/myoem_base.mk`:

```makefile
# 1. Soong namespace — lets the build system find Android.bp
PRODUCT_SOONG_NAMESPACES += \
    ...
    vendor/myoem/services/helloworld

# 2. Product package — tells the build to include the binary in the vendor image
PRODUCT_PACKAGES += \
    ...
    helloworldd

# 3. SELinux policy directory
BOARD_VENDOR_SEPOLICY_DIRS += \
    ...
    vendor/myoem/services/helloworld/sepolicy/private
```

## Why All Three Are Required

**Missing `PRODUCT_SOONG_NAMESPACES`**: Soong cannot find the `Android.bp`. The
module `helloworldd` is undefined. Any `PRODUCT_PACKAGES` reference fails with
`No rule to make target 'helloworldd'`.

**Missing `PRODUCT_PACKAGES`**: The binary is built but not included in the
vendor filesystem image. It will not be present on the device at `/vendor/bin/`.

**Missing `BOARD_VENDOR_SEPOLICY_DIRS`**: The `.te` and `file_contexts` files
are not compiled into the policy. The binary exists on disk but has no SELinux
domain. `init` will fail to start it with an avc denial.

All three are required. Missing any one of them produces a different failure mode.

---

<a name="part-8"></a>
# Part 8: Build, Flash, and Verify

## Build

```bash
# Source the environment (new terminal required before any m command)
source build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug

# Build only the helloworld daemon
m helloworldd
```

Expected output:
```
[ 50%] Building: vendor/myoem/services/helloworld/src/main.cpp
[100%] Install: out/target/product/rpi5/vendor/bin/helloworldd
#### build completed successfully ####
```

## Flash the Vendor Image

SELinux policy changes require a full vendor image rebuild and flash. Source file
edits alone are insufficient — the policy is compiled separately.

```bash
# Build the complete vendor image (includes updated SELinux policy)
make vendorimage -j$(nproc)

# Write to SD card (check your SD card device with lsblk first)
sudo dd if=out/target/product/rpi5/vendor.img of=/dev/sdX bs=4M status=progress
sync
```

## Dev Iteration (Skipping Full Flash)

For code-only changes (no SELinux policy changes), you can push directly:

```bash
adb root
adb shell mount -o remount,rw /vendor

# Push the source binary (vendor/myoem/... not out/...)
adb push out/target/product/rpi5/vendor/bin/helloworldd /vendor/bin/helloworldd

# Restart the daemon
adb shell stop helloworldd
adb shell start helloworldd
```

**Important**: Push the build output (`out/target/.../helloworldd`), not the
source file. The source is C++ — it needs to be compiled first.

## Verify the Daemon Is Running

```bash
# Check process is alive
adb shell ps -eZ | grep helloworldd

# Expected output:
# u:r:helloworldd:s0  system  <pid>  1  /vendor/bin/helloworldd

# Watch the log output
adb logcat -s HelloWorldDaemon:*
```

Expected logcat output:
```
04-24 12:00:00.000  1234  1234 I HelloWorldDaemon: HelloWorld daemon started
04-24 12:00:00.001  1234  1234 I HelloWorldDaemon: Hello World
04-24 12:00:30.001  1234  1234 I HelloWorldDaemon: Hello World
04-24 12:01:00.001  1234  1234 I HelloWorldDaemon: Hello World
```

The `I` in column 6 is the log level (`I` = Info, from `ALOGI`). The tag
`HelloWorldDaemon` is our `LOG_TAG`. The 30-second spacing confirms the
`sleep(30)` is working.

## Diagnosing No Output

If you see nothing in logcat:

```bash
# 1. Check if the binary exists on device
adb shell ls -la /vendor/bin/helloworldd

# 2. Check if init tried to start it (and failed)
adb logcat -d | grep helloworldd

# 3. Check SELinux denials
adb shell dmesg | grep "avc: denied" | grep helloworldd

# 4. Check the service file was installed
adb shell ls /vendor/etc/init/ | grep hello

# 5. Check the SELinux label on the binary
adb shell ls -laZ /vendor/bin/helloworldd
# Must be: u:object_r:helloworldd_exec:s0
# If it says: u:object_r:vendor_file:s0 → file_contexts not applied → reflash
```

---

<a name="part-9"></a>
# Part 9: CTS vs VTS — Why the Test Type Matters

Before writing the test, the most important question: **which test suite?**

This question has real consequences. The wrong answer produces build errors that
are confusing because they look like dependency problems when they are actually
policy violations.

## The Two Test Universes

| | CTS | VTS |
|---|-----|-----|
| Full name | Compatibility Test Suite | Vendor Test Suite |
| Purpose | Verify Android public API compatibility | Verify vendor code correctness |
| Who uses it | Google, for device certification | OEMs, for vendor validation |
| Can it see vendor processes? | No | Yes |
| Build type | `android_test` or `cc_test` | `cc_test` (with `vendor: true`) |
| Runner | JUnit on device | gtest on device |
| `platform_apis` allowed? | No | Yes |
| Right for `helloworldd`? | **No** | **Yes** |

## Why CTS Cannot Test helloworldd

CTS tests are designed to run on **any** Android device — including devices that
have never heard of `helloworldd`. A CTS module cannot:
- Reference vendor-specific processes by name
- Call `@hide` Android APIs
- Use `platform_apis: true`
- Live in `vendor/`

A CTS test that checks for `helloworldd` would fail on every device except ours.
CTS is for testing the Android contract (does `Intent.putExtra()` work?), not
for testing OEM-specific vendor code.

## Why VTS Is the Right Choice

VTS is built specifically for vendor code. A `cc_test` with:

```bp
cc_test {
    name: "VtsHelloWorldTest",
    vendor: true,          // lives on vendor partition, next to helloworldd
    test_suites: ["vts"],  // registered with the VTS harness
    ...
}
```

This test:
- Runs with vendor-level privileges (can observe vendor processes)
- Lives on the same partition as the code it tests
- Is registered with `atest` and the VTS test harness
- Is the industry standard for this type of test

## The Industry Standard Decision Rule

```
IF the code lives in /vendor/ and is written in C++:
    → cc_test, vendor: true, test_suites: ["vts"]
    → Name convention: VtsXxxTest

IF the code is a Java/Kotlin manager library (system or vendor partition):
    → android_test, platform_apis: true, test_suites: ["general-tests"]
    → Name convention: MyOemXxxTests

IF the code only uses public Android APIs and is generic:
    → android_test, sdk_version: "current", test_suites: ["cts"]
    → Name convention: CtsXxxTest
```

`helloworldd` is C++, lives in `/vendor/`, is OEM-specific → VTS.

---

<a name="part-10"></a>
# Part 10: Writing the VTS Test

## What to Test in a Logging Daemon

A pure logging daemon has no Binder interface to call, no return values to
check, no AIDL methods to invoke. So what do we test?

The two observable facts about a running daemon:

1. **The process exists** — verifiable with `pidof`
2. **The log output appears** — verifiable with `logcat`

These two assertions cover the complete startup and operational path:
- Process exists → RC file was processed, SELinux transition succeeded, binary launched
- Log output appears → `main()` ran, `liblog` is linked correctly, logd is receiving

## The Test File

```cpp
// vendor/myoem/services/helloworld/tests/VtsHelloWorldTest.cpp

#define LOG_TAG "VtsHelloWorldTest"

#include <gtest/gtest.h>
#include <log/log.h>

#include <cstdio>
#include <string>
#include <unistd.h>

// Run a shell command and return its stdout as a string.
static std::string shellOutput(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return {};
    char buf[256];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    return result;
}

// ── Test 1: process is alive ──────────────────────────────────────────────────
//
// pidof returns the PID (non-empty) when the process is running.
// This confirms the RC file was processed and init launched the daemon.
//
TEST(HelloWorldDaemonTest, ProcessIsRunning) {
    std::string pid = shellOutput("pidof helloworldd");

    if (!pid.empty() && pid.back() == '\n') pid.pop_back();

    EXPECT_FALSE(pid.empty())
        << "helloworldd is not running — check RC file and SELinux denials";
    ALOGD("helloworldd PID: %s", pid.c_str());
}

// ── Test 2: "Hello World" appears in logcat ───────────────────────────────────
//
// The daemon logs once at startup and then every 30 seconds.
// We poll logcat for up to 35 seconds so the test passes even if it starts
// just after a cycle boundary.
//
// Why 35s and not 30s? One-second buffer for scheduling jitter.
//
TEST(HelloWorldDaemonTest, LogsHelloWorldMessage) {
    constexpr int kMaxWaitSeconds     = 35;
    constexpr int kPollIntervalSeconds = 2;

    for (int waited = 0; waited <= kMaxWaitSeconds; waited += kPollIntervalSeconds) {
        std::string output = shellOutput("logcat -d -s HelloWorldDaemon:I 2>/dev/null");

        if (output.find("Hello World") != std::string::npos) {
            ALOGD("Found 'Hello World' in logcat after %d seconds", waited);
            SUCCEED();
            return;
        }

        if (waited < kMaxWaitSeconds) {
            sleep(kPollIntervalSeconds);
        }
    }

    FAIL() << "'Hello World' not found in logcat after " << kMaxWaitSeconds
           << " seconds.\nRun: adb logcat -s HelloWorldDaemon:* to debug.";
}
```

## Design Decisions in the Test

### Why `popen()` instead of calling system APIs directly?

`helloworldd` has no API surface — no Binder, no socket, no shared memory.
The only observable behavior is:
1. The process in the process table
2. The log output in logd

`popen()` lets the test inspect both using standard shell tools (`pidof`,
`logcat`) that the Android runtime already provides. This is not ideal for
production test code (shell parsing is fragile), but for a daemon with no API
surface, it is the appropriate tool.

For daemons with Binder interfaces, you would instead call
`AServiceManager_waitForService()` and invoke AIDL methods directly — see the
VTS tests for `calculatord` and `thermalcontrold`.

### Why poll logcat instead of checking once?

The daemon logs "Hello World" at two points:
1. At startup (always)
2. Every 30 seconds thereafter

If the test runs immediately after a "Hello World" was logged, the next one is
29 seconds away. By polling every 2 seconds for up to 35 seconds, we guarantee
the test passes regardless of where in the 30-second cycle the test starts.

The poll interval (2 seconds) is small enough to be responsive but large enough
to not spam `logcat` with excessive calls.

### Why `logcat -d` (dump-and-exit)?

`logcat -d` reads the in-memory ring buffer and exits. The alternative, `logcat`
without `-d`, would follow the log and never return. The test would hang forever.
`-d` is always the right flag in test code.

### Why `-s HelloWorldDaemon:I`?

`-s HelloWorldDaemon:I` means: show only lines where:
- Tag is exactly `HelloWorldDaemon` (our `LOG_TAG`)
- Level is `I` (Info) or higher

Without the `-s` filter, the output would contain thousands of lines from other
processes, and `find("Hello World")` might match unrelated log lines that happen
to contain those words.

## The Test Android.bp

```bp
// vendor/myoem/services/helloworld/tests/Android.bp

cc_test {
    name: "VtsHelloWorldTest",

    // vendor: true — binary lives on vendor partition, same as helloworldd
    vendor: true,

    srcs: ["VtsHelloWorldTest.cpp"],

    shared_libs: [
        "liblog",    // ALOGD for test diagnostics
    ],

    static_libs: [
        "libgtest",
        "libgtest_main",
    ],

    test_suites: ["vts"],

    cflags: [
        "-Wall",
        "-Wextra",
        "-Werror",
    ],
}
```

### `static_libs: ["libgtest", "libgtest_main"]`

gtest is statically linked into the test binary. This is different from Java
tests where JUnit is bundled as a DEX in the APK — but the principle is the same:
the test framework is not pre-installed on the device, so it must be bundled into
the test artifact.

`libgtest_main` provides the `main()` function that initializes gtest, parses
`--gtest_filter` flags, and runs the test cases. Without it, you would need to
write your own `main()` with `RUN_ALL_TESTS()`.

### Why no AIDL stubs in `static_libs`?

Compare to `VtsCalculatorServiceTest` which has:
```bp
static_libs: ["calculatorservice-aidl-ndk"]
```

`helloworldd` has no AIDL interface, so there are no stubs. The test binary
is smaller and has fewer dependencies — exactly what we want for the simplest
possible daemon test.

---

<a name="part-11"></a>
# Part 11: Running the Test with atest

## Prerequisites

```bash
# Source environment (required every new terminal session)
source build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug

# Device connected
adb devices
# 192.168.x.x:5555   device
```

## Build the Test

```bash
m VtsHelloWorldTest
```

Expected:
```
[100%] Install: out/target/product/rpi5/testcases/VtsHelloWorldTest/arm64/VtsHelloWorldTest
#### build completed successfully ####
```

## Run the Test

```bash
atest VtsHelloWorldTest
```

`atest` automatically:
1. Finds `VtsHelloWorldTest` in `module-info.json`
2. Creates a staging directory on the device under `/data/local/tests/vendor/VtsHelloWorldTest/`
3. Pushes the test binary
4. Runs it on the device
5. Pulls results back
6. Displays the summary

## Reading the Output

**Passing:**
```
arm64-v8a VtsHelloWorldTest
---------------------------
[==========] Running 2 tests from 1 test suite.
[ RUN      ] HelloWorldDaemonTest.ProcessIsRunning
[       OK ] HelloWorldDaemonTest.ProcessIsRunning (3 ms)
[ RUN      ] HelloWorldDaemonTest.LogsHelloWorldMessage
[       OK ] HelloWorldDaemonTest.LogsHelloWorldMessage (2154 ms)
[==========] 2 tests from 1 test suite ran. (2157 ms total)
[  PASSED  ] 2 tests.

Passed: 2, Failed: 0
```

Note that `LogsHelloWorldMessage` takes ~2 seconds — that is the poll finding
the startup message quickly. If the daemon was running for a long time without
fresh log entries, it could take up to 35 seconds.

**ProcessIsRunning fails:**
```
[ RUN      ] HelloWorldDaemonTest.ProcessIsRunning
[  FAILED  ] HelloWorldDaemonTest.ProcessIsRunning
  helloworldd is not running — check RC file and SELinux denials
```

Diagnosis: daemon did not start. Check:
```bash
adb shell ls /vendor/etc/init/ | grep hello   # RC file installed?
adb shell ls -laZ /vendor/bin/helloworldd     # correct SELinux label?
adb shell dmesg | grep "avc: denied" | grep helloworld
```

**LogsHelloWorldMessage fails:**
```
[ RUN      ] HelloWorldDaemonTest.LogsHelloWorldMessage
[  FAILED  ] HelloWorldDaemonTest.LogsHelloWorldMessage
  'Hello World' not found in logcat after 35 seconds.
```

Diagnosis: process is running but not logging. Check:
```bash
adb logcat -s HelloWorldDaemon:*   # any output at all?
adb shell ls -la /dev/socket/logd  # logd socket exists?
```

## Common Error: Stale File Blocking the Push

If you see:
```
RUNNER ERROR: Attempting to push dir '.../VtsHelloWorldTest' to an existing
device file '/data/local/tests/vendor/VtsHelloWorldTest'
```

This means a previous push left a plain file at that path. `atest` wants to
push a directory. Fix:

```bash
adb root
adb shell rm /data/local/tests/vendor/VtsHelloWorldTest
atest VtsHelloWorldTest
```

This is a one-time cleanup. It happens when a binary was manually pushed to
that path before `atest` ran.

## Run a Specific Test Only

```bash
# Run only the process check
atest VtsHelloWorldTest -- --gtest_filter="HelloWorldDaemonTest.ProcessIsRunning"

# Run only the logcat check
atest VtsHelloWorldTest -- --gtest_filter="HelloWorldDaemonTest.LogsHelloWorldMessage"
```

---

<a name="part-12"></a>
# Part 12: Debugging Guide

## The Four Layers of Failure

Every problem with a native daemon falls into one of four categories:

```
Layer 1: Build failure  →  Binary doesn't exist on device
Layer 2: Init failure   →  Binary exists but init won't start it
Layer 3: SELinux denial →  Init starts it but SELinux blocks operations
Layer 4: Runtime failure→  Process runs but behavior is wrong
```

## Layer 1: Build Failures

**Symptom**: `m helloworldd` fails.

```bash
# Missing header
main.cpp:3:10: fatal error: 'log/log.h' file not found
# Fix: Add "liblog" to shared_libs in Android.bp

# Missing namespace
Android.bp:1:1: error: module "helloworldd" is not in the dependency tree
# Fix: Add vendor/myoem/services/helloworld to PRODUCT_SOONG_NAMESPACES

# LOG_TAG warning (not error, but with -Werror becomes error)
main.cpp:5:2: error: "LOG_TAG" redefined [-Werror,-Wmacro-redefined]
# Fix: Move #define LOG_TAG above all #include lines
```

## Layer 2: Init Failures

**Symptom**: Binary on device, but daemon not running.

```bash
# Verify the RC file was installed
adb shell ls /vendor/etc/init/ | grep hello
# Should show: helloworld.rc

# If missing: check init_rc in Android.bp, rebuild + reflash vendor image

# Check init logs for parsing errors
adb logcat -d -b system | grep -i "helloworldd"
# Look for: "service not found" or "Could not start service 'helloworldd'"

# Manually trigger init to start the service
adb root
adb shell start helloworldd

# Check if it started
adb shell ps -e | grep helloworldd
```

## Layer 3: SELinux Denials

**Symptom**: `init` tried to start the service but SELinux blocked it.

```bash
# Check kernel audit log for denials
adb shell dmesg | grep "avc: denied" | grep helloworldd

# Common denial patterns:
# avc: denied { entrypoint } for pid=1234 comm="(helloworldd)"
#   path="/vendor/bin/helloworldd" scontext=u:r:init:s0
#   tcontext=u:object_r:vendor_file:s0 tclass=file
# → file_contexts not applied. Check: adb shell ls -laZ /vendor/bin/helloworldd
# → Must be: u:object_r:helloworldd_exec:s0

# Verify the label on the binary
adb shell ls -laZ /vendor/bin/helloworldd

# Check the running domain of the process (once it starts)
adb shell ps -eZ | grep helloworldd
# Expected: u:r:helloworldd:s0   system  <pid>
# If:       u:r:vendor:s0        → domain transition failed
```

**Forcing SELinux permissive for debugging (NOT for production):**
```bash
adb root
adb shell setenforce 0   # permissive mode globally
adb shell start helloworldd
# If it starts now, the issue is SELinux policy, not init config
adb shell setenforce 1   # re-enable enforcing
```

## Layer 4: Runtime Failures

**Symptom**: Process is running, but no log output.

```bash
# Check if logd is running
adb shell ps -e | grep logd

# Verify the log socket exists
adb shell ls -la /dev/socket/logd

# Try writing a manual log message from the helloworldd domain
# (confirm it works from a shell as system user)
adb shell
su system
log -t HelloWorldDaemon -p I "manual test message"
logcat -d -s HelloWorldDaemon:*
```

## Quick Diagnostic One-Liner

```bash
# The complete health check in one command
adb shell "
  echo '=== Binary ===' && ls -laZ /vendor/bin/helloworldd 2>&1;
  echo '=== RC file ===' && ls /vendor/etc/init/ | grep hello;
  echo '=== Process ===' && ps -eZ | grep helloworldd;
  echo '=== Denials ===' && dmesg | grep 'avc: denied' | grep hello | tail -5;
  echo '=== Logcat ===' && logcat -d -s HelloWorldDaemon:* | tail -10;
"
```

---

<a name="part-13"></a>
# Part 13: Lessons Learned — The Foundation Everything Else Builds On

## What This Project Teaches That No Other Project Can

Every other project in this series — calculator, BMI, ThermalControl, PIR Detector
— has Binder complexity layered on top of everything. When something goes wrong,
it is hard to know whether the problem is in the daemon startup, the SELinux policy,
the AIDL binding, the service registration, or the client code.

`helloworldd` has none of that layering. If it doesn't work, the problem space is
exactly four files and five concepts. This is why it is the right project to revisit
when debugging anything else — reduce the other project to this pattern and you can
isolate the layer where things break.

## The Five Concepts, Summarized

**1. `vendor: true` is not optional.**
It determines partition placement, which determines what libraries you can link,
which determines whether Treble is honored. Forgetting it puts your binary on
the system partition where it doesn't belong.

**2. `#define LOG_TAG` must be first.**
This is a preprocessor rule. If any header is included before it, the tag is
baked into the log macros before you define yours. The AOSP build team enforced
this with Clang-Tidy checks. Follow the rule, don't fight it.

**3. The RC file is how init learns about your daemon.**
You never edit the global `init.rc`. You deliver an RC fragment via `init_rc`
in `Android.bp`, which installs it to `/vendor/etc/init/`. Init reads all files
in that directory at boot. This is the vendor-partition-safe way to define
services.

**4. SELinux requires two files, not one.**
`helloworldd.te` defines the **domain** (what the process can do).
`file_contexts` defines the **label** (what the executable file is called).
The domain transition (`init` → `helloworldd`) requires both: the policy rule
in `.te` AND the label on the file. A missing or wrong label means the transition
never fires, even if the policy is perfect.

**5. VTS tests vendor code. CTS tests public Android APIs.**
These are not interchangeable. A `cc_test` with `vendor: true` and
`test_suites: ["vts"]` is the standard for testing vendor C++ daemons. Putting
it in `test_suites: ["cts"]` would be a mistake both architecturally (CTS can't
see vendor processes) and practically (it would fail on every non-MyOEM device).

## Extending This Pattern

When you are ready to add IPC to a daemon, the additions are incremental:

```
helloworldd (this project)
│
├── Add AIDL interface
│   └── aidl_interface { name: "helloworldservice-aidl", ... }
│
├── Add ServiceManager registration
│   └── AServiceManager_addService("com.myoem.hello.IHelloService/default", binder)
│
├── Add Binder thread pool
│   └── ABinderProcess_startThreadPool() + ABinderProcess_joinThreadPool()
│
├── Add SELinux Binder rules
│   └── binder_use(helloworldd), binder_service(helloworldd), add_service(...)
│
└── Add service_contexts
    └── com.myoem.hello.IHelloService/default   u:object_r:helloworldd_service:s0
```

Each step is one layer added on top of a working daemon. Starting from
`helloworldd`, each addition has one reason to fail — not four or five.

## The File Inventory

```
vendor/myoem/services/helloworld/
│
├── Android.bp                          25 lines  — cc_binary, vendor:true, liblog
├── helloworld.rc                        5 lines  — service definition for init
│
├── src/
│   └── main.cpp                        20 lines  — LOG_TAG + infinite loop + ALOGI
│
├── sepolicy/private/
│   ├── helloworldd.te                   5 lines  — domain type + init_daemon_domain
│   └── file_contexts                    3 lines  — /vendor/bin/helloworldd label
│
└── tests/
    ├── Android.bp                       25 lines  — cc_test, vendor:true, vts
    └── VtsHelloWorldTest.cpp            60 lines  — ProcessIsRunning + LogsHelloWorld
```

**Total: ~143 lines for a complete, production-quality vendor daemon with VTS test.**

The calculator service, for comparison, is ~800 lines including AIDL, Binder
threading, SELinux Binder rules, VINTF manifest, and client binary. Both are
correct for what they do. `helloworldd` is correct in its simplicity — there is
no missing piece, no placeholder, no "fill this in later". It is complete.

---

## Build and Run Summary

```bash
# One-time environment setup
source build/envsetup.sh && lunch myoem_rpi5-trunk_staging-userdebug

# Build the daemon
m helloworldd

# Build the test
m VtsHelloWorldTest

# Flash (needed for SELinux policy changes)
make vendorimage -j$(nproc)
sudo dd if=out/target/product/rpi5/vendor.img of=/dev/sdX bs=4M status=progress

# Dev iteration (no SELinux changes)
adb root && adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/bin/helloworldd /vendor/bin/
adb shell stop helloworldd && adb shell start helloworldd

# Verify
adb logcat -s HelloWorldDaemon:*

# Run tests
atest VtsHelloWorldTest
```

---

*Last updated: 2026-04-24 | AOSP android-15.0.0_r14 | Target: myoem_rpi5*
