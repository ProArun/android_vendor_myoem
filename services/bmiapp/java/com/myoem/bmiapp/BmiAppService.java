// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.bmiapp;

import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.os.ServiceManager;
import android.util.Log;

/**
 * BmiAppService — system-partition intermediary between apps and vendor services.
 *
 * Architecture:
 *   App (BMICalculatorC)
 *     → IBmiAppService AIDL (LOCAL stability, plain Java, no JNI in app)
 *   BmiAppService [this class, runs in system/priv-app]
 *     → libbmiappsvc_jni.so  (FLAG_PRIVATE_VENDOR)
 *   bmid / calculatord [vendor services]
 *
 * Key benefit over BMICalculatorB:
 *   • BmiCalManager (B) still calls vendor binder from the app process.
 *     If that process gets frozen by Android 15 AMS, binder calls through
 *     Java BinderProxy would throw IllegalArgumentException.
 *   • BmiAppService runs as a persistent privileged system app — it is NEVER
 *     frozen. So it can safely use Java code to serve apps, while internally
 *     using JNI only for the cross-partition vendor calls.
 *
 * Service registration:
 *   ServiceManager.addService("bmi_system_service", mBinder)
 *   Called in onCreate() — happens at boot because android:persistent="true".
 */
public class BmiAppService extends Service {

    private static final String TAG = "BmiAppService";
    static final String SERVICE_NAME = "bmi_system_service";

    // ── IBmiAppService implementation ─────────────────────────────────────────

    private final IBmiAppService.Stub mBinder = new IBmiAppService.Stub() {

        @Override
        public boolean isBmiAvailable() {
            return nativeIsBmiAvailable();
        }

        @Override
        public boolean isCalcAvailable() {
            return nativeIsCalcAvailable();
        }

        @Override
        public float getBMI(float height, float weight) {
            return nativeGetBMI(height, weight);
        }

        @Override
        public int add(int a, int b) {
            return nativeAdd(a, b);
        }

        @Override
        public int subtract(int a, int b) {
            return nativeSubtract(a, b);
        }

        @Override
        public int multiply(int a, int b) {
            return nativeMultiply(a, b);
        }

        @Override
        public int divide(int a, int b) {
            return nativeDivide(a, b);
        }
    };

    // ── Service lifecycle ─────────────────────────────────────────────────────

    @Override
    public void onCreate() {
        super.onCreate();
        System.loadLibrary("bmiappsvc_jni");
        // Register with ServiceManager so any process can look us up by name.
        // Requires platform certificate + privileged install.
        ServiceManager.addService(SERVICE_NAME, mBinder);
        Log.i(TAG, SERVICE_NAME + " registered");
    }

    @Override
    public IBinder onBind(Intent intent) {
        // Also support standard bindService() if callers prefer that path.
        return mBinder;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        return START_STICKY;
    }

    // ── JNI — calls bmid/calculatord via FLAG_PRIVATE_VENDOR ─────────────────
    // Defined in BmiAppServiceJni.cpp.
    // Private native methods — apps never see these; they use IBmiAppService.

    private native boolean nativeIsBmiAvailable();
    private native boolean nativeIsCalcAvailable();
    private native float   nativeGetBMI(float height, float weight);
    private native int     nativeAdd(int a, int b);
    private native int     nativeSubtract(int a, int b);
    private native int     nativeMultiply(int a, int b);
    private native int     nativeDivide(int a, int b);
}
