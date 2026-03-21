// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.safemode;

import com.myoem.safemode.VehicleData;

/**
 * ISafeModeCallback — client-side callback interface.
 *
 * App clients (via safemode_library) implement this interface and register it
 * with ISafeModeService. The service calls onVehicleDataChanged() every time
 * any of the subscribed VHAL properties changes.
 *
 * ─── WHY oneway? ────────────────────────────────────────────────────────────
 * The "oneway" keyword makes EVERY method in this interface non-blocking.
 * Without it:
 *   - The service thread that calls cb->onVehicleDataChanged() would BLOCK
 *     waiting for the client to return from the callback.
 *   - If the client is slow (doing UI work) or dead, the service hangs.
 *   - With multiple clients, one slow client delays ALL others.
 *
 * With oneway:
 *   - The call is fire-and-forget. Service enqueues the call in the client's
 *     Binder thread pool and returns immediately.
 *   - The client processes it asynchronously on its own thread.
 *   - A dead client does NOT block the service.
 *
 * RESTRICTION: oneway methods cannot have return values (return type must be
 * void). That is why this is a separate callback interface and not a method
 * on ISafeModeService itself.
 * ─────────────────────────────────────────────────────────────────────────────
 */
oneway interface ISafeModeCallback {

    /**
     * Called by the service whenever vehicle data changes.
     *
     * Triggered by ANY of: speed change, gear change, fuel level change.
     * The full VehicleData snapshot is provided every time — the client does
     * not need to track deltas.
     *
     * Threading: delivered on a Binder thread in the client process.
     * The safemode_library will re-dispatch this onto the main thread before
     * calling the app's SafeModeListener.
     *
     * @param data  current snapshot of all vehicle properties
     */
    void onVehicleDataChanged(in VehicleData data);
}
