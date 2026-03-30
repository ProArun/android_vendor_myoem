#
# Copyright (C) 2024 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# myoem_base.mk — shared config included by every MyOEM product.
# Add packages, overlays, and properties here that apply to ALL variants.
#

# Soong namespace for every module under vendor/myoem/
PRODUCT_SOONG_NAMESPACES += \
    vendor/myoem/services/calculator \
    vendor/myoem/services/bmi \
    vendor/myoem/hal/thermalcontrol \
    vendor/myoem/services/thermalcontrol \
    vendor/myoem/libs/thermalcontrol \
    vendor/myoem/apps/ThermalMonitor \
    vendor/myoem/services/safemode \
    vendor/myoem/libs/safemode \
    vendor/myoem/apps/SafeModeDemo \
    vendor/myoem/services/potvolumed

# ── OEM services (present on all products) ─────────────────────────────────
PRODUCT_PACKAGES += \
    calculatord \
    calculator_client \
    bmid \
    bmi_client \
    thermalcontrold \
    thermalcontrol_client \
    thermalcontrol-manager \
    thermalcontrol-vintf-fragment \
    ThermalMonitor \
    safemoded \
    safemode_client \
    safemode_library \
    com.myoem.safemode-service \
    SafeModeDemo \
    potvolumed

# ── SELinux ────────────────────────────────────────────────────────────────
# Vendor services must use BOARD_VENDOR_SEPOLICY_DIRS, not PRODUCT_PRIVATE_SEPOLICY_DIRS.
# vendor_sepolicy.cil and vendor_file_contexts only pull from BoardVendorSepolicyDirs (.vendor tag).
# PRODUCT_PRIVATE_SEPOLICY_DIRS feeds product_sepolicy.cil (product partition) — wrong for /vendor/bin/* services.
BOARD_VENDOR_SEPOLICY_DIRS += \
    vendor/myoem/services/calculator/sepolicy/private \
    vendor/myoem/services/bmi/sepolicy/private \
    vendor/myoem/services/thermalcontrol/sepolicy/private \
    vendor/myoem/services/safemode/sepolicy/private \
    vendor/myoem/services/potvolumed/sepolicy/private

# ── OEM properties ────────────────────────────────────────────────────────
# Readable at runtime via android.os.SystemProperties.get("ro.myoem.version")
PRODUCT_VENDOR_PROPERTIES += \
    ro.myoem.version=1.0 \
    ro.myoem.build.type=$(TARGET_BUILD_VARIANT)
