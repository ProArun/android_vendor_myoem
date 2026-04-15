// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

package com.myoem.pirdetector;

import com.myoem.pirdetector.MotionEvent;

/**
 * IPirDetectorCallback — client-side callback interface.
 *
 * Implemented by the Java manager (PirDetectorManager) and registered with
 * IPirDetectorService. The service calls onMotionEvent() on every GPIO edge
 * interrupt — both rising (motion starts) and falling (motion ends).
 *
 * ── WHY oneway? ───────────────────────────────────────────────────────────────
 * The service fires this callback from the EventThread — the same thread that
 * calls GpioHal::waitForEdge() and then loops back to wait for the next edge.
 *
 * Without "oneway":
 *   - The EventThread BLOCKS waiting for the client to return from the callback.
 *   - If the client is slow (e.g., doing UI work on a binder thread), the service
 *     misses the next hardware edge while waiting.
 *   - A dead client process causes the call to block until the binder timeout
 *     fires (~30s), hanging the EventThread for that entire duration.
 *   - With multiple registered clients, one slow or dead client delays ALL others.
 *
 * With "oneway":
 *   - The call is fire-and-forget: the Binder runtime enqueues the invocation in
 *     the client's thread pool and returns to the service immediately.
 *   - The EventThread is free to loop back to waitForEdge() right away.
 *   - Dead clients do not block the service (the Binder runtime handles the error).
 *   - The service can serve many clients with no head-of-line blocking.
 *
 * RESTRICTION: oneway methods must return void and cannot use [out] parameters.
 * That is why this is a separate callback interface rather than a return value
 * on IPirDetectorService.
 *
 * ── Threading in the client ───────────────────────────────────────────────────
 * onMotionEvent() is invoked on a Binder thread in the client's process —
 * NOT on the main thread. The PirDetectorManager re-dispatches the event to
 * the main (UI) thread via a Handler before calling the app's MotionListener.
 * The app should never do heavy work directly in onMotionEvent().
 */
@VintfStability
oneway interface IPirDetectorCallback {

    /**
     * Called by pirdetectord on every GPIO edge interrupt.
     *
     * Triggered on BOTH rising (motion detected) and falling (motion ended) edges.
     * The full MotionEvent is delivered each time — no delta tracking needed.
     *
     * Threading: invoked on a Binder thread in the client process.
     *            PirDetectorManager dispatches to main thread before calling
     *            the app's MotionListener.
     *
     * @param event  GPIO edge data: state (0 or 1) and kernel timestamp (ns)
     */
    void onMotionEvent(in MotionEvent event);
}
