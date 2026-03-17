#
# Copyright (C) 2024 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# myoem_rpi5 — standard tablet build for Raspberry Pi 5
#

$(call inherit-product, device/brcm/rpi5/aosp_rpi5.mk)
$(call inherit-product, vendor/myoem/common/myoem_base.mk)

# Tablet-specific packages go here
# PRODUCT_PACKAGES += MyOEMApp

PRODUCT_DEVICE       := rpi5
PRODUCT_NAME         := myoem_rpi5
PRODUCT_BRAND        := MyOEM
PRODUCT_MODEL        := MyOEM Raspberry Pi 5
PRODUCT_MANUFACTURER := MyOEM
