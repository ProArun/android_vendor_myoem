
#pragma once

#include <aidl/com/myoem/hwcalculator/BnHwCalculatorService.h>

namespace aidl::com::myoem::hwcalculator {

class HwCalculatorService : public BnHwCalculatorService {
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

}  // namespace aidl::com::myoem::hwcalculator
