// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.thermalcontrol;

import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

/**
 * ThermalControlManager — Java API for apps to interact with thermalcontrold.
 *
 * ServiceManager is @hide and unavailable in vendor SDK builds, so the caller
 * (the app, which has platform_apis:true) is responsible for the service lookup
 * and passes the IBinder here. This keeps the manager independent of @hide APIs.
 *
 * Typical usage in an app:
 *
 *   IBinder binder = ServiceManager.checkService(ThermalControlManager.SERVICE_NAME);
 *   ThermalControlManager mgr = new ThermalControlManager(binder);
 *   if (mgr.isAvailable()) {
 *       float temp = mgr.getCpuTemperatureCelsius();
 *       mgr.setFanSpeed(50);
 *   }
 */
public class ThermalControlManager {

    private static final String TAG = "ThermalControlManager";

    /** Must match kServiceName in main.cpp and service_contexts exactly. */
    public static final String SERVICE_NAME =
            "com.myoem.thermalcontrol.IThermalControlService/default";

    // ── Error codes (mirror AIDL constants) ──────────────────────────────────
    public static final int ERROR_HAL_UNAVAILABLE = 1;
    public static final int ERROR_INVALID_SPEED   = 2;
    public static final int ERROR_SYSFS_WRITE     = 3;

    // ── Temperature categories ────────────────────────────────────────────────
    public static final String TEMP_COOL     = "Cool";
    public static final String TEMP_WARM     = "Warm";
    public static final String TEMP_HOT      = "Hot";
    public static final String TEMP_CRITICAL = "Critical";

    private final IThermalControlService mService;

    /**
     * Constructs a ThermalControlManager from a raw binder.
     *
     * @param binder The IBinder returned by
     *               {@code ServiceManager.checkService(SERVICE_NAME)}.
     *               Pass null if the service is not available — all methods
     *               will return safe sentinel values in that case.
     */
    public ThermalControlManager(IBinder binder) {
        if (binder != null) {
            mService = IThermalControlService.Stub.asInterface(binder);
            Log.d(TAG, "Connected to " + SERVICE_NAME);
        } else {
            mService = null;
            Log.w(TAG, "Service not found: " + SERVICE_NAME
                    + " — is thermalcontrold running?");
        }
    }

    /**
     * Returns true if the thermalcontrold service is reachable.
     * Call this before any other method; if false, all methods return defaults.
     */
    public boolean isAvailable() {
        return mService != null;
    }

    // ── Read operations ───────────────────────────────────────────────────────

    /**
     * Returns the current CPU temperature in degrees Celsius.
     * Returns 0.0f if service is unavailable or a RemoteException occurs.
     */
    public float getCpuTemperatureCelsius() {
        if (mService == null) return 0.0f;
        try {
            return mService.getCpuTemperatureCelsius();
        } catch (RemoteException e) {
            Log.e(TAG, "getCpuTemperatureCelsius failed: " + e.getMessage());
            return 0.0f;
        }
    }

    /**
     * Returns the current fan speed in RPM.
     * Returns -1 if service is unavailable or the tachometer is not wired.
     */
    public int getFanSpeedRpm() {
        if (mService == null) return -1;
        try {
            return mService.getFanSpeedRpm();
        } catch (RemoteException e) {
            Log.e(TAG, "getFanSpeedRpm failed: " + e.getMessage());
            return -1;
        }
    }

    /**
     * Returns true if the fan is currently spinning (PWM > 0).
     * Returns false if service is unavailable.
     */
    public boolean isFanRunning() {
        if (mService == null) return false;
        try {
            return mService.isFanRunning();
        } catch (RemoteException e) {
            Log.e(TAG, "isFanRunning failed: " + e.getMessage());
            return false;
        }
    }

    /**
     * Returns current fan speed as a percentage (0–100).
     * Returns 0 if service is unavailable.
     */
    public int getFanSpeedPercent() {
        if (mService == null) return 0;
        try {
            return mService.getFanSpeedPercent();
        } catch (RemoteException e) {
            Log.e(TAG, "getFanSpeedPercent failed: " + e.getMessage());
            return 0;
        }
    }

    /**
     * Returns true if the fan is under kernel thermal governor control.
     * Returns true (safe default) if service is unavailable.
     */
    public boolean isFanAutoMode() {
        if (mService == null) return true;
        try {
            return mService.isFanAutoMode();
        } catch (RemoteException e) {
            Log.e(TAG, "isFanAutoMode failed: " + e.getMessage());
            return true;
        }
    }

    // ── Write operations ──────────────────────────────────────────────────────

    /**
     * Turn the fan fully on (100%) or fully off (0%).
     * Exits auto mode. No-op if service is unavailable.
     */
    public void setFanEnabled(boolean enabled) {
        if (mService == null) return;
        try {
            mService.setFanEnabled(enabled);
        } catch (RemoteException e) {
            Log.e(TAG, "setFanEnabled(" + enabled + ") failed: " + e.getMessage());
        }
    }

    /**
     * Set fan speed to a specific percentage (0–100).
     * Validates range client-side before IPC to avoid a round-trip on bad input.
     * Exits auto mode.
     *
     * @param speedPercent 0–100
     * @return true on success, false if out of range or service unavailable
     */
    public boolean setFanSpeed(int speedPercent) {
        if (mService == null) return false;
        if (speedPercent < 0 || speedPercent > 100) {
            Log.w(TAG, "setFanSpeed: invalid speedPercent=" + speedPercent
                    + " (must be 0–100)");
            return false;
        }
        try {
            mService.setFanSpeed(speedPercent);
            return true;
        } catch (RemoteException e) {
            Log.e(TAG, "setFanSpeed(" + speedPercent + ") failed: " + e.getMessage());
            return false;
        }
    }

    /**
     * Enable or disable kernel auto mode.
     * autoMode=true  → kernel thermal governor controls the fan.
     * autoMode=false → manual control; current speed is preserved.
     * No-op if service is unavailable.
     */
    public void setFanAutoMode(boolean autoMode) {
        if (mService == null) return;
        try {
            mService.setFanAutoMode(autoMode);
        } catch (RemoteException e) {
            Log.e(TAG, "setFanAutoMode(" + autoMode + ") failed: " + e.getMessage());
        }
    }

    // ── Static utility methods ────────────────────────────────────────────────

    /**
     * Classifies a CPU temperature into a human-readable category.
     *
     * @param celsius temperature in degrees Celsius
     * @return one of: "Cool", "Warm", "Hot", "Critical"
     */
    public static String categorizeTemperature(float celsius) {
        if (celsius < 50.0f)  return TEMP_COOL;
        if (celsius < 70.0f)  return TEMP_WARM;
        if (celsius < 85.0f)  return TEMP_HOT;
        return TEMP_CRITICAL;
    }

    /**
     * Returns a color-hint integer for the temperature category.
     * Values are standard Android Color constants (no dependency on Resources).
     *
     *  Cool     → 0xFF4CAF50  (Material Green 500)
     *  Warm     → 0xFFFFC107  (Material Amber 500)
     *  Hot      → 0xFFFF5722  (Material Deep Orange 500)
     *  Critical → 0xFFF44336  (Material Red 500)
     */
    public static int temperatureColor(float celsius) {
        if (celsius < 50.0f)  return 0xFF4CAF50;
        if (celsius < 70.0f)  return 0xFFFFC107;
        if (celsius < 85.0f)  return 0xFFFF5722;
        return 0xFFF44336;
    }
}
