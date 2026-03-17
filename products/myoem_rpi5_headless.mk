#
# Copyright (C) 2024 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# myoem_rpi5_headless — headless/embedded build (no display stack)
#

$(call inherit-product, device/brcm/rpi5/aosp_rpi5.mk)
$(call inherit-product, vendor/myoem/common/myoem_base.mk)

# Strip out display-heavy packages for headless use
PRODUCT_PACKAGES := $(filter-out \
    android.hardware.composer.hwc3-service.drm \
    SurfaceFlinger, \
    $(PRODUCT_PACKAGES))

# Headless-specific packages go here
# PRODUCT_PACKAGES += MyOEMHeadlessApp

PRODUCT_DEVICE       := rpi5
PRODUCT_NAME         := myoem_rpi5_headless
PRODUCT_BRAND        := MyOEM
PRODUCT_MODEL        := MyOEM Raspberry Pi 5 Headless
PRODUCT_MANUFACTURER := MyOEM
