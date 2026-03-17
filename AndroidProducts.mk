#
# Copyright (C) 2024 MyOEM
#
# SPDX-License-Identifier: Apache-2.0
#

PRODUCT_MAKEFILES := \
    $(LOCAL_DIR)/products/myoem_rpi5.mk \
    $(LOCAL_DIR)/products/myoem_rpi5_car.mk \
    $(LOCAL_DIR)/products/myoem_rpi5_headless.mk

COMMON_LUNCH_CHOICES := \
    myoem_rpi5-trunk_staging-userdebug \
    myoem_rpi5-trunk_staging-user \
    myoem_rpi5_car-trunk_staging-userdebug \
    myoem_rpi5_headless-trunk_staging-userdebug
