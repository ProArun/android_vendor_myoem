' ============================================================
' AOSP Vendor Layer Development on Raspberry Pi 5
' PowerPoint Presentation Generator
' Based on 10-Part Medium Article Series by ProArun (aruncse2k20)
'
' HOW TO USE:
'   1. Open Microsoft PowerPoint
'   2. Press Alt+F11 to open VBA Editor
'   3. Click File > Import File, select this .bas file
'   4. Press F5 or click Run > Run Sub/UserForm
'   5. Select "CreateAOSPPresentation" and click Run
'   6. Save the generated .pptx file
' ============================================================

Option Explicit

Dim gPrs As Presentation

' ============================================================
' HELPERS
' ============================================================

Sub Sec(title As String, subtitle As String)
    Dim sld As Slide
    Set sld = gPrs.Slides.Add(gPrs.Slides.Count + 1, ppLayoutTitle)
    sld.Shapes.Title.TextFrame.TextRange.Text = title
    sld.Shapes(2).TextFrame.TextRange.Text = subtitle
    With sld.Shapes.Title.TextFrame.TextRange.Font
        .Size = 36
        .Bold = True
    End With
End Sub

Sub S(title As String, ParamArray b() As Variant)
    Dim sld As Slide
    Dim tf As TextFrame
    Dim i As Integer
    Dim txt As String
    Set sld = gPrs.Slides.Add(gPrs.Slides.Count + 1, ppLayoutText)
    sld.Shapes.Title.TextFrame.TextRange.Text = title
    Set tf = sld.Shapes(2).TextFrame
    txt = ""
    For i = 0 To UBound(b)
        If i = 0 Then
            txt = CStr(b(i))
        Else
            txt = txt & Chr(13) & CStr(b(i))
        End If
    Next i
    tf.TextRange.Text = txt
    tf.TextRange.Font.Size = 14
End Sub

' ============================================================
' MAIN ENTRY POINT
' ============================================================

Sub CreateAOSPPresentation()
    Set gPrs = Presentations.Add
    gPrs.PageSetup.SlideWidth = 960
    gPrs.PageSetup.SlideHeight = 540

    Call Slides_Cover
    Call Slides_Overview
    Call Slides_Part1_BigPicture
    Call Slides_Part2_VendorSetup
    Call Slides_Part3_BinderIPC
    Call Slides_Part4_AIDL
    Call Slides_Part5_Calculator
    Call Slides_Part6_BMI
    Call Slides_Part7_SELinux
    Call Slides_Part8_Debugging
    Call Slides_Part9_ManagerPattern
    Call Slides_Conclusion

    MsgBox "Done! " & gPrs.Slides.Count & " slides created. Save as .pptx now.", vbInformation
End Sub

' ============================================================
' COVER
' ============================================================

Sub Slides_Cover()
    Sec "AOSP Vendor Layer Development" & Chr(13) & "on Raspberry Pi 5", _
        "A 10-Part Engineering Deep Dive  |  by ProArun (aruncse2k20)" & Chr(13) & _
        "Android 15  |  AIDL  |  Binder IPC  |  SELinux  |  Soong"
End Sub

' ============================================================
' SERIES OVERVIEW ARTICLE
' ============================================================

Sub Slides_Overview()
    Sec "Series Overview", "What This Series Is About"

    S "About This Series", _
        "10 hands-on articles covering AOSP OEM vendor layer development", _
        "Real hardware: Raspberry Pi 5 running Android 15 (android-15.0.0_r14)", _
        "Product target: myoem_rpi5-trunk_staging-userdebug", _
        "Goal: Build production-quality native services entirely in vendor/", _
        "No changes to device/ or frameworks/ - everything is OEM-owned", _
        "Target audience: C++ developers learning AOSP-specific patterns"

    S "Technologies Covered in This Series", _
        "Binder IPC  - Android's inter-process communication kernel driver", _
        "AIDL        - Android Interface Definition Language for service contracts", _
        "Soong       - Android.bp build system: modules, namespaces, backends", _
        "SELinux     - Mandatory Access Control for process and file isolation", _
        "VTS Testing - Vendor Test Suite: automated validation of vendor services", _
        "NDK Binder  - libbinder_ndk: stable ABI for vendor C++ services", _
        "Java Manager Pattern - Exposing native services to Java/Kotlin apps"

    S "Projects Built in This Series", _
        "1. Calculator Service  - 4 arithmetic ops over Binder (add/sub/mul/div)", _
        "2. BMI Service         - Float BMI calculation with health category output", _
        "3. ThermalControl HAL  - sysfs-based CPU temp + fan control on RPi5", _
        "4. ThermalControl Svc  - AIDL service wrapping the HAL (VINTF stable)", _
        "5. SafeMode Service    - VHAL integration for vehicle gear/speed events", _
        "6. PotVolume Daemon    - SPI ADC potentiometer -> audio volume control", _
        "Full stack: AIDL -> C++ Service -> Java Manager -> Kotlin App"

    S "Development Environment", _
        "Device      : Raspberry Pi 5 (BCM2712, 8 GB RAM)", _
        "AOSP branch : android-15.0.0_r14", _
        "Build host  : Ubuntu 22.04 (x86_64)", _
        "Build cmd   : lunch myoem_rpi5-trunk_staging-userdebug && m <target>", _
        "Deployment  : adb push + adb shell mount -o remount,rw /vendor", _
        "Constraint  : No fastboot bootloader, no adb remount, no adb disable-verity", _
        "Full image  : write to SD card via dd"
End Sub

' ============================================================
' PART 1 - THE BIG PICTURE
' ============================================================

Sub Slides_Part1_BigPicture()
    Sec "Part 1: The Big Picture", "How Android is Actually Organized"

    S "Android Architecture - 5 Layers", _
        "Layer 5 (Top)  Applications       - User apps, System apps, OEM apps", _
        "Layer 4        Android Framework  - ActivityManager, WindowManager, etc.", _
        "Layer 3        Native C++ Libs    - libc, libm, OpenGL, Binder services", _
        "Layer 2        HAL               - Vendor hardware abstraction (AIDL/HIDL)", _
        "Layer 1 (Bot)  Linux Kernel       - Device drivers, memory, scheduling", _
        "", _
        "OEM work lives in Layer 2 (HAL) and between Layer 2-3 (vendor services)"

    S "The Partition Model", _
        "/system  - Google-owned: Android framework, core libraries (read-only)", _
        "/vendor  - OEM-owned: HALs, vendor daemons, device-specific binaries", _
        "/product - Additional OEM apps and runtime configurations", _
        "/odm     - ODM overlays and device-specific configurations", _
        "", _
        "Key rule: /vendor code must NOT depend on /system private internals", _
        "VINTF (Vendor Interface) defines the stable ABI boundary between them"

    S "Android Treble - Why the Vendor Partition Exists", _
        "Pre-Treble (before Android 8): OEM had to rebase all of Android per patch", _
        "Google update -> OEM had to re-integrate months of work -> slow updates", _
        "", _
        "Android Treble (Android 8+): Separated /system from /vendor", _
        "Google updates /system independently; OEM maintains /vendor", _
        "VINTF defines the contract both sides agree on at build time", _
        "", _
        "Result: Faster security patches, longer device support lifetime", _
        "Our project follows Treble: vendor/myoem/ never touches /system internals"

    S "AOSP vs OEM Customization Model", _
        "AOSP = Android Open Source Project - base Android from Google", _
        "OEM layer lives entirely in vendor/myoem/ - additions without touching AOSP", _
        "", _
        "Our rule: ZERO changes to device/brcm/rpi5/ or frameworks/", _
        "All services, HALs, and apps are declared in vendor/myoem/", _
        "Product makefiles (myoem_rpi5.mk) tie vendor additions into the image", _
        "OEM services register via ServiceManager - accessible system-wide"

    S "Service Discovery in Android", _
        "ServiceManager = central registry for all Binder services (handle 0)", _
        "Server registers at startup:  AServiceManager_addService(binder, name)", _
        "Client discovers:             AServiceManager_checkService(name)", _
        "", _
        "Service names are strings:", _
        "  com.myoem.calculator.ICalculatorService", _
        "  com.myoem.bmi.IBMIService", _
        "  com.myoem.thermalcontrol.IThermalControlService/default", _
        "", _
        "Java: ServiceManager.checkService(name)  (hidden API - platform only)"

    S "Soong Build System Basics", _
        "Soong replaced Make as Android primary build system (Android 7+)", _
        "Build files: Android.bp (Blueprint - JSON-like declarative format)", _
        "Android.mk still supported but deprecated for new code", _
        "", _
        "Common module types:", _
        "  cc_binary          - Native executable (/vendor/bin/)", _
        "  cc_library_shared  - .so shared library", _
        "  aidl_interface     - Generates Binder stubs from .aidl files", _
        "  java_library       - Java .jar for platform use", _
        "  android_app        - APK application"

    S "RPi5-Specific Constraints (Important!)", _
        "No fastboot bootloader  - adb reboot bootloader just reboots to Android", _
        "No adb remount          - Use: adb shell mount -o remount,rw /vendor", _
        "No adb disable-verity   - OEM unlock not available on this board", _
        "Full system update      - Write image to SD card via dd command", _
        "", _
        "Dev iteration workflow:", _
        "  m <target> -> adb root -> adb shell mount -o remount,rw /vendor", _
        "  -> adb push out/.../bin/foo /vendor/bin/ -> adb shell stop/start foo", _
        "", _
        "hwmon sysfs: sysfs_hwmon does NOT exist in AOSP 15 - use generic sysfs"
End Sub

' ============================================================
' PART 2 - VENDOR DIRECTORY SETUP
' ============================================================

Sub Slides_Part2_VendorSetup()
    Sec "Part 2: Setting Up Your Own Vendor Directory", _
        "The Foundation of OEM Customization"

    S "vendor/myoem/ - Complete Directory Layout", _
        "vendor/myoem/", _
        "  AndroidProducts.mk          <- Entry point: lists all product .mk files", _
        "  myoem_base.mk               <- PRODUCT_PACKAGES, NAMESPACES, SEPOLICY", _
        "  products/myoem_rpi5.mk      <- Device-specific product configuration", _
        "  hal/thermalcontrol/         <- HAL shared library (libthermalcontrolhal)", _
        "  services/calculator/        <- Calculator native Binder service", _
        "  services/bmi/               <- BMI native Binder service", _
        "  services/thermalcontrol/    <- ThermalControl VINTF service", _
        "  services/safemode/          <- SafeMode VHAL-integrated service", _
        "  services/potvolumed/        <- SPI ADC -> audio volume daemon", _
        "  libs/thermalcontrol/        <- Java ThermalControlManager library"

    S "AndroidProducts.mk - The Build Entry Point", _
        "Tells the build system what products exist in vendor/myoem/", _
        "", _
        "PRODUCT_MAKEFILES := \\", _
        "    $(LOCAL_DIR)/products/myoem_rpi5.mk", _
        "", _
        "COMMON_LUNCH_CHOICES := \\", _
        "    myoem_rpi5-trunk_staging-userdebug", _
        "", _
        "Build system scans ALL vendor/ subdirs for AndroidProducts.mk at parse time", _
        "COMMON_LUNCH_CHOICES makes the product available in the lunch menu"

    S "myoem_base.mk - Adding Packages and Policies", _
        "# Register Soong module directories (so build can find Android.bp files)", _
        "PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/calculator", _
        "PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/bmi", _
        "PRODUCT_SOONG_NAMESPACES += vendor/myoem/services/thermalcontrol", _
        "", _
        "# Binaries to include in the system image", _
        "PRODUCT_PACKAGES += calculatord calculator_client", _
        "PRODUCT_PACKAGES += bmid bmi_client", _
        "PRODUCT_PACKAGES += thermalcontrold thermalcontrol_client", _
        "", _
        "# SELinux policy source directories", _
        "PRODUCT_PRIVATE_SEPOLICY_DIRS += vendor/myoem/services/calculator/sepolicy/private"

    S "myoem_rpi5.mk - Product Configuration", _
        "# Inherit base RPi5 AOSP product", _
        "$(call inherit-product, device/brcm/rpi5/aosp_rpi5.mk)", _
        "", _
        "# Layer in OEM additions", _
        "$(call inherit-product, vendor/myoem/myoem_base.mk)", _
        "", _
        "# Product metadata", _
        "PRODUCT_DEVICE   := rpi5", _
        "PRODUCT_NAME     := myoem_rpi5", _
        "PRODUCT_BRAND    := MyOEM", _
        "PRODUCT_MANUFACTURER := MyOEM", _
        "", _
        "Variant: userdebug = release build + root access (for development)"

    S "Android.bp - cc_binary Module", _
        "cc_binary {", _
        "    name:        ""calculatord"",", _
        "    vendor:      true,          // installs to /vendor/bin/", _
        "    srcs:        [""src/CalculatorService.cpp"", ""src/main.cpp""],", _
        "    shared_libs: [""libbinder_ndk"", ""liblog""],", _
        "    static_libs: [""calculatorservice-aidl-ndk""],", _
        "    init_rc:     [""calculatord.rc""],  // bundled, copied to /vendor/etc/init/", _
        "}", _
        "", _
        "vendor: true  =>  installs to /vendor/bin/, links vendor-available libs only"

    S "Android.bp - aidl_interface Module", _
        "aidl_interface {", _
        "    name:             ""calculatorservice-aidl"",", _
        "    srcs:             [""aidl/**/*.aidl""],", _
        "    vendor_available: true,", _
        "    unstable:         true,     // no frozen versions needed", _
        "    backend: {", _
        "        ndk:  { enabled: true },   // calculatorservice-aidl-ndk  (C++)", _
        "        cpp:  { enabled: false },  // NEVER in vendor!", _
        "        java: { enabled: true },   // calculatorservice-aidl-java", _
        "        rust: { enabled: false },", _
        "    },", _
        "}"

    S "Build, Deploy and Verify Commands", _
        "# Setup environment (once per shell session)", _
        "source build/envsetup.sh", _
        "lunch myoem_rpi5-trunk_staging-userdebug", _
        "", _
        "# Build a specific target", _
        "m calculatord", _
        "", _
        "# Output location", _
        "out/target/product/rpi5/vendor/bin/calculatord", _
        "", _
        "# Deploy to device", _
        "adb root", _
        "adb shell mount -o remount,rw /vendor", _
        "adb push out/target/product/rpi5/vendor/bin/calculatord /vendor/bin/", _
        "adb shell stop calculatord && adb shell start calculatord"
End Sub

' ============================================================
' PART 3 - BINDER IPC
' ============================================================

Sub Slides_Part3_BinderIPC()
    Sec "Part 3: Binder IPC", "The Backbone of Android Services"

    S "What is IPC and Why Android Needs It", _
        "IPC = Inter-Process Communication", _
        "Android runs each app/service in its own Linux process (sandboxed)", _
        "Processes cannot share memory - OS enforces strict isolation", _
        "IPC is required for a Calculator app to call CalculatorService", _
        "", _
        "Linux IPC options: pipes, Unix sockets, shared memory, signals", _
        "Android chose Binder - a custom kernel driver (/dev/binder)", _
        "Binder is used for every service call in Android - thousands per second"

    S "Why Binder Over Other IPC Mechanisms?", _
        "Performance   - Single data copy (vs 2 copies for pipes/sockets)", _
        "Security      - Kernel enforces caller UID/PID - no spoofing possible", _
        "Object model  - Pass interface references (IBinder) across processes", _
        "Thread mgmt   - Built-in thread pool management per process", _
        "Death notify  - linkToDeath: know immediately when server process dies", _
        "Synchronous   - Caller blocks until server replies (like a function call)", _
        "Used by all   - ActivityManager, WindowManager, AudioService, etc."

    S "Binder Architecture Components", _
        "Kernel driver:   /dev/binder  - character device, handles transactions", _
        "Thread pool:     Each process has worker threads waiting for incoming calls", _
        "IBinder:         Base interface - any Binder object implements this", _
        "BnFoo (Native):  Server-side stub - YOU implement this class", _
        "BpFoo (Proxy):   Client-side proxy - auto-generated from AIDL", _
        "ServiceManager:  Special Binder (handle 0) - the global name registry", _
        "", _
        "Bn = Binder Native (server),  Bp = Binder Proxy (client)"

    S "Binder Transaction Lifecycle - Step by Step", _
        "1. Client calls method on BpFoo proxy object (looks like a normal C++ call)", _
        "2. Proxy marshals arguments into Parcel (serialized byte buffer)", _
        "3. Parcel sent to kernel via ioctl(BC_TRANSACTION) on /dev/binder", _
        "4. Kernel maps data into server process memory (zero extra copy)", _
        "5. Server Binder thread wakes up, receives BR_TRANSACTION event", _
        "6. onTransact() called - unmarshals args, calls your implementation", _
        "7. Return value marshaled into reply Parcel, sent back same path", _
        "8. Client receives reply - the original call returns with the result"

    S "libbinder vs libbinder_ndk - Critical Difference", _
        "libbinder (C++ Binder):", _
        "  - Part of /system partition - ABI can change between Android versions", _
        "  - Uses sp<>, wp<> reference-counted smart pointers", _
        "  - NOT safe to use in vendor - violates Treble stability rules", _
        "  - NEVER use in vendor services!", _
        "", _
        "libbinder_ndk (NDK/LLNDK Binder):", _
        "  - Stable ABI defined in NDK - safe across Android version boundaries", _
        "  - Uses ABinderProcess_*, AServiceManager_* C functions", _
        "  - ALWAYS use this in vendor services", _
        "  - Declared as: shared_libs: [""libbinder_ndk""] in Android.bp"

    S "NDK Binder - Server Bootstrap (main.cpp)", _
        "#define LOG_TAG ""calculatord""  // MUST be first line!", _
        "#include <android/binder_manager.h>", _
        "#include <android/binder_process.h>", _
        "#include ""CalculatorService.h""", _
        "", _
        "int main() {", _
        "    ABinderProcess_setThreadPoolMaxThreadCount(4);", _
        "    ABinderProcess_startThreadPool();", _
        "    auto svc = ndk::SharedRefBase::make<CalculatorService>();", _
        "    AServiceManager_addService(svc->asBinder().get(),", _
        "                              ""com.myoem.calculator.ICalculatorService"");", _
        "    ABinderProcess_joinThreadPool();  // blocks - never returns", _
        "    return 0;", _
        "}"

    S "NDK Binder - Client Discovery Pattern", _
        "#include <android/binder_manager.h>", _
        "#include <aidl/com/myoem/calculator/ICalculatorService.h>", _
        "", _
        "// Step 1: Get raw binder handle from ServiceManager", _
        "ndk::SpAIBinder binder(AServiceManager_checkService(", _
        "    ""com.myoem.calculator.ICalculatorService""));", _
        "if (!binder.get()) { /* service not running yet */ }", _
        "", _
        "// Step 2: Wrap in typed AIDL proxy", _
        "auto client = ICalculatorService::fromBinder(binder);", _
        "", _
        "// Step 3: Make calls", _
        "int32_t result;", _
        "auto status = client->add(10, 20, &result);", _
        "if (status.isOk()) { printf(""Result: %d"", result); }"

    S "Binder Threading Model", _
        "ABinderProcess_setThreadPoolMaxThreadCount(4)  - set pool size", _
        "ABinderProcess_startThreadPool()               - launch worker threads", _
        "ABinderProcess_joinThreadPool()                - main thread joins pool", _
        "", _
        "Incoming Binder calls are dispatched to idle worker threads", _
        "Thread count of 4 is sufficient for most vendor services", _
        "", _
        "oneway keyword in AIDL - fire-and-forget (no reply, non-blocking)", _
        "Regular methods - synchronous, caller blocks until server returns", _
        "Re-entrancy - a service can be both client and server simultaneously"
End Sub

' ============================================================
' PART 4 - AIDL
' ============================================================

Sub Slides_Part4_AIDL()
    Sec "Part 4: AIDL", "Defining Your Service Interface"

    S "What is AIDL?", _
        "AIDL = Android Interface Definition Language", _
        "Describes the API contract between a service and its clients", _
        "Analogous to: IDL, Protobuf .proto, Thrift .thrift, gRPC .proto", _
        "", _
        "You write:         ICalculatorService.aidl  (the contract)", _
        "Build generates:   BnCalculatorService      (server stub)", _
        "                   BpCalculatorService      (client proxy)", _
        "You implement:     extend BnCalculatorService and fill in methods", _
        "", _
        "AIDL eliminates all Binder marshaling/unmarshaling boilerplate"

    S "AIDL File Syntax - ICalculatorService.aidl", _
        "package com.myoem.calculator;", _
        "", _
        "interface ICalculatorService {", _
        "    // Error code constants (visible in C++ and Java)", _
        "    const int ERROR_DIVIDE_BY_ZERO = 1;", _
        "", _
        "    // Methods - all int32 for calculator ops", _
        "    int add(int a, int b);", _
        "    int subtract(int a, int b);", _
        "    int multiply(int a, int b);", _
        "    int divide(int a, int b);  // throws on b==0", _
        "}", _
        "", _
        "File path: aidl/com/myoem/calculator/ICalculatorService.aidl"

    S "AIDL Supported Types", _
        "Primitives:   boolean, byte, char, int, long, float, double", _
        "String:       String  (Java) / std::string (C++ NDK)", _
        "Arrays:       int[], String[]  - requires 'in' directional tag", _
        "Parcelables:  Custom structs declared in separate .aidl files", _
        "Interfaces:   Pass AIDL interfaces as params (callback pattern)", _
        "IBinder:      Raw binder handle (rarely used directly)", _
        "", _
        "Directional tags for non-primitive params:", _
        "  in    - client sends data to server", _
        "  out   - server fills data for client", _
        "  inout - both directions"

    S "Error Handling in AIDL - C++ NDK", _
        "Every AIDL method returns ndk::ScopedAStatus (never throw in C++)", _
        "", _
        "Success:                  return ndk::ScopedAStatus::ok();", _
        "", _
        "Service error:            return ndk::ScopedAStatus", _
        "                              ::fromServiceSpecificError(ERROR_CODE);", _
        "", _
        "Client side check:", _
        "  auto status = client->divide(10, 0, &result);", _
        "  if (!status.isOk()) {", _
        "      int code = status.getServiceSpecificError();", _
        "      // code == ICalculatorService::ERROR_DIVIDE_BY_ZERO", _
        "  }"

    S "aidl_interface in Android.bp - Full Config", _
        "aidl_interface {", _
        "    name:             ""calculatorservice-aidl"",", _
        "    srcs:             [""aidl/com/myoem/calculator/ICalculatorService.aidl""],", _
        "    vendor_available: true,   // usable from vendor modules", _
        "    unstable:         true,   // no API versioning required", _
        "    backend: {", _
        "        ndk:  { enabled: true  },  // -> calculatorservice-aidl-ndk", _
        "        cpp:  { enabled: false },  // disabled - Treble violation in vendor", _
        "        java: { enabled: true  },  // -> calculatorservice-aidl-java", _
        "        rust: { enabled: false },", _
        "    },", _
        "}"

    S "AIDL Stability Levels - unstable vs vintf", _
        "unstable: true", _
        "  - No versioning, ABI can change freely between builds", _
        "  - Use for: within-vendor services, prototypes", _
        "  - Generated lib: calculatorservice-aidl-ndk", _
        "  - Used by: Calculator, BMI services", _
        "", _
        "stability: ""vintf""", _
        "  - Frozen ABI, versioned, cross-partition safe", _
        "  - Required for: HAL interfaces, services called from /system", _
        "  - Must appear in VINTF manifest and compatibility matrix", _
        "  - Generated lib: thermalcontrolservice-aidl-V1-ndk", _
        "  - Used by: ThermalControl, SafeMode services"

    S "AIDL Parcelable - VehicleData Example", _
        "// VehicleData.aidl - custom data type for SafeMode service", _
        "package com.myoem.safemode;", _
        "", _
        "@VintfStability", _
        "parcelable VehicleData {", _
        "    float   speedMs      = 0.0;  // PERF_VEHICLE_SPEED in m/s", _
        "    int     gear         = 4;    // CURRENT_GEAR (4=PARK)", _
        "    float   fuelLevelMl  = 0.0;  // FUEL_LEVEL in ml", _
        "}", _
        "", _
        "Used as: VehicleData getCurrentData();  in ISafeModeService.aidl", _
        "C++ NDK: VehicleData is a generated struct with = operator and Parcel support"

    S "AIDL Callback Pattern - ISafeModeService", _
        "// Register a callback interface for async notifications", _
        "interface ISafeModeService {", _
        "    VehicleData getCurrentData();          // synchronous snapshot", _
        "    void registerCallback(ISafeModeCallback cb);   // subscribe", _
        "    void unregisterCallback(ISafeModeCallback cb); // unsubscribe", _
        "    int  getVersion();", _
        "}", _
        "", _
        "interface ISafeModeCallback {", _
        "    oneway void onVehicleDataChanged(in VehicleData data);", _
        "}", _
        "", _
        "oneway = fire-and-forget (server does not wait for client to process)"
End Sub

' ============================================================
' PART 5 - CALCULATOR SERVICE
' ============================================================

Sub Slides_Part5_Calculator()
    Sec "Part 5: Building the Calculator Service", _
        "First Native Binder Service from Scratch"

    S "Calculator Service - What We Build", _
        "Binary:      calculatord  (runs as /vendor/bin/calculatord)", _
        "AIDL:        ICalculatorService  (4 operations)", _
        "Operations:  add, subtract, multiply, divide", _
        "Error:       divide-by-zero -> ServiceSpecificException(1)", _
        "Test client: calculator_client (CLI tool)", _
        "VTS tests:   VtsCalculatorServiceTest (30+ test cases)", _
        "", _
        "This is the reference implementation - BMI follows same pattern"

    S "ICalculatorService.aidl - The Contract", _
        "// File: aidl/com/myoem/calculator/ICalculatorService.aidl", _
        "package com.myoem.calculator;", _
        "", _
        "interface ICalculatorService {", _
        "    const int ERROR_DIVIDE_BY_ZERO = 1;", _
        "", _
        "    int add(int a, int b);", _
        "    int subtract(int a, int b);", _
        "    int multiply(int a, int b);", _
        "    int divide(int a, int b);", _
        "}"

    S "CalculatorService.h - Class Declaration", _
        "#pragma once", _
        "#include <aidl/com/myoem/calculator/BnCalculatorService.h>", _
        "", _
        "namespace aidl::com::myoem::calculator {", _
        "", _
        "class CalculatorService : public BnCalculatorService {", _
        "public:", _
        "    ndk::ScopedAStatus add     (int32_t a, int32_t b, int32_t* out) override;", _
        "    ndk::ScopedAStatus subtract(int32_t a, int32_t b, int32_t* out) override;", _
        "    ndk::ScopedAStatus multiply(int32_t a, int32_t b, int32_t* out) override;", _
        "    ndk::ScopedAStatus divide  (int32_t a, int32_t b, int32_t* out) override;", _
        "}; }", _
        "", _
        "Extends BnCalculatorService  <- generated by AIDL compiler from .aidl"

    S "CalculatorService.cpp - Implementation", _
        "#define LOG_TAG ""CalculatorService""  // MUST be first - before any #include", _
        "#include <log/log.h>", _
        "#include ""CalculatorService.h""", _
        "using namespace aidl::com::myoem::calculator;", _
        "", _
        "ScopedAStatus CalculatorService::add(int32_t a, int32_t b, int32_t* out) {", _
        "    *out = a + b;", _
        "    ALOGD(""add(%d, %d) = %d"", a, b, *out);", _
        "    return ScopedAStatus::ok();", _
        "}", _
        "ScopedAStatus CalculatorService::divide(int32_t a, int32_t b, int32_t* out) {", _
        "    if (b == 0) return ScopedAStatus::fromServiceSpecificError(ERROR_DIVIDE_BY_ZERO);", _
        "    *out = a / b;  return ScopedAStatus::ok();", _
        "}"

    S "main.cpp - Service Bootstrap", _
        "#define LOG_TAG ""calculatord""", _
        "#include <android/binder_manager.h>", _
        "#include <android/binder_process.h>", _
        "#include ""CalculatorService.h""", _
        "", _
        "static const char* SERVICE_NAME =", _
        "    ""com.myoem.calculator.ICalculatorService"";", _
        "", _
        "int main() {", _
        "    ABinderProcess_setThreadPoolMaxThreadCount(4);", _
        "    ABinderProcess_startThreadPool();", _
        "    auto svc = ndk::SharedRefBase::make<CalculatorService>();", _
        "    AServiceManager_addService(svc->asBinder().get(), SERVICE_NAME);", _
        "    ABinderProcess_joinThreadPool();", _
        "    return 0;", _
        "}"

    S "calculatord.rc - Init Service File", _
        "# File: services/calculator/calculatord.rc", _
        "# Bundled via: init_rc: [""calculatord.rc""] in Android.bp", _
        "# Installed to: /vendor/etc/init/calculatord.rc", _
        "", _
        "service calculatord /vendor/bin/calculatord", _
        "    class main          # starts with main class at boot", _
        "    user system         # runs as system user", _
        "    group system", _
        "    # No 'oneshot' = service restarts automatically if it crashes", _
        "", _
        "Verify startup:  adb logcat -s calculatord", _
        "Manual control: adb shell start calculatord | stop calculatord"

    S "calculator_client - CLI Test Tool", _
        "# Usage:", _
        "adb shell calculator_client add 10 20   ->  Result: 30", _
        "adb shell calculator_client sub 100 37  ->  Result: 63", _
        "adb shell calculator_client mul 6 7     ->  Result: 42", _
        "adb shell calculator_client div 10 2    ->  Result: 5", _
        "adb shell calculator_client div 10 0    ->  Error: divide by zero (code 1)", _
        "", _
        "Client flow:", _
        "  1. AServiceManager_checkService(SERVICE_NAME)", _
        "  2. ICalculatorService::fromBinder(binder)", _
        "  3. client->add(a, b, &result)", _
        "  4. Check status.isOk() or getServiceSpecificError()"

    S "VTS Tests - VtsCalculatorServiceTest", _
        "Test suite with Google Test framework, 30+ test cases:", _
        "", _
        "ServiceRegistered    - AServiceManager_checkService returns non-null", _
        "AddPositive          - 10 + 20 = 30", _
        "AddNegative          - (-5) + (-3) = -8", _
        "AddZero              - 0 + 0 = 0", _
        "SubtractBasic        - various combinations", _
        "MultiplyBasic        - edge cases including INT_MAX boundary", _
        "DivideBasic          - valid divisions, integer truncation", _
        "DivideByZero         - expects getServiceSpecificError() == 1", _
        "ConcurrencyTest      - 8 threads x 100 calls each (no crashes)", _
        "StabilityTest        - 1000 sequential calls (no memory leaks)"

    S "Running VTS Tests on Device", _
        "# Build the test binary", _
        "m VtsCalculatorServiceTest", _
        "", _
        "# Push to device", _
        "adb push out/target/product/rpi5/data/nativetest64/\\", _
        "         VtsCalculatorServiceTest /data/local/tmp/", _
        "", _
        "# Run on device", _
        "adb shell /data/local/tmp/VtsCalculatorServiceTest", _
        "", _
        "# Expected output:", _
        "[==========] Running 32 tests from 1 test suite.", _
        "[  PASSED  ] 32 tests.", _
        "", _
        "Android.bp: vendor: true, test_suites: [""vts""]"
End Sub

' ============================================================
' PART 6 - BMI SERVICE
' ============================================================

Sub Slides_Part6_BMI()
    Sec "Part 6: Building the BMI Service", _
        "Float Arithmetic and Health Categories"

    S "BMI Service Overview", _
        "Second service - demonstrates float handling in AIDL/Binder", _
        "Binary:     bmid  (/vendor/bin/bmid)", _
        "AIDL:       IBMIService  (single method)", _
        "Operation:  getBMI(float height_m, float weight_kg) -> float", _
        "Formula:    BMI = weight / (height * height)", _
        "Validation: height <= 0 or weight <= 0 -> ERROR_INVALID_INPUT", _
        "Client:     bmi_client also shows health category (Underweight/Normal/etc.)"

    S "IBMIService.aidl - The Interface", _
        "// File: aidl/com/myoem/bmi/IBMIService.aidl", _
        "package com.myoem.bmi;", _
        "", _
        "interface IBMIService {", _
        "    const int ERROR_INVALID_INPUT = 1;", _
        "", _
        "    // height in meters, weight in kilograms", _
        "    // Returns BMI value (float)", _
        "    // Throws ServiceSpecificException(1) if height<=0 or weight<=0", _
        "    float getBMI(float height, float weight);", _
        "}"

    S "BMIService.cpp - Implementation", _
        "#define LOG_TAG ""BMIService""", _
        "#include <log/log.h>", _
        "#include ""BMIService.h""", _
        "using namespace aidl::com::myoem::bmi;", _
        "", _
        "ScopedAStatus BMIService::getBMI(float height, float weight, float* out) {", _
        "    if (height <= 0.0f || weight <= 0.0f) {", _
        "        return ScopedAStatus::fromServiceSpecificError(ERROR_INVALID_INPUT);", _
        "    }", _
        "    *out = weight / (height * height);", _
        "    ALOGD(""getBMI(%.2f m, %.2f kg) = %.2f"", height, weight, *out);", _
        "    return ScopedAStatus::ok();", _
        "}"

    S "BMI Health Categories - bmi_client Output", _
        "Implemented in bmi_client.cpp for display:", _
        "", _
        "BMI < 18.5              ->  Underweight", _
        "BMI 18.5 - 24.9         ->  Normal weight", _
        "BMI 25.0 - 29.9         ->  Overweight", _
        "BMI >= 30.0             ->  Obese", _
        "", _
        "Example runs:", _
        "  adb shell bmi_client 1.75 70   ->  BMI: 22.86 (Normal weight)", _
        "  adb shell bmi_client 1.60 45   ->  BMI: 17.58 (Underweight)", _
        "  adb shell bmi_client 1.80 100  ->  BMI: 30.86 (Obese)", _
        "  adb shell bmi_client 0 70      ->  Error: invalid input (code 1)"

    S "Float Precision in AIDL", _
        "AIDL float maps to C++ float (32-bit IEEE 754)", _
        "32-bit float gives ~7 significant decimal digits - plenty for BMI", _
        "", _
        "Key coding rules:", _
        "  Use 0.0f not 0.0 (avoids implicit double conversion)", _
        "  Division order: weight / (h * h), NOT weight / h / h", _
        "  Validate BEFORE computation - prevents NaN and Infinity results", _
        "", _
        "VTS test uses EXPECT_NEAR not EXPECT_EQ for float comparison:", _
        "  EXPECT_NEAR(result, 22.86f, 0.01f);  // tolerance of 0.01"

    S "Calculator vs BMI - Structural Comparison", _
        "Aspect           Calculator              BMI", _
        "Methods          4 (add/sub/mul/div)     1 (getBMI)", _
        "Return type      int32                   float", _
        "Error codes      ERROR_DIVIDE_BY_ZERO=1  ERROR_INVALID_INPUT=1", _
        "Input validation b == 0 for divide       height<=0 || weight<=0", _
        "main.cpp         Identical pattern       Identical pattern", _
        "Android.bp       Identical structure     Identical structure", _
        "RC file          Identical pattern       Identical pattern", _
        "", _
        "Pattern is identical - only AIDL types and business logic differ"

    S "VTS Tests - VtsBMIServiceTest", _
        "SetUpTestSuite: verifies service is available before all tests", _
        "", _
        "ServiceAvailable    - checkService returns non-null", _
        "NormalBMI           - 1.75m, 70kg -> EXPECT_NEAR(22.86, 0.01)", _
        "UnderweightBMI      - low weight scenario", _
        "OverweightBMI       - elevated weight scenario", _
        "ObeseBMI            - high BMI scenario", _
        "ZeroHeight          - expects ERROR_INVALID_INPUT", _
        "ZeroWeight          - expects ERROR_INVALID_INPUT", _
        "NegativeHeight      - expects ERROR_INVALID_INPUT", _
        "NegativeWeight      - expects ERROR_INVALID_INPUT", _
        "FloatPrecision      - checks multiple known BMI values with tolerance"
End Sub

' ============================================================
' PART 7 - SELINUX
' ============================================================

Sub Slides_Part7_SELinux()
    Sec "Part 7: SELinux", "Teaching Android to Trust Your Service"

    S "What is SELinux?", _
        "SELinux = Security-Enhanced Linux (developed by NSA, mainlined ~2003)", _
        "Implements MAC = Mandatory Access Control", _
        "", _
        "Traditional Linux DAC problem:", _
        "  DAC = Discretionary Access Control (chmod, chown, rwxrwxrwx)", _
        "  Root user can bypass ALL DAC rules - compromised root = total compromise", _
        "", _
        "MAC solution:", _
        "  The kernel enforces policy regardless of UID/root status", _
        "  Even root cannot do what SELinux policy does not explicitly allow", _
        "  Android runs in ENFORCING mode - denials are both logged AND blocked"

    S "SELinux Security Contexts", _
        "Every process has a security context (domain):", _
        "  u:r:calculatord:s0", _
        "  u:r:init:s0", _
        "  u:r:system_server:s0", _
        "", _
        "Every file/socket/service has a security context (type):", _
        "  u:object_r:vendor_file:s0         (vendor binaries)", _
        "  u:object_r:calculatord_exec:s0    (our executable)", _
        "  u:object_r:calculatord_service:s0 (our service registration)", _
        "", _
        "Format:  user : role : type : level", _
        "  user = always 'u' in Android", _
        "  role = 'r' for processes, 'object_r' for files/services"

    S "How Processes Get Their SELinux Domain", _
        "1. init launches the service (init is in u:r:init:s0)", _
        "2. init does fork() + exec() of /vendor/bin/calculatord", _
        "3. SELinux applies a domain transition based on 3 factors:", _
        "   - Source domain: u:r:init:s0", _
        "   - Target executable type: u:object_r:calculatord_exec:s0", _
        "   - Type transition rule in .te file", _
        "4. After exec, process is in: u:r:calculatord:s0", _
        "", _
        "Verify: adb shell ps -eZ | grep calculatord", _
        "Result: u:r:calculatord:s0  <pid>  1  /vendor/bin/calculatord"

    S "SELinux Policy Files Structure", _
        "vendor/myoem/services/calculator/sepolicy/private/", _
        "  calculatord.te       <- Type enforcement rules (the core policy)", _
        "  file_contexts        <- Maps file paths to SELinux types", _
        "  service_contexts     <- Maps service names to SELinux types", _
        "", _
        "Registered in myoem_base.mk:", _
        "  PRODUCT_PRIVATE_SEPOLICY_DIRS += .../sepolicy/private", _
        "", _
        "Build system merges all .te files from all registered dirs at build time", _
        "Policy compiled to binary and stored in /vendor/etc/selinux/"

    S "file_contexts and service_contexts Files", _
        "# file_contexts - maps executable path to type", _
        "/vendor/bin/calculatord   u:object_r:calculatord_exec:s0", _
        "", _
        "# service_contexts - maps service name to type", _
        "com.myoem.calculator.ICalculatorService  u:object_r:calculatord_service:s0", _
        "", _
        "These labels are checked at:", _
        "  - exec() time (file_contexts) for domain transition", _
        "  - AServiceManager_addService() time (service_contexts) for registration", _
        "  - AServiceManager_checkService() time for client access"

    S "Writing .te (Type Enforcement) Rules", _
        "# 1. Declare the domain and executable type", _
        "type calculatord, domain;", _
        "type calculatord_exec, exec_type, vendor_file_type, file_type;", _
        "", _
        "# 2. Allow init to launch and transition to our domain", _
        "init_daemon_domain(calculatord)", _
        "", _
        "# 3. Declare service type and allow registration", _
        "type calculatord_service, service_manager_type;", _
        "add_service(calculatord, calculatord_service)", _
        "", _
        "# 4. Allow Binder use (usually included by init_daemon_domain)", _
        "binder_use(calculatord)"

    S "RPi5 SELinux Lesson: sysfs_hwmon Does Not Exist", _
        "Situation: ThermalControl service needs to write fan PWM via sysfs", _
        "Assumption: sysfs_hwmon type exists for /sys/class/hwmon/ files", _
        "Reality:    sysfs_hwmon does NOT exist in AOSP 15 base policy!", _
        "", _
        "Verification on device:", _
        "  adb shell ls -laZ /sys/class/hwmon/hwmon2/pwm1", _
        "  -> u:object_r:sysfs:s0   (generic 'sysfs', NOT 'sysfs_hwmon')", _
        "", _
        "Fix in .te file:", _
        "  allow thermalcontrold sysfs:file { read write open getattr };", _
        "", _
        "Lesson: ALWAYS verify sysfs labels empirically with ls -laZ on real device"

    S "Diagnosing SELinux Denials", _
        "All denials appear in Android log:", _
        "  adb logcat -d | grep 'avc: denied'", _
        "  adb logcat -b kernel | grep avc", _
        "", _
        "Sample denial:", _
        "  avc: denied { write } for pid=1234 comm=""calculatord""", _
        "    path=""/dev/binder""", _
        "    scontext=u:r:calculatord:s0", _
        "    tcontext=u:object_r:binder_device:s0 tclass=chr_file", _
        "", _
        "Reading it: calculatord (scontext) tried to write (action)", _
        "  to binder_device chr_file (tcontext:tclass) -> DENIED"

    S "SELinux Debugging Workflow", _
        "Step 1: Run service, see denial in logcat (avc: denied)", _
        "Step 2: Identify: WHO (scontext) + ACTION ({write}) + WHAT (tcontext:tclass)", _
        "Step 3: Add allow rule in .te file:", _
        "    allow calculatord binder_device:chr_file { read write open ioctl };", _
        "Step 4: Rebuild SELinux policy:  m selinux_policy", _
        "Step 5: Push and reboot, check for remaining denials", _
        "", _
        "Dev shortcut (DEBUG ONLY - never ship):", _
        "    adb shell setenforce 0   <- permissive mode, denials logged not blocked", _
        "    adb shell getenforce     <- check current mode"

    S "Common SELinux Macros for Vendor Services", _
        "init_daemon_domain(myservice)", _
        "  -> Allows init to exec and transition to myservice domain", _
        "", _
        "add_service(myservice, myservice_svc)", _
        "  -> Allows myservice to register with ServiceManager", _
        "", _
        "binder_use(myservice)", _
        "  -> Allows myservice to use Binder IPC", _
        "", _
        "binder_call(myservice, other_service)", _
        "  -> Allows myservice to call another Binder service", _
        "", _
        "get_prop(myservice, vendor_default_prop)", _
        "  -> Allows reading system properties"
End Sub

' ============================================================
' PART 8 - DEBUGGING
' ============================================================

Sub Slides_Part8_Debugging()
    Sec "Part 8: Debugging", "When Things Don't Work"

    S "Debugging Philosophy", _
        "Start with logs - Android has excellent logging infrastructure", _
        "Check SELinux first - most 'service not working' = SELinux denial", _
        "Verify service is actually running before debugging client issues", _
        "Use the simplest test first (CLI client) before complex Java clients", _
        "Never assume - verify labels, names, paths empirically on device", _
        "Don't guess at SELinux types - use ls -laZ to verify on real hardware"

    S "Is My Service Running? - Essential Checks", _
        "# Is the process alive?", _
        "adb shell ps -e | grep calculatord", _
        "", _
        "# Is it registered with ServiceManager?", _
        "adb shell service list | grep calculator", _
        "", _
        "# What SELinux domain is it running in?", _
        "adb shell ps -eZ | grep calculatord", _
        "", _
        "# View service startup logs", _
        "adb logcat -s calculatord", _
        "adb logcat -s CalculatorService", _
        "", _
        "# Check for init errors", _
        "adb logcat | grep 'init:'"

    S "Service Not Found - Top Causes", _
        "Cause 1: Service name mismatch (most common!)", _
        "  main.cpp:         AServiceManager_addService(b, ""com.myoem.bmi.IBMIService"")", _
        "  service_contexts: com.myoem.bmi.IBMIService  u:object_r:bmid_service:s0", _
        "  client code:      AServiceManager_checkService(""com.myoem.bmi.IBMIService"")", _
        "  Must match EXACTLY - case sensitive, no trailing spaces", _
        "", _
        "Cause 2: SELinux blocked service registration", _
        "  Check: adb logcat -d | grep 'avc: denied'", _
        "", _
        "Cause 3: Service crashed at startup", _
        "  Check: adb logcat -s bmid  (look for SIGABRT or SIGSEGV)"

    S "Android Logging - Macros and Usage", _
        "ALWAYS put #define LOG_TAG before any #include in .cpp files!", _
        "", _
        "#define LOG_TAG ""MyService""  // <- First line", _
        "#include <log/log.h>          // <- Then includes", _
        "", _
        "Log levels:", _
        "  ALOGV(fmt, ...)  - Verbose  (lowest - often filtered)", _
        "  ALOGD(fmt, ...)  - Debug", _
        "  ALOGI(fmt, ...)  - Info", _
        "  ALOGW(fmt, ...)  - Warning", _
        "  ALOGE(fmt, ...)  - Error    (highest - always visible)", _
        "", _
        "Filter by tag: adb logcat -s CalculatorService:D"

    S "Dev Iteration Workflow on RPi5", _
        "# 1. Build the changed binary", _
        "m calculatord", _
        "", _
        "# 2. Unlock vendor partition (RPi5: no adb remount!)", _
        "adb root", _
        "adb shell mount -o remount,rw /vendor", _
        "", _
        "# 3. Push the binary", _
        "adb push out/target/product/rpi5/vendor/bin/calculatord /vendor/bin/", _
        "adb shell chmod 755 /vendor/bin/calculatord", _
        "", _
        "# 4. Restart the service", _
        "adb shell stop calculatord", _
        "adb shell start calculatord", _
        "", _
        "# 5. Watch the logs", _
        "adb logcat -s calculatord CalculatorService"

    S "Debugging RC File and Boot Issues", _
        "Service won't start at boot?", _
        "", _
        "# Check init processed the RC file", _
        "adb logcat | grep 'init:' | grep calculator", _
        "adb logcat -b events | grep service_start", _
        "", _
        "Common RC file mistakes:", _
        "  Wrong binary path (/system/bin instead of /vendor/bin)", _
        "  Typo in service name (used by start/stop commands)", _
        "  'oneshot' keyword prevents auto-restart on crash", _
        "  Missing user/group causing permission denied on exec", _
        "", _
        "Manually start for testing:", _
        "  adb shell start calculatord"

    S "dumpsys - Service Introspection Tool", _
        "# List ALL registered services", _
        "adb shell dumpsys -l", _
        "", _
        "# Dump a specific service (if it implements dump())", _
        "adb shell dumpsys com.myoem.calculator.ICalculatorService", _
        "", _
        "# Useful system service dumps:", _
        "adb shell dumpsys activity          # app/activity manager state", _
        "adb shell dumpsys window            # window manager state", _
        "adb shell dumpsys battery           # battery state", _
        "adb shell dumpsys audio             # audio service state", _
        "", _
        "Native services can implement BnBinder::dump() to expose debug info"

    S "Common Issues Reference Table", _
        "Issue: 'Service not found'", _
        "  Fix:  Check service name matches in main.cpp, service_contexts, client", _
        "", _
        "Issue: Service crashes immediately at launch", _
        "  Fix:  adb logcat -s <LOG_TAG>  -- look for SIGABRT/SIGSEGV/exception", _
        "", _
        "Issue: Permission denied on sysfs write", _
        "  Fix:  adb logcat | grep 'avc: denied'  -- add SELinux allow rule", _
        "", _
        "Issue: Cannot push to /vendor", _
        "  Fix:  adb root; adb shell mount -o remount,rw /vendor", _
        "", _
        "Issue: Build error 'module not found'", _
        "  Fix:  Add module dir to PRODUCT_SOONG_NAMESPACES in myoem_base.mk", _
        "", _
        "Issue: AIDL client can't find service method", _
        "  Fix:  Rebuild aidl_interface, verify -ndk library name in Android.bp"
End Sub

' ============================================================
' PART 9 - MANAGER PATTERN
' ============================================================

Sub Slides_Part9_ManagerPattern()
    Sec "Part 9: The Manager Pattern", "Exposing Native Services to Java and Kotlin Apps"

    S "Why the Manager Pattern?", _
        "Problem 1: Java/Kotlin apps cannot use NDK Binder APIs directly", _
        "Problem 2: Raw AIDL usage in application code is verbose and fragile", _
        "Problem 3: Apps should not deal with RemoteException, null checks, reconnects", _
        "", _
        "Solution: Java Manager library wraps AIDL in a clean, documented API", _
        "", _
        "Pattern from Android itself:", _
        "  AudioManager, WindowManager, LocationManager, InputManager...", _
        "  All are Java wrappers hiding AIDL details from app developers", _
        "  We follow the same pattern for ThermalControlManager"

    S "Manager Architecture - Full Stack", _
        "App (Kotlin/Java Activity)", _
        "    |  ThermalControlManager.getCpuTemperatureCelsius()", _
        "    v", _
        "ThermalControlManager.java  (java_library, vendor: true)", _
        "    |  ServiceManager.checkService(SERVICE_NAME)", _
        "    |  IThermalControlService.Stub.asInterface(binder)", _
        "    v", _
        "thermalcontrold  (C++ native service, /vendor/bin/)", _
        "    |  HAL reads/writes sysfs files", _
        "    v", _
        "/sys/class/thermal/...  and  /sys/class/hwmon/...  (Linux kernel)"

    S "ThermalControlManager.java - Constructor and Availability", _
        "public class ThermalControlManager {", _
        "    private static final String SERVICE_NAME =", _
        "        ""com.myoem.thermalcontrol.IThermalControlService/default"";", _
        "    private IThermalControlService mService;", _
        "    private static final String TAG = ""ThermalControlManager"";", _
        "", _
        "    public ThermalControlManager(Context context) {", _
        "        IBinder binder = ServiceManager.checkService(SERVICE_NAME);", _
        "        if (binder != null) {", _
        "            mService = IThermalControlService.Stub.asInterface(binder);", _
        "        } else {", _
        "            Log.w(TAG, ""ThermalControl service not available"");", _
        "        }", _
        "    }", _
        "    public boolean isAvailable() { return mService != null; }", _
        "}"

    S "ThermalControlManager.java - Method Pattern", _
        "public float getCpuTemperatureCelsius() {", _
        "    if (mService == null) return 0.0f;  // safe default", _
        "    try {", _
        "        return mService.getCpuTemperatureCelsius();", _
        "    } catch (RemoteException e) {", _
        "        Log.e(TAG, ""getCpuTemperature failed: "" + e.getMessage());", _
        "        return 0.0f;", _
        "    }", _
        "}", _
        "", _
        "public boolean setFanAutoMode(boolean enable) {", _
        "    if (mService == null) return false;", _
        "    try {", _
        "        mService.setFanAutoMode(enable);  return true;", _
        "    } catch (RemoteException e) { return false; }", _
        "}"

    S "Android.bp for Java Manager Library", _
        "java_library {", _
        "    name:        ""thermalcontrol-manager"",", _
        "    vendor:      true,", _
        "    srcs:        [""java/**/*.java""],", _
        "    libs: [", _
        "        ""thermalcontrolservice-aidl-V1-java"",  // AIDL-generated", _
        "        ""framework"",                           // for Context, IBinder, Log", _
        "    ],", _
        "    sdk_version: ""system_current"",  // needed for ServiceManager access", _
        "    visibility:  [""//vendor/myoem:__subpackages__""],", _
        "}", _
        "", _
        "sdk_version: system_current -> access to @SystemApi (hidden APIs)"

    S "Using ThermalControlManager in an App", _
        "// In Activity.onCreate() or ViewModel:", _
        "ThermalControlManager tcm = new ThermalControlManager(this);", _
        "", _
        "if (tcm.isAvailable()) {", _
        "    float temp  = tcm.getCpuTemperatureCelsius();  // e.g. 42.3", _
        "    int   rpm   = tcm.getFanSpeedRpm();            // e.g. 1800", _
        "    boolean auto = tcm.isFanAutoMode();            // true", _
        "", _
        "    textView.setText(String.format(""%.1f C  |  %d RPM"", temp, rpm));", _
        "} else {", _
        "    textView.setText(""Thermal service unavailable"");", _
        "}", _
        "", _
        "// Set fan to manual at 50% speed:", _
        "tcm.setFanAutoMode(false);", _
        "tcm.setFanSpeed(50);"

    S "Error Handling and Death Notifications", _
        "Three failure scenarios to handle:", _
        "", _
        "1. Service not started yet -> isAvailable() returns false", _
        "   Handle: show 'unavailable' state, retry later", _
        "", _
        "2. RemoteException -> service crashed mid-call", _
        "   Handle: catch, log, return safe default", _
        "", _
        "3. Service dies after connection -> linkToDeath for notification:", _
        "   binder.linkToDeath(() -> {", _
        "       mService = null;  // null out dead reference", _
        "       notifyUnavailable();", _
        "   }, 0);", _
        "", _
        "4. ServiceSpecificException -> service returned error code", _
        "   Handle per method based on known error codes from AIDL"

    S "Key Learnings - Manager Pattern", _
        "java_library vs android_library:", _
        "  use android_library if you have res/ or AndroidManifest.xml", _
        "  use java_library for pure-Java, no resources", _
        "", _
        "sdk_version: 'system_current' is required for ServiceManager access", _
        "  (it is a @SystemApi - not available to regular app SDK)", _
        "", _
        "ServiceManager.checkService() vs getService():", _
        "  checkService: returns null immediately if service not found (non-blocking)", _
        "  getService:   blocks up to 5 seconds waiting for service to appear", _
        "", _
        "Always expose isAvailable() - apps must handle the 'no service' case", _
        "Return meaningful defaults (0.0f, false, -1) not exceptions to callers"
End Sub

' ============================================================
' CONCLUSION SLIDES
' ============================================================

Sub Slides_Conclusion()
    Sec "Summary and Key Takeaways", "10 Parts - One Complete Vendor Stack"

    S "What We Built - The Full Stack", _
        "HAL Layer:      libthermalcontrolhal  - sysfs read/write for fan and CPU temp", _
        "Native Services: calculatord, bmid, thermalcontrold, safemoded, potvolumed", _
        "AIDL Interfaces: ICalculatorService, IBMIService, IThermalControlService,", _
        "                 ISafeModeService (with VehicleData parcelable + callbacks)", _
        "Java Manager:   ThermalControlManager - clean Java API wrapping AIDL", _
        "VTS Tests:      30+ test cases per service - automated CI validation", _
        "SELinux:        Per-service .te + file_contexts + service_contexts", _
        "Init RC Files:  Automatic startup at boot, user=system, auto-restart"

    S "The Golden Rules of AOSP Vendor Development", _
        "Rule 1: ALWAYS use libbinder_ndk in vendor - NEVER libbinder", _
        "Rule 2: #define LOG_TAG MUST be the very first line in every .cpp", _
        "Rule 3: vendor: true in Android.bp - installs to /vendor/ not /system/", _
        "Rule 4: Add every service dir to PRODUCT_SOONG_NAMESPACES", _
        "Rule 5: Service names must match exactly in main.cpp, service_contexts, Manager", _
        "Rule 6: NEVER assume sysfs type - verify with ls -laZ on real device", _
        "Rule 7: ZERO changes to device/ or frameworks/ - everything in vendor/myoem/"

    S "AIDL Design Best Practices", _
        "Define error codes as const int inside the interface - accessible everywhere", _
        "Validate ALL inputs at service boundary - never trust callers", _
        "unstable: true for intra-vendor services (simpler, no versioning overhead)", _
        "stability: vintf for cross-partition interfaces (required for AOSP HALs)", _
        "cpp backend: always disabled in vendor - ABI instability risk", _
        "ndk backend: always enabled - this is the safe, stable path", _
        "Use oneway for callbacks - prevents ANR if client is slow"

    S "SELinux Best Practices", _
        "Start with setenforce 0 during development - fix policies before shipping", _
        "Verify all file labels with ls -laZ BEFORE writing .te rules", _
        "Read avc: denied messages carefully - they tell you EXACTLY what to add", _
        "Use init_daemon_domain() macro - handles most init->daemon transitions", _
        "sysfs_hwmon does NOT exist in AOSP 15 - use generic sysfs type on RPi5", _
        "Group each service's sepolicy in its own directory under the service folder", _
        "Never ship with 'permissive myservice' - fix the policy properly"

    S "Debugging Quick Reference", _
        "Service running?     adb shell ps -e | grep <daemon>", _
        "Service listed?      adb shell service list | grep <name>", _
        "SELinux denied?      adb logcat -d | grep 'avc: denied'", _
        "Service logs?        adb logcat -s <LOG_TAG>", _
        "Process domain?      adb shell ps -eZ | grep <daemon>", _
        "File label?          adb shell ls -laZ <path>", _
        "Manual start/stop?   adb shell start <svc> / adb shell stop <svc>", _
        "Remount vendor?      adb shell mount -o remount,rw /vendor"

    S "ThermalControl Service - Bonus Deep Dive", _
        "HAL pattern: libthermalcontrolhal (cc_library_shared) wraps all sysfs I/O", _
        "  IThermalControlHal pure virtual interface + ThermalControlHal implementation", _
        "  SysfsHelper: sysfsReadInt/Float, sysfsWriteInt, discoverHwmonPath()", _
        "  hwmon index discovered dynamically - not hardcoded (scans hwmon0..15)", _
        "", _
        "Service owns unique_ptr<IThermalControlHal> mHal", _
        "All AIDL methods delegate to mHal with logging and validation", _
        "VINTF stable interface: stability: vintf (vs unstable for Calculator/BMI)", _
        "RC file: on boot chown/chmod hwmon sysfs files (kernel creates as root:root)"

    S "SafeMode Service - VHAL Integration", _
        "Connects to VHAL: IVehicle::fromBinder(AServiceManager_waitForService(...))", _
        "Subscribes to VHAL properties: PERF_VEHICLE_SPEED, CURRENT_GEAR, FUEL_LEVEL", _
        "VehicleData parcelable passed to registered callbacks on every VHAL event", _
        "", _
        "Simulator mode: reads /data/local/tmp/safemode_sim.txt for testing without car", _
        "Compiled with: -DSAFEMODE_SIM_MODE flag in Android.bp", _
        "", _
        "Gear values: GEAR_UNKNOWN=0, GEAR_NEUTRAL=1, GEAR_REVERSE=2,", _
        "             GEAR_PARK=4, GEAR_DRIVE=8, GEAR_1..9=0x10..0x1000"

    S "Resources for Further Learning", _
        "Medium Series:    @aruncse2k20 - 10-part AOSP Vendor Development series", _
        "GitHub:           github.com/ProArun - vendor/myoem source code", _
        "AOSP Docs:        source.android.com - official documentation", _
        "AIDL Guide:       source.android.com/docs/core/architecture/aidl", _
        "SELinux Android:  source.android.com/docs/security/selinux", _
        "VINTF Guide:      source.android.com/docs/core/architecture/vintf", _
        "Binder Overview:  source.android.com/docs/core/architecture/hidl/binder-ipc"

    Sec "Thank You!", _
        "10 articles.  5 services.  1 complete vendor layer." & Chr(13) & _
        "From zero to production-quality AOSP services on real hardware." & Chr(13) & _
        "Questions?"
End Sub
