// ============================================================
// AOSP Vendor Layer Development on Raspberry Pi 5
// Google Slides Presentation Generator - Google Apps Script
// Based on 10-Part Medium Article Series by ProArun (aruncse2k20)
//
// HOW TO USE:
//   1. Go to https://script.google.com
//   2. Click "New Project"
//   3. Delete any existing code in the editor
//   4. Paste the entire contents of this file
//   5. Click Save (Ctrl+S), give the project any name
//   6. Click Run -> createAOSPPresentation
//   7. Grant permissions when prompted (first run only)
//   8. Open your Google Drive - the presentation will be there!
// ============================================================

var gPrs; // global presentation reference

// ============================================================
// MAIN ENTRY POINT
// ============================================================

function createAOSPPresentation() {
  gPrs = SlidesApp.create('AOSP Vendor Layer Development on Raspberry Pi 5');

  // Remove the default blank slide added by Google
  var existing = gPrs.getSlides();
  if (existing.length > 0) existing[0].remove();

  slidesCover();
  slidesOverview();
  slidesPart1_BigPicture();
  slidesPart2_VendorSetup();
  slidesPart3_BinderIPC();
  slidesPart4_AIDL();
  slidesPart5_Calculator();
  slidesPart6_BMI();
  slidesPart7_SELinux();
  slidesPart8_Debugging();
  slidesPart9_ManagerPattern();
  slidesConclusion();

  var count = gPrs.getSlides().length;
  var url   = gPrs.getUrl();
  Logger.log('Done! ' + count + ' slides. URL: ' + url);
}

// ============================================================
// HELPERS
// ============================================================

// Section/title divider slide
function Sec(title, subtitle) {
  var slide = gPrs.appendSlide(SlidesApp.PredefinedLayout.TITLE);
  var t = slide.getPlaceholder(SlidesApp.PlaceholderType.CENTERED_TITLE);
  var s = slide.getPlaceholder(SlidesApp.PlaceholderType.SUBTITLE);
  if (t) t.asShape().getText().setText(title);
  if (s) s.asShape().getText().setText(subtitle);
}

// Content slide: title + array of bullet strings
function S(title, bullets) {
  var slide = gPrs.appendSlide(SlidesApp.PredefinedLayout.TITLE_AND_BODY);
  var t = slide.getPlaceholder(SlidesApp.PlaceholderType.TITLE);
  var b = slide.getPlaceholder(SlidesApp.PlaceholderType.BODY);
  if (t) t.asShape().getText().setText(title);
  if (b) b.asShape().getText().setText(bullets.join('\n'));
}

// ============================================================
// COVER
// ============================================================

function slidesCover() {
  Sec(
    'AOSP Vendor Layer Development\non Raspberry Pi 5',
    'A 10-Part Engineering Deep Dive  |  by ProArun (aruncse2k20)\n' +
    'Android 15  |  AIDL  |  Binder IPC  |  SELinux  |  Soong'
  );
}

// ============================================================
// SERIES OVERVIEW ARTICLE
// ============================================================

function slidesOverview() {
  Sec('Series Overview', 'What This Series Is About');

  S('About This Series', [
    '10 hands-on articles covering AOSP OEM vendor layer development',
    'Real hardware: Raspberry Pi 5 running Android 15 (android-15.0.0_r14)',
    'Product target: myoem_rpi5-trunk_staging-userdebug',
    'Goal: Build production-quality native services entirely in vendor/',
    'No changes to device/ or frameworks/ — everything is OEM-owned',
    'Target audience: C++ developers learning AOSP-specific patterns'
  ]);

  S('Technologies Covered in This Series', [
    'Binder IPC   — Android\'s inter-process communication kernel driver',
    'AIDL         — Android Interface Definition Language for service contracts',
    'Soong        — Android.bp build system: modules, namespaces, backends',
    'SELinux      — Mandatory Access Control for process and file isolation',
    'VTS Testing  — Vendor Test Suite: automated validation of vendor services',
    'NDK Binder   — libbinder_ndk: stable ABI for vendor C++ services',
    'Java Manager — Exposing native services to Java/Kotlin apps'
  ]);

  S('Projects Built in This Series', [
    '1. Calculator Service   — 4 arithmetic ops over Binder (add/sub/mul/div)',
    '2. BMI Service          — Float BMI calculation with health category output',
    '3. ThermalControl HAL   — sysfs-based CPU temp + fan control on RPi5',
    '4. ThermalControl Svc   — AIDL service wrapping the HAL (VINTF stable)',
    '5. SafeMode Service     — VHAL integration for vehicle gear/speed events',
    '6. PotVolume Daemon     — SPI ADC potentiometer → audio volume control',
    'Full stack: AIDL → C++ Service → Java Manager → Kotlin App'
  ]);

  S('Development Environment', [
    'Device      : Raspberry Pi 5 (BCM2712, 8 GB RAM)',
    'AOSP branch : android-15.0.0_r14',
    'Build host  : Ubuntu 22.04 (x86_64)',
    'Build cmd   : lunch myoem_rpi5-trunk_staging-userdebug && m <target>',
    'Deployment  : adb push + adb shell mount -o remount,rw /vendor',
    'Constraint  : No fastboot bootloader, no adb remount, no adb disable-verity',
    'Full image  : write to SD card via dd'
  ]);
}

// ============================================================
// PART 1 — THE BIG PICTURE
// ============================================================

function slidesPart1_BigPicture() {
  Sec('Part 1: The Big Picture', 'How Android is Actually Organized');

  S('Android Architecture — 5 Layers', [
    'Layer 5 (Top)  Applications      — User apps, System apps, OEM apps',
    'Layer 4        Android Framework — ActivityManager, WindowManager, etc.',
    'Layer 3        Native C++ Libs   — libc, libm, OpenGL, Binder services',
    'Layer 2        HAL               — Vendor hardware abstraction (AIDL/HIDL)',
    'Layer 1 (Bot)  Linux Kernel      — Device drivers, memory, scheduling',
    '',
    'OEM work lives in Layer 2 (HAL) and between Layer 2-3 (vendor services)'
  ]);

  S('The Partition Model', [
    '/system  — Google-owned: Android framework, core libraries (read-only)',
    '/vendor  — OEM-owned: HALs, vendor daemons, device-specific binaries',
    '/product — Additional OEM apps and runtime configurations',
    '/odm     — ODM overlays and device-specific configurations',
    '',
    'Key rule: /vendor code must NOT depend on /system private internals',
    'VINTF (Vendor Interface) defines the stable ABI boundary between them'
  ]);

  S('Android Treble — Why the Vendor Partition Exists', [
    'Pre-Treble (before Android 8): OEM had to rebase ALL of Android per patch',
    'Google security update → OEM had to re-integrate months of work → slow',
    '',
    'Android Treble (Android 8+): Separated /system from /vendor completely',
    'Google updates /system independently; OEM maintains /vendor',
    'VINTF defines the contract both sides agree on at build time',
    '',
    'Result: Faster security patches, longer device support lifetime',
    'Our project follows Treble: vendor/myoem/ never touches /system internals'
  ]);

  S('AOSP vs OEM Customization Model', [
    'AOSP = Android Open Source Project — base Android from Google',
    'OEM layer lives entirely in vendor/myoem/ — additions without touching AOSP',
    '',
    'Our rule: ZERO changes to device/brcm/rpi5/ or frameworks/',
    'All services, HALs, and apps are declared in vendor/myoem/',
    'Product makefiles (myoem_rpi5.mk) tie vendor additions into the image',
    'OEM services register via ServiceManager — accessible system-wide'
  ]);

  S('Service Discovery in Android', [
    'ServiceManager = central registry for all Binder services (handle 0)',
    'Server registers at startup:  AServiceManager_addService(binder, name)',
    'Client discovers:             AServiceManager_checkService(name)',
    '',
    'Service names are strings:',
    '  com.myoem.calculator.ICalculatorService',
    '  com.myoem.bmi.IBMIService',
    '  com.myoem.thermalcontrol.IThermalControlService/default',
    '',
    'Java: ServiceManager.checkService(name)  (hidden API — platform only)'
  ]);

  S('Soong Build System Basics', [
    'Soong replaced Make as Android primary build system (Android 7+)',
    'Build files: Android.bp (Blueprint — JSON-like declarative format)',
    'Android.mk still supported but deprecated for new code',
    '',
    'Common module types:',
    '  cc_binary          — Native executable (/vendor/bin/)',
    '  cc_library_shared  — .so shared library',
    '  aidl_interface     — Generates Binder stubs from .aidl files',
    '  java_library       — Java .jar for platform use',
    '  android_app        — APK application'
  ]);

  S('RPi5-Specific Constraints (Important!)', [
    'No fastboot bootloader  — adb reboot bootloader just reboots to Android',
    'No adb remount          — Use: adb shell mount -o remount,rw /vendor',
    'No adb disable-verity   — OEM unlock not available on this board',
    'Full system update      — Write image to SD card via dd command',
    '',
    'Dev iteration workflow:',
    '  m <target> → adb root → adb shell mount -o remount,rw /vendor',
    '  → adb push out/.../bin/foo /vendor/bin/ → adb shell stop/start foo',
    '',
    'hwmon sysfs: sysfs_hwmon does NOT exist in AOSP 15 — use generic sysfs'
  ]);
}

// ============================================================
// PART 2 — VENDOR DIRECTORY SETUP
// ============================================================

function slidesPart2_VendorSetup() {
  Sec('Part 2: Setting Up Your Own Vendor Directory', 'The Foundation of OEM Customization');

  S('vendor/myoem/ — Complete Directory Layout', [
    'vendor/myoem/',
    '  AndroidProducts.mk          ← Entry point: lists all product .mk files',
    '  myoem_base.mk               ← PRODUCT_PACKAGES, NAMESPACES, SEPOLICY',
    '  products/myoem_rpi5.mk      ← Device-specific product configuration',
    '  hal/thermalcontrol/         ← HAL shared library (libthermalcontrolhal)',
    '  services/calculator/        ← Calculator native Binder service',
    '  services/bmi/               ← BMI native Binder service',
    '  services/thermalcontrol/    ← ThermalControl VINTF service',
    '  services/safemode/          ← SafeMode VHAL-integrated service',
    '  services/potvolumed/        ← SPI ADC → audio volume daemon',
    '  libs/thermalcontrol/        ← Java ThermalControlManager library'
  ]);

  S('AndroidProducts.mk — The Build Entry Point', [
    'Tells the build system what products exist in vendor/myoem/',
    '',
    'PRODUCT_MAKEFILES := \\',
    '    $(LOCAL_DIR)/products/myoem_rpi5.mk',
    '',
    'COMMON_LUNCH_CHOICES := \\',
    '    myoem_rpi5-trunk_staging-userdebug',
    '',
    'Build system scans ALL vendor/ subdirs for AndroidProducts.mk at parse time',
    'COMMON_LUNCH_CHOICES makes the product available in the lunch menu'
  ]);

  S('myoem_base.mk — Adding Packages and Policies', [
    '# Register Soong module directories',
    'PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/calculator',
    'PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/bmi',
    'PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/thermalcontrol',
    '',
    '# Binaries to include in the system image',
    'PRODUCT_PACKAGES += calculatord calculator_client',
    'PRODUCT_PACKAGES += bmid bmi_client',
    'PRODUCT_PACKAGES += thermalcontrold thermalcontrol_client',
    '',
    '# SELinux policy source directories',
    'PRODUCT_PRIVATE_SEPOLICY_DIRS += vendor/myoem/services/calculator/sepolicy/private'
  ]);

  S('myoem_rpi5.mk — Product Configuration', [
    '# Inherit base RPi5 AOSP product',
    '$(call inherit-product, device/brcm/rpi5/aosp_rpi5.mk)',
    '',
    '# Layer in OEM additions',
    '$(call inherit-product, vendor/myoem/myoem_base.mk)',
    '',
    '# Product metadata',
    'PRODUCT_DEVICE       := rpi5',
    'PRODUCT_NAME         := myoem_rpi5',
    'PRODUCT_BRAND        := MyOEM',
    'PRODUCT_MANUFACTURER := MyOEM',
    '',
    'Variant: userdebug = release build + root access (for development)'
  ]);

  S('Android.bp — cc_binary Module', [
    'cc_binary {',
    '    name:        "calculatord",',
    '    vendor:      true,          // installs to /vendor/bin/',
    '    srcs:        ["src/CalculatorService.cpp", "src/main.cpp"],',
    '    shared_libs: ["libbinder_ndk", "liblog"],',
    '    static_libs: ["calculatorservice-aidl-ndk"],',
    '    init_rc:     ["calculatord.rc"],',
    '}',
    '',
    'vendor: true  =>  installs to /vendor/bin/, links vendor-available libs only'
  ]);

  S('Android.bp — aidl_interface Module', [
    'aidl_interface {',
    '    name:             "calculatorservice-aidl",',
    '    srcs:             ["aidl/**/*.aidl"],',
    '    vendor_available: true,',
    '    unstable:         true,     // no frozen versions needed',
    '    backend: {',
    '        ndk:  { enabled: true  },   // calculatorservice-aidl-ndk  (C++)',
    '        cpp:  { enabled: false },   // NEVER in vendor!',
    '        java: { enabled: true  },   // calculatorservice-aidl-java',
    '        rust: { enabled: false },',
    '    },',
    '}'
  ]);

  S('Build, Deploy and Verify Commands', [
    '# Setup environment (once per shell session)',
    'source build/envsetup.sh',
    'lunch myoem_rpi5-trunk_staging-userdebug',
    '',
    '# Build a specific target',
    'm calculatord',
    '',
    '# Output location',
    'out/target/product/rpi5/vendor/bin/calculatord',
    '',
    '# Deploy to device',
    'adb root',
    'adb shell mount -o remount,rw /vendor',
    'adb push out/target/product/rpi5/vendor/bin/calculatord /vendor/bin/',
    'adb shell stop calculatord && adb shell start calculatord'
  ]);
}

// ============================================================
// PART 3 — BINDER IPC
// ============================================================

function slidesPart3_BinderIPC() {
  Sec('Part 3: Binder IPC', 'The Backbone of Android Services');

  S('What is IPC and Why Android Needs It', [
    'IPC = Inter-Process Communication',
    'Android runs each app/service in its own Linux process (sandboxed)',
    'Processes cannot share memory — OS enforces strict isolation',
    'IPC is required for a Calculator app to call CalculatorService',
    '',
    'Linux IPC options: pipes, Unix sockets, shared memory, signals',
    'Android chose Binder — a custom kernel driver (/dev/binder)',
    'Binder is used for every service call in Android — thousands per second'
  ]);

  S('Why Binder Over Other IPC Mechanisms?', [
    'Performance   — Single data copy (vs 2 copies for pipes/sockets)',
    'Security      — Kernel enforces caller UID/PID — no spoofing possible',
    'Object model  — Pass interface references (IBinder) across processes',
    'Thread mgmt   — Built-in thread pool management per process',
    'Death notify  — linkToDeath: know immediately when server process dies',
    'Synchronous   — Caller blocks until server replies (like a function call)',
    'Used by all   — ActivityManager, WindowManager, AudioService, etc.'
  ]);

  S('Binder Architecture Components', [
    'Kernel driver:   /dev/binder  — character device, handles transactions',
    'Thread pool:     Each process has worker threads waiting for incoming calls',
    'IBinder:         Base interface — any Binder object implements this',
    'BnFoo (Native):  Server-side stub — YOU implement this class',
    'BpFoo (Proxy):   Client-side proxy — auto-generated from AIDL',
    'ServiceManager:  Special Binder (handle 0) — the global name registry',
    '',
    'Bn = Binder Native (server),  Bp = Binder Proxy (client)'
  ]);

  S('Binder Transaction Lifecycle — Step by Step', [
    '1. Client calls method on BpFoo proxy (looks like a normal C++ call)',
    '2. Proxy marshals arguments into Parcel (serialized byte buffer)',
    '3. Parcel sent to kernel via ioctl(BC_TRANSACTION) on /dev/binder',
    '4. Kernel maps data into server process memory (zero extra copy)',
    '5. Server Binder thread wakes up, receives BR_TRANSACTION event',
    '6. onTransact() called — unmarshals args, calls your implementation',
    '7. Return value marshaled into reply Parcel, sent back same path',
    '8. Client receives reply — the original call returns with the result'
  ]);

  S('libbinder vs libbinder_ndk — Critical Difference', [
    'libbinder (C++ Binder):',
    '  — Part of /system partition — ABI can change between Android versions',
    '  — Uses sp<>, wp<> reference-counted smart pointers',
    '  — NOT safe to use in vendor — violates Treble stability rules',
    '  — NEVER use in vendor services!',
    '',
    'libbinder_ndk (NDK/LLNDK Binder):',
    '  — Stable ABI defined in NDK — safe across Android version boundaries',
    '  — Uses ABinderProcess_*, AServiceManager_* C functions',
    '  — ALWAYS use this in vendor services',
    '  — Declared as: shared_libs: ["libbinder_ndk"] in Android.bp'
  ]);

  S('NDK Binder — Server Bootstrap (main.cpp)', [
    '#define LOG_TAG "calculatord"  // MUST be first line!',
    '#include <android/binder_manager.h>',
    '#include <android/binder_process.h>',
    '#include "CalculatorService.h"',
    '',
    'int main() {',
    '    ABinderProcess_setThreadPoolMaxThreadCount(4);',
    '    ABinderProcess_startThreadPool();',
    '    auto svc = ndk::SharedRefBase::make<CalculatorService>();',
    '    AServiceManager_addService(svc->asBinder().get(),',
    '                              "com.myoem.calculator.ICalculatorService");',
    '    ABinderProcess_joinThreadPool();  // blocks — never returns',
    '    return 0;',
    '}'
  ]);

  S('NDK Binder — Client Discovery Pattern', [
    '#include <android/binder_manager.h>',
    '#include <aidl/com/myoem/calculator/ICalculatorService.h>',
    '',
    '// Step 1: Get raw binder handle from ServiceManager',
    'ndk::SpAIBinder binder(AServiceManager_checkService(',
    '    "com.myoem.calculator.ICalculatorService"));',
    'if (!binder.get()) { /* service not running yet */ }',
    '',
    '// Step 2: Wrap in typed AIDL proxy',
    'auto client = ICalculatorService::fromBinder(binder);',
    '',
    '// Step 3: Make calls',
    'int32_t result;',
    'auto status = client->add(10, 20, &result);',
    'if (status.isOk()) { printf("Result: %d", result); }'
  ]);

  S('Binder Threading Model', [
    'ABinderProcess_setThreadPoolMaxThreadCount(4)  — set pool size',
    'ABinderProcess_startThreadPool()               — launch worker threads',
    'ABinderProcess_joinThreadPool()                — main thread joins pool',
    '',
    'Incoming Binder calls are dispatched to idle worker threads',
    'Thread count of 4 is sufficient for most vendor services',
    '',
    'oneway keyword in AIDL — fire-and-forget (no reply, non-blocking)',
    'Regular methods — synchronous, caller blocks until server returns',
    'Re-entrancy — a service can be both client and server simultaneously'
  ]);
}

// ============================================================
// PART 4 — AIDL
// ============================================================

function slidesPart4_AIDL() {
  Sec('Part 4: AIDL', 'Defining Your Service Interface');

  S('What is AIDL?', [
    'AIDL = Android Interface Definition Language',
    'Describes the API contract between a service and its clients',
    'Analogous to: IDL, Protobuf .proto, Thrift .thrift, gRPC .proto',
    '',
    'You write:         ICalculatorService.aidl  (the contract)',
    'Build generates:   BnCalculatorService      (server stub)',
    '                   BpCalculatorService      (client proxy)',
    'You implement:     extend BnCalculatorService and fill in methods',
    '',
    'AIDL eliminates all Binder marshaling/unmarshaling boilerplate'
  ]);

  S('AIDL File Syntax — ICalculatorService.aidl', [
    'package com.myoem.calculator;',
    '',
    'interface ICalculatorService {',
    '    // Error code constants (visible in C++ and Java)',
    '    const int ERROR_DIVIDE_BY_ZERO = 1;',
    '',
    '    // Methods — all int32 for calculator ops',
    '    int add(int a, int b);',
    '    int subtract(int a, int b);',
    '    int multiply(int a, int b);',
    '    int divide(int a, int b);  // throws on b==0',
    '}',
    '',
    'File path: aidl/com/myoem/calculator/ICalculatorService.aidl'
  ]);

  S('AIDL Supported Types', [
    'Primitives:   boolean, byte, char, int, long, float, double',
    'String:       String  (Java) / std::string (C++ NDK)',
    'Arrays:       int[], String[]  — requires "in" directional tag',
    'Parcelables:  Custom structs declared in separate .aidl files',
    'Interfaces:   Pass AIDL interfaces as params (callback pattern)',
    'IBinder:      Raw binder handle (rarely used directly)',
    '',
    'Directional tags for non-primitive params:',
    '  in    — client sends data to server',
    '  out   — server fills data for client',
    '  inout — both directions'
  ]);

  S('Error Handling in AIDL — C++ NDK', [
    'Every AIDL method returns ndk::ScopedAStatus (never throw in C++)',
    '',
    'Success:        return ndk::ScopedAStatus::ok();',
    '',
    'Service error:  return ndk::ScopedAStatus',
    '                    ::fromServiceSpecificError(ERROR_CODE);',
    '',
    'Client side check:',
    '  auto status = client->divide(10, 0, &result);',
    '  if (!status.isOk()) {',
    '      int code = status.getServiceSpecificError();',
    '      // code == ICalculatorService::ERROR_DIVIDE_BY_ZERO',
    '  }'
  ]);

  S('aidl_interface in Android.bp — Full Config', [
    'aidl_interface {',
    '    name:             "calculatorservice-aidl",',
    '    srcs:             ["aidl/com/myoem/calculator/ICalculatorService.aidl"],',
    '    vendor_available: true,   // usable from vendor modules',
    '    unstable:         true,   // no API versioning required',
    '    backend: {',
    '        ndk:  { enabled: true  },  // → calculatorservice-aidl-ndk',
    '        cpp:  { enabled: false },  // disabled — Treble violation in vendor',
    '        java: { enabled: true  },  // → calculatorservice-aidl-java',
    '        rust: { enabled: false },',
    '    },',
    '}'
  ]);

  S('AIDL Stability Levels — unstable vs vintf', [
    'unstable: true',
    '  — No versioning, ABI can change freely between builds',
    '  — Use for: within-vendor services, prototypes',
    '  — Generated lib: calculatorservice-aidl-ndk',
    '  — Used by: Calculator, BMI services',
    '',
    'stability: "vintf"',
    '  — Frozen ABI, versioned, cross-partition safe',
    '  — Required for: HAL interfaces, services called from /system',
    '  — Must appear in VINTF manifest and compatibility matrix',
    '  — Generated lib: thermalcontrolservice-aidl-V1-ndk',
    '  — Used by: ThermalControl, SafeMode services'
  ]);

  S('AIDL Parcelable — VehicleData Example', [
    '// VehicleData.aidl — custom data type for SafeMode service',
    'package com.myoem.safemode;',
    '',
    '@VintfStability',
    'parcelable VehicleData {',
    '    float   speedMs      = 0.0;  // PERF_VEHICLE_SPEED in m/s',
    '    int     gear         = 4;    // CURRENT_GEAR (4=PARK)',
    '    float   fuelLevelMl  = 0.0;  // FUEL_LEVEL in ml',
    '}',
    '',
    'Used as: VehicleData getCurrentData();  in ISafeModeService.aidl'
  ]);

  S('AIDL Callback Pattern — ISafeModeService', [
    '// Register a callback interface for async notifications',
    'interface ISafeModeService {',
    '    VehicleData getCurrentData();           // synchronous snapshot',
    '    void registerCallback(ISafeModeCallback cb);',
    '    void unregisterCallback(ISafeModeCallback cb);',
    '    int  getVersion();',
    '}',
    '',
    'interface ISafeModeCallback {',
    '    oneway void onVehicleDataChanged(in VehicleData data);',
    '}',
    '',
    'oneway = fire-and-forget (server does not block waiting for client)'
  ]);
}

// ============================================================
// PART 5 — CALCULATOR SERVICE
// ============================================================

function slidesPart5_Calculator() {
  Sec('Part 5: Building the Calculator Service', 'First Native Binder Service from Scratch');

  S('Calculator Service — What We Build', [
    'Binary:      calculatord  (runs as /vendor/bin/calculatord)',
    'AIDL:        ICalculatorService  (4 operations)',
    'Operations:  add, subtract, multiply, divide',
    'Error:       divide-by-zero → ServiceSpecificException(1)',
    'Test client: calculator_client (CLI tool)',
    'VTS tests:   VtsCalculatorServiceTest (30+ test cases)',
    '',
    'This is the reference implementation — BMI follows same pattern'
  ]);

  S('ICalculatorService.aidl — The Contract', [
    '// File: aidl/com/myoem/calculator/ICalculatorService.aidl',
    'package com.myoem.calculator;',
    '',
    'interface ICalculatorService {',
    '    const int ERROR_DIVIDE_BY_ZERO = 1;',
    '',
    '    int add(int a, int b);',
    '    int subtract(int a, int b);',
    '    int multiply(int a, int b);',
    '    int divide(int a, int b);',
    '}'
  ]);

  S('CalculatorService.h — Class Declaration', [
    '#pragma once',
    '#include <aidl/com/myoem/calculator/BnCalculatorService.h>',
    '',
    'namespace aidl::com::myoem::calculator {',
    '',
    'class CalculatorService : public BnCalculatorService {',
    'public:',
    '    ndk::ScopedAStatus add     (int32_t a, int32_t b, int32_t* out) override;',
    '    ndk::ScopedAStatus subtract(int32_t a, int32_t b, int32_t* out) override;',
    '    ndk::ScopedAStatus multiply(int32_t a, int32_t b, int32_t* out) override;',
    '    ndk::ScopedAStatus divide  (int32_t a, int32_t b, int32_t* out) override;',
    '}; }',
    '',
    'Extends BnCalculatorService  ← generated by AIDL compiler from .aidl'
  ]);

  S('CalculatorService.cpp — Implementation', [
    '#define LOG_TAG "CalculatorService"  // MUST be first — before any #include',
    '#include <log/log.h>',
    '#include "CalculatorService.h"',
    'using namespace aidl::com::myoem::calculator;',
    '',
    'ScopedAStatus CalculatorService::add(int32_t a, int32_t b, int32_t* out) {',
    '    *out = a + b;',
    '    ALOGD("add(%d, %d) = %d", a, b, *out);',
    '    return ScopedAStatus::ok();',
    '}',
    'ScopedAStatus CalculatorService::divide(int32_t a, int32_t b, int32_t* out) {',
    '    if (b == 0) return ScopedAStatus::fromServiceSpecificError(ERROR_DIVIDE_BY_ZERO);',
    '    *out = a / b;  return ScopedAStatus::ok();',
    '}'
  ]);

  S('main.cpp — Service Bootstrap', [
    '#define LOG_TAG "calculatord"',
    '#include <android/binder_manager.h>',
    '#include <android/binder_process.h>',
    '#include "CalculatorService.h"',
    '',
    'static const char* SERVICE_NAME =',
    '    "com.myoem.calculator.ICalculatorService";',
    '',
    'int main() {',
    '    ABinderProcess_setThreadPoolMaxThreadCount(4);',
    '    ABinderProcess_startThreadPool();',
    '    auto svc = ndk::SharedRefBase::make<CalculatorService>();',
    '    AServiceManager_addService(svc->asBinder().get(), SERVICE_NAME);',
    '    ABinderProcess_joinThreadPool();',
    '    return 0;',
    '}'
  ]);

  S('calculatord.rc — Init Service File', [
    '# File: services/calculator/calculatord.rc',
    '# Bundled via: init_rc: ["calculatord.rc"] in Android.bp',
    '# Installed to: /vendor/etc/init/calculatord.rc',
    '',
    'service calculatord /vendor/bin/calculatord',
    '    class main          # starts with main class at boot',
    '    user system         # runs as system user',
    '    group system',
    '    # No "oneshot" = service restarts automatically if it crashes',
    '',
    'Verify startup:  adb logcat -s calculatord',
    'Manual control:  adb shell start calculatord | stop calculatord'
  ]);

  S('calculator_client — CLI Test Tool', [
    '# Usage:',
    'adb shell calculator_client add 10 20   →  Result: 30',
    'adb shell calculator_client sub 100 37  →  Result: 63',
    'adb shell calculator_client mul 6 7     →  Result: 42',
    'adb shell calculator_client div 10 2    →  Result: 5',
    'adb shell calculator_client div 10 0    →  Error: divide by zero (code 1)',
    '',
    'Client flow:',
    '  1. AServiceManager_checkService(SERVICE_NAME)',
    '  2. ICalculatorService::fromBinder(binder)',
    '  3. client->add(a, b, &result)',
    '  4. Check status.isOk() or getServiceSpecificError()'
  ]);

  S('VTS Tests — VtsCalculatorServiceTest', [
    'Test suite with Google Test framework, 30+ test cases:',
    '',
    'ServiceRegistered  — AServiceManager_checkService returns non-null',
    'AddPositive        — 10 + 20 = 30',
    'AddNegative        — (-5) + (-3) = -8',
    'AddZero            — 0 + 0 = 0',
    'SubtractBasic      — various combinations',
    'MultiplyBasic      — edge cases including INT_MAX boundary',
    'DivideBasic        — valid divisions, integer truncation',
    'DivideByZero       — expects getServiceSpecificError() == 1',
    'ConcurrencyTest    — 8 threads × 100 calls each (no crashes)',
    'StabilityTest      — 1000 sequential calls (no memory leaks)'
  ]);

  S('Running VTS Tests on Device', [
    '# Build the test binary',
    'm VtsCalculatorServiceTest',
    '',
    '# Push to device',
    'adb push out/target/product/rpi5/data/nativetest64/',
    '         VtsCalculatorServiceTest /data/local/tmp/',
    '',
    '# Run on device',
    'adb shell /data/local/tmp/VtsCalculatorServiceTest',
    '',
    '# Expected output:',
    '[==========] Running 32 tests from 1 test suite.',
    '[  PASSED  ] 32 tests.',
    '',
    'Android.bp: vendor: true, test_suites: ["vts"]'
  ]);
}

// ============================================================
// PART 6 — BMI SERVICE
// ============================================================

function slidesPart6_BMI() {
  Sec('Part 6: Building the BMI Service', 'Float Arithmetic and Health Categories');

  S('BMI Service Overview', [
    'Second service — demonstrates float handling in AIDL/Binder',
    'Binary:     bmid  (/vendor/bin/bmid)',
    'AIDL:       IBMIService  (single method)',
    'Operation:  getBMI(float height_m, float weight_kg) → float',
    'Formula:    BMI = weight / (height * height)',
    'Validation: height <= 0 or weight <= 0 → ERROR_INVALID_INPUT',
    'Client:     bmi_client shows health category (Underweight/Normal/etc.)'
  ]);

  S('IBMIService.aidl — The Interface', [
    '// File: aidl/com/myoem/bmi/IBMIService.aidl',
    'package com.myoem.bmi;',
    '',
    'interface IBMIService {',
    '    const int ERROR_INVALID_INPUT = 1;',
    '',
    '    // height in meters, weight in kilograms',
    '    // Returns BMI value (float)',
    '    // Throws ServiceSpecificException(1) if height<=0 or weight<=0',
    '    float getBMI(float height, float weight);',
    '}'
  ]);

  S('BMIService.cpp — Implementation', [
    '#define LOG_TAG "BMIService"',
    '#include <log/log.h>',
    '#include "BMIService.h"',
    'using namespace aidl::com::myoem::bmi;',
    '',
    'ScopedAStatus BMIService::getBMI(float height, float weight, float* out) {',
    '    if (height <= 0.0f || weight <= 0.0f) {',
    '        return ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_INPUT);',
    '    }',
    '    *out = weight / (height * height);',
    '    ALOGD("getBMI(%.2f m, %.2f kg) = %.2f", height, weight, *out);',
    '    return ScopedAStatus::ok();',
    '}'
  ]);

  S('BMI Health Categories — bmi_client Output', [
    'Implemented in bmi_client.cpp for display:',
    '',
    'BMI < 18.5          →  Underweight',
    'BMI 18.5 – 24.9     →  Normal weight',
    'BMI 25.0 – 29.9     →  Overweight',
    'BMI >= 30.0         →  Obese',
    '',
    'Example runs:',
    '  adb shell bmi_client 1.75 70   →  BMI: 22.86 (Normal weight)',
    '  adb shell bmi_client 1.60 45   →  BMI: 17.58 (Underweight)',
    '  adb shell bmi_client 1.80 100  →  BMI: 30.86 (Obese)',
    '  adb shell bmi_client 0 70      →  Error: invalid input (code 1)'
  ]);

  S('Float Precision in AIDL', [
    'AIDL float maps to C++ float (32-bit IEEE 754)',
    '32-bit float gives ~7 significant decimal digits — plenty for BMI',
    '',
    'Key coding rules:',
    '  Use 0.0f not 0.0 (avoids implicit double conversion)',
    '  Division order: weight / (h * h), NOT weight / h / h',
    '  Validate BEFORE computation — prevents NaN and Infinity results',
    '',
    'VTS test uses EXPECT_NEAR not EXPECT_EQ for float comparison:',
    '  EXPECT_NEAR(result, 22.86f, 0.01f);  // tolerance of 0.01'
  ]);

  S('Calculator vs BMI — Structural Comparison', [
    'Aspect           Calculator              BMI',
    'Methods          4 (add/sub/mul/div)     1 (getBMI)',
    'Return type      int32                   float',
    'Error codes      ERROR_DIVIDE_BY_ZERO=1  ERROR_INVALID_INPUT=1',
    'Input validation b == 0 for divide       height<=0 || weight<=0',
    'main.cpp         Identical pattern       Identical pattern',
    'Android.bp       Identical structure     Identical structure',
    'RC file          Identical pattern       Identical pattern',
    '',
    'Pattern is identical — only AIDL types and business logic differ'
  ]);

  S('VTS Tests — VtsBMIServiceTest', [
    'SetUpTestSuite: verifies service is available before all tests',
    '',
    'ServiceAvailable   — checkService returns non-null',
    'NormalBMI          — 1.75m, 70kg → EXPECT_NEAR(22.86, 0.01)',
    'UnderweightBMI     — low weight scenario',
    'OverweightBMI      — elevated weight scenario',
    'ObeseBMI           — high BMI scenario',
    'ZeroHeight         — expects ERROR_INVALID_INPUT',
    'ZeroWeight         — expects ERROR_INVALID_INPUT',
    'NegativeHeight     — expects ERROR_INVALID_INPUT',
    'NegativeWeight     — expects ERROR_INVALID_INPUT',
    'FloatPrecision     — checks multiple known BMI values with tolerance'
  ]);
}

// ============================================================
// PART 7 — SELINUX
// ============================================================

function slidesPart7_SELinux() {
  Sec('Part 7: SELinux', 'Teaching Android to Trust Your Service');

  S('What is SELinux?', [
    'SELinux = Security-Enhanced Linux (developed by NSA, mainlined ~2003)',
    'Implements MAC = Mandatory Access Control',
    '',
    'Traditional Linux DAC problem:',
    '  DAC = Discretionary Access Control (chmod, chown, rwxrwxrwx)',
    '  Root user can bypass ALL DAC rules — compromised root = total compromise',
    '',
    'MAC solution:',
    '  Kernel enforces policy regardless of UID/root status',
    '  Even root cannot do what SELinux policy does not explicitly allow',
    '  Android runs in ENFORCING mode — denials are both logged AND blocked'
  ]);

  S('SELinux Security Contexts', [
    'Every process has a security context (domain):',
    '  u:r:calculatord:s0',
    '  u:r:init:s0',
    '  u:r:system_server:s0',
    '',
    'Every file/socket/service has a security context (type):',
    '  u:object_r:vendor_file:s0          (vendor binaries)',
    '  u:object_r:calculatord_exec:s0     (our executable)',
    '  u:object_r:calculatord_service:s0  (our service registration)',
    '',
    'Format:  user : role : type : level',
    '  user = always "u",  role = "r" for processes / "object_r" for files'
  ]);

  S('How Processes Get Their SELinux Domain', [
    '1. init launches the service (init is in u:r:init:s0)',
    '2. init does fork() + exec() of /vendor/bin/calculatord',
    '3. SELinux applies a domain transition based on 3 factors:',
    '   — Source domain: u:r:init:s0',
    '   — Target executable type: u:object_r:calculatord_exec:s0',
    '   — Type transition rule in .te file',
    '4. After exec, process is in: u:r:calculatord:s0',
    '',
    'Verify: adb shell ps -eZ | grep calculatord',
    'Result: u:r:calculatord:s0  <pid>  1  /vendor/bin/calculatord'
  ]);

  S('SELinux Policy Files Structure', [
    'vendor/myoem/services/calculator/sepolicy/private/',
    '  calculatord.te       ← Type enforcement rules (the core policy)',
    '  file_contexts        ← Maps file paths to SELinux types',
    '  service_contexts     ← Maps service names to SELinux types',
    '',
    'Registered in myoem_base.mk:',
    '  PRODUCT_PRIVATE_SEPOLICY_DIRS += .../sepolicy/private',
    '',
    'Build system merges all .te files from all registered dirs at build time',
    'Policy compiled to binary and stored in /vendor/etc/selinux/'
  ]);

  S('file_contexts and service_contexts Files', [
    '# file_contexts — maps executable path to type',
    '/vendor/bin/calculatord   u:object_r:calculatord_exec:s0',
    '',
    '# service_contexts — maps service name to type',
    'com.myoem.calculator.ICalculatorService  u:object_r:calculatord_service:s0',
    '',
    'These labels are checked at:',
    '  — exec() time (file_contexts) for domain transition',
    '  — AServiceManager_addService() time (service_contexts) for registration',
    '  — AServiceManager_checkService() time for client access control'
  ]);

  S('Writing .te (Type Enforcement) Rules', [
    '# 1. Declare the domain and executable type',
    'type calculatord, domain;',
    'type calculatord_exec, exec_type, vendor_file_type, file_type;',
    '',
    '# 2. Allow init to launch and transition to our domain',
    'init_daemon_domain(calculatord)',
    '',
    '# 3. Declare service type and allow registration',
    'type calculatord_service, service_manager_type;',
    'add_service(calculatord, calculatord_service)',
    '',
    '# 4. Allow Binder use (usually included by init_daemon_domain)',
    'binder_use(calculatord)'
  ]);

  S('RPi5 SELinux Lesson: sysfs_hwmon Does Not Exist', [
    'Situation: ThermalControl service needs to write fan PWM via sysfs',
    'Assumption: sysfs_hwmon type exists for /sys/class/hwmon/ files',
    'Reality:    sysfs_hwmon does NOT exist in AOSP 15 base policy!',
    '',
    'Verification on device:',
    '  adb shell ls -laZ /sys/class/hwmon/hwmon2/pwm1',
    '  → u:object_r:sysfs:s0   (generic "sysfs", NOT "sysfs_hwmon")',
    '',
    'Fix in .te file:',
    '  allow thermalcontrold sysfs:file { read write open getattr };',
    '',
    'Lesson: ALWAYS verify sysfs labels empirically with ls -laZ on real device'
  ]);

  S('Diagnosing SELinux Denials', [
    'All denials appear in Android log:',
    '  adb logcat -d | grep "avc: denied"',
    '  adb logcat -b kernel | grep avc',
    '',
    'Sample denial:',
    '  avc: denied { write } for pid=1234 comm="calculatord"',
    '    path="/dev/binder"',
    '    scontext=u:r:calculatord:s0',
    '    tcontext=u:object_r:binder_device:s0 tclass=chr_file',
    '',
    'Reading it: calculatord (scontext) tried to write (action)',
    '  to binder_device chr_file (tcontext:tclass) → DENIED'
  ]);

  S('SELinux Debugging Workflow', [
    'Step 1: Run service, see denial in logcat (avc: denied)',
    'Step 2: Identify WHO (scontext) + ACTION ({write}) + WHAT (tcontext:tclass)',
    'Step 3: Add allow rule in .te file:',
    '    allow calculatord binder_device:chr_file { read write open ioctl };',
    'Step 4: Rebuild SELinux policy:  m selinux_policy',
    'Step 5: Push and reboot, check for remaining denials',
    '',
    'Dev shortcut (DEBUG ONLY — never ship):',
    '    adb shell setenforce 0   ← permissive mode, denials logged not blocked',
    '    adb shell getenforce     ← check current mode (Enforcing/Permissive)'
  ]);

  S('Common SELinux Macros for Vendor Services', [
    'init_daemon_domain(myservice)',
    '  → Allows init to exec and transition to myservice domain',
    '',
    'add_service(myservice, myservice_svc)',
    '  → Allows myservice to register with ServiceManager',
    '',
    'binder_use(myservice)',
    '  → Allows myservice to use Binder IPC (/dev/binder)',
    '',
    'binder_call(myservice, other_service)',
    '  → Allows myservice to call another Binder service',
    '',
    'get_prop(myservice, vendor_default_prop)',
    '  → Allows reading system properties'
  ]);
}

// ============================================================
// PART 8 — DEBUGGING
// ============================================================

function slidesPart8_Debugging() {
  Sec('Part 8: Debugging', 'When Things Don\'t Work');

  S('Debugging Philosophy', [
    'Start with logs — Android has excellent logging infrastructure',
    'Check SELinux first — most "service not working" = SELinux denial',
    'Verify service is actually running before debugging client issues',
    'Use the simplest test first (CLI client) before complex Java clients',
    'Never assume — verify labels, names, paths empirically on device',
    'Don\'t guess at SELinux types — use ls -laZ to verify on real hardware'
  ]);

  S('Is My Service Running? — Essential Checks', [
    '# Is the process alive?',
    'adb shell ps -e | grep calculatord',
    '',
    '# Is it registered with ServiceManager?',
    'adb shell service list | grep calculator',
    '',
    '# What SELinux domain is it running in?',
    'adb shell ps -eZ | grep calculatord',
    '',
    '# View service startup logs',
    'adb logcat -s calculatord',
    'adb logcat -s CalculatorService',
    '',
    '# Check for init errors',
    'adb logcat | grep "init:"'
  ]);

  S('Service Not Found — Top Causes', [
    'Cause 1: Service name mismatch (most common!)',
    '  main.cpp:         AServiceManager_addService(b, "com.myoem.bmi.IBMIService")',
    '  service_contexts: com.myoem.bmi.IBMIService  u:object_r:bmid_service:s0',
    '  client code:      AServiceManager_checkService("com.myoem.bmi.IBMIService")',
    '  Must match EXACTLY — case sensitive, no trailing spaces',
    '',
    'Cause 2: SELinux blocked service registration',
    '  Check: adb logcat -d | grep "avc: denied"',
    '',
    'Cause 3: Service crashed at startup',
    '  Check: adb logcat -s bmid  (look for SIGABRT or SIGSEGV)'
  ]);

  S('Android Logging — Macros and Usage', [
    'ALWAYS put #define LOG_TAG before any #include in .cpp files!',
    '',
    '#define LOG_TAG "MyService"   // ← First line',
    '#include <log/log.h>          // ← Then includes',
    '',
    'Log levels:',
    '  ALOGV(fmt, ...)  — Verbose  (lowest — often filtered)',
    '  ALOGD(fmt, ...)  — Debug',
    '  ALOGI(fmt, ...)  — Info',
    '  ALOGW(fmt, ...)  — Warning',
    '  ALOGE(fmt, ...)  — Error    (highest — always visible)',
    '',
    'Filter by tag: adb logcat -s CalculatorService:D'
  ]);

  S('Dev Iteration Workflow on RPi5', [
    '# 1. Build the changed binary',
    'm calculatord',
    '',
    '# 2. Unlock vendor partition (RPi5: no adb remount!)',
    'adb root',
    'adb shell mount -o remount,rw /vendor',
    '',
    '# 3. Push the binary',
    'adb push out/target/product/rpi5/vendor/bin/calculatord /vendor/bin/',
    'adb shell chmod 755 /vendor/bin/calculatord',
    '',
    '# 4. Restart the service',
    'adb shell stop calculatord',
    'adb shell start calculatord',
    '',
    '# 5. Watch the logs',
    'adb logcat -s calculatord CalculatorService'
  ]);

  S('Debugging RC File and Boot Issues', [
    'Service won\'t start at boot?',
    '',
    '# Check init processed the RC file',
    'adb logcat | grep "init:" | grep calculator',
    'adb logcat -b events | grep service_start',
    '',
    'Common RC file mistakes:',
    '  Wrong binary path (/system/bin instead of /vendor/bin)',
    '  Typo in service name (used by start/stop commands)',
    '  "oneshot" keyword prevents auto-restart on crash',
    '  Missing user/group causing permission denied on exec',
    '',
    'Manually start for testing:',
    '  adb shell start calculatord'
  ]);

  S('dumpsys — Service Introspection Tool', [
    '# List ALL registered services',
    'adb shell dumpsys -l',
    '',
    '# Dump a specific service (if it implements dump())',
    'adb shell dumpsys com.myoem.calculator.ICalculatorService',
    '',
    '# Useful system service dumps:',
    'adb shell dumpsys activity   # app/activity manager state',
    'adb shell dumpsys window     # window manager state',
    'adb shell dumpsys battery    # battery state',
    'adb shell dumpsys audio      # audio service state',
    '',
    'Native services can implement BnBinder::dump() to expose debug info'
  ]);

  S('Common Issues Reference Table', [
    'Issue: "Service not found"',
    '  Fix:  Check service name matches in main.cpp, service_contexts, client',
    '',
    'Issue: Service crashes immediately at launch',
    '  Fix:  adb logcat -s <LOG_TAG>  — look for SIGABRT/SIGSEGV/exception',
    '',
    'Issue: Permission denied on sysfs write',
    '  Fix:  adb logcat | grep "avc: denied"  — add SELinux allow rule',
    '',
    'Issue: Cannot push to /vendor',
    '  Fix:  adb root; adb shell mount -o remount,rw /vendor',
    '',
    'Issue: Build error "module not found"',
    '  Fix:  Add module dir to PRODUCT_SOONG_NAMESPACES in myoem_base.mk',
    '',
    'Issue: AIDL client can\'t find service method',
    '  Fix:  Rebuild aidl_interface, verify -ndk library name in Android.bp'
  ]);
}

// ============================================================
// PART 9 — MANAGER PATTERN
// ============================================================

function slidesPart9_ManagerPattern() {
  Sec('Part 9: The Manager Pattern', 'Exposing Native Services to Java and Kotlin Apps');

  S('Why the Manager Pattern?', [
    'Problem 1: Java/Kotlin apps cannot use NDK Binder APIs directly',
    'Problem 2: Raw AIDL usage in application code is verbose and fragile',
    'Problem 3: Apps should not deal with RemoteException, null checks, reconnects',
    '',
    'Solution: Java Manager library wraps AIDL in a clean, documented API',
    '',
    'Pattern from Android itself:',
    '  AudioManager, WindowManager, LocationManager, InputManager...',
    '  All are Java wrappers hiding AIDL details from app developers',
    '  We follow the same pattern for ThermalControlManager'
  ]);

  S('Manager Architecture — Full Stack', [
    'App (Kotlin/Java Activity)',
    '    |  ThermalControlManager.getCpuTemperatureCelsius()',
    '    ↓',
    'ThermalControlManager.java  (java_library, vendor: true)',
    '    |  ServiceManager.checkService(SERVICE_NAME)',
    '    |  IThermalControlService.Stub.asInterface(binder)',
    '    ↓',
    'thermalcontrold  (C++ native service, /vendor/bin/)',
    '    |  HAL reads/writes sysfs files',
    '    ↓',
    '/sys/class/thermal/...  and  /sys/class/hwmon/...  (Linux kernel)'
  ]);

  S('ThermalControlManager.java — Constructor', [
    'public class ThermalControlManager {',
    '    private static final String SERVICE_NAME =',
    '        "com.myoem.thermalcontrol.IThermalControlService/default";',
    '    private IThermalControlService mService;',
    '    private static final String TAG = "ThermalControlManager";',
    '',
    '    public ThermalControlManager(Context context) {',
    '        IBinder binder = ServiceManager.checkService(SERVICE_NAME);',
    '        if (binder != null) {',
    '            mService = IThermalControlService.Stub.asInterface(binder);',
    '        } else {',
    '            Log.w(TAG, "ThermalControl service not available");',
    '        }',
    '    }',
    '    public boolean isAvailable() { return mService != null; }',
    '}'
  ]);

  S('ThermalControlManager.java — Method Pattern', [
    'public float getCpuTemperatureCelsius() {',
    '    if (mService == null) return 0.0f;  // safe default',
    '    try {',
    '        return mService.getCpuTemperatureCelsius();',
    '    } catch (RemoteException e) {',
    '        Log.e(TAG, "getCpuTemperature failed: " + e.getMessage());',
    '        return 0.0f;',
    '    }',
    '}',
    '',
    'public boolean setFanAutoMode(boolean enable) {',
    '    if (mService == null) return false;',
    '    try {',
    '        mService.setFanAutoMode(enable);  return true;',
    '    } catch (RemoteException e) { return false; }',
    '}'
  ]);

  S('Android.bp for Java Manager Library', [
    'java_library {',
    '    name:        "thermalcontrol-manager",',
    '    vendor:      true,',
    '    srcs:        ["java/**/*.java"],',
    '    libs: [',
    '        "thermalcontrolservice-aidl-V1-java",  // AIDL-generated',
    '        "framework",                           // for Context, IBinder, Log',
    '    ],',
    '    sdk_version: "system_current",  // needed for ServiceManager access',
    '    visibility:  ["//vendor/myoem:__subpackages__"],',
    '}',
    '',
    'sdk_version: system_current → access to @SystemApi (hidden APIs)'
  ]);

  S('Using ThermalControlManager in an App', [
    '// In Activity.onCreate() or ViewModel:',
    'ThermalControlManager tcm = new ThermalControlManager(this);',
    '',
    'if (tcm.isAvailable()) {',
    '    float temp   = tcm.getCpuTemperatureCelsius();  // e.g. 42.3',
    '    int   rpm    = tcm.getFanSpeedRpm();            // e.g. 1800',
    '    boolean auto = tcm.isFanAutoMode();             // true',
    '',
    '    textView.setText(String.format("%.1f°C  |  %d RPM", temp, rpm));',
    '} else {',
    '    textView.setText("Thermal service unavailable");',
    '}',
    '',
    '// Set fan to manual 50% speed:',
    'tcm.setFanAutoMode(false);',
    'tcm.setFanSpeed(50);'
  ]);

  S('Error Handling and Death Notifications', [
    'Three failure scenarios to handle:',
    '',
    '1. Service not started yet → isAvailable() returns false',
    '   Handle: show "unavailable" state, retry later',
    '',
    '2. RemoteException → service crashed mid-call',
    '   Handle: catch, log, return safe default',
    '',
    '3. Service dies after connection → linkToDeath for notification:',
    '   binder.linkToDeath(() -> {',
    '       mService = null;  // null out dead reference',
    '       notifyUnavailable();',
    '   }, 0);',
    '',
    '4. ServiceSpecificException → service returned error code',
    '   Handle per method based on known error codes from AIDL'
  ]);

  S('Key Learnings — Manager Pattern', [
    'java_library vs android_library:',
    '  use android_library if you have res/ or AndroidManifest.xml',
    '  use java_library for pure-Java, no resources',
    '',
    'sdk_version: "system_current" is required for ServiceManager access',
    '  (it is a @SystemApi — not available to regular app SDK)',
    '',
    'ServiceManager.checkService() vs getService():',
    '  checkService: returns null immediately if not found (non-blocking)',
    '  getService:   blocks up to 5 seconds waiting for service to appear',
    '',
    'Always expose isAvailable() — apps must handle the "no service" case',
    'Return meaningful defaults (0.0f, false, -1) not exceptions to callers'
  ]);
}

// ============================================================
// CONCLUSION
// ============================================================

function slidesConclusion() {
  Sec('Summary and Key Takeaways', '10 Parts — One Complete Vendor Stack');

  S('What We Built — The Full Stack', [
    'HAL Layer:       libthermalcontrolhal — sysfs read/write for fan and CPU temp',
    'Native Services: calculatord, bmid, thermalcontrold, safemoded, potvolumed',
    'AIDL Interfaces: ICalculatorService, IBMIService, IThermalControlService,',
    '                 ISafeModeService (with VehicleData parcelable + callbacks)',
    'Java Manager:    ThermalControlManager — clean Java API wrapping AIDL',
    'VTS Tests:       30+ test cases per service — automated CI validation',
    'SELinux:         Per-service .te + file_contexts + service_contexts',
    'Init RC Files:   Automatic startup at boot, user=system, auto-restart'
  ]);

  S('The Golden Rules of AOSP Vendor Development', [
    'Rule 1: ALWAYS use libbinder_ndk in vendor — NEVER libbinder',
    'Rule 2: #define LOG_TAG MUST be the very first line in every .cpp',
    'Rule 3: vendor: true in Android.bp — installs to /vendor/ not /system/',
    'Rule 4: Add every service dir to PRODUCT_SOONG_NAMESPACES',
    'Rule 5: Service names must match exactly in main.cpp, service_contexts, Manager',
    'Rule 6: NEVER assume sysfs type — verify with ls -laZ on real device',
    'Rule 7: ZERO changes to device/ or frameworks/ — everything in vendor/myoem/'
  ]);

  S('AIDL Design Best Practices', [
    'Define error codes as const int inside the interface — accessible everywhere',
    'Validate ALL inputs at service boundary — never trust callers',
    'unstable: true for intra-vendor services (simpler, no versioning overhead)',
    'stability: vintf for cross-partition interfaces (required for AOSP HALs)',
    'cpp backend: always disabled in vendor — ABI instability risk',
    'ndk backend: always enabled — this is the safe, stable path',
    'Use oneway for callbacks — prevents ANR if client is slow'
  ]);

  S('SELinux Best Practices', [
    'Start with setenforce 0 during development — fix policies before shipping',
    'Verify all file labels with ls -laZ BEFORE writing .te rules',
    'Read avc: denied messages carefully — they tell you EXACTLY what to add',
    'Use init_daemon_domain() macro — handles most init→daemon transitions',
    'sysfs_hwmon does NOT exist in AOSP 15 — use generic sysfs type on RPi5',
    'Group each service\'s sepolicy in its own directory under the service folder',
    'Never ship with "permissive myservice" — fix the policy properly'
  ]);

  S('Debugging Quick Reference', [
    'Service running?     adb shell ps -e | grep <daemon>',
    'Service listed?      adb shell service list | grep <name>',
    'SELinux denied?      adb logcat -d | grep "avc: denied"',
    'Service logs?        adb logcat -s <LOG_TAG>',
    'Process domain?      adb shell ps -eZ | grep <daemon>',
    'File label?          adb shell ls -laZ <path>',
    'Manual start/stop?   adb shell start <svc> / adb shell stop <svc>',
    'Remount vendor?      adb shell mount -o remount,rw /vendor'
  ]);

  S('ThermalControl Service — Deep Dive', [
    'HAL pattern: libthermalcontrolhal (cc_library_shared) wraps all sysfs I/O',
    '  IThermalControlHal pure virtual interface + ThermalControlHal implementation',
    '  SysfsHelper: sysfsReadInt/Float, sysfsWriteInt, discoverHwmonPath()',
    '  hwmon index discovered dynamically — scans hwmon0..15 for pwm1 file',
    '',
    'Service owns unique_ptr<IThermalControlHal> mHal',
    'All AIDL methods delegate to mHal with logging and validation',
    'VINTF stable interface: stability: "vintf" (vs unstable for Calculator/BMI)',
    'RC file: on boot chown/chmod hwmon sysfs files (kernel creates as root:root)'
  ]);

  S('SafeMode Service — VHAL Integration', [
    'Connects to VHAL: IVehicle::fromBinder(AServiceManager_waitForService(...))',
    'Subscribes to VHAL properties: PERF_VEHICLE_SPEED, CURRENT_GEAR, FUEL_LEVEL',
    'VehicleData parcelable passed to registered callbacks on every VHAL event',
    '',
    'Simulator mode: reads /data/local/tmp/safemode_sim.txt (no real car needed)',
    'Compiled with: -DSAFEMODE_SIM_MODE flag in Android.bp',
    '',
    'Gear values: GEAR_UNKNOWN=0x0000, GEAR_NEUTRAL=0x0001, GEAR_REVERSE=0x0002,',
    '             GEAR_PARK=0x0004,    GEAR_DRIVE=0x0008,   GEAR_1..9=0x0010..'
  ]);

  S('Resources for Further Learning', [
    'Medium Series:    @aruncse2k20 — 10-part AOSP Vendor Development series',
    'GitHub:           github.com/ProArun — vendor/myoem source code',
    'AOSP Docs:        source.android.com — official documentation',
    'AIDL Guide:       source.android.com/docs/core/architecture/aidl',
    'SELinux Android:  source.android.com/docs/security/selinux',
    'VINTF Guide:      source.android.com/docs/core/architecture/vintf',
    'Binder Overview:  source.android.com/docs/core/architecture/hidl/binder-ipc'
  ]);

  Sec('Thank You!',
    '10 articles.  5 services.  1 complete vendor layer.\n' +
    'From zero to production-quality AOSP services on real hardware.\n\n' +
    'Questions?'
  );
}
