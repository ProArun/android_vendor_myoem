# Senior Android/AOSP Developer — Interview Preparation Plan
### Target: RDT Sweden AB, Lund, Sweden
### Start Date: 2026-04-06 | End Date: 2026-07-12 (14 Weeks)

---

## Section 1 — Gap Analysis: Your Skills vs. The Job

### What You Already Have (Strong Evidence)

| Skill | Your Project Evidence |
|---|---|
| Android Application Development | 5+ years — Kotlin, Compose, Java, XML |
| Vendor HAL development | ThermalControl HAL (`libthermalcontrolhal`) — sysfs, hwmon, dynamic path discovery |
| AIDL + Binder IPC | 4 services: ThermalControl (ndk backend), SafeMode (vintf stability), Calculator, HwCalculator |
| SELinux policy authoring | Custom types (`spidev_device`), real label debugging, `.te` + `file_contexts` |
| Soong build system | `cc_binary`, `cc_library_shared`, `java_sdk_library`, `aidl_interface`, `android_app` |
| Init / RC files | `on boot` hooks, `chown`, `chmod`, service startup ordering |
| Vendor service lifecycle | `ServiceManager`, `addService`, `joinThreadPool`, RC class/user/group |
| AAOS / VHAL | SafeMode reading CURRENT_GEAR + PERF_VEHICLE_SPEED via `IVhalClient` |
| Hardware device interaction | SPI (`/dev/spidev10.0`), uinput (`uhid_device`), hwmon, sysfs, BMI sensor |
| Debugging on real hardware | `adb logcat`, `adb shell ps -eZ`, `ls -laZ`, manual `remount` on RPi5 |
| Java/Kotlin system layer | `SafeModeManager` (polling), `java_sdk_library`, Android app (Jetpack Compose) |

### Gaps to Close (Prioritized)

| Gap | Priority | Why It Matters for This Job |
|---|---|---|
| Kernel / Linux driver fundamentals | **P1 — Critical** | BSP bring-up is a core responsibility |
| Device Tree (DTS/DTSI) | **P1 — Critical** | Embedded hardware integration |
| Camera / Audio / Sensors HAL architecture | **P1 — High** | Explicitly listed in job responsibilities |
| Power management (WakeLock, PowerManagerService) | **P2 — High** | Explicitly listed required skill |
| Gauntlet / CTS / VTS testing | **P2 — High** | Explicitly listed in responsibilities |
| C++20 specific features | **P2 — Medium** | Listed requirement |
| Performance profiling (systrace, perfetto) | **P2 — Medium** | "Performance optimization" in duties |
| Android security model depth | **P3 — Medium** | Listed required skill |
| Framework services (system_server, Zygote) | **P3 — Medium** | Kernel→HAL→Framework→Apps full stack |

### Your Strongest Differentiators
1. You have **working code on real hardware** (RPi5), not just tutorials
2. You've debugged real hardware-specific issues (hwmon labeling, spidev bus numbering, no-fastboot workflow)
3. You've built the **full stack**: sysfs → HAL → Binder → AIDL → Java SDK library → App
4. You understand **why** things are the way they are: partition boundaries, vendor stability, SELinux model
5. **5+ years Android app dev** means you understand the consumer side — rare for an AOSP engineer

---

## Section 2 — Week-by-Week Study Plan

---

### PHASE 1 — Portfolio Consolidation
#### Week 1–2 | 2026-04-06 to 2026-04-19

---

#### Topic 1: Complete ThermalControl Full Stack (Phase 3 & 4)
**Dates:** 2026-04-06 to 2026-04-12

**What It Is**
Completing the ThermalControl project means adding the Java Manager (`thermalcontrol-manager` `java_sdk_library`) and an Android demo app on top of your working C++ HAL + Binder service. This closes the loop: sysfs → HAL → AIDL service → Java Manager → App.

**Why It Is Important**
This gives you the single most powerful interview asset you can have: a complete, working, full-stack AOSP component on real hardware that you built yourself from scratch. Every layer of the stack you can point to.

**Best Practices**
- `java_sdk_library` generates a `.jar` stub and a `.aar` — the rest of the build depends on the stub, not the impl
- The Manager class must `ServiceManager.waitForService("com.myoem.thermalcontrol.IThermalControlService")` before use
- Wrap the AIDL calls in a background thread / `ExecutorService` — never call Binder on the main thread (ANR)
- Hide the AIDL boilerplate behind the Manager API — callers should never see `IBinder`
- Phase 4 app: use Jetpack Compose, poll via `LaunchedEffect` + `delay(1000)`, display temp + fan RPM

**Mini Project**
- Finish `ThermalControlManager.java` with: `getCpuTemperature()`, `getFanRpm()`, `setFanSpeed(int)`, `setAutoMode()`, `setManualMode()`
- Write a Compose demo app: shows live temp gauge + fan RPM + a slider for manual PWM control
- Add the Manager and App to `myoem_base.mk` `PRODUCT_PACKAGES`

**Interview Questions**
1. "Walk me through a HAL you wrote end-to-end." → Full ThermalControl story, every layer
2. "Why is `java_sdk_library` used instead of `java_library` for a system API?" → Stub generation, API surface enforcement, `@hide` annotation
3. "Why can't you call Binder from the main thread in Android?" → Binder calls can block; main thread drives the UI event loop; blocking → ANR after 5s
4. "How does ServiceManager know your service is available?" → `addService()` in `main.cpp`; client uses `checkService()` or `waitForService()`

---

#### Topic 2: Writing AOSP Project Stories (STAR Format)
**Dates:** 2026-04-13 to 2026-04-19

**What It Is**
Converting your technical work into structured, interview-ready narratives. Each project story follows the STAR format: **S**ituation (what was the problem/goal), **T**ask (what was your specific responsibility), **A**ction (what you designed and built), **R**esult (what worked, what you learned).

**Why It Is Important**
Senior engineering interviews at companies like RDT are typically 60% technical and 40% behavioral. You need to articulate *why* you made design decisions, not just *what* you built. Swedish engineering culture values clarity and structured thinking.

**Best Practices**
- Each story should be 2–3 minutes max when spoken
- Always include one thing that went wrong and how you diagnosed it — this is what interviewers remember
- Connect your app dev experience to AOSP: "My 5 years of app dev meant I already understood the consumer side of Binder — I knew what the Java SDK layer needed to feel like before I wrote the native service"
- Quantify where possible: "reduced to 3 SELinux rules from 8", "service starts in under 200ms"

**Stories to Prepare**

| Story | Key Debugging Moment | What It Shows |
|---|---|---|
| ThermalControl | hwmon write failure — checking AVC → ps -eZ → logcat → ls -la → chown fix | Systematic hardware debugging |
| SafeMode | Java callback instability (vendor→system boundary), switch to polling | Understanding AOSP partition boundaries |
| PotVolume | `libaudioclient` not vendor-accessible → switched to uinput | Understanding system/vendor split, creative problem solving |
| PotVolume | `spidev0.0` assumed → actual was `spidev10.0` | "Always verify on real hardware" |
| ThermalControl | `sysfs_hwmon` doesn't exist in AOSP 15 → use `sysfs` | SELinux label verification methodology |

**Mini Project**
- Write a 1-page "project brief" for each of your 5 AOSP projects
- Record yourself telling each story — play it back, cut to 2.5 minutes
- Publish one project as a README on GitHub (vendor/myoem as a standalone repo)

**Interview Questions**
1. "Tell me about the most complex debugging session you've had."
2. "Tell me about a time your assumption was wrong."
3. "How do you approach integrating new hardware you've never worked with before?"
4. "What's the difference between developing Android apps and developing AOSP?"

---

### PHASE 2 — Linux Kernel & BSP Fundamentals
#### Week 3–4 | 2026-04-20 to 2026-05-03

---

#### Topic 3: Linux Kernel Module Development
**Dates:** 2026-04-20 to 2026-04-26

**What It Is**
A Linux kernel module is a piece of code that can be loaded into and unloaded from the kernel at runtime without rebooting. Modules implement device drivers, filesystems, and system calls. In Android BSP work, kernel modules implement the drivers for hardware: sensors, display controllers, audio codecs, cameras. Understanding how to write and debug them is fundamental to BSP work.

**Why It Is Important**
The job description says "BSP bring-up and platform integration for embedded Android devices" and "integration of hardware components such as camera, display, audio, and sensors." Every one of those integrations starts with a kernel driver. Without this layer, HALs have nothing to talk to.

**Best Practices**
- Always use `platform_driver` for hardware tied to a specific SoC — not `misc_register` directly
- `probe()` is called when the kernel matches your driver to a Device Tree node — do all hardware init here, not in `module_init`
- Use `devm_*` allocators (`devm_kmalloc`, `devm_ioremap`) — they auto-free on driver unbind, preventing leaks
- Never sleep in interrupt context — use `tasklet` or `workqueue` for deferred work
- Use `dev_err(dev, ...)` not `printk` — it prefixes with the device name automatically
- `CONFIG_DYNAMIC_DEBUG` — use `pr_debug` / `dev_dbg` for debug-only logging
- Always handle `probe()` returning `-EPROBE_DEFER` — dependency ordering

**Kernel Module Structure**
```c
#include <linux/module.h>
#include <linux/platform_device.h>

static int mydrv_probe(struct platform_device *pdev) {
    dev_info(&pdev->dev, "probe called\n");
    return 0;
}

static int mydrv_remove(struct platform_device *pdev) {
    return 0;
}

static const struct of_device_id mydrv_of_match[] = {
    { .compatible = "myoem,mydevice" },
    {}
};
MODULE_DEVICE_TABLE(of, mydrv_of_match);

static struct platform_driver mydrv = {
    .probe  = mydrv_probe,
    .remove = mydrv_remove,
    .driver = {
        .name = "mydrv",
        .of_match_table = mydrv_of_match,
    },
};
module_platform_driver(mydrv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ProArun");
```

**Mini Projects**
1. **Hello Module:** Write a loadable kernel module for RPi5. `insmod`/`rmmod` and see `dmesg` output. Build it out-of-tree using the RPi5 kernel headers.
2. **Sysfs Module:** Extend it to expose a read-only sysfs attribute (`/sys/kernel/mydrv/value`) that returns a counter incremented each time it is read. Connect this to your ThermalControl HAL pattern.
3. **Char Device:** Register a `/dev/mydrv` character device. Write a userspace program that `open()`/`read()`/`write()` it. This is the kernel side of what your SPI work did from userspace.

**Interview Questions**
1. "What is the difference between a kernel module and a built-in driver (`=y` vs `=m`)?"
   → Module can be loaded/unloaded at runtime; built-in is always in the kernel image; security implications
2. "What does `probe()` do and when is it called?"
   → Called by the kernel when it matches the driver's `of_match_table` against a Device Tree node; initializes hardware
3. "Why use `devm_kmalloc` instead of `kmalloc`?"
   → Automatically freed when the device is unbound from the driver; prevents memory leaks on hot-unplug or probe failure
4. "What is `-EPROBE_DEFER` and why is it important?"
   → Returned when a dependency (e.g., a clock, regulator, GPIO) is not yet ready; kernel retries probe later
5. "How does userspace see a kernel driver?"
   → Via sysfs attributes, character/block device nodes, or netlink — kernel exposes an interface, HAL reads it

---

#### Topic 4: Device Tree (DTS / DTSI)
**Dates:** 2026-04-27 to 2026-05-03

**What It Is**
The Device Tree is a data structure that describes the hardware topology of a system to the Linux kernel — which peripherals are connected, at which addresses, on which buses, with which interrupts and clocks. On ARM systems (including RPi5 and all Android phones), the bootloader passes a Device Tree Blob (DTB) to the kernel at boot. The kernel uses it to instantiate drivers automatically, without any hardcoded hardware knowledge in the kernel source.

**Why It Is Important**
BSP bring-up for a new device literally starts here. Before any driver runs, before any HAL exists, you add your hardware to the Device Tree and the kernel can find it. Every hardware component the job mentions — camera, display, audio, sensors — is declared in DTS. If you can't read and write DTS, you cannot do BSP work.

**Best Practices**
- Always inherit from the SoC `.dtsi` — never copy-paste base SoC nodes
- Use `&node_name { ... }` to extend existing nodes from the base DTSI, not redefine them
- `status = "okay"` to enable, `status = "disabled"` to disable a peripheral
- `compatible` must match exactly what the kernel driver declares in `of_device_id`
- Use `reg` for MMIO addresses; `interrupts` with the correct GIC format for your SoC
- DTS overlays (`.dtbo`) are the RPi way — you apply them at boot via `config.txt`

**DTS Example: I2C sensor**
```dts
/* bcm2712-rpi-5-b.dts or an overlay */
&i2c1 {
    status = "okay";
    clock-frequency = <400000>;

    bmi280@76 {
        compatible = "bosch,bmi280";
        reg = <0x76>;           /* I2C address */
        interrupt-parent = <&gpio>;
        interrupts = <4 IRQ_TYPE_EDGE_RISING>;
        vdd-supply = <&reg_3v3>;
    };
};
```

**Connecting DTS → Kernel Driver → HAL**
```
DTS node (compatible = "bosch,bmi280")
    ↓  kernel matches compatible string
platform_driver.probe() called
    ↓  driver creates /sys/bus/i2c/devices/1-0076/ or /dev/iio:device0
HAL opens sysfs or /dev node
    ↓
SensorService via Sensors HAL
    ↓
App: SensorManager.getDefaultSensor()
```

**Mini Projects**
1. **Read the RPi5 DTS:** Find `/home/arun/aosp-rpi/` kernel source. Locate `arch/arm64/boot/dts/broadcom/bcm2712-rpi-5-b.dts`. Find the `pwm-fan` node — this is the hardware your ThermalControl HAL reads. Trace it from DTS → kernel driver → sysfs path.
2. **Write a DTS overlay:** Add a dummy node `myoem,hello-device` to the device tree. Write a kernel module (from Topic 3) that matches it via `of_match_table`. Verify `probe()` is called at boot.
3. **Study the BMI sensor DTS:** You have `services/bmi` — find what sysfs path it reads. Trace that path back to the DTS node and the kernel driver.

**Interview Questions**
1. "What is the Device Tree and why does Android use it?"
   → Hardware description passed from bootloader to kernel; replaces board files; allows one kernel binary for multiple hardware variants
2. "What is `compatible` used for?"
   → String matched by kernel to find the right driver; must match `of_device_id.compatible` in the driver exactly
3. "What is a DTS overlay and when would you use one?"
   → A partial DTS applied at runtime (or boot time) to modify the base tree; used for add-on boards, HATs, or conditional hardware configurations
4. "How would you add a new I2C sensor to an Android device?"
   → Add DTS node with correct compatible/reg/interrupts → write/enable kernel driver → add HAL → expose via SensorService
5. "What is the difference between `.dts`, `.dtsi`, and `.dtbo`?"
   → `.dts` = board-level source; `.dtsi` = included shared SoC/platform description; `.dtbo` = overlay binary for runtime patching

---

### PHASE 3 — BSP Bring-Up
#### Week 5 | 2026-05-04 to 2026-05-10

---

#### Topic 5: BSP Bring-Up Process End-to-End
**Dates:** 2026-05-04 to 2026-05-10

**What It Is**
BSP (Board Support Package) bring-up is the process of making a new hardware board run Android from scratch — from "nothing boots" to "full Android UI." It involves: bootloader bring-up, kernel port, device tree creation, peripheral driver enablement, HAL implementation, and product configuration.

**Why It Is Important**
This is the primary job responsibility listed: "BSP bring-up and platform integration for embedded Android devices." Understanding this process end-to-end — even if you haven't done a full bring-up — shows you understand how the pieces connect and what the role involves day-to-day.

**Best Practices**
- Always bring up in layers — get UART serial console first, then kernel, then userspace
- Validate each layer before moving to the next (don't debug HAL if kernel isn't stable)
- Use a minimal kernel config first (`defconfig`), add features incrementally
- Use `dmesg` as your primary diagnostic — read it completely from the start
- Check `early_param` and `__setup` handlers if boot arguments aren't being parsed
- For Android specifically: get `init` running before worrying about any Android services

**BSP Bring-Up Sequence**

```
1. Bootloader (U-Boot / ABL)
   - UART output visible? → hardware is alive
   - Can load kernel image from SD/eMMC?
   - Passes correct DTB + cmdline to kernel?

2. Kernel Boot
   - ARM64 head.S → decompress → start_kernel()
   - Early console (earlycon=) → dmesg visible
   - Device Tree parsed → drivers probed
   - VFS initialized → rootfs mounted

3. Init (Android init)
   - /init runs from ramdisk
   - Parses /init.rc + /vendor/etc/init/*.rc
   - Mounts /system, /vendor, /data partitions
   - Starts critical services: ueventd, logd, servicemanager

4. Zygote
   - Started by init
   - Loads Android Runtime (ART)
   - Pre-loads classes/resources
   - Forks app processes on demand

5. System Server
   - Forked from Zygote
   - Starts all Java framework services:
     ActivityManagerService, WindowManagerService,
     PackageManagerService, PowerManagerService...

6. Launcher / Home Screen
   - SurfaceFlinger ready
   - ActivityManager starts HOME intent
   - UI visible
```

**What You've Already Experienced**
- Your RPi5 AOSP work covers steps 3–6 entirely
- Your `thermalcontrold.rc` is part of step 3 (init)
- Your vendor services participate in step 3 onwards
- **Gap:** Steps 1–2 (bootloader + kernel bring-up)

**Mini Projects**
1. **Boot log analysis:** On your RPi5, capture full boot log: `adb logcat -b all -d > boot_full.log` + `adb shell dmesg > kernel_boot.log`. Annotate 10 key events: when is `init` started? When does `thermalcontrold` start? When does Zygote fork?
2. **Init trace:** Add `BootAnimation` timing markers to understand when each phase completes. Use `adb shell getprop | grep init` to see service states.
3. **Partition map:** Run `adb shell cat /proc/partitions` and `adb shell mount` — draw a diagram of the partition layout and what is mounted where.

**Interview Questions**
1. "Walk me through the Android boot sequence from power-on to home screen."
   → U-Boot → kernel → init → Zygote → SystemServer → Launcher (know each step)
2. "What is the first thing you do when bringing up a new board?"
   → Get UART serial output — confirms hardware is alive and you can see kernel logs before ADB is available
3. "The board boots to a black screen. How do you debug it?"
   → UART first; check if kernel panics; check if init starts (`dmesg | grep init`); check if SurfaceFlinger starts; check if display driver is loaded
4. "What is `init.rc` and what can it do?"
   → Android's init language: define services (start/stop), define actions (on boot/property), mount filesystems, set permissions
5. "What is the difference between `class main` and `class core` in a service definition?"
   → `core` services start very early (logd, servicemanager, ueventd); `main` services start after core is ready; ordering ensures dependencies are available

---

### PHASE 4 — Hardware HAL Ecosystem
#### Week 6–8 | 2026-05-11 to 2026-05-31

---

#### Topic 6: Camera HAL3 Architecture
**Dates:** 2026-05-11 to 2026-05-17

**What It Is**
Camera HAL3 (Hardware Abstraction Layer v3) is the interface between Android's `CameraService` (framework) and the hardware-specific camera driver. HAL3 introduced the "capture pipeline" model: applications submit capture requests, the HAL processes them through the hardware pipeline, and returns capture results with filled buffers. Modern Android uses AIDL-based camera HALs (`android.hardware.camera.provider@2.7` or `ICameraProvider` in AIDL).

**Why It Is Important**
Camera is explicitly listed in the job responsibilities ("Integration of hardware components such as camera, display, audio, and sensors"). Camera HAL is also one of the most complex HALs in AOSP — understanding it demonstrates depth.

**Best Practices**
- HAL3 is request-based and asynchronous — never block the capture pipeline
- Always implement `processCaptureRequest()` quickly and return immediately; do actual work in background threads
- Buffer management: use `gralloc` buffers (now `IMapper`/`IAllocator`) — never copy pixels unnecessarily
- `CameraMetadata` is your control/result channel — learn the key tags (`ANDROID_CONTROL_AE_MODE`, `ANDROID_SENSOR_EXPOSURE_TIME`)
- Validate in `configureStreams()` — reject unsupported format/size combinations early
- Error paths: `CAMERA3_MSG_ERROR_REQUEST` vs `CAMERA3_MSG_ERROR_BUFFER` vs `CAMERA3_MSG_ERROR_DEVICE`

**Architecture Diagram**
```
App (Camera2 API / CameraX)
    ↓ Binder / AIDL
CameraService (system_server)
    ↓ HIDL / AIDL
CameraProvider HAL  (vendor process: android.hardware.camera.provider)
    ↓
CameraDevice HAL    (per-camera-device impl)
    ↓
Camera kernel driver (/dev/videoN  — V4L2)
    ↓
Image Sensor (MIPI CSI-2)
```

**Key Interfaces to Know**
- `ICameraProvider` — enumerate cameras, open a camera device
- `ICameraDevice` — get static metadata, open a session
- `ICameraDeviceSession` — `configureStreams()`, `processCaptureRequest()`, `close()`
- `ICameraDeviceCallback` — `processCaptureResult()`, `notify()` (called from HAL back to framework)

**Mini Projects**
1. **Read the AOSP Camera HAL reference implementation:** `hardware/interfaces/camera/` — read `ICameraDevice.aidl` and `ICameraDeviceSession.aidl`. Draw a sequence diagram of a single capture request.
2. **Camera2 API app:** Since you know Kotlin/Compose, write a Camera2 API app that reads `CameraCharacteristics`. This shows you the consumer API that the HAL must satisfy.
3. **V4L2 exploration on RPi5:** Run `v4l2-ctl --list-devices` on your device. Find what V4L2 devices exist. Read from `/dev/video0` if a camera is attached.

**Interview Questions**
1. "What is the difference between Camera HAL1 and HAL3?"
   → HAL1 was synchronous and limited; HAL3 introduced the capture request/result pipeline, enabling ZSL, RAW capture, and per-frame controls
2. "What is a capture request and a capture result?"
   → Request: app specifies what to capture and how (exposure, focus, which streams); Result: HAL fills in what actually happened + output buffers
3. "What is `configureStreams()` responsible for?"
   → HAL validates requested stream configurations (format, size, max buffers) and allocates internal resources; must complete before requests can flow
4. "How does a camera image get from sensor to app memory?"
   → Sensor → MIPI CSI → ISP → DMA into gralloc buffer → HAL signals buffer ready → CameraService → app

---

#### Topic 7: Audio HAL Architecture
**Dates:** 2026-05-18 to 2026-05-24

**What It Is**
The Audio HAL is the interface between Android's `AudioFlinger` (the low-level audio mixing engine in system_server) and the hardware audio driver. It handles opening output/input streams, setting sample rate/format/channel config, writing PCM data, and managing audio routing (speaker, headphone, Bluetooth). In AAOS, audio routing is more complex due to multi-zone audio and the `AudioControl` HAL.

**Why It Is Important**
Audio is listed in the job responsibilities. Your PotVolume project shows you understand the system/vendor partition boundary (`libaudioclient` not vendor-accessible) — that is directly relevant here. Understanding *why* `libaudioclient` is off-limits is the same understanding you need for Audio HAL design.

**Best Practices**
- Audio HAL must be low-latency — `write()` should never block longer than one buffer period
- Use `AUDIO_FORMAT_PCM_16_BIT` or `AUDIO_FORMAT_PCM_FLOAT` — match what the codec hardware expects
- `IDevice::openOutputStream()` and `openInputStream()` are the main entry points
- For AAOS: `IAudioControl` HAL handles focus/routing; do not mix it with the core audio HAL
- Always implement `setParameters()` — AudioFlinger uses key-value pairs for routing commands
- Audio policy (`audio_policy_configuration.xml`) maps logical streams to physical HAL modules

**Architecture Diagram**
```
App (AudioTrack / MediaPlayer)
    ↓ Binder
AudioFlinger (system_server)
    ↓ calls Audio HAL interface
AudioHAL (android.hardware.audio@7.0 / AIDL IModule)
    ↓ writes PCM data
ALSA driver / TinyALSA (kernel)
    ↓
Codec hardware (I2S bus → DAC → speaker)
```

**Key Files in AOSP Source**
- `hardware/interfaces/audio/` — AIDL definitions for modern audio HAL
- `system/media/audio/include/system/audio.h` — audio format/channel constants
- `frameworks/av/services/audioflinger/` — AudioFlinger source (system side)
- `hardware/libhardware/include/hardware/audio.h` — legacy HAL interface

**Connection to Your Work**
Your PotVolume project hit the `libaudioclient` wall because `libaudioclient` is a *system-side* library that talks to AudioFlinger via Binder. Vendor processes cannot use it because it crosses the system/vendor boundary. The correct vendor-side approach is either:
- Audio HAL (if you own the audio path)
- uinput key events (your solution — correct for volume buttons)
- AIDL HAL to AudioFlinger (for audio routing in AAOS)

**Mini Projects**
1. **Read `audio_policy_configuration.xml`:** Find or generate an AOSP build for RPi5 and read this file. Understand `module`, `mixPort`, `devicePort`, `route` elements.
2. **TinyALSA exploration:** Run `adb shell tinymix` and `adb shell tinyplay` on your device. See what ALSA controls exist. This is the layer just below the Audio HAL.
3. **Write an Audio HAL stub:** Implement a minimal AIDL `IModule` that logs all calls but outputs silence. Use your existing AIDL experience from ThermalControl.

**Interview Questions**
1. "Why can't a vendor service use `libaudioclient`?"
   → `libaudioclient` has `image:system` — it links against system libraries not available in vendor partition; crossing the system/vendor boundary violates Treble
2. "What is AudioFlinger and what does it do?"
   → Android's audio mixing engine running in system_server; mixes multiple audio tracks, applies effects, routes to HAL; the central audio hub
3. "What is audio policy in Android?"
   → Rules for routing audio streams to correct output devices (speaker vs headphone vs BT); configured via `audio_policy_configuration.xml`; enforced by `AudioPolicyService`
4. "What is the difference between a `mixPort` and a `devicePort` in audio policy?"
   → `mixPort` = logical stream type (media, call, notification); `devicePort` = physical hardware (speaker, headphone jack, BT); routes connect them

---

#### Topic 8: Sensors HAL
**Dates:** 2026-05-25 to 2026-05-31

**What It Is**
The Sensors HAL provides Android's `SensorService` with access to hardware sensors: accelerometer, gyroscope, magnetometer, proximity, light, pressure, temperature, and custom sensors. Modern Android uses the `ISensors` AIDL HAL. The HAL is responsible for opening sensor devices, polling for data, and delivering timestamped sensor events.

**Why It Is Important**
Sensors are explicitly in the job's hardware integration list. More importantly, your BMI sensor project (`services/bmi`) is the closest thing you have to a real sensors HAL implementation — connecting that work to the formal Sensors HAL architecture is a strong interview story.

**Best Practices**
- Sensors HAL runs in a separate HAL process: `android.hardware.sensors-service`
- Use `SensorInfo` accurately — wrong `maxRange`, `resolution`, or `power` causes app calibration issues
- Batch mode: support `batchEvents()` for power-efficient background sensing
- Timestamps must be from `CLOCK_BOOTTIME` — not `CLOCK_REALTIME` (which can jump on NTP sync)
- Wake-up sensors vs non-wake-up: wake-up sensors can wake the AP from suspend
- Multi-HAL: `SensorsHidlHal` wrapper allows mixing multiple HAL implementations (e.g. separate IMU + environmental sensors)

**Architecture Diagram**
```
App (SensorManager.getDefaultSensor())
    ↓ Binder
SensorService (system_server)
    ↓ AIDL / HIDL
ISensors HAL (vendor process)
    ↓
Kernel driver (/dev/iio:device0 or /dev/input/eventX)
    ↓
Physical sensor (I2C/SPI connected IC)
```

**Connection to Your BMI Project**
Your `services/bmi` is reading BMI sensor data at the vendor/HAL level. The next step on that path would be:
- Wrap that reading into an `ISensors` AIDL HAL implementation
- Register sensor types: `TYPE_ACCELEROMETER` + `TYPE_GYROSCOPE`
- SensorService would then expose it to apps via `SensorManager`

**Mini Projects**
1. **Read your BMI service code:** Understand how it reads from the kernel. What sysfs/dev path does it use? What is the data format?
2. **Study ISensors AIDL:** Read `hardware/interfaces/sensors/aidl/android/hardware/sensors/ISensors.aidl`. Understand `activate()`, `batch()`, `poll()`/`injectSensorData()`.
3. **SensorManager app:** Write a Kotlin app using `SensorManager` that logs all available sensors with their properties. Run it on your device. Do you see the BMI sensor? Why or why not?

**Interview Questions**
1. "How does a physical sensor become accessible via Android's SensorManager?"
   → Hardware → kernel driver (IIO/input) → Sensors HAL reads the driver → SensorService distributes events → app via SensorManager
2. "Why must sensor timestamps use `CLOCK_BOOTTIME`?"
   → `CLOCK_REALTIME` can be adjusted by NTP causing jumps; `CLOCK_BOOTTIME` monotonically increases since boot, including during suspend — essential for sensor fusion
3. "What is a wake-up sensor?"
   → A sensor that wakes the AP from suspend when a threshold is crossed (e.g. significant motion, wake gesture); requires the HAL to hold a wakelock until the event is delivered
4. "What is sensor batching and why is it useful?"
   → HAL buffers sensor events in FIFO and delivers them in batches; AP can sleep longer → significant power saving for background sensing

---

### PHASE 5 — Power Management
#### Week 9 | 2026-06-01 to 2026-06-07

---

#### Topic 9: Android Power Management
**Dates:** 2026-06-01 to 2026-06-07

**What It Is**
Android's power management system controls the device's power states to maximize battery life while meeting application and user requirements. It spans the full stack: Linux kernel PM (suspend/resume, runtime PM, wakeup sources) → HAL layer (power HAL, thermal HAL) → Framework (`PowerManagerService`, `DeviceIdleController`) → Application layer (wakelocks, JobScheduler, Doze).

**Why It Is Important**
"Android security, power management, and performance optimization" is listed as a required skill. Power management bugs are among the hardest to debug (they only reproduce in specific power states) and most impactful (battery drain is a top user complaint). For AAOS specifically, the vehicle power lifecycle (IGN ON/OFF, sleep, deep sleep) adds another layer.

**Best Practices**
- Always release wakelocks in `finally` blocks — leaked wakelocks drain the battery silently
- Prefer `JobScheduler` / `WorkManager` over long-running services for deferrable work
- Use `PARTIAL_WAKE_LOCK` only — `FULL_WAKE_LOCK` is deprecated; keep screen management separate
- In native code: `PowerManager::acquireWakeLock()` + always `releaseWakeLock()` in error paths
- Kernel side: use `wakeup_source_register()` + `__pm_stay_awake()`/`__pm_relax()`
- For AAOS: use `CarPowerManager` for vehicle-aware power state transitions

**Power State Machine (Android)**
```
AWAKE
  ↓ screen off + no wakelocks
DREAMING (screensaver)
  ↓ timeout
DOZE (light idle)
  ↓ extended idle
DOZE_SUSPEND (deep idle — CPU suspended, only AON sensor hub active)
  ↓ inactivity threshold
SUSPEND (Linux kernel suspend-to-RAM)
```

**AAOS Power State Machine**
```
OFF → WAIT_FOR_VHAL → ON_DISP_OFF → ON → SHUTDOWN_PREPARE → SHUTDOWN
                                      ↕
                                   SUSPEND
```

**Key Framework Components**
- `PowerManagerService` — manages wakelocks, screen state, battery state notifications
- `DeviceIdleController` — Doze mode orchestration; controls which apps get network/CPU
- `BatteryStatsService` — tracks what consumed battery (per-app, per-sensor, per-wakelock)
- `ThermalService` — integrates with your ThermalControl work! Routes thermal events to throttle CPU/GPU

**Connection to Your Work**
Your ThermalControl project is part of Android's power management stack: `ThermalService` subscribes to thermal events from the HAL to apply thermal throttling policies. You can describe ThermalControl as "a thermal HAL feeding Android's power management subsystem."

**Mini Projects**
1. **Wakelock audit:** Run `adb shell dumpsys power` on your RPi5. Find active wakelocks. Which services hold them? How long?
2. **Battery stats:** Run `adb shell dumpsys batterystats --charged` — read the top consumers.
3. **Thermal integration:** Read `frameworks/base/services/core/java/com/android/server/ThermalManagerService.java`. Find where it calls the Thermal HAL. How does it relate to your `libthermalcontrolhal`?
4. **PowerManagerService walkthrough:** Find the `acquireWakeLock()` and `releaseWakeLock()` code path in `PowerManagerService.java`. Trace a wakelock from Java API to kernel `/sys/power/wake_lock`.

**Interview Questions**
1. "What is a wakelock and what happens if you forget to release one?"
   → Prevents system from entering suspend; leaked wakelock keeps CPU running at full power, drains battery silently
2. "What is Doze mode and how does it affect app behavior?"
   → Battery saving mode when device is idle and unplugged; restricts network, wakelocks, jobs, alarms except high-priority FCM messages
3. "How does Android's ThermalService interact with the Thermal HAL?"
   → ThermalService subscribes to thermal events via the HAL; on thermal threshold crossing it notifies registered `IThermalEventListener`s and can trigger throttling via `PowerManagerService`
4. "In AAOS, what triggers the transition from ON to SHUTDOWN_PREPARE?"
   → VHAL sends `AP_POWER_STATE_REQ` with `SHUTDOWN_PREPARE`; `CarPowerManagerService` responds by gracefully stopping services before cut-off
5. "How do you debug a battery drain issue on Android?"
   → `dumpsys batterystats`, check `wakelocks`, `kernel wakelocks`, `wakeup reasons`; `bugreport` for full picture; `Battery Historian` for visualization

---

### PHASE 6 — Testing & Performance
#### Week 10–11 | 2026-06-08 to 2026-06-21

---

#### Topic 10: CTS / VTS / Gauntlet Testing
**Dates:** 2026-06-08 to 2026-06-14

**What It Is**
- **CTS (Compatibility Test Suite):** Tests that ensure Android devices behave consistently from an app developer's perspective. Required by Google for GMS certification.
- **VTS (Vendor Test Suite):** Tests the vendor interface (HALs) against the AIDL/HIDL specifications. Ensures HALs comply with the interface they declare.
- **Gauntlet:** Google's internal name for the automated test infrastructure that runs CTS/VTS at scale on device farms. Also refers to the test harness — TradeFed-based.

**Why It Is Important**
The job explicitly lists "development of test frameworks and automation in C and Gauntlet." VTS is the testing layer directly above your HAL work — every HAL you write should have a VTS test verifying it.

**Best Practices**
- VTS tests are GTest-based (C++) — the same testing framework you'd use for native code
- Use `ASSERT_*` for fatal failures (stops test), `EXPECT_*` for non-fatal (continues)
- Test the interface contract, not the implementation — HAL tests should be black-box
- Use `atest` to run individual tests: `atest VtsHalThermalV2_0TargetTest`
- Parameterize tests with `INSTANTIATE_TEST_SUITE_P` when testing all HAL instances
- Use `getAidlHalInstanceNames<IFoo>()` helper to discover all HAL instances automatically

**VTS Test Example for Your ThermalControl HAL**
```cpp
#include <android/hardware/thermal/IThermal.h>
#include <gtest/gtest.h>
#include <aidl/Gtest.h>
#include <aidl/Vintf.h>

using namespace aidl::android::hardware::thermal;

class ThermalHalAidlTest : public testing::TestWithParam<std::string> {
  public:
    void SetUp() override {
        const auto instance = GetParam();
        SpAIBinder binder(AServiceManager_waitForService(instance.c_str()));
        thermal = IThermal::fromBinder(binder);
        ASSERT_NE(thermal, nullptr);
    }
    std::shared_ptr<IThermal> thermal;
};

TEST_P(ThermalHalAidlTest, GetThermalZones) {
    std::vector<TemperatureType> types;
    auto status = thermal->getTemperatures(false, TemperatureType::CPU, &types);
    EXPECT_TRUE(status.isOk());
    EXPECT_FALSE(types.empty());  // at least one CPU thermal zone
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(ThermalHalAidlTest);
INSTANTIATE_TEST_SUITE_P(
    PerInstance, ThermalHalAidlTest,
    testing::ValuesIn(android::getAidlHalInstanceNames<IThermal>()),
    android::PrintInstanceNameToString);
```

**Mini Projects**
1. **Write a VTS-style test for ThermalControl:** Using GTest and your AIDL client code, write a test binary that: connects to `thermalcontrold`, calls `getCpuTemperature()`, asserts result is between 0°C and 120°C, calls `getFanRpm()`, asserts it's >= 0.
2. **Run existing tests:** Run `atest` on any existing test in your AOSP tree. Understand the output format.
3. **CTS study:** Read the `CtsHardwareTestCases` module structure. Understand how a test is declared in `AndroidManifest.xml` + `Android.bp`.

**Interview Questions**
1. "What is the difference between CTS and VTS?"
   → CTS tests API compatibility from app perspective; VTS tests HAL interface compliance from framework perspective; both are required for GMS certification
2. "How do you write a VTS test for a new HAL?"
   → Use GTest + `getAidlHalInstanceNames` to discover instances; test each interface method; assert return values are within spec; build as `cc_test` with `test_suites: ["vts"]`
3. "What is TradeFed?"
   → Google's test harness that orchestrates test execution on devices; handles device allocation, test running, result collection; used by CTS/VTS/Gauntlet
4. "How do you run a single test on device?"
   → `atest TestModuleName` (builds + pushes + runs); or `adb shell /data/nativetest/mytest/mytest` for pre-pushed native tests
5. "What makes a good HAL test?"
   → Tests the interface contract (not impl details); handles all error cases; is deterministic; does not assume hardware state; parameterized to test all instances

---

#### Topic 11: Performance Profiling
**Dates:** 2026-06-15 to 2026-06-21

**What It Is**
Android performance profiling is the process of identifying and eliminating CPU bottlenecks, memory pressure, and latency spikes in native services, HALs, and the framework. Tools range from system-wide tracing (systrace/Perfetto) to per-process CPU profiling (simpleperf) to memory analysis (heapprofd, AddressSanitizer).

**Why It Is Important**
"System debugging, performance optimization, and stability improvements" is a core job responsibility. Senior engineers are expected to know which tool to reach for and how to interpret results.

**Best Practices**
- **Perfetto over systrace** for new work — systrace is deprecated; Perfetto is the modern replacement and has a better UI
- Add `ATRACE_CALL()` macros to your native code — they show up in Perfetto traces
- Use `simpleperf record -p <pid>` then `simpleperf report` for CPU hot spots
- Memory: `heapprofd` (native heap profiler, no recompile needed) before reaching for ASan
- ASan (`-fsanitize=address` in Android.bp) for correctness — catches use-after-free, buffer overflows, heap corruption
- `perfetto --config :test --out /data/misc/perfetto-traces/trace` for system-wide trace
- Interpret Perfetto: look for long slices on Binder threads (contention), long GC pauses (Java), missing frames (SurfaceFlinger)

**Tool Summary**

| Tool | What It Finds | How to Use |
|---|---|---|
| Perfetto | System-wide latency, Binder calls, scheduling | `adb shell perfetto -c :test -o /data/trace` |
| simpleperf | CPU hotspots in native code | `adb shell simpleperf record -p <pid> -g` |
| heapprofd | Native memory allocations | `adb shell heapprofd --pid <pid>` |
| AddressSanitizer | Memory corruption bugs | Add `-fsanitize=address` to Android.bp |
| `dumpsys meminfo` | Per-process memory breakdown | `adb shell dumpsys meminfo <package>` |
| `dumpsys cpuinfo` | CPU usage per process | `adb shell dumpsys cpuinfo` |
| `atrace` / ATRACE macros | Custom trace points in your code | `ATRACE_CALL()` in C++, `Trace.beginSection()` in Java |

**Adding Trace Points to ThermalControl**
```cpp
#include <utils/Trace.h>

binder::Status ThermalControlService::getCpuTemperature(float* temp) {
    ATRACE_CALL();  // <-- appears in Perfetto as a slice
    *temp = mHal->readCpuTemperature();
    return binder::Status::ok();
}
```

**Mini Projects**
1. **Profile thermalcontrold:** Add `ATRACE_CALL()` to your `ThermalControlService` methods. Capture a Perfetto trace while running `thermalcontrol_client`. Open in `ui.perfetto.dev`. Find your service's slices.
2. **simpleperf on your service:** `adb shell simpleperf record -p $(pidof thermalcontrold) sleep 10`. Report the top functions.
3. **Memory audit:** `adb shell dumpsys meminfo thermalcontrold`. Understand PSS, RSS, VSS. Is there a memory leak if you call getCpuTemperature() in a loop?

**Interview Questions**
1. "How do you find a CPU bottleneck in a native Android service?"
   → `simpleperf record` on the process, then `simpleperf report` to see top functions by sample count; add `ATRACE_CALL()` markers to correlate with system trace
2. "What is Perfetto and what can it show you?"
   → System-wide tracing tool; shows CPU scheduling, Binder IPC timing, custom ATRACE slices, memory allocation events; replaces systrace
3. "A system service is causing dropped frames. How do you approach it?"
   → Perfetto trace with SurfaceFlinger; find the frame that dropped; look at what Binder calls were made on the UI thread during that frame; find which service was slow
4. "What is AddressSanitizer and when do you use it?"
   → Compiler instrumentation that detects memory errors at runtime (use-after-free, buffer overflow, heap corruption); use during development/testing, not in production builds (2x slowdown)
5. "What is PSS and why is it more meaningful than RSS for Android?"
   → PSS (Proportional Set Size) = private memory + shared memory proportionally divided; RSS counts shared pages fully for each process; PSS gives accurate per-process memory cost

---

### PHASE 7 — C++20 & Android Security
#### Week 12 | 2026-06-22 to 2026-06-28

---

#### Topic 12: C++20 for Android Native Development
**Dates:** 2026-06-22 to 2026-06-25

**What It Is**
C++20 is the current major C++ standard and the one this job explicitly requires ("Solid programming skills in C/C++ (C++20)"). Key additions relevant to system/Android native work: concepts, ranges, coroutines, `std::span`, `std::format`, designated initializers, `[[likely]]`/`[[unlikely]]`, and `constexpr` expansions.

**Why It Is Important**
AOSP native code is moving to C++20. Modern Android.bp files set `cpp_std: "c++20"`. Knowing C++20 idioms is expected for senior roles, and interviews will probe this.

**Best Practices**
- Use `std::span<T>` instead of `(T*, size_t)` pairs — safer, no ownership confusion
- Use concepts to constrain template parameters instead of SFINAE (much more readable)
- Designated initializers for struct initialization — `SensorInfo info{.name="BMI280", .type=1}`
- `[[nodiscard]]` on functions where ignoring the return value is a bug (very common in HAL code)
- `if constexpr` for compile-time branching in templates
- Android NDK supports C++20 with `clang` (AOSP uses clang, not GCC)

**Key C++20 Features for Your Work**

```cpp
// std::span — safer than raw pointer+length
void processSamples(std::span<const float> samples) {
    for (float s : samples) { /* ... */ }
}

// Concepts — constrain templates cleanly
template<typename T>
concept Numeric = std::integral<T> || std::floating_point<T>;

template<Numeric T>
T clamp(T val, T lo, T hi) { return std::max(lo, std::min(hi, val)); }

// Designated initializers — readable struct init
struct SensorConfig {
    int samplingRateHz;
    float threshold;
    bool wakeup;
};
SensorConfig cfg{.samplingRateHz = 100, .threshold = 1.5f, .wakeup = true};

// [[nodiscard]] — catches ignored error returns
[[nodiscard]] binder::Status writeToSysfs(std::string_view path, int val);

// std::format (C++20) — type-safe string formatting
std::string msg = std::format("Temp: {:.1f}°C, Fan: {} RPM", temp, rpm);
```

**Mini Projects**
1. **Refactor SysfsHelper:** Apply `std::span`, `std::string_view`, `[[nodiscard]]`, and designated initializers to your `SysfsHelper.cpp`. Note what improves.
2. **Write concept-constrained HAL utilities:** Write a `clamp<Numeric>` and a `mapRange<Numeric>` (maps a value from one range to another) using C++20 concepts.
3. **C++20 quiz prep:** Write 10 code snippets that use different C++20 features; explain what each does and why it improves on the C++17 equivalent.

**Interview Questions**
1. "What is `std::span` and how does it improve over `T*, size_t`?"
   → Non-owning view over contiguous data; bounds-checked in debug; no dangling pointer risk; works with arrays, vectors, C-arrays uniformly
2. "What are C++ concepts and how do you use them?"
   → Compile-time constraints on template parameters; defined with `concept` keyword and `requires`; give much clearer error messages than SFINAE
3. "What is the difference between `const std::string&` and `std::string_view`?"
   → `string_view` is a non-owning view — no allocation; works on substrings, C strings, string literals without copying; `const string&` requires a `std::string` to exist
4. "What does `[[nodiscard]]` do?"
   → Compiler warning if the return value is ignored; important for functions returning error codes where ignoring the error is almost always a bug

---

#### Topic 13: Android Security Model
**Dates:** 2026-06-26 to 2026-06-28

**What It Is**
Android's security model is multi-layered: Linux DAC (user/group permissions), SELinux MAC (mandatory access control), Verified Boot (dm-verity, vbmeta), Keystore/Keymaster (hardware-backed crypto), app sandboxing (each app = unique Linux UID), and the permission model (runtime permissions, `protectionLevel`).

**Why It Is Important**
Security is a listed required skill. More importantly, you've lived several security layers already: SELinux policy writing, the system/vendor partition boundary (Treble security model), and the "no adb remount without OEM unlock" behavior (which is Verified Boot in action).

**Best Practices**
- SELinux: follow least-privilege — only allow exactly what is needed, nothing more
- Never add `permissive` to a domain in production — use audit2allow carefully and understand each rule
- Verified Boot: never disable dm-verity in production images; use test keys only in dev builds
- Keymaster: always use hardware-backed keys for sensitive operations — no software fallback for signing
- `neverallow` rules in SELinux are compile-time enforced — they are your security net; don't bypass them

**Security Architecture Quick Reference**

| Layer | Component | What It Does |
|---|---|---|
| Bootloader | ABL/U-Boot | Verifies kernel image signature |
| Kernel | dm-verity | Verifies every block of system/vendor partitions |
| Kernel | SELinux | Mandatory access control on all processes/files |
| HAL | Keymaster/KeyMint | Hardware-backed key storage and crypto operations |
| Framework | `PackageManagerService` | App signature verification, permission grants |
| Framework | `SELinux` policy | Loaded at boot from `/vendor/etc/selinux/` |
| App | Sandbox | Each app = unique UID; cannot access other app data |

**Connection to Your Work**
- Your entire SELinux experience (`thermalcontrond.te`, `potvolumed.te`, debugging unknown types, `ls -laZ`) is directly the Android security model in practice
- The "no adb remount" issue on RPi5 is Verified Boot preventing system partition modification
- The system/vendor partition split (why `libaudioclient` is inaccessible from vendor) is the Treble security/stability boundary

**Mini Projects**
1. **SELinux audit2allow exercise:** Intentionally remove one `allow` rule from `thermalcontrold.te`. Rebuild + push policy. Observe the AVC denial in logcat. Use `audit2allow` to regenerate the rule. Understand what it generates.
2. **Verified Boot research:** Read `external/avb/` in AOSP source — understand `vbmeta` structure, chain of trust from bootloader to partition.
3. **Security model diagram:** Draw the full chain: "How does Android know the system partition hasn't been tampered with?" from power-on to app running.

**Interview Questions**
1. "What is dm-verity and why does it prevent `adb remount` on production devices?"
   → Block-level integrity checking using a Merkle tree; any modification to a verified block causes a kernel panic or recovery; OEM unlock disables it for dev devices
2. "What is SELinux `neverallow` and why is it important?"
   → Compile-time rule that forbids certain allow rules from being added; enforced at policy build time; prevents security regressions from being silently added
3. "What is the difference between `protectionLevel normal`, `dangerous`, and `signature`?"
   → Normal: auto-granted, low risk; Dangerous: shown to user at runtime (location, camera, contacts); Signature: only granted to apps signed with the same certificate (system permissions)
4. "What is the Treble partition model and what security benefit does it provide?"
   → System/vendor partition split enforced by SELinux and mount policies; vendor code cannot depend on system internals; enables independent system + vendor updates without rebuilding the other
5. "How does Android verify an APK hasn't been tampered with?"
   → APK signature scheme v2/v3: entire APK is signed; signature stored in APK Signing Block; `PackageManagerService` verifies on install and on each class load (v4 scheme)

---

### PHASE 8 — Framework Layer
#### Week 13 | 2026-06-29 to 2026-07-05

---

#### Topic 14: Android Framework Internals
**Dates:** 2026-06-29 to 2026-07-05

**What It Is**
The Android Framework is the Java layer running in `system_server` that provides the high-level APIs that apps use. It bridges between apps (via Binder IPC) and native services / HALs. Key components: `ActivityManagerService` (app lifecycle), `WindowManagerService` (window/display), `PackageManagerService` (APK management), `PowerManagerService`, `SensorService`, and dozens more.

**Why It Is Important**
The job lists "Full-stack AOSP development (Kernel → HAL → Framework → System Applications)" — you need to be able to speak to the Framework layer even if your primary work is in the HAL/native layer below it.

**Best Practices**
- Framework services are accessed via `ServiceManager.getService("activity")` — same pattern you use in your vendor services
- Framework services run in `system_server` — a crash here reboots the phone; always handle errors gracefully
- Binder thread pool in system_server is limited — expensive synchronous calls block other clients
- Use `@GuardedBy` annotations to document lock ownership in Java framework code
- `Binder.clearCallingIdentity()` / `Binder.restoreCallingIdentity()` when making calls to other services from within a binder call

**system_server Startup**
```
Zygote forks system_server
    ↓
SystemServer.main()
    ↓
startBootstrapServices()  → ActivityManagerService, PackageManagerService
startCoreServices()       → BatteryService, UsageStatsService
startOtherServices()      → WindowManagerService, InputManagerService,
                            NetworkManagementService, TelephonyService ...
    ↓
ActivityManagerService.systemReady()
    ↓
Launcher started (HOME intent)
```

**Binder IPC Deep Dive (Kernel Level)**
```
Process A: proxy.method()
    ↓ Java Binder stub → JNI → libbinder → /dev/binder ioctl(BC_TRANSACTION)
Kernel Binder driver
    ↓ copies data to target process (zero-copy via mmap where possible)
Process B: Binder thread wakes on ioctl(BR_TRANSACTION)
    ↓ libbinder → JNI → onTransact() in Java stub → your service method
```

**Key Things You Already Know**
- You've written the **vendor side** of this: `addService()` in `main.cpp`, `onTransact()` via BnXxx, client proxies
- Your Java Manager classes (`SafeModeManager`, `ThermalControlManager`) are the **framework-side consumer pattern** — the same pattern framework services use internally
- This means you can speak to Binder from both sides

**Mini Projects**
1. **Trace a framework service call:** Pick `AudioManager.setVolume()` in Android source. Trace it: Java API → `IAudioService.aidl` → `AudioService.java` in system_server → Audio HAL call. Write the full trace as a diagram.
2. **Read system_server startup:** In `frameworks/base/services/java/com/android/server/SystemServer.java`, find where `SensorService` is started. Note the pattern — same `ServiceManager.addService()` you use.
3. **`dumpsys` deep dive:** Run `adb shell dumpsys activity` and `adb shell dumpsys window`. Read the output. Understand what state each service exposes.

**Interview Questions**
1. "What runs in system_server and why is it important?"
   → All major framework Java services (AMS, WMS, PMS, etc.); a crash here triggers a full system restart; it is the central hub for all app-facing services
2. "How does an Android app call a system service?"
   → App gets a Binder proxy via `ServiceManager.getService()` (or `Context.getSystemService()`); calls method on proxy; Binder kernel driver routes to the service's thread in system_server; result returned
3. "What is Zygote and why does it exist?"
   → Pre-loaded JVM process that forks to create app processes; forking after pre-loading ART classes/resources is much faster than starting a fresh JVM; all apps share pre-loaded read-only pages (COW)
4. "What is `Binder.clearCallingIdentity()` used for?"
   → Within a Binder call, the caller's UID is set as the calling identity; `clearCallingIdentity()` resets this to your own UID before calling another service (otherwise the downstream service sees your caller's UID); `restoreCallingIdentity()` must always be called after
5. "How many Binder threads does system_server have and why does it matter?"
   → By default 15–31 Binder threads; if all are blocked waiting on slow Binder calls, new incoming calls are queued; too many slow HAL calls from system_server can cause ANRs in apps

---

### PHASE 9 — Interview Preparation
#### Week 14 | 2026-07-06 to 2026-07-12

---

#### Topic 15: Mock Interviews & Final Preparation
**Dates:** 2026-07-06 to 2026-07-12

**Technical Questions — Master Answers for All**

**Stack & Architecture**
1. "Walk me through adding a completely new hardware peripheral to Android."
   → DTS node → kernel driver → verify sysfs/dev path → HAL implementation → AIDL interface → register with ServiceManager → Java/Kotlin Manager → App API

2. "Explain the Treble architecture. Why was it introduced?"
   → Splits system and vendor partitions with a stable AIDL/HIDL interface between them; allows Google to update system partition without re-certifying vendor; introduced Android 8.0

3. "What is LLNDK and why must vendor services use `libbinder_ndk` instead of `libbinder`?"
   → LLNDK = Low-Level NDK; C-ABI stable libraries that vendor code can use; `libbinder` (C++) has no ABI stability guarantee across system updates; `libbinder_ndk` is the stable C wrapper

**Debugging**
4. "A vendor service starts but immediately crashes. Debug it."
   → `adb logcat -s <tag>` for service logs; `adb logcat -b crash` for crash dump; `adb shell ps -eZ | grep <name>` to check if it ran; if SELinux issue → `logcat | grep avc`; tombstone in `/data/tombstones/`

5. "SELinux is blocking your service. Walk me through fixing it."
   → `adb logcat | grep "avc: denied"` to find the denial; `ls -laZ <path>` to see the actual label; write minimal `allow` rule in `.te`; never use `permissive` for whole domain

**Design**
6. "How would you design a HAL for a new temperature sensor?"
   → Define AIDL interface (`ITemperatureSensor` with `getTemperature()`, `setAlertThreshold()`); implement as C++ vendor service using `libbinder_ndk`; read from sysfs; write SELinux policy; RC file with correct user/group; VTS test

**Behavioral Questions — Your Stories**

| Question | Your Story | Key Point |
|---|---|---|
| "Hardest debug?" | ThermalControl hwmon write failure | Systematic: AVC → ps -eZ → logcat → ls -la → root cause |
| "Wrong assumption?" | `sysfs_hwmon` doesn't exist; `spidev0.0` was `spidev10.0` | Always verify on real hardware |
| "Design decision you're proud of?" | Polling vs callbacks in SafeModeManager | Understood vendor→system stability boundary |
| "Technical disagreement?" | `libaudioclient` not vendor-accessible — chose uinput | Understood system constraint, found correct solution |
| "App dev → AOSP transition?" | 5 years app dev → understand consumer API, designed HALs that feel right to app developers | Unique angle: you've built both ends |

**Questions to Ask the Interviewer**
1. "What does the typical hardware integration lifecycle look like here — from getting a new board to first Android boot?"
2. "What is the biggest technical challenge on the current project?"
3. "How does the team split work between kernel/BSP and HAL/framework layers?"
4. "What does the test infrastructure look like — do you run VTS regularly?"

**GitHub Portfolio Checklist**
- [ ] Create `aosp-rpi-vendor` public repo mirroring `vendor/myoem/`
- [ ] README: project overview, hardware used (RPi5 + Android 15), what each sub-project does
- [ ] Each sub-project has its own README: architecture diagram, how to build, how to test
- [ ] Include debugging stories in the READMEs — interviewers read these

---

## Section 3 — Complete Topic Checklist

Mark each topic complete as you finish it.

### Phase 1 — Portfolio Consolidation (Week 1–2)
- [ ] **Topic 1:** Complete ThermalControl Phase 3 (Java Manager) + Phase 4 (App)
- [ ] **Topic 2:** Write AOSP project stories (STAR format) for all 5 projects

### Phase 2 — Linux Kernel & BSP (Week 3–4)
- [ ] **Topic 3:** Linux Kernel Module Development
  - [ ] Hello Module on RPi5 (`insmod`/`rmmod`)
  - [ ] Sysfs-exposing Module
  - [ ] Character Device Module
- [ ] **Topic 4:** Device Tree (DTS/DTSI)
  - [ ] Read RPi5 DTS — trace pwm-fan to sysfs
  - [ ] Write DTS overlay + matching kernel module
  - [ ] Study BMI sensor DTS integration

### Phase 3 — BSP Bring-Up (Week 5)
- [ ] **Topic 5:** BSP Bring-Up Process
  - [ ] Capture and annotate boot log
  - [ ] Init trace — service startup timing
  - [ ] Partition map diagram

### Phase 4 — Hardware HAL Ecosystem (Week 6–8)
- [ ] **Topic 6:** Camera HAL3 Architecture
  - [ ] Read AOSP Camera AIDL interfaces
  - [ ] Draw capture request/result sequence diagram
  - [ ] Camera2 API app in Kotlin
- [ ] **Topic 7:** Audio HAL Architecture
  - [ ] Read `audio_policy_configuration.xml`
  - [ ] TinyALSA exploration on device
  - [ ] Write Audio HAL AIDL stub
- [ ] **Topic 8:** Sensors HAL
  - [ ] Study BMI service → ISensors AIDL mapping
  - [ ] Read ISensors AIDL interface
  - [ ] SensorManager Kotlin app

### Phase 5 — Power Management (Week 9)
- [ ] **Topic 9:** Android Power Management
  - [ ] `dumpsys power` — wakelock audit
  - [ ] `dumpsys batterystats` — battery consumers
  - [ ] Trace ThermalService → Thermal HAL integration
  - [ ] PowerManagerService wakelock code path

### Phase 6 — Testing & Performance (Week 10–11)
- [ ] **Topic 10:** CTS / VTS / Gauntlet Testing
  - [ ] Write VTS-style test for ThermalControl HAL
  - [ ] Run `atest` on an existing test
  - [ ] Study CTS test module structure
- [ ] **Topic 11:** Performance Profiling
  - [ ] Add `ATRACE_CALL()` to ThermalControlService
  - [ ] Capture Perfetto trace — view at ui.perfetto.dev
  - [ ] Run simpleperf on thermalcontrold
  - [ ] `dumpsys meminfo` memory audit

### Phase 7 — C++20 & Security (Week 12)
- [ ] **Topic 12:** C++20 for Android Native Development
  - [ ] Refactor SysfsHelper with C++20 features
  - [ ] Write concept-constrained HAL utilities
  - [ ] 10 C++20 feature code snippets prepared
- [ ] **Topic 13:** Android Security Model
  - [ ] SELinux audit2allow exercise
  - [ ] Verified Boot (vbmeta) research
  - [ ] Draw full security chain-of-trust diagram

### Phase 8 — Framework Layer (Week 13)
- [ ] **Topic 14:** Android Framework Internals
  - [ ] Trace `AudioManager.setVolume()` → HAL
  - [ ] Read system_server startup sequence
  - [ ] `dumpsys activity` + `dumpsys window` deep dive

### Phase 9 — Interview Preparation (Week 14)
- [ ] **Topic 15:** Mock Interviews & Final Preparation
  - [ ] Answer all 6 technical questions out loud (recorded)
  - [ ] Tell all 5 behavioral stories (timed, < 3 min each)
  - [ ] GitHub portfolio repo published
  - [ ] 4 questions to ask the interviewer prepared

---

*Generated: 2026-04-03 | Target interview: RDT Sweden AB, Lund — Senior Android/AOSP Developer*
