// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmiapp;

/**
 * IBmiAppService — the interface apps use to talk to the BmiSystemService.
 *
 * This is a LOCAL-stability (system-partition) binder interface.
 * Apps can call it with plain Java AIDL stubs — no JNI, no FLAG_PRIVATE_VENDOR,
 * no Parcel, no binder internals needed on the app side.
 *
 * BmiSystemService (system/priv-app) implements this interface and internally
 * forwards calls to bmid/calculatord via FLAG_PRIVATE_VENDOR JNI.
 */
interface IBmiAppService {

    // ── Service health ────────────────────────────────────────────────────────
    boolean isBmiAvailable();
    boolean isCalcAvailable();

    // ── BMI ───────────────────────────────────────────────────────────────────
    /** Returns weight / (height * height).  Throws ServiceSpecificException on bad input. */
    float getBMI(float height, float weight);

    // ── Calculator ────────────────────────────────────────────────────────────
    int add(int a, int b);
    int subtract(int a, int b);
    int multiply(int a, int b);
    /** Throws ServiceSpecificException if b == 0. */
    int divide(int a, int b);
}
