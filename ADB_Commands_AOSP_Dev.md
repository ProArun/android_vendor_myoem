# Top 100 ADB Commands for AOSP Developers

> Compiled for AOSP 15 / Raspberry Pi 5 development context.
> Commands are grouped by category for quick lookup.

---

## 1. Connection & Device Management

| # | Command | Explanation |
|---|---------|-------------|
| 1 | `adb devices` | List all connected devices/emulators with their serial numbers and state. |
| 2 | `adb devices -l` | Same as above but with more details (product, model, transport). |
| 3 | `adb connect <ip>:5555` | Connect to a device over TCP/IP (useful for Wi-Fi debugging). |
| 4 | `adb disconnect` | Disconnect all TCP/IP connected devices. |
| 5 | `adb -s <serial> <command>` | Target a specific device when multiple are connected (e.g., `adb -s emulator-5554 shell`). |
| 6 | `adb kill-server` | Kill the ADB server process (useful when ADB hangs or gets stuck). |
| 7 | `adb start-server` | Start the ADB server. Usually automatic, but useful after `kill-server`. |
| 8 | `adb wait-for-device` | Block until a device is connected. Useful in scripts after flashing. |
| 9 | `adb get-state` | Print the device state: `device`, `offline`, or `bootloader`. |
| 10 | `adb get-serialno` | Print the device serial number. |

---

## 2. Reboot & Boot Modes

| # | Command | Explanation |
|---|---------|-------------|
| 11 | `adb reboot` | Reboot the device normally. |
| 12 | `adb reboot recovery` | Reboot into recovery mode (if supported). |
| 13 | `adb reboot bootloader` | Reboot into bootloader/fastboot mode. **Note:** On RPi5, this just reboots to Android — no fastboot bootloader exists. |
| 14 | `adb reboot sideload` | Reboot into sideload mode for OTA-style installs. |
| 15 | `adb shell reboot -p` | Power off the device from shell. |

---

## 3. Shell Access

| # | Command | Explanation |
|---|---------|-------------|
| 16 | `adb shell` | Open an interactive shell on the device. |
| 17 | `adb shell <command>` | Run a single shell command and return output (e.g., `adb shell ls /vendor`). |
| 18 | `adb root` | Restart adbd with root privileges. Required for many debug operations. |
| 19 | `adb unroot` | Restart adbd without root privileges. |
| 20 | `adb remount` | Remount `/system`, `/vendor` as read-write. **Note:** On RPi5 use `adb shell mount -o remount,rw /vendor` instead. |

---

## 4. File Transfer

| # | Command | Explanation |
|---|---------|-------------|
| 21 | `adb push <local> <remote>` | Copy a file/directory from PC to device (e.g., `adb push mylib.so /vendor/lib64/`). |
| 22 | `adb pull <remote> <local>` | Copy a file/directory from device to PC (e.g., `adb pull /data/tombstones/ .`). |
| 23 | `adb push --sync <local> <remote>` | Push only files that are newer on the host (incremental push). |
| 24 | `adb pull /data/anr/ .` | Pull ANR traces folder for analysis. |
| 25 | `adb pull /data/tombstones/ .` | Pull native crash tombstones. |

---

## 5. Logcat — The AOSP Developer's Best Friend

| # | Command | Explanation |
|---|---------|-------------|
| 26 | `adb logcat` | Stream all logs from the device in real time. |
| 27 | `adb logcat -s <TAG>` | Filter logs to a specific tag (e.g., `adb logcat -s ThermalControlHal`). |
| 28 | `adb logcat -s <TAG1>:<level> <TAG2>:<level>` | Filter by multiple tags with log levels (V/D/I/W/E). |
| 29 | `adb logcat *:E` | Show only Error-level and above logs across all tags. |
| 30 | `adb logcat *:W` | Show Warning-level and above logs across all tags. |
| 31 | `adb logcat -d` | Dump current log buffer and exit (don't stream). |
| 32 | `adb logcat -c` | Clear (flush) the log buffer. |
| 33 | `adb logcat -b kernel` | Show kernel ring buffer logs (dmesg equivalent over ADB). |
| 34 | `adb logcat -b crash` | Show crash buffer — native crashes and Java exceptions. |
| 35 | `adb logcat -b all` | Show all log buffers (main, system, radio, crash, kernel). |
| 36 | `adb logcat -v time` | Include timestamps in log output. |
| 37 | `adb logcat -v threadtime` | Include timestamps + PID + TID in log output. |
| 38 | `adb logcat -f /sdcard/logcat.txt` | Save logcat output to a file on the device. |
| 39 | `adb logcat \| grep "avc: denied"` | Filter SELinux denials in real time. |
| 40 | `adb logcat -d \| grep "avc: denied"` | Dump and grep SELinux denials from existing buffer. |

---

## 6. Package Management (APKs)

| # | Command | Explanation |
|---|---------|-------------|
| 41 | `adb install <apk>` | Install an APK on the device. |
| 42 | `adb install -r <apk>` | Reinstall an APK, keeping its data (replace existing). |
| 43 | `adb install -t <apk>` | Install a test APK (allows `testOnly` manifest flag). |
| 44 | `adb uninstall <package>` | Uninstall an app by package name (e.g., `com.myoem.app`). |
| 45 | `adb shell pm list packages` | List all installed packages. |
| 46 | `adb shell pm list packages -f` | List packages with their APK file path. |
| 47 | `adb shell pm list packages -s` | List only system packages. |
| 48 | `adb shell pm list packages -3` | List only third-party (user-installed) packages. |
| 49 | `adb shell pm clear <package>` | Clear app data and cache (equivalent to "Clear Data" in Settings). |
| 50 | `adb shell pm disable-user <package>` | Disable a package without uninstalling it. |

---

## 7. Activity & Intent Control

| # | Command | Explanation |
|---|---------|-------------|
| 51 | `adb shell am start -n <pkg>/<activity>` | Start an activity explicitly (e.g., `am start -n com.myoem.app/.MainActivity`). |
| 52 | `adb shell am start -a android.intent.action.VIEW -d <uri>` | Start an activity using an implicit intent with a URI. |
| 53 | `adb shell am force-stop <package>` | Force stop an app immediately. |
| 54 | `adb shell am kill <package>` | Kill a background app gracefully. |
| 55 | `adb shell am broadcast -a <action>` | Send a broadcast intent (e.g., simulate system events). |
| 56 | `adb shell am startservice -n <pkg>/<service>` | Start a service by component name. |
| 57 | `adb shell am stopservice -n <pkg>/<service>` | Stop a running service. |
| 58 | `adb shell am stack list` | List activity stacks (useful for multi-window/AAOS debugging). |

---

## 8. System Properties

| # | Command | Explanation |
|---|---------|-------------|
| 59 | `adb shell getprop` | Dump all system properties (build fingerprint, ro.*, persist.*, etc.). |
| 60 | `adb shell getprop ro.build.version.release` | Get Android version (e.g., `15`). |
| 61 | `adb shell getprop ro.product.model` | Get the device model name. |
| 62 | `adb shell getprop ro.build.type` | Get build type: `user`, `userdebug`, or `eng`. |
| 63 | `adb shell getprop persist.sys.locale` | Read a persisted property (survives reboots). |
| 64 | `adb shell setprop <key> <value>` | Set a system property at runtime (non-`ro.*` only). Useful for toggling feature flags. |
| 65 | `adb shell getprop \| grep selinux` | Check SELinux mode from properties. |

---

## 9. Dumpsys — Runtime System State

| # | Command | Explanation |
|---|---------|-------------|
| 66 | `adb shell dumpsys` | Dump state of ALL system services (very verbose). |
| 67 | `adb shell dumpsys -l` | List all registered system services by name. |
| 68 | `adb shell dumpsys activity` | Dump ActivityManager state: running tasks, back stack, history. |
| 69 | `adb shell dumpsys package <pkg>` | Dump all info about an installed package (permissions, activities, version). |
| 70 | `adb shell dumpsys meminfo` | Show memory usage for all processes. |
| 71 | `adb shell dumpsys meminfo <pkg>` | Show detailed memory usage for a specific process. |
| 72 | `adb shell dumpsys battery` | Show battery state (charge, health, temperature). |
| 73 | `adb shell dumpsys thermal` | Show thermal engine state and temperatures. |
| 74 | `adb shell dumpsys power` | Show PowerManager state: wake locks, screen state, doze. |
| 75 | `adb shell dumpsys window` | Show WindowManager state: visible windows, focus, surfaces. |
| 76 | `adb shell dumpsys connectivity` | Show network/connectivity service state. |
| 77 | `adb shell service list` | List all Binder services registered in ServiceManager. Use to verify your AIDL service is registered. |
| 78 | `adb shell service check <name>` | Check if a specific service is registered (e.g., `service check thermalcontrolservice`). |

---

## 10. SELinux Debugging

| # | Command | Explanation |
|---|---------|-------------|
| 79 | `adb shell getenforce` | Get SELinux enforcement mode: `Enforcing` or `Permissive`. |
| 80 | `adb shell setenforce 0` | Set SELinux to Permissive mode (bypasses denials — dev/debug only). |
| 81 | `adb shell setenforce 1` | Restore SELinux to Enforcing mode. |
| 82 | `adb shell ls -laZ <path>` | List files with SELinux security context labels (e.g., `ls -laZ /sys/class/hwmon/hwmon2/`). |
| 83 | `adb shell ps -eZ \| grep <name>` | Show SELinux domain of a running process (e.g., verify `thermalcontrold` runs in correct domain). |
| 84 | `adb logcat -d \| grep "avc: denied"` | Dump existing SELinux denial messages from log buffer. |
| 85 | `adb shell dmesg \| grep "avc:"` | Find SELinux denials in kernel ring buffer (often more complete than logcat). |
| 86 | `adb shell cat /proc/kmsg \| grep "avc:"` | Read kernel message buffer for SELinux denials (requires root). |

---

## 11. Process & Memory Inspection

| # | Command | Explanation |
|---|---------|-------------|
| 87 | `adb shell ps -A` | List all processes. |
| 88 | `adb shell ps -eZ \| grep <name>` | Find a process and its SELinux domain. |
| 89 | `adb shell top` | Live CPU and memory usage per process (like `htop`). |
| 90 | `adb shell top -b -n 1` | Single snapshot of `top` output (non-interactive, for scripts). |
| 91 | `adb shell kill -3 <pid>` | Send SIGQUIT to a Java process — triggers a stack trace dump to logcat. |
| 92 | `adb shell cat /proc/<pid>/maps` | Show memory map of a process (libraries loaded, addresses). |
| 93 | `adb shell cat /proc/<pid>/status` | Show process status: memory, threads, UID, GID. |

---

## 12. Filesystem & Mount Operations

| # | Command | Explanation |
|---|---------|-------------|
| 94 | `adb shell mount` | List all current mount points and their options. |
| 95 | `adb shell mount -o remount,rw /vendor` | Remount `/vendor` as read-write. **RPi5 workflow** for pushing files without full rebuild. |
| 96 | `adb shell mount -o remount,ro /vendor` | Remount `/vendor` back as read-only after pushing. |
| 97 | `adb shell df -h` | Show disk usage in human-readable format for all partitions. |
| 98 | `adb shell ls -la /vendor/lib64/` | List contents of a vendor directory with permissions. |
| 99 | `adb shell cat /sys/class/thermal/thermal_zone0/temp` | Read CPU temperature directly from sysfs. |
| 100 | `adb shell cat /sys/class/hwmon/hwmon2/fan1_input` | Read fan RPM from sysfs hwmon interface. |

---

## RPi5-Specific Quick Reference

> Commands particularly important for the RPi5 / AOSP 15 dev workflow:

```bash
# Remount vendor RW and push a shared library
adb root
adb shell mount -o remount,rw /vendor
adb push out/target/product/rpi5/vendor/lib64/libthermalcontrolhal.so /vendor/lib64/
adb shell sync
adb reboot

# Verify your service is running and in the right SELinux domain
adb shell ps -eZ | grep thermalcontrold

# Check if Binder service is registered
adb shell service check thermalcontrolservice

# Watch logs for your service only
adb logcat -s ThermalControlHal ThermalControlService

# Dump SELinux denials
adb logcat -d | grep "avc: denied"
adb shell dmesg | grep "avc:"

# Read sysfs sensor values directly
adb shell cat /sys/class/thermal/thermal_zone0/temp
adb shell cat /sys/class/hwmon/hwmon2/pwm1
adb shell cat /sys/class/hwmon/hwmon2/fan1_input
```

---

## Log Level Reference

| Letter | Level | When to Use |
|--------|-------|-------------|
| `V` | Verbose | Most detailed; disabled in production |
| `D` | Debug | Development-time diagnostic info |
| `I` | Info | Normal operational messages |
| `W` | Warning | Unexpected but recoverable situations |
| `E` | Error | Errors that should not happen |
| `F` | Fatal | Unrecoverable errors — triggers crash |
| `S` | Silent | Suppresses all output for a tag |

---

*Last updated: March 2026 — AOSP 15 / android-15.0.0_r14 / Raspberry Pi 5*
