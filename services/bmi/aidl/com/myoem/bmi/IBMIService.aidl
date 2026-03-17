// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmi;

/**
 * OEM BMI (Body Mass Index) system service.
 * Service name: "com.myoem.bmi.IBMIService"
 * @hide
 */
interface IBMIService {
    /**
     * Calculate Body Mass Index.
     *
     * @param height  height of the person in metres (e.g. 1.75)
     * @param weight  weight of the person in kilograms (e.g. 70.0)
     * @return        BMI value = weight / (height * height)
     *
     * Throws ServiceSpecificException with code ERROR_INVALID_INPUT
     * if height <= 0 or weight <= 0.
     */
    float getBMI(float height, float weight);

    /** Error code when height or weight is zero or negative. */
    const int ERROR_INVALID_INPUT = 1;
}
