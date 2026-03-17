# vendor/myoem — Project Context & Developer Reference

This document captures every architectural decision, pattern, rule, and hard-learned lesson
from building the MyOEM vendor layer on AOSP Raspberry Pi 5 (Android 15, `android-15.0.0_r14`).
Read this before starting any new component in this directory.

---

## 1. What This Vendor Layer Is

`vendor/myoem/` is the OEM product layer sitting on top of:
- **`frameworks/`** — Android platform (Google, do not touch)
- **`device/brcm/rpi5/`** — Raspberry Pi 5 board support (do not touch)

**The rule that shapes everything:** We never modify any file outside `vendor/myoem/`.
AOSP provides explicit extension points for everything we need. If you feel the urge to
edit a file in `frameworks/` or `device/`, stop — there is almost certainly a product-level
hook that achieves the same result.

---

## 2. Repository Setup

This directory is tracked in a separate git repo, referenced from the local manifest:

```xml
<!-- .repo/local_manifests/manifest_brcm_rpi.xml -->
<project
    path="vendor/myoem"
    name="YOUR_USERNAME/android_vendor_myoem"
    remote="github"
    revision="main" />
```

The same manifest pins `external/drm_hwcomposer` to a specific commit to avoid an
Android 16 API incompatibility in trunk_staging builds:

```xml
<project
    path="external/drm_hwcomposer"
    name="raspberry-vanilla/android_external_drm_hwcomposer"
    remote="github"
    revision="094b09d976e88621d574e72b6440bd90f3c7285e" />
```

**Why that pin exists:** The `android-15.0` branch of drm_hwcomposer received Android 16
API additions guarded by `#if __ANDROID_API__ >= 36`. In trunk_staging builds
`__ANDROID_API__ = 10000`, which activates those guards, but AOSP 15 composer3 AIDL
doesn't have those types. The commit above is the last clean state before that breakage.
Do not remove the pin.

---

## 3. Directory Structure

```
vendor/myoem/
├── AndroidProducts.mk          ← Build system entry point (auto-discovered)
├── CONTEXT.md                  ← This file
│
├── products/                   ← One .mk file per product variant
│   ├── myoem_rpi5.mk           ← Standard tablet product
│   ├── myoem_rpi5_car.mk       ← Android Automotive variant
│   └── myoem_rpi5_headless.mk  ← Headless / no-UI variant
│
├── common/
│   └── myoem_base.mk           ← Shared config inherited by all products
│
├── services/                   ← Native daemon services (C++, binder-based)
│   ├── calculator/             ← ICalculatorService (add/sub/mul/div)
│   └── bmi/                    ← IBMIService (getBMI)
│
└── libs/                       ← Java Manager libraries for app developers
    ├── calculator/             ← CalculatorManager.java (future)
    └── bmi/                    ← BMIManager.java (future)
```

When adding something new, choose the right top-level directory:

| What you're adding | Where it goes |
|--------------------|---------------|
| Native daemon / system service | `services/<name>/` |
| Java Manager wrapper for a service | `libs/<name>/` |
| Vendor APK | `apps/<name>/` |
| HAL implementation | `hal/<name>/` |
| Resource overlay | `overlay/` |
| Pre-compiled binary | `prebuilts/` |

---

## 4. How Products Are Registered (`AndroidProducts.mk`)

The AOSP build system scans the entire tree for files named `AndroidProducts.mk`.
No central registration is needed — just placing the file here is enough.

```makefile
PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/products/myoem_rpi5.mk \
    $(LOCAL_DIR)/products/myoem_rpi5_car.mk \
    $(LOCAL_DIR)/products/myoem_rpi5_headless.mk

COMMON_LUNCH_CHOICES := \
    myoem_rpi5-trunk_staging-userdebug \
    myoem_rpi5-trunk_staging-user \
    myoem_rpi5_car-trunk_staging-userdebug \
    myoem_rpi5_headless-trunk_staging-userdebug
```

Lunch target format: `<PRODUCT_NAME>-<release>-<variant>`
- `userdebug` — for development (has root, debugging tools)
- `user` — for production
- `eng` — everything enabled, very verbose

---

## 5. Product Makefile Pattern

Every product makefile inherits two things:

```makefile
$(call inherit-product, device/brcm/rpi5/aosp_rpi5.mk)   # hardware
$(call inherit-product, vendor/myoem/common/myoem_base.mk) # OEM config

PRODUCT_DEVICE       := rpi5          # must match device/ directory name
PRODUCT_NAME         := myoem_rpi5    # must match first segment of lunch target
PRODUCT_BRAND        := MyOEM
PRODUCT_MODEL        := MyOEM Raspberry Pi 5
PRODUCT_MANUFACTURER := MyOEM
```

---

## 6. `myoem_base.mk` — The Central Config File

**Every new service/library you add must be registered here.**
This is the single file that connects your code to the build system.

```makefile
# 1. Tell Soong to process Android.bp files in these directories
PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/myservice

# 2. Tell the build to install these modules in the final image
PRODUCT_PACKAGES += myserviced my_client

# 3. Tell the SELinux compiler to include your policy
PRODUCT_PRIVATE_SEPOLICY_DIRS += vendor/myoem/services/myservice/sepolicy/private
```

All three lines are required for a new service. Missing any one causes:
- No namespace → Soong can't find the module → build error "module not found"
- No packages → binary compiled but never installed on device
- No sepolicy → service blocked in enforcing mode / denials in permissive mode

---

## 7. The Critical Binder Rule (Read This Before Writing Any Service)

Android has three binder devices:

| Device | Used by | ServiceManager |
|--------|---------|----------------|
| `/dev/binder` | System processes, apps | System ServiceManager (what apps query) |
| `/dev/vndbinder` | Vendor-to-vendor IPC | Vendor ServiceManager |
| `/dev/hwbinder` | HAL IPC | HAL ServiceManager |

**Our services are `vendor: true` but must be reachable by Android apps.**
This means they must register with the system ServiceManager on `/dev/binder`.

**The library choice determines which device is used:**

| Library | Type | Binder device | Use in vendor? |
|---------|------|---------------|----------------|
| `libbinder` | VNDK (two copies) | Vendor copy → `/dev/vndbinder` | **NO** |
| `libbinder_ndk` | LLNDK (one copy) | Always `/dev/binder` | **YES** |

**Always use `libbinder_ndk` in vendor services that talk to the system ServiceManager.**

Using `libbinder` with `ProcessState::initWithDriver("/dev/binder")` as a workaround
does NOT work — the kernel rejects cross-domain transactions with `-129 (EX_TRANSACTION_FAILED)`
even if you force the device path. The kernel tracks library identity, not just which fd is open.

---

## 8. Native Service Template

Every service has exactly these files. Copy this structure for each new service.

```
services/<name>/
├── Android.bp
├── <name>d.rc
├── aidl/com/myoem/<name>/
│   └── I<Name>Service.aidl
├── src/
│   ├── <Name>Service.h
│   ├── <Name>Service.cpp
│   └── main.cpp
├── test/
│   └── <name>_client.cpp
└── sepolicy/private/
    ├── <name>d.te
    ├── service.te
    ├── file_contexts
    └── service_contexts
```

### 8a. AIDL File

```aidl
package com.myoem.<name>;

interface I<Name>Service {
    ReturnType methodName(ParamType param);
    const int ERROR_SOMETHING = 1;
}
```

- Package must mirror directory path: `com.myoem.<name>` → `aidl/com/myoem/<name>/`
- `I` prefix is convention (Interface)
- Define error codes as `const int` — shared between server and client via generated header

### 8b. `Android.bp`

```json
soong_namespace {}   // required — must match PRODUCT_SOONG_NAMESPACES entry

aidl_interface {
    name: "<name>service-aidl",
    vendor_available: true,
    unstable: true,   // set false + version when interface is stable
    srcs: ["aidl/com/myoem/<name>/I<Name>Service.aidl"],
    local_include_dir: "aidl",
    backend: {
        cpp: { enabled: false },          // DO NOT enable — uses libbinder (VNDK)
        java: { enabled: true, platform_apis: true },
        ndk: { enabled: true },           // uses libbinder_ndk (LLNDK) — correct
        rust: { enabled: false },
    },
}

cc_binary {
    name: "<name>d",
    vendor: true,
    init_rc: ["<name>d.rc"],
    srcs: ["src/<Name>Service.cpp", "src/main.cpp"],
    shared_libs: ["libbinder_ndk", "liblog"],
    static_libs: ["<name>service-aidl-ndk"],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}

cc_binary {
    name: "<name>_client",
    vendor: true,
    srcs: ["test/<name>_client.cpp"],
    shared_libs: ["libbinder_ndk", "liblog"],
    static_libs: ["<name>service-aidl-ndk"],
    cflags: ["-Wall", "-Wextra", "-Werror"],
}
```

### 8c. Service Header (`<Name>Service.h`)

```cpp
#pragma once
#include <aidl/com/myoem/<name>/Bn<Name>Service.h>

namespace aidl::com::myoem::<name> {

class <Name>Service : public Bn<Name>Service {
public:
    ndk::ScopedAStatus methodName(ParamType param, ReturnType* _aidl_return) override;
};

}
```

- Include path always starts with `aidl/` for NDK backend generated headers
- Namespace is `aidl::com::myoem::<name>` — mirrors package with `aidl::` prefix
- Inherit from `Bn<Name>Service` (Binder Native = server stub)
- Return type is always `ndk::ScopedAStatus`
- Actual return value goes in output pointer `_aidl_return`

### 8d. Service Implementation (`<Name>Service.cpp`)

```cpp
#define LOG_TAG "<name>d"   // MUST be first line, before any #include

#include "<Name>Service.h"
#include <log/log.h>

namespace aidl::com::myoem::<name> {

ndk::ScopedAStatus <Name>Service::methodName(
        ParamType param, ReturnType* _aidl_return) {
    // validate inputs
    if (invalid) {
        return ndk::ScopedAStatus::fromServiceSpecificError(
                I<Name>Service::ERROR_SOMETHING);
    }
    // compute
    *_aidl_return = result;
    ALOGD("methodName(%...) = %...", param, *_aidl_return);
    return ndk::ScopedAStatus::ok();
}

}
```

**Critical:** `#define LOG_TAG` must be the very first line before any `#include`.
If log headers are included first, they define `LOG_TAG NULL`, causing a macro
redefinition error with `-Werror`. This will break the build.

### 8e. `main.cpp`

```cpp
#define LOG_TAG "<name>d"   // first line

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>
#include "<Name>Service.h"

static constexpr const char* kServiceName = "com.myoem.<name>.I<Name>Service";

int main() {
    ALOGI("<name>d starting");

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto service = ndk::SharedRefBase::make<
            aidl::com::myoem::<name>::<Name>Service>();

    binder_status_t status = AServiceManager_addService(
            service->asBinder().get(), kServiceName);

    if (status != STATUS_OK) {
        ALOGE("addService('%s') failed: %d", kServiceName, status);
        return 1;
    }

    ALOGI("<name>d registered as '%s'", kServiceName);
    ABinderProcess_joinThreadPool();
    return 0;
}
```

The five-step skeleton never changes:
1. `setThreadPoolMaxThreadCount` — how many concurrent calls to handle
2. `startThreadPool` — spawn worker threads
3. `SharedRefBase::make<>()` — create service instance
4. `AServiceManager_addService` — register with system ServiceManager
5. `joinThreadPool` — block main thread forever

### 8f. Init RC File (`<name>d.rc`)

```
service <name>d /vendor/bin/<name>d
    class main
    user system
    group system
```

- `class main` — starts after core system is up, before UI. Right for app-facing services.
- `user system` — UID 1000. Standard for system services.
- Binary lands in `/vendor/bin/` because `vendor: true` in Android.bp.
- RC file is auto-installed to `/vendor/etc/init/` via `init_rc` in Android.bp.

### 8g. Test Client (`<name>_client.cpp`)

```cpp
#define LOG_TAG "<name>_client"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <aidl/com/myoem/<name>/I<Name>Service.h>

using aidl::com::myoem::<name>::I<Name>Service;

int main(int argc, char* argv[]) {
    ABinderProcess_setThreadPoolMaxThreadCount(0);  // 0 = client only, no worker threads

    ndk::SpAIBinder binder(AServiceManager_checkService(
            "com.myoem.<name>.I<Name>Service"));
    if (binder.get() == nullptr) {
        fprintf(stderr, "ERROR: <name>d not found\n");
        return 1;
    }
    auto svc = I<Name>Service::fromBinder(binder);

    // make calls
    ReturnType result;
    ndk::ScopedAStatus status = svc->methodName(param, &result);
    if (!status.isOk()) {
        fprintf(stderr, "ERROR: %s\n", status.getDescription().c_str());
        return 1;
    }
    printf("result: ...\n");
    return 0;
}
```

Key differences from the daemon:
- `setThreadPoolMaxThreadCount(0)` — clients don't receive calls, no worker threads needed
- `AServiceManager_checkService` — non-blocking lookup (use `waitForService` if you want to block)
- `SpAIBinder` — smart pointer wrapper for raw `AIBinder*`
- `fromBinder` — wraps raw binder in typed proxy
- `status.getDescription()` — human-readable error string (not `toString8()` which is libbinder C++)

---

## 9. SELinux Policy Template

Every service needs exactly these four files in `sepolicy/private/`.

### `<name>d.te`

```
type <name>d, domain;
type <name>d_exec, exec_type, vendor_file_type, file_type;

init_daemon_domain(<name>d)
binder_use(<name>d)
binder_service(<name>d)
add_service(<name>d, <name>d_service)
```

- `init_daemon_domain` — allows init to launch and transition into this domain
- `binder_use` — allows making binder calls (needed even for services that only receive)
- `binder_service` — allows receiving binder calls
- `add_service` — allows registering with ServiceManager

### `service.te`

```
type <name>d_service, app_api_service, service_manager_type;
```

- `app_api_service` — allows Android apps to look up this service. Remove this if
  the service is only for other vendor processes (use `vendor_service` instead).
- `service_manager_type` — mandatory for all ServiceManager entries.

### `file_contexts`

```
/vendor/bin/<name>d    u:object_r:<name>d_exec:s0
```

Labels the binary. Path must exactly match where the binary is installed.
Without this, the domain transition from `init` to your domain never happens.

### `service_contexts`

```
com.myoem.<name>.I<Name>Service    u:object_r:<name>d_service:s0
```

Labels the ServiceManager entry. The string here must exactly match:
- `kServiceName` in `main.cpp`
- The string passed to `AServiceManager_addService`

**Do not** add `allow <name>d log_device:chr_file { read write open };` —
`log_device` is a vendor policy type, not visible in `PRODUCT_PRIVATE_SEPOLICY_DIRS`.
Logging works without it via `init_daemon_domain`'s inherited permissions.

---

## 10. Build Commands

```bash
# Build just a specific module (fastest during development)
m calculatord
m bmid
m calculatord bmid calculator_client bmi_client

# Full image build
m

# Flash only vendor partition (fastest for testing service changes)
adb reboot bootloader
fastboot flash vendor out/target/product/rpi5/vendor.img
fastboot reboot
```

Build outputs land in `out/target/product/rpi5/vendor/`.

---

## 11. Verification Commands After Flash

```bash
# Check service is registered with ServiceManager
adb shell service list | grep -E "calculator|bmi"

# Check process is running
adb shell ps -e | grep -E "calculatord|bmid"

# Check SELinux domain (should show u:r:<name>d:s0, not u:r:init:s0)
adb shell ps -eZ | grep -E "calculatord|bmid"

# Check binary label (should show <name>d_exec)
adb shell ls -laZ /vendor/bin/calculatord

# Watch service logs live
adb logcat -s calculatord,bmid,ServiceManager

# Check for SELinux denials
adb logcat | grep "avc: denied"

# Run test clients
adb shell calculator_client add 10 3
adb shell calculator_client div 10 0    # tests error handling
adb shell bmi_client 1.75 70.0
adb shell bmi_client 0.0 70.0           # tests invalid input
```

---

## 12. Debugging Reference

### Service not starting

1. `adb shell ps -e | grep <name>d` — is it running at all?
2. `adb logcat -s <name>d` — any startup errors?
3. `adb logcat | grep "avc: denied" | grep <name>d` — SELinux blocking it?
4. Check `file_contexts` — path must match `/vendor/bin/<name>d` exactly

### Service running but not found by clients

1. `adb shell service list | grep <name>` — is it in ServiceManager?
2. `adb logcat -s <name>d | grep addService` — did registration succeed?
3. If addService returned `-129`: wrong binder library — switch to `libbinder_ndk`
4. Check `service_contexts` — service name must match `kServiceName` in main.cpp exactly

### SELinux denials

Read the full denial line:
```
avc: denied { add } for name="com.myoem.calculator.ICalculatorService"
    scontext=u:r:calculatord:s0        ← who was denied
    tcontext=u:object_r:default_android_service:s0  ← what they accessed
    tclass=service_manager             ← type of object
```

| tcontext shows | Cause | Fix |
|----------------|-------|-----|
| `default_android_service` | No matching `service_contexts` entry | Check string matches exactly |
| `init` in scontext | Domain transition failed | Check `file_contexts` path |
| `unlabeled` | No file label | Check `file_contexts` regex |

### Build errors

| Error | Cause | Fix |
|-------|-------|-----|
| `'LOG_TAG' macro redefined` | `#define LOG_TAG` after `#include <log/log.h>` | Move `#define LOG_TAG` to first line |
| `module "xyz" not found` | Not in `PRODUCT_SOONG_NAMESPACES` | Add to `myoem_base.mk` |
| `unknown type log_device` | Vendor-only type used in product-private policy | Remove the line |
| `EX_TRANSACTION_FAILED (-129)` | Using `libbinder` VNDK in vendor | Switch to `libbinder_ndk` |

---

## 13. Manager Pattern (Java Wrapper for Services)

For app developers to consume a service without knowing about binder, create a Java
Manager class in `libs/<name>/`.

```
libs/<name>/
├── Android.bp
└── java/com/myoem/<name>/
    └── <Name>Manager.java
```

`Android.bp`:
```json
java_sdk_library {
    name: "<name>-manager",
    vendor: true,
    srcs: ["java/**/*.java"],
    libs: ["framework", "<name>service-aidl-java"],
    sdk_version: "system_current",
}
```

The Manager class:
- Uses `android.os.ServiceManager.checkService(SERVICE_NAME)` to get the binder
- Wraps it with `I<Name>Service.Stub.asInterface(binder)` for typed access
- Catches `RemoteException` and returns sentinel values
- Does client-side input validation before making IPC calls
- Exposes static utility methods (e.g., category classification) that don't need IPC

Add to `myoem_base.mk`:
```makefile
PRODUCT_SOONG_NAMESPACES += vendor/myoem/libs/<name>
PRODUCT_PACKAGES += <name>-manager
```

---

## 14. Existing Services

### CalculatorService

| Property | Value |
|----------|-------|
| AIDL package | `com.myoem.calculator` |
| Interface | `ICalculatorService` |
| Service name | `com.myoem.calculator.ICalculatorService` |
| Binary | `/vendor/bin/calculatord` |
| Methods | `add`, `subtract`, `multiply`, `divide` (all `int`) |
| Error codes | `ERROR_DIVIDE_BY_ZERO = 1` |
| SELinux domain | `calculatord` |
| SELinux service type | `calculatord_service` |

### BMIService

| Property | Value |
|----------|-------|
| AIDL package | `com.myoem.bmi` |
| Interface | `IBMIService` |
| Service name | `com.myoem.bmi.IBMIService` |
| Binary | `/vendor/bin/bmid` |
| Methods | `getBMI(float height, float weight)` returns `float` |
| Error codes | `ERROR_INVALID_INPUT = 1` |
| SELinux domain | `bmid` |
| SELinux service type | `bmid_service` |

---

## 15. Key Rules Summary

1. **Never modify files outside `vendor/myoem/`**
2. **Always use `libbinder_ndk` in vendor services** — never `libbinder`
3. **`#define LOG_TAG` must be the first line** of every `.cpp` file
4. **Three lines in `myoem_base.mk` for every new service**: `SOONG_NAMESPACES`, `PACKAGES`, `SEPOLICY_DIRS`
5. **Four SELinux files for every service**: `.te`, `service.te`, `file_contexts`, `service_contexts`
6. **Service name string must be identical** in `main.cpp`, `service_contexts`, and `Manager.java`
7. **`cpp` backend disabled, `ndk` backend enabled** in every `aidl_interface`
8. **Do not use `log_device`** in `PRODUCT_PRIVATE_SEPOLICY_DIRS` policy — it's a vendor-only type
9. **Pin external dependencies** in `.repo/local_manifests/` when upstream branches break

---

*Last updated: March 2026 | AOSP android-15.0.0_r14 | Target: rpi5*
