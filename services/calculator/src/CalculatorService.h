// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <aidl/com/myoem/calculator/BnCalculatorService.h>

namespace aidl::com::myoem::calculator {

class CalculatorService : public BnCalculatorService {
public:
    ndk::ScopedAStatus add(int32_t a, int32_t b,
                           int32_t* _aidl_return) override;
    ndk::ScopedAStatus subtract(int32_t a, int32_t b,
                                int32_t* _aidl_return) override;
    ndk::ScopedAStatus multiply(int32_t a, int32_t b,
                                int32_t* _aidl_return) override;
    ndk::ScopedAStatus divide(int32_t a, int32_t b,
                              int32_t* _aidl_return) override;
};

}  // namespace aidl::com::myoem::calculator
