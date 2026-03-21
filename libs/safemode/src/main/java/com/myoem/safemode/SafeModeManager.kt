// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemode

import android.content.Context
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import com.myoem.safemode.ISafeModeService
import com.myoem.safemode.VehicleData
import java.util.concurrent.ConcurrentHashMap

/**
 * SafeModeManager — the single entry point for the safemode_library.
 *
 * ── Why polling instead of push callbacks? ───────────────────────────────────
 * Even with stability:"vintf" on the AIDL interface, the Java AIDL compiler
 * does NOT emit AIBinder_markVintfStability() for Stub objects. A Java
 * ISafeModeCallback.Stub() created in the system partition gets SYSTEM
 * stability. Vendor processes (safemoded) cannot call into SYSTEM stability
 * binders — the NDK rejects with:
 *   "Cannot do a user transaction on a system stability binder … in a vendor
 *    stability context"
 *
 * Polling solution: SafeModeManager calls getCurrentData() every POLL_INTERVAL_MS
 * on the main thread. This is system→vendor (allowed direction). No callback
 * binder is sent from system to vendor.
 *
 * This is equivalent to the ThermalControl approach and works correctly.
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * Usage:
 *   // Activity.onStart()
 *   SafeModeManager.getInstance(this).attach(this, myListener)
 *
 *   // Activity.onStop()
 *   SafeModeManager.getInstance(this).dispose(this)
 */
class SafeModeManager private constructor(context: Context) {

    companion object {
        private const val TAG = "SafeModeManager"

        // VINTF format: "package.InterfaceName/instance"
        // Must match kServiceName in safemoded's main.cpp and vintf/manifest.xml.
        private const val SERVICE_NAME = "com.myoem.safemode.ISafeModeService/default"

        /** Poll safemoded for new data every 200 ms. */
        private const val POLL_INTERVAL_MS = 200L

        @Volatile private var instance: SafeModeManager? = null

        fun getInstance(context: Context): SafeModeManager =
            instance ?: synchronized(this) {
                instance ?: SafeModeManager(context.applicationContext).also { instance = it }
            }
    }

    private val mainHandler = Handler(Looper.getMainLooper())

    // Listeners keyed by owner (Activity / Fragment).
    private val listeners = ConcurrentHashMap<Any, SafeModeListener>()

    // Cached last-known state — delivered immediately on attach().
    @Volatile private var mCurrentState = SafeModeState.NO_SAFE_MODE
    @Volatile private var mCurrentInfo  = VehicleInfo()

    // Binder to SafeModeService. Null when disconnected.
    @Volatile private var mService: ISafeModeService? = null

    // True while the polling loop should keep running.
    @Volatile private var mPolling = false

    // ── Binder death recipient ────────────────────────────────────────────────
    private val mDeathRecipient = IBinder.DeathRecipient {
        Log.w(TAG, "SafeModeService died — will reconnect on next poll")
        mService = null
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Public API
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Attach a [SafeModeListener] and start polling.
     *
     * Immediately delivers the current cached state so the UI is populated
     * before the first poll returns. Polling runs on the main thread every
     * [POLL_INTERVAL_MS] milliseconds.
     *
     * Call from Activity.onStart() (or onResume()).
     */
    fun attach(owner: Any, listener: SafeModeListener) {
        listeners[owner] = listener

        // Deliver cached state immediately (before first poll)
        val state = mCurrentState
        val info  = mCurrentInfo
        mainHandler.post { listener.onSafeModeChanged(state, info) }

        // Start the polling loop if not already running
        if (!mPolling) {
            mPolling = true
            mainHandler.post(pollRunnable)
            Log.i(TAG, "Polling started (${POLL_INTERVAL_MS}ms interval)")
        }
    }

    /**
     * Detach the [SafeModeListener] for [owner].
     *
     * If no listeners remain, the polling loop is stopped.
     *
     * Call from Activity.onStop() (or onDestroy()).
     */
    fun dispose(owner: Any) {
        listeners.remove(owner)
        if (listeners.isEmpty()) {
            mPolling = false
            mainHandler.removeCallbacks(pollRunnable)
            Log.i(TAG, "Polling stopped (no listeners)")
        }
    }

    fun getCurrentState(): SafeModeState = mCurrentState
    fun getCurrentInfo():  VehicleInfo  = mCurrentInfo

    // ─────────────────────────────────────────────────────────────────────────
    // Polling loop
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Runs on the main thread every [POLL_INTERVAL_MS] ms.
     * Calls [ISafeModeService.getCurrentData] (system→vendor, always allowed),
     * converts the result, and notifies listeners when anything changes.
     */
    private val pollRunnable = object : Runnable {
        override fun run() {
            if (!mPolling) return

            try {
                val svc = getOrConnect()
                if (svc != null) {
                    val data = svc.getCurrentData()
                    if (data != null) processData(data)
                }
            } catch (e: Exception) {
                Log.e(TAG, "Poll error [${e.javaClass.simpleName}]: ${e.message}")
                mService = null   // force reconnect on next poll
            }

            if (mPolling) mainHandler.postDelayed(this, POLL_INTERVAL_MS)
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Connection management
    // ─────────────────────────────────────────────────────────────────────────

    private fun getOrConnect(): ISafeModeService? {
        mService?.let { return it }

        return try {
            val binder = getServiceBinder(SERVICE_NAME) ?: run {
                Log.e(TAG, "Service '$SERVICE_NAME' not found — is safemoded running?")
                return null
            }
            binder.linkToDeath(mDeathRecipient, 0)
            val svc = ISafeModeService.Stub.asInterface(binder)
            mService = svc
            Log.i(TAG, "Connected to SafeModeService (version=${safeGetVersion(svc)})")
            svc
        } catch (e: Exception) {
            Log.e(TAG, "Connect failed: ${e.message}")
            null
        }
    }

    private fun getServiceBinder(name: String): IBinder? {
        return try {
            val smClass = Class.forName("android.os.ServiceManager")
            val method  = smClass.getMethod("getService", String::class.java)
            val binder  = method.invoke(null, name) as? IBinder
            Log.d(TAG, "ServiceManager.getService('$name') → " +
                    if (binder != null) "found (alive=${binder.isBinderAlive})" else "null")
            binder
        } catch (e: Exception) {
            Log.e(TAG, "ServiceManager reflection failed: ${e.message}")
            null
        }
    }

    private fun safeGetVersion(svc: ISafeModeService): Int =
        try { svc.getVersion() } catch (e: Exception) { -1 }

    // ─────────────────────────────────────────────────────────────────────────
    // Data processing
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * Convert [VehicleData] → [VehicleInfo] + [SafeModeState] and notify listeners.
     * Always called on the main thread (pollRunnable runs on mainHandler).
     */
    private fun processData(data: VehicleData) {
        val info     = VehicleInfo(speedMs = data.speedMs, gear = data.gear, fuelLevelMl = data.fuelLevelMl)
        val newState = SafeModeState.fromVehicleInfo(info)

        mCurrentState = newState
        mCurrentInfo  = info

        Log.d(TAG, "Poll: ${info.speedKmh} km/h  gear=${info.gearName}  fuel=${info.fuelLevelL}L  → $newState")

        listeners.values.forEach { it.onSafeModeChanged(newState, info) }
    }
}
