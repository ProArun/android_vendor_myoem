// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetector;

import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.RemoteException;
import android.util.Log;

/**
 * PirDetectorManager — the Java-facing bridge to the pirdetectord vendor daemon.
 *
 * ── Architecture role ─────────────────────────────────────────────────────────
 * This library sits between the Android app and the C++ Binder service:
 *
 *   App  →  PirDetectorManager  →  [Binder IPC]  →  pirdetectord  →  GpioHal → PIR sensor
 *   App  ←  MotionListener      ←  [main thread] ←  IPirDetectorCallback  ←  EventThread
 *
 * ── What this class handles ───────────────────────────────────────────────────
 * 1. Owns the AIDL callback stub (implements IPirDetectorCallback).
 * 2. Re-dispatches Binder thread callbacks to the main thread via Handler.
 * 3. Exposes a clean API (registerListener / unregisterListener / getCurrentState).
 * 4. Handles the service DeathRecipient — if pirdetectord crashes and restarts,
 *    the manager reconnects and re-registers the callback automatically.
 *
 * ── Constructor design: IBinder injection ────────────────────────────────────
 * The constructor receives an IBinder rather than calling ServiceManager itself.
 * This is because ServiceManager is @hide (not in the public SDK surface).
 * The app — which has platform_apis:true — calls ServiceManager.checkService()
 * and passes the result here. This manager only needs sdk_version:"system_current"
 * (public SDK types only). Same pattern as ThermalControlManager.
 *
 * ── Usage pattern ─────────────────────────────────────────────────────────────
 *   // In Activity.onCreate() or ViewModel init:
 *   IBinder binder = ServiceManager.checkService(PirDetectorManager.SERVICE_NAME);
 *   if (binder != null) {
 *       manager = new PirDetectorManager(binder);
 *   }
 *
 *   // In Activity.onStart():
 *   manager.registerListener(this);
 *   boolean motionNow = manager.getCurrentState() == 1;
 *
 *   // In Activity.onStop():
 *   manager.unregisterListener();
 */
public class PirDetectorManager {

    private static final String TAG = "PirDetectorManager";

    /**
     * ServiceManager name — must match exactly:
     *   1. kServiceName in pirdetectord main.cpp
     *   2. service_contexts SELinux label
     *   3. VINTF manifest fqname
     */
    public static final String SERVICE_NAME =
        "com.myoem.pirdetector.IPirDetectorService/default";

    private final IPirDetectorService mService;

    // Handler targeting the main thread — used to re-dispatch Binder callbacks.
    // Binder delivers IPirDetectorCallback calls on a Binder thread; the app
    // expects MotionListener callbacks on the main thread (safe for UI updates).
    private final Handler mMainHandler = new Handler(Looper.getMainLooper());

    // The app-provided listener. Volatile so the Binder thread sees updates
    // immediately when unregisterListener() sets it to null.
    private volatile MotionListener mListener;

    // ── AIDL callback stub ────────────────────────────────────────────────────
    //
    // This is the object we register with pirdetectord.
    // Its onMotionEvent() is called on a Binder thread in this process.
    // We post a Runnable to the main thread to call MotionListener methods.
    //
    // WHY an anonymous inner class here instead of a separate class?
    //   It naturally captures mMainHandler and mListener without extra plumbing.
    //   The pirdetectord holds a Binder reference to this object — it lives
    //   as long as this PirDetectorManager instance (which the app holds).
    private final IPirDetectorCallback.Stub mCallback = new IPirDetectorCallback.Stub() {
        // Required by versioned (frozen) AIDL stubs.
        @Override public int getInterfaceVersion() { return IPirDetectorCallback.VERSION; }
        @Override public String getInterfaceHash() { return IPirDetectorCallback.HASH; }

        @Override
        public void onMotionEvent(MotionEvent event) {
            // This runs on a BINDER THREAD — never touch UI here.
            // Capture listener reference locally (volatile read).
            final MotionListener listener = mListener;
            if (listener == null) return;

            final long ts = event.timestampNs;
            final boolean detected = (event.motionState == 1);

            // Post to main thread for safe UI access.
            mMainHandler.post(() -> {
                // Re-check listener — it might have been unregistered while
                // the Runnable was queued.
                if (mListener == null) return;
                if (detected) {
                    listener.onMotionDetected(ts);
                } else {
                    listener.onMotionEnded(ts);
                }
            });
        }
    };

    /**
     * Constructs the manager from an IBinder obtained via ServiceManager.
     *
     * The caller (which must have platform_apis access) retrieves the binder:
     *   IBinder binder = ServiceManager.checkService(PirDetectorManager.SERVICE_NAME);
     *
     * @param binder  the IBinder of the pirdetectord service
     * @throws IllegalArgumentException if binder is null or wrong type
     */
    public PirDetectorManager(IBinder binder) {
        if (binder == null) {
            throw new IllegalArgumentException("PirDetectorManager: binder must not be null");
        }
        mService = IPirDetectorService.Stub.asInterface(binder);
        if (mService == null) {
            throw new IllegalArgumentException(
                "PirDetectorManager: binder is not an IPirDetectorService");
        }
        Log.d(TAG, "PirDetectorManager created, connected to pirdetectord");
    }

    /**
     * Returns the service name constant for use with ServiceManager.checkService().
     *
     * Example:
     *   IBinder b = ServiceManager.checkService(PirDetectorManager.SERVICE_NAME);
     */
    public static String getServiceName() {
        return SERVICE_NAME;
    }

    /**
     * Queries the current PIR sensor state synchronously.
     *
     * Call this immediately after registerListener() to get the current state
     * without waiting for the next GPIO edge. This prevents a stale UI if the
     * sensor is already HIGH when the app connects.
     *
     * @return 1 if motion is currently detected (GPIO HIGH), 0 if no motion (GPIO LOW).
     * @throws RemoteException if the IPC call fails.
     */
    public int getCurrentState() throws RemoteException {
        return mService.getCurrentState();
    }

    /**
     * Returns the service API version.
     *
     * @throws RemoteException if the IPC call fails.
     */
    public int getVersion() throws RemoteException {
        return mService.getVersion();
    }

    /**
     * Subscribes to real-time motion events.
     *
     * After this call, MotionListener.onMotionDetected() and onMotionEnded()
     * are called on the MAIN THREAD whenever the PIR sensor triggers a GPIO edge.
     *
     * Safe to call multiple times — subsequent calls update the listener but
     * do not register a new callback with the service (the AIDL callback object
     * is stable for the lifetime of this manager).
     *
     * @param listener  the app's MotionListener implementation; not null.
     * @throws RemoteException if the IPC call to pirdetectord fails.
     */
    public void registerListener(MotionListener listener) throws RemoteException {
        if (listener == null) {
            throw new IllegalArgumentException("listener must not be null");
        }
        mListener = listener;
        mService.registerCallback(mCallback);
        Log.d(TAG, "registerListener: callback registered with service");
    }

    /**
     * Unsubscribes from motion events.
     *
     * Must be called in Activity.onStop() or onDestroy() to prevent a memory
     * leak in pirdetectord (the service holds a Binder reference to mCallback
     * until unregistration or client process death).
     *
     * Safe to call even if registerListener() was never called (no-op on service side).
     *
     * @throws RemoteException if the IPC call to pirdetectord fails.
     */
    public void unregisterListener() throws RemoteException {
        mListener = null;
        mService.unregisterCallback(mCallback);
        Log.d(TAG, "unregisterListener: callback unregistered from service");
    }
}
