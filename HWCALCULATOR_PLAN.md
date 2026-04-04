# hwCalculator Service — Implementation Plan

A vendor C++ Binder service identical in shape to `calculatord`, under the name `hwCalculatord`.

**Goal:** Build it yourself step-by-step. Each step is self-contained with all commands you need.

---

## Directory Layout (target state)

```
vendor/myoem/services/hwcalculator/
├── Android.bp
├── hwcalculatord.rc
├── aidl/
│   └── com/myoem/hwcalculator/
│       └── IHwCalculatorService.aidl
├── src/
│   ├── HwCalculatorService.h
│   ├── HwCalculatorService.cpp
│   └── main.cpp
├── test/
│   └── hwcalculator_client.cpp
├── tests/
│   ├── Android.bp
│   └── VtsHwCalculatorServiceTest.cpp
└── sepolicy/private/
    ├── hwcalculatord.te
    ├── service.te
    ├── service_contexts
    └── file_contexts
```

---

## Name Mapping (calculator → hwCalculator)

| Old (calculator) | New (hwCalculator) |
|---|---|
| `calculatord` | `hwcalculatord` |
| `calculator_client` | `hwcalculator_client` |
| `ICalculatorService` | `IHwCalculatorService` |
| `com.myoem.calculator.ICalculatorService` | `com.myoem.hwcalculator.IHwCalculatorService` |
| `calculatorservice-aidl` | `hwcalculatorservice-aidl` |
| `CalculatorService` | `HwCalculatorService` |
| `calculatord_service` | `hwcalculatord_service` |
| `calculatord_exec` | `hwcalculatord_exec` |
| `calculatord` (domain) | `hwcalculatord` (domain) |
| `/vendor/bin/calculatord` | `/vendor/bin/hwcalculatord` |
| `LOG_TAG "calculatord"` | `LOG_TAG "hwcalculatord"` |
| `VtsCalculatorServiceTest` | `VtsHwCalculatorServiceTest` |

---

## Step 1 — Create the directory tree

```bash
mkdir -p vendor/myoem/services/hwcalculator/aidl/com/myoem/hwcalculator
mkdir -p vendor/myoem/services/hwcalculator/src
mkdir -p vendor/myoem/services/hwcalculator/test
mkdir -p vendor/myoem/services/hwcalculator/tests
mkdir -p vendor/myoem/services/hwcalculator/sepolicy/private
```

**Verify:**
```bash
find vendor/myoem/services/hwcalculator -type d
```

---

## Step 2 — Write the AIDL interface

**File:** `vendor/myoem/services/hwcalculator/aidl/com/myoem/hwcalculator/IHwCalculatorService.aidl`

**What to write:** Same four methods (add, subtract, multiply, divide) and the
`ERROR_DIVIDE_BY_ZERO = 1` constant. Change:
- Package: `com.myoem.hwcalculator`
- Interface name: `IHwCalculatorService`

**Verify (after writing):**
```bash
cat vendor/myoem/services/hwcalculator/aidl/com/myoem/hwcalculator/IHwCalculatorService.aidl
```

---

## Step 3 — Write Android.bp

**File:** `vendor/myoem/services/hwcalculator/Android.bp`

Three modules to declare:
1. `soong_namespace {}` — required because we will add this path to `PRODUCT_SOONG_NAMESPACES`
2. `aidl_interface` — name `hwcalculatorservice-aidl`, ndk backend enabled, java backend enabled, cpp/rust disabled
3. `cc_binary hwcalculatord` — depends on `hwcalculatorservice-aidl-ndk`, `libbinder_ndk`, `liblog`; has `init_rc`
4. `cc_binary hwcalculator_client` — same deps, src from `test/`

**Key things that must be different from calculator:**
- `name: "hwcalculatorservice-aidl"`
- `srcs: ["aidl/com/myoem/hwcalculator/IHwCalculatorService.aidl"]`
- `name: "hwcalculatord"`, `init_rc: ["hwcalculatord.rc"]`
- `name: "hwcalculator_client"`
- Static lib is `hwcalculatorservice-aidl-ndk`

**Verify (after writing):**
```bash
cat vendor/myoem/services/hwcalculator/Android.bp
```

---

## Step 4 — Write the service header

**File:** `vendor/myoem/services/hwcalculator/src/HwCalculatorService.h`

- Include: `<aidl/com/myoem/hwcalculator/BnHwCalculatorService.h>`
- Namespace: `aidl::com::myoem::hwcalculator`
- Class `HwCalculatorService : public BnHwCalculatorService`
- Declare all four methods with `ndk::ScopedAStatus` signatures (same as calculator)

---

### How `BnHwCalculatorService` gets generated (no commands needed between steps 3 and 4)

You will `#include <aidl/com/myoem/hwcalculator/BnHwCalculatorService.h>` in Step 4, but that file does not exist on disk yet — and that is fine. You do not need to run any command between steps 3 and 4.

**The flow:**
```
Step 2:  You write IHwCalculatorService.aidl
Step 3:  You declare aidl_interface { ndk: { enabled: true } } in Android.bp
          ↓
Steps 4–11: You write .h / .cpp / .rc / .te files that #include the not-yet-generated header.
          The compiler does not need it yet — these are just source files on disk.
          ↓
Step 12: m hwcalculatord
         Soong sees the aidl_interface module → runs the `aidl` compiler tool → generates:

           out/soong/.intermediates/vendor/myoem/services/hwcalculator/
               hwcalculatorservice-aidl-ndk-source/gen/include/aidl/com/myoem/hwcalculator/
                   BnHwCalculatorService.h   ← server-side base class  (you extend this)
                   IHwCalculatorService.h    ← interface + client proxy
                   BpHwCalculatorService.h   ← proxy implementation

         Soong automatically adds the generated include path to your cc_binary, so
         #include <aidl/com/myoem/hwcalculator/BnHwCalculatorService.h> resolves
         correctly at compile time even though it didn't exist before Step 12.
```

After Step 12 succeeds you can inspect the generated header:
```bash
find out/soong/.intermediates/vendor/myoem/services/hwcalculator \
    -name "BnHwCalculatorService.h"
```

---

## Step 5 — Write the service implementation

**File:** `vendor/myoem/services/hwcalculator/src/HwCalculatorService.cpp`

- First line: `#define LOG_TAG "hwcalculatord"` (before any `#include`)
- Include `"HwCalculatorService.h"` and `<log/log.h>`
- Namespace: `aidl::com::myoem::hwcalculator`
- Implement add, subtract, multiply — same as calculator
- Implement divide — check `b == 0`, return `fromServiceSpecificError(IHwCalculatorService::ERROR_DIVIDE_BY_ZERO)`

---

## Step 6 — Write main.cpp

**File:** `vendor/myoem/services/hwcalculator/src/main.cpp`

- First line: `#define LOG_TAG "hwcalculatord"`
- Service name constant: `"com.myoem.hwcalculator.IHwCalculatorService"`
  - This string **must match exactly** in `service_contexts` (Step 9) and `kServiceName` here
- Use `ABinderProcess_setThreadPoolMaxThreadCount(4)` + `ABinderProcess_startThreadPool()`
- Create `ndk::SharedRefBase::make<aidl::com::myoem::hwcalculator::HwCalculatorService>()`
- Register with `AServiceManager_addService(service->asBinder().get(), kServiceName)`
- Block with `ABinderProcess_joinThreadPool()`

---

## Step 7 — Write the RC file

**File:** `vendor/myoem/services/hwcalculator/hwcalculatord.rc`

```
service hwcalculatord /vendor/bin/hwcalculatord
    class main
    user system
    group system
```

No `on boot` block needed (no sysfs permissions required for a pure calculator).

---

## Step 8 — Write the test client

**File:** `vendor/myoem/services/hwcalculator/test/hwcalculator_client.cpp`

- First line: `#define LOG_TAG "hwcalculator_client"`
- Use `AServiceManager_checkService("com.myoem.hwcalculator.IHwCalculatorService")`
- Include `<aidl/com/myoem/hwcalculator/IHwCalculatorService.h>`
- `using aidl::com::myoem::hwcalculator::IHwCalculatorService`
- Same argument parsing: `<add|sub|mul|div> <a> <b>`
- Use `ABinderProcess_setThreadPoolMaxThreadCount(0)` (client-only, no incoming calls)

---

## Step 9 — Write SELinux files

### `sepolicy/private/hwcalculatord.te`
```
type hwcalculatord, domain;
type hwcalculatord_exec, exec_type, vendor_file_type, file_type;

init_daemon_domain(hwcalculatord)
binder_use(hwcalculatord)
binder_service(hwcalculatord)
add_service(hwcalculatord, hwcalculatord_service)
```

### `sepolicy/private/service.te`
```
type hwcalculatord_service, app_api_service, service_manager_type;
```

### `sepolicy/private/service_contexts`
```
com.myoem.hwcalculator.IHwCalculatorService    u:object_r:hwcalculatord_service:s0
```
> The string before the spaces **must be identical** to `kServiceName` in `main.cpp`.

### `sepolicy/private/file_contexts`
```
/vendor/bin/hwcalculatord    u:object_r:hwcalculatord_exec:s0
```

---

## Step 10 — Write VTS test

### `tests/Android.bp`
- `cc_test`, `name: "VtsHwCalculatorServiceTest"`, `vendor: true`
- `static_libs: ["hwcalculatorservice-aidl-ndk"]`
- `test_suites: ["vts"]`

### `tests/VtsHwCalculatorServiceTest.cpp`
- Copy the structure from `VtsCalculatorServiceTest.cpp`
- Rename: `CalculatorServiceTest` → `HwCalculatorServiceTest`
- Change service name constant to `"com.myoem.hwcalculator.IHwCalculatorService"`
- Change includes to `<aidl/com/myoem/hwcalculator/IHwCalculatorService.h>`
- Change `using` to `aidl::com::myoem::hwcalculator::IHwCalculatorService`
- Keep all test cases identical (same math, same error codes)

---

## Step 11 — Register in myoem_base.mk

**File:** `vendor/myoem/common/myoem_base.mk`

Add to `PRODUCT_SOONG_NAMESPACES`:
```makefile
    vendor/myoem/services/hwcalculator \
```

Add to `PRODUCT_PACKAGES`:
```makefile
    hwcalculatord \
    hwcalculator_client \
```

Add to `BOARD_VENDOR_SEPOLICY_DIRS`:
```makefile
    vendor/myoem/services/hwcalculator/sepolicy/private \
```

**Verify (after editing):**
```bash
grep hwcalculator vendor/myoem/common/myoem_base.mk
```

---

## Step 12 — Build

```bash
# From AOSP root
source build/envsetup.sh
lunch myoem_rpi5-trunk_staging-userdebug

# Build just the service and client
m hwcalculatord hwcalculator_client

# Build the VTS test too
m VtsHwCalculatorServiceTest
```

**What to check if it fails:**
- Soong parse error → typo in `Android.bp` (braces, commas, quotes)
- AIDL backend error → wrong `srcs` path in `Android.bp`
- Linker error → wrong `static_libs` name (must be `hwcalculatorservice-aidl-ndk`)
- `-Werror` → check for unused variables or wrong function signatures

---

## Step 13 — Push to device (dev iteration without full flash)

```bash
# Remount vendor partition as read-write
adb root
adb shell mount -o remount,rw /vendor

# Push binaries
adb push out/target/product/rpi5/vendor/bin/hwcalculatord /vendor/bin/
adb push out/target/product/rpi5/vendor/bin/hwcalculator_client /vendor/bin/

# Push RC file
adb push out/target/product/rpi5/vendor/etc/init/hwcalculatord.rc /vendor/etc/init/

# Fix permissions
adb shell chmod 755 /vendor/bin/hwcalculatord
adb shell chmod 755 /vendor/bin/hwcalculator_client
```

### Why `adb shell mount -o remount,rw /vendor`?

The `/vendor` partition is mounted **read-only** by default on Android — this is intentional for security (prevents runtime tampering of OEM binaries). When you do a full image flash, everything is written once and the partition is locked read-only on boot.

During development you don't want to reflash the SD card for every small binary change. So you temporarily remount the partition as read-write, push your new binary, and test — without touching the rest of the image.

| Flag | Meaning |
|---|---|
| `mount -o` | pass options to mount |
| `remount` | re-mount an already-mounted filesystem (don't change mount point) |
| `rw` | change the permission to read-write |
| `/vendor` | the partition to remount |

This change is **not persistent** — after a reboot `/vendor` is read-only again.

### Why you cannot push SELinux policy files at runtime

SELinux policy (`.te`, `service_contexts`, `file_contexts`) is compiled into a **monolithic binary** (`vendor_sepolicy.cil`, `sepolicy.zip`) during the build. At boot, the kernel loads this compiled binary once into kernel memory. It cannot be updated while the system is running.

Specifically:
- `.te` files define domains and rules → compiled into `vendor_sepolicy.cil`
- `service_contexts` maps service names to labels → compiled into `servicecontexts`
- `file_contexts` maps file paths to labels → compiled into `file_contexts`

Pushing the source `.te` file to the device does nothing — the kernel never reads `.te` files directly. The compiled binary is what matters, and it is only updated by a full flash.

**Workaround during dev iteration:**
```bash
adb root
adb shell setenforce 0    # switch to permissive mode globally
                           # SELinux logs denials but does NOT block them
```
This lets you test the service logic without a flash. Once everything works, do a full flash so enforcing mode is validated.

---

## Step 14 — Start the service and verify

### Option A: Permissive mode (for dev iteration without reflash)
```bash
adb root
adb shell setenforce 0          # global permissive

# Start service manually (if init hasn't picked up the RC yet)
adb shell /vendor/bin/hwcalculatord &
```

### Option B: After full image flash (SELinux enforcing)
The RC file will auto-start the service on boot.

### Check it is registered
```bash
adb shell service list | grep hwcalculator
# Expected: com.myoem.hwcalculator.IHwCalculatorService: [...]
```

### Check the process is running
```bash
adb shell ps -eZ | grep hwcalculatord
```

### Check logs
```bash
adb logcat -s hwcalculatord
# Should see: hwcalculatord starting
#             hwcalculatord registered as 'com.myoem.hwcalculator.IHwCalculatorService'
```

---

## Step 15 — Test with the client binary

```bash
# Basic operations
adb shell hwcalculator_client add 10 3      # → 10 add 3 = 13
adb shell hwcalculator_client sub 10 3      # → 10 sub 3 = 7
adb shell hwcalculator_client mul 4 5       # → 4 mul 5 = 20
adb shell hwcalculator_client div 10 2      # → 10 div 2 = 5

# Division by zero — must print an error, not crash
adb shell hwcalculator_client div 10 0      # → ERROR: ... divide by zero

# Wrong arg count — must print usage
adb shell hwcalculator_client add 1         # → Usage: ...
```

---

## Step 16 — Run VTS tests (manual method — reliable)

```bash
# 1. Make sure the service is running first
adb shell service list | grep hwcalculator
# If nothing shows → start it:
adb shell /vendor/bin/hwcalculatord &

# 2. Push the test binary
#    VTS tests land under data/nativetest64/ (NOT vendor/nativetest64/)
adb push out/target/product/rpi5/data/nativetest64/VtsHwCalculatorServiceTest/VtsHwCalculatorServiceTest \
    /data/local/tmp/

adb shell chmod 755 /data/local/tmp/VtsHwCalculatorServiceTest
adb shell /data/local/tmp/VtsHwCalculatorServiceTest
```

**Expected output:**
```
[==========] Running 30 tests from 1 test suite.
[----------] 30 tests from HwCalculatorServiceTest
[ RUN      ] HwCalculatorServiceTest.ServiceRegistered
[       OK ] HwCalculatorServiceTest.ServiceRegistered
...
[  PASSED  ] 30 tests.
```

> **IMPORTANT — test hangs at startup:**
> If the test prints the header and then freezes, the service is not running.
> `AServiceManager_waitForService()` blocks indefinitely until the service appears.
> Fix: start `hwcalculatord` first (see step 1 above), then re-run.
>
> The service drops whenever the device reboots or the adb shell session that
> started it exits. A full image flash (Step 17) makes init auto-start it on boot.

### atest — known issue

`atest VtsHwCalculatorServiceTest` has a TradeFed push conflict on this setup.
The manual method above is equivalent and fully validates the service.
Do not spend time debugging atest for this project.

---

## Step 17 — Full image flash (for enforcing SELinux)

After all dev iteration is done and you want full enforcing mode:

```bash
# Build full image
m

# Flash (write to SD card — no fastboot on RPi5)
# Power off RPi5, remove SD card, then on host:
sudo dd if=out/target/product/rpi5/myoem_rpi5-img.zip of=/dev/sdX bs=4M status=progress
# (replace /dev/sdX with your SD card device)
```

---

## Lessons Learned During This Build

Real problems hit during the hwCalculator build and exactly how they were fixed.

---

### Problem 1 — VTS test binary was not at `vendor/nativetest64/`

**Symptom:** Plan said to push from `out/.../vendor/nativetest64/` but that directory did not exist.

**Root cause:** The build output for `cc_test` goes to `data/nativetest64/`, not `vendor/nativetest64/`. The vendor subdir only appears when `vendor: true` is set in the `cc_test` module.

**Fix:** Correct push path is:
```bash
out/target/product/rpi5/data/nativetest64/VtsHwCalculatorServiceTest/VtsHwCalculatorServiceTest
```

---

### Problem 2 — `atest` failing with "push dir to existing device FILE"

**Symptom:**
```
RUNNER ERROR: Attempting to push dir '.../VtsHwCalculatorServiceTest'
to an existing device file '/data/local/tmp/VtsHwCalculatorServiceTest'
```

**Root cause:** An earlier manual `adb push VtsHwCalculatorServiceTest /data/local/tmp/` had left a **plain file** at `/data/local/tmp/VtsHwCalculatorServiceTest`. TradeFed then tried to push the whole test **directory** to the same path — a file cannot become a directory, so it failed.

**Fix:**
```bash
adb root
adb shell rm -rf /data/local/tmp/VtsHwCalculatorServiceTest
```
Note: use `rm -rf` not `rm -f` — `rm -f` silently ignores directories.

**Rule:** Never manually push the VTS test binary to `/data/local/tmp/` before running `atest`. If you do, clean it up first.

---

### Problem 3 — `vendor: true` in `cc_test` caused TradeFed push path conflict

**Symptom:** Even after fixing Problem 2, `atest` kept finding an existing file. The path changed from `/data/local/tests/vendor/` (with vendor:true) to `/data/local/tmp/` (without), but the conflict kept coming back.

**Root cause:** With `vendor: true` in `cc_test`, TradeFed pushes to `/data/local/tests/vendor/` AND separately tries to push the directory, creating a self-conflict within the same run.

**Fix:** Remove `vendor: true` from `tests/Android.bp`. The test binary does NOT need to be on the vendor partition. `libbinder_ndk` is LLNDK and reaches vendor services from anywhere.

---

### Problem 4 — VTS test hangs at startup (never prints test results)

**Symptom:** Test binary starts, prints the GTest header, then freezes:
```
[==========] Running 30 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 30 tests from HwCalculatorServiceTest
(hangs here forever, must Ctrl-C)
```

**Root cause:** `SetUpTestSuite()` calls `AServiceManager_waitForService()` which **blocks indefinitely** until the service registers. The `hwcalculatord` service was not running (it had been stopped since it was started manually in a previous adb session, not via the RC file from a full flash).

**Fix:**
```bash
# Check first
adb shell service list | grep hwcalculator

# If nothing — start it
adb shell /vendor/bin/hwcalculatord &

# Confirm registered
adb shell service list | grep hwcalculator

# Now run the test
adb shell /data/local/tmp/VtsHwCalculatorServiceTest
```

**Why this happens only with manual start:** When started via `adb shell /vendor/bin/hwcalculatord &`, the service process is tied to that adb shell session. When the session ends (adb disconnect, device sleep, etc.), the process dies. A full image flash makes `init` start the service from the RC file on every boot, so it is always running.

---

### Problem 5 — `atest` cannot resolve push file path (CONFIGURED_ARTIFACT_NOT_FOUND)

**Symptom:**
```
CONFIGURED_ARTIFACT_NOT_FOUND: Local source file 'arm64/VtsHwCalculatorServiceTest' does not exist
```

**Root cause:** TradeFed's `PushFilePreparer` resolves push source paths relative to the **testcases root** (`out/target/product/rpi5/testcases/`), not relative to the module's own subdirectory. Paths like `arm64/VtsHwCalculatorServiceTest` or `VtsHwCalculatorServiceTest/arm64/...` were not being found consistently.

**Current state:** `atest` on this RPi5 + AOSP 15 setup has unresolved TradeFed push path issues. The manual method (Problem 4 fix) runs the identical binary and produces identical results. `atest` is not required to validate the service.

---

## Common Mistakes to Avoid

| Mistake | Symptom | Fix |
|---|---|---|
| Service name mismatch between `main.cpp` and `service_contexts` | Service registers but clients can't find it — SELinux denial or null binder | Make the three strings identical (main.cpp, service_contexts, client) |
| `LOG_TAG` not first line in `.cpp` | `-Werror` compile failure about redefinition | Move `#define LOG_TAG` before all `#include` |
| Wrong AIDL library name in static_libs | Linker error: undefined symbol `BnHwCalculatorService` | Use `hwcalculatorservice-aidl-ndk` (not `-cpp`) |
| `cpp` backend enabled in Android.bp | Build warning / unused library | Set `cpp: { enabled: false }` |
| Forgetting `soong_namespace {}` in Android.bp | Module not visible from myoem_base.mk namespace | Add the empty `soong_namespace {}` block |
| Using `libbinder` instead of `libbinder_ndk` | Linker error or service runs but can't reach system ServiceManager | Always use `libbinder_ndk` in vendor code |
| Pushing SELinux policy via adb | Policy not updated, old denials remain | Full image flash required for policy changes |

---

## Checklist

- [ ] Step 1: Directories created
- [ ] Step 2: AIDL file written
- [ ] Step 3: Android.bp written
- [ ] Step 4: HwCalculatorService.h written
- [ ] Step 5: HwCalculatorService.cpp written
- [ ] Step 6: main.cpp written
- [ ] Step 7: hwcalculatord.rc written
- [ ] Step 8: hwcalculator_client.cpp written
- [ ] Step 9: Four SELinux files written
- [ ] Step 10: VTS test written
- [ ] Step 11: myoem_base.mk updated
- [ ] Step 12: Build succeeds
- [ ] Step 13: Binaries pushed to device
- [ ] Step 14: Service running and registered
- [ ] Step 15: Client tests pass
- [ ] Step 16: VTS tests pass
- [ ] Step 17: Full flash with SELinux enforcing (optional final validation)
