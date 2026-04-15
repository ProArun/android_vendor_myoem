// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

namespace myoem::pirdetector {

/**
 * EdgeType — direction of the GPIO voltage transition.
 *
 * RISING:  GPIO line went 0V → 3.3V  (HC-SR501 OUT asserted HIGH = motion detected)
 * FALLING: GPIO line went 3.3V → 0V  (HC-SR501 OUT released LOW  = motion ended)
 */
enum class EdgeType {
    RISING,
    FALLING,
};

/**
 * GpioEvent — data delivered to the caller on each edge interrupt.
 *
 * edge:         which transition occurred (RISING or FALLING)
 * timestampNs:  monotonic kernel timestamp in nanoseconds, from the GPIO interrupt.
 *               Useful for latency measurement and event ordering in the app.
 */
struct GpioEvent {
    EdgeType edge;
    int64_t  timestampNs;
};

/**
 * GpioHal — wraps the Linux GPIO character-device ioctl API.
 *
 * ── Why character device, not sysfs? ──────────────────────────────────────────
 * /sys/class/gpio/ (the legacy sysfs API) is deprecated since kernel 4.8 and
 * does not expose edge-event file descriptors suitable for poll(). The character
 * device API (/dev/gpiochipN) was introduced in kernel 4.8 as the official
 * replacement. It gives us a real event fd that supports poll() — meaning the
 * daemon thread sleeps in the kernel scheduler until a hardware interrupt fires,
 * with zero busy-waiting.
 *
 * ── API overview ──────────────────────────────────────────────────────────────
 *  1. init()        — open /dev/gpiochipN, request the GPIO line for edge events.
 *                     Returns an event fd via GPIO_GET_LINEEVENT_IOCTL.
 *  2. waitForEdge() — blocks on poll(event_fd | shutdown_pipe_read) until either
 *                     a hardware edge fires or shutdown() is called.
 *  3. shutdown()    — writes one byte to the self-pipe write end, waking
 *                     waitForEdge() so the EventThread can exit cleanly.
 *
 * ── Self-pipe trick ──────────────────────────────────────────────────────────
 * poll() can only be interrupted by signals or a ready fd. To stop the
 * EventThread without SIGKILL, GpioHal maintains a pipe2(). waitForEdge()
 * monitors both the GPIO event fd AND the pipe read end. shutdown() writes one
 * byte to the pipe write end — poll() returns immediately and the thread exits.
 *
 * ── Thread safety ────────────────────────────────────────────────────────────
 * shutdown() is safe to call from any thread while waitForEdge() is blocking
 * on another thread. Internally it only writes one byte to a pipe (atomic for
 * writes ≤ PIPE_BUF on Linux).
 */
class GpioHal {
public:
    GpioHal() = default;

    // Non-copyable, non-movable — owns file descriptors
    GpioHal(const GpioHal&)            = delete;
    GpioHal& operator=(const GpioHal&) = delete;

    ~GpioHal();

    /**
     * Opens the GPIO character device and requests the given line for
     * both-edges event monitoring.
     *
     * @param chipPath    path to the gpiochip device, e.g. "/dev/gpiochip4"
     *                    On RPi5 with AOSP 15, the 40-pin header controller
     *                    (RP1 chip) is typically gpiochip4.
     *                    Verify with: adb shell cat /sys/bus/gpio/devices/gpiochip4/label
     *                    Expected output: "pinctrl-rp1"
     * @param lineOffset  GPIO line number (BCM numbering), e.g. 17 for GPIO17.
     * @return            true if initialization succeeded, false otherwise.
     */
    bool init(const std::string& chipPath, int lineOffset);

    /**
     * Blocks until a RISING or FALLING edge event occurs on the GPIO line,
     * or until shutdown() is called.
     *
     * This method calls poll() and sleeps in the kernel scheduler —
     * zero CPU usage while idle.
     *
     * @param outEvent  filled with edge direction and kernel timestamp on success.
     * @return          true  if a real GPIO edge was detected (outEvent is valid).
     *                  false if shutdown() was called or a fatal poll error occurred
     *                        (caller should exit the loop).
     */
    bool waitForEdge(GpioEvent& outEvent);

    /**
     * Signals waitForEdge() to return false immediately.
     * Thread-safe. Call from the service destructor to stop the EventThread.
     */
    void shutdown();

private:
    // Event fd returned by GPIO_GET_LINEEVENT_IOCTL.
    // poll() on this fd blocks until a hardware edge interrupt fires.
    int mEventFd = -1;

    // Self-pipe: mShutdownPipe[0]=read end, mShutdownPipe[1]=write end.
    // shutdown() writes one byte to [1]; waitForEdge() polls [0].
    int mShutdownPipe[2] = {-1, -1};

    void closeFds();
};

}  // namespace myoem::pirdetector
