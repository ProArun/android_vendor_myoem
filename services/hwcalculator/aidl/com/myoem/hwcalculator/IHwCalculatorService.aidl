package com.myoem.hwcalculator;

/**
 * OEM hwcalculator system service.
 * Service name: "com.myoem.hwcalculator.IHwCalculatorService"
 * @hide
 */
interface IHwCalculatorService {
    /** Add two integers. */
    int add(int a, int b);

    /** Subtract b from a. */
    int subtract(int a, int b);

    /** Multiply two integers. */
    int multiply(int a, int b);

    /**
     * Divide a by b.
     * Throws a ServiceSpecificException with code ERROR_DIVIDE_BY_ZERO when b == 0.
     */
    int divide(int a, int b);

    /** Error code returned via ServiceSpecificException when dividing by zero. */
    const int ERROR_DIVIDE_BY_ZERO = 1;
}
