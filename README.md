# vendor/myoem — OEM Vendor Layer for Raspberry Pi 5

**Device:** Raspberry Pi 5 | **AOSP Branch:** android-15.0.0_r14
**Product:** `myoem_rpi5-trunk_staging-userdebug`
**GitHub:** ProArun

---

## Directory Structure

```
vendor/myoem/
├── common/
│   └── myoem_base.mk           ← PRODUCT_PACKAGES, PRODUCT_SOONG_NAMESPACES, SEPOLICY
├── products/
│   └── myoem_rpi5.mk           ← inherits aosp_rpi5.mk, includes myoem_base.mk
│
├── hal/
│   └── thermalcontrol/         ← cc_library_shared: libthermalcontrolhal
│
├── services/
│   ├── calculator/             ← cc_binary: calculatord  (AIDL, Binder, VINTF)
│   ├── bmi/                    ← cc_binary: bmid          (AIDL, NDK binder, VINTF)
│   ├── thermalcontrol/         ← cc_binary: thermalcontrold
│   ├── safemode/               ← cc_binary: safemoded     (VHAL, CarProperty)
│   ├── hwcalculator/           ← cc_binary: hwcalculatord (SPI/HW-backed calculator)
│   ├── potvolumed/             ← cc_binary: potvolumed    (SPI ADC → uinput volume)
│   └── bmiapp/                 ← android_app: BmiSystemService (system UID intermediary)
│
├── libs/
│   ├── thermalcontrol/         ← java_sdk_library: thermalcontrol-manager
│   ├── safemode/               ← java_library: safemode_library
│   └── bmicalculator/          ← java_library: bmicalculator-manager + cc_library_shared: libbmicalmanager_jni
│
└── apps/
    ├── ThermalMonitor/         ← android_app: ThermalMonitor
    ├── SafeModeDemo/           ← android_app: SafeModeDemo
    ├── BMICalculatorA/         ← android_app: BMICalculatorA (direct JNI)
    ├── BMICalculatorB/         ← android_app: BMICalculatorB (manager pattern)
    └── BMICalculatorC/         ← android_app: BMICalculatorC (industry-standard)
```

---

## Projects

### 1. Calculator Service (`services/calculator/`)

A basic C++ Binder service demonstrating AIDL in the vendor layer.

| Item | Value |
|------|-------|
| Binary | `/vendor/bin/calculatord` |
| AIDL | `com.myoem.calculator.ICalculatorService` |
| Operations | `add`, `subtract`, `multiply`, `divide` |
| Backend | cpp + java (libbinder, not libbinder_ndk) |
| Binder type | Regular binder (not NDK) |
| SELinux | permissive |

**Modules:** `calculatorservice-aidl`, `calculatord`, `calculatord-vintf-fragment`, `calculator_client`

---

### 2. BMI Service (`services/bmi/`)

A C++ NDK binder service that computes BMI. Used as the backend by the BMI Calculator apps.

| Item | Value |
|------|-------|
| Binary | `/vendor/bin/bmid` |
| AIDL | `com.myoem.bmi.IBMIService/default` |
| Operations | `getBMI(height, weight)` |
| Backend | NDK (libbinder_ndk) |
| VINTF | `vintf/bmid.xml` |
| Service contexts | `com.myoem.bmi.IBMIService/default u:object_r:bmid_service:s0` |

**Modules:** `bmiservice-aidl`, `bmid`, `bmid-vintf-fragment`, `bmi_client`

---

### 3. Thermal Control (`hal/thermalcontrol/` + `services/thermalcontrol/` + `libs/thermalcontrol/`)

Full-stack thermal management: sysfs HAL → AIDL service → Java manager → Compose app.

#### HAL (`hal/thermalcontrol/`)
- `libthermalcontrolhal` — reads `/sys/class/thermal/thermal_zone0/temp`, controls fan via `/sys/class/hwmon/hwmon*/pwm1`
- hwmon path discovered dynamically via `discoverHwmonPath()`

#### Service (`services/thermalcontrol/`)

| Item | Value |
|------|-------|
| Binary | `/vendor/bin/thermalcontrold` |
| AIDL | `com.myoem.thermalcontrol.IThermalControlService` |
| Operations | `getCpuTemp()`, `getFanPwm()`, `setFanPwm()`, `getFanMode()`, `setFanMode()`, `getFanRpm()` |
| sysfs — CPU temp | `/sys/class/thermal/thermal_zone0/temp` (millidegrees ÷ 1000) |
| sysfs — Fan PWM | `/sys/class/hwmon/hwmon2/pwm1` (0–255) |
| sysfs — Fan mode | `/sys/class/hwmon/hwmon2/pwm1_enable` (1=manual, 2=auto) |
| SELinux note | hwmon files use generic `sysfs` label — `sysfs_hwmon` does not exist in AOSP 15 |

#### Manager (`libs/thermalcontrol/`)
- `thermalcontrol-manager` — Java SDK library wrapping IThermalControlService

#### App (`apps/ThermalMonitor/`)
- `ThermalMonitor` — Jetpack Compose app showing temp, fan RPM, fan control

**Modules:** `libthermalcontrolhal`, `thermalcontrolservice-aidl`, `thermalcontrold`, `thermalcontrol-vintf-fragment`, `thermalcontrol_client`, `thermalcontrol-manager`, `ThermalMonitor`

---

### 4. Safe Mode (`services/safemode/` + `libs/safemode/` + `apps/SafeModeDemo/`)

AAOS Safe Mode stack: subscribes to VHAL `CURRENT_GEAR` + `PERF_VEHICLE_SPEED` via `IVhalClient` and notifies apps via AIDL callback.

#### Service (`services/safemode/`)

| Item | Value |
|------|-------|
| Binary | `/vendor/bin/safemoded` |
| AIDL | `com.myoem.safemode.ISafeModeService` |
| Callback | `ISafeModeCallback` with `VehicleData` parcelable |
| VHAL properties | `CURRENT_GEAR`, `PERF_VEHICLE_SPEED` |
| Gear values | PARK=0x4, REVERSE=0x2, NEUTRAL=0x1, DRIVE=0x8 |

#### Library (`libs/safemode/`)
- `safemode_library` — Java library, `com.myoem.safemode-service` AAR

#### App (`apps/SafeModeDemo/`)
- `SafeModeDemo` — Compose demo showing gear/speed and safe mode state

**Modules:** `safemodeservice-aidl`, `safemoded`, `com.myoem.safemode-service`, `safemode_client`, `safemode_library`, `SafeModeDemo`

---

### 5. HW Calculator (`services/hwcalculator/`)

Calculator service backed by real hardware (SPI/GPIO), demonstrating hardware-interfacing vendor services.

| Item | Value |
|------|-------|
| Binary | `/vendor/bin/hwcalculatord` |
| AIDL | `com.myoem.hwcalculator.IHwCalculatorService` |
| Operations | `add`, `subtract`, `multiply`, `divide` |
| Hardware | SPI-connected computation unit |

**Modules:** `hwcalculatorservice-aidl`, `hwcalculatord`, `hwcalculator_client`

---

### 6. Pot Volume Daemon (`services/potvolumed/`)

Reads a potentiometer (MCP3008 SPI ADC) and injects `KEY_VOLUMEUP` / `KEY_VOLUMEDOWN` events via `/dev/uinput`.

| Item | Value |
|------|-------|
| Binary | `/vendor/bin/potvolumed` |
| Hardware | Potentiometer → MCP3008 → `/dev/spidev0.0` |
| Kernel interface | `/dev/uinput` (no system libraries needed) |
| Why uinput | `libaudioclient` has no vendor image variant in AOSP 15 |
| Flow | ADC → InputReader → PhoneWindowManager → AudioManager |

**Modules:** `potvolumed`

---

### 7. BMI Calculator Apps — Three-Way Architecture Demo

Three apps with identical UI (BMI + calculator), each demonstrating a different pattern for calling vendor binder services. Uses `bmid` and `calculatord` as backends.

#### App A — Direct JNI (`apps/BMICalculatorA/`)

App calls vendor binder services directly from JNI in the app process.

```
App ──(NativeBinder JNI, FLAG_PRIVATE_VENDOR)──→ bmid / calculatord
```

| Item | Value |
|------|-------|
| JNI lib | `libbmicalculator_jni` |
| Key technique | `IBinder::FLAG_PRIVATE_VENDOR` — lowers required binder stability from LOCAL to VENDOR |
| Why JNI | Bypasses `BinderProxyTransactListener` (avoids IllegalArgumentException on frozen processes) |
| Article | `BMICalculatorA_Article_Series.md` |

**Modules:** `libbmicalculator_jni`, `BMICalculatorA`

#### App B — Manager Pattern (`apps/BMICalculatorB/` + `libs/bmicalculator/`)

JNI moved into a manager library. App sees only a plain Kotlin class.

```
App ──(BmiCalManager)──→ libbmicalmanager_jni (FLAG_PRIVATE_VENDOR) ──→ bmid / calculatord
```

| Item | Value |
|------|-------|
| Library | `bmicalculator-manager` (java_library, `sdk_version: "current"`) |
| JNI lib | `libbmicalmanager_jni` |
| App binder code | 0 lines — manager owns all of it |
| Key design | `private external` JNI functions hidden from callers |
| Article | `BMICalculatorB_Article_Series.md` |

**Modules:** `libbmicalmanager_jni`, `bmicalculator-manager`, `BMICalculatorB`

#### App C — Industry Standard (`services/bmiapp/` + `apps/BMICalculatorC/`)

Intermediate system service (system UID, never frozen) sits between the app and vendor services. App uses plain Java AIDL — no JNI anywhere in the app.

```
App ──(IBmiAppService AIDL)──→ BmiSystemService (UID=system) ──(JNI, FLAG_PRIVATE_VENDOR)──→ bmid / calculatord
```

| Item | Value |
|------|-------|
| System service | `BmiSystemService` — `android:persistent="true"`, `android:sharedUserId="android.uid.system"` |
| System service AIDL | `com.myoem.bmiapp.IBmiAppService` (LOCAL stability) |
| JNI lib | `libbmiappsvc_jni` (in system service, invisible to app) |
| App binder code | 3 lines (`ServiceManager.getService` + `Stub.asInterface` + null check) |
| App JNI | **None** |
| Why system UID | `ServiceManager.addService()` requires UID 1000; `android:sharedUserId="android.uid.system"` + platform certificate |
| Article | `BMICalculatorC_Article_Series.md` |

**Modules:** `bmiapp-aidl`, `bmiapp-aidl-java`, `libbmiappsvc_jni`, `BmiSystemService`, `BMICalculatorC`

---

## Build Commands

```bash
# Set up environment
lunch myoem_rpi5-trunk_staging-userdebug

# Build individual targets
m calculatord
m bmid
m thermalcontrold ThermalMonitor
m safemoded SafeModeDemo
m hwcalculatord
m potvolumed
m BMICalculatorA
m BmiSystemService BMICalculatorB BMICalculatorC
```

## Dev Iteration (no full rebuild)

```bash
# Remount system partition
adb root && adb shell mount -o remount,rw /

# Push vendor binary
adb push out/target/product/rpi5/vendor/bin/<service> /vendor/bin/<service>

# Push system APK
adb push out/target/product/rpi5/system/priv-app/<App>/<App>.apk \
         /system/priv-app/<App>/<App>.apk

# Push .so
adb push out/target/product/rpi5/system/lib64/<lib>.so /system/lib64/

adb reboot
```

## RPi5 Quirks

| Issue | Note |
|-------|------|
| No fastboot | `adb reboot bootloader` just reboots to Android |
| No `adb remount` | Use `adb shell mount -o remount,rw /vendor` |
| No `adb disable-verity` | OEM unlock not available |
| Full image | Write to SD card via `dd` |
| SELinux hwmon | `sysfs_hwmon` does not exist in AOSP 15 — use generic `sysfs` |

## Key Debugging Commands

```bash
# Check service is registered
adb shell service list | grep <name>

# Check process domain
adb shell ps -eZ | grep <service>

# SELinux denials
adb logcat -d | grep "avc: denied"

# Service logs
adb logcat -s <LOG_TAG>

# Kernel logs
adb logcat -b kernel
```

## Articles

| File | Topic |
|------|-------|
| `Binder_Complete_Guide.md` | Binder internals reference |
| `ThermalControl_Article_Series.md` | HAL → service → manager → app full stack |
| `SafeMode_Article_Series.md` | VHAL, CarProperty, AIDL callbacks |
| `PotVolume_Article_Series.md` | SPI ADC, uinput, why not libaudioclient |
| `BMICalculatorA_Article_Series.md` | Direct JNI, FLAG_PRIVATE_VENDOR, binder stability |
| `BMICalculatorB_Article_Series.md` | Manager pattern, sdk_version, private external |
| `BMICalculatorC_Article_Series.md` | Industry standard, system service, sharedUserId |
| `Testing_Article_Series.md` | Unit + instrumented testing in vendor layer |
| `ADB_Commands_AOSP_Dev.md` | ADB reference for AOSP development |
| `Shell_Commands_AOSP_Dev.md` | Shell command reference |
