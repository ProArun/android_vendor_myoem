#
# Copyright (C) 2024 MyOEM
# SPDX-License-Identifier: Apache-2.0
#
# myoem_rpi5_car — automotive build for Raspberry Pi 5
#

$(call inherit-product, device/brcm/rpi5/aosp_rpi5_car.mk)
$(call inherit-product, vendor/myoem/common/myoem_base.mk)

# Car-specific packages go here
# PRODUCT_PACKAGES += MyOEMCarApp

PRODUCT_DEVICE       := rpi5
PRODUCT_NAME         := myoem_rpi5_car
PRODUCT_BRAND        := MyOEM
PRODUCT_MODEL        := MyOEM Raspberry Pi 5 Car
PRODUCT_MANUFACTURER := MyOEM
