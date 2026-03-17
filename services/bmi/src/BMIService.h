// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/com/myoem/bmi/BnBMIService.h>

namespace aidl::com::myoem::bmi {

class BMIService : public BnBMIService {
public:
    ndk::ScopedAStatus getBMI(float height, float weight,
                              float* _aidl_return) override;
};

}  // namespace aidl::com::myoem::bmi
