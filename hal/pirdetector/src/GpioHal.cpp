// Copyright (C) 2025 MyOEM
// SPDX-License-Identifier: Apache-2.0

// ─── LOG_TAG must be defined before ANY #include ──────────────────────────────
// AOSP log macros (ALOGI/ALOGE/ALOGD) use LOG_TAG at compile time.
// Defining it after any header that includes <log/log.h> causes a redefinition
// warning or picks up the wrong tag.
#define LOG_TAG "GpioHal"

#include "pirdetector/GpioHal.h"

// Standard POSIX / Linux headers — available in every Android build
#include <errno.h>
#include <fcntl.h>      // O_RDONLY, O_CLOEXEC, pipe2()
#include <poll.h>       // poll(), struct pollfd, POLLIN
#include <string.h>     // strerror()
#include <unistd.h>     // read(), write(), close()
#include <sys/ioctl.h>  // ioctl()

// Linux GPIO UAPI — always available via the kernel headers in the Android build.
// These are the V1 character-device ioctl structs:
//   gpioevent_request  — passed to GPIO_GET_LINEEVENT_IOCTL to request an event fd
//   gpioevent_data     — read() from the event fd to get edge direction + timestamp
#include <linux/gpio.h>

// Android logcat
#include <log/log.h>

namespace myoem::pirdetector {

// ─────────────────────────────────────────────────────────────────────────────
// Destructor — close all fds on destruction
// ─────────────────────────────────────────────────────────────────────────────
GpioHal::~GpioHal() {
    closeFds();
}

// ─────────────────────────────────────────────────────────────────────────────
// init() — open gpiochipN and request the line for edge-event monitoring
//
// Step-by-step:
//   1. Create the self-pipe for shutdown signalling (must be before any failure
//      path that calls closeFds, so fds are in a known state).
//   2. Open the gpiochip character device (O_RDONLY is sufficient for ioctls).
//   3. Call GPIO_GET_LINEEVENT_IOCTL — kernel validates the line, configures the
//      interrupt, and returns a new file descriptor (req.fd) that supports poll().
//   4. Close chipFd — it is no longer needed after the ioctl.
//   5. Store req.fd as mEventFd for use in waitForEdge().
// ─────────────────────────────────────────────────────────────────────────────
bool GpioHal::init(const std::string& chipPath, int lineOffset) {
    // Step 1: self-pipe for clean shutdown
    // O_CLOEXEC: close the pipe fds on exec() — avoids leaking into child processes.
    if (pipe2(mShutdownPipe, O_CLOEXEC) != 0) {
        ALOGE("pipe2() failed: %s", strerror(errno));
        return false;
    }

    // Step 2: open the gpiochip character device
    // O_RDONLY is sufficient — GPIO_GET_LINEEVENT_IOCTL only needs the device open,
    // it does not require write permissions on the chip itself.
    // O_CLOEXEC: do not leak this fd into child processes.
    int chipFd = open(chipPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (chipFd < 0) {
        ALOGE("open(%s) failed: %s", chipPath.c_str(), strerror(errno));
        closeFds();
        return false;
    }

    // Step 3: request the GPIO line for edge events (Linux GPIO V1 API)
    //
    // struct gpioevent_request fields:
    //   lineoffset:     the GPIO line number (BCM numbering on RPi5)
    //   handleflags:    GPIOHANDLE_REQUEST_INPUT — configure the pin as input
    //   eventflags:     GPIOEVENT_REQUEST_BOTH_EDGES — interrupt on rising AND falling
    //   consumer_label: string tag visible in /sys/kernel/debug/gpio (for debugging)
    //   fd:             (output) event fd returned by the kernel after the ioctl
    //
    // After this ioctl:
    //   - The kernel has configured the GPIO interrupt for both edges.
    //   - req.fd is a new file descriptor. poll(POLLIN) on it blocks until the
    //     next edge. read() on it returns a gpioevent_data struct with timestamp
    //     and edge direction.
    struct gpioevent_request req = {};
    req.lineoffset  = static_cast<uint32_t>(lineOffset);
    req.handleflags = GPIOHANDLE_REQUEST_INPUT;
    req.eventflags  = GPIOEVENT_REQUEST_BOTH_EDGES;
    strncpy(req.consumer_label, "pirdetectord", sizeof(req.consumer_label) - 1);

    if (ioctl(chipFd, GPIO_GET_LINEEVENT_IOCTL, &req) != 0) {
        ALOGE("GPIO_GET_LINEEVENT_IOCTL failed on %s line %d: %s",
              chipPath.c_str(), lineOffset, strerror(errno));
        close(chipFd);
        closeFds();
        return false;
    }

    // Step 4: chipFd is no longer needed — the kernel gave us req.fd
    close(chipFd);

    // Step 5: store the event fd
    mEventFd = req.fd;

    ALOGI("GpioHal initialized: chip=%s  line=%d  event_fd=%d",
          chipPath.c_str(), lineOffset, mEventFd);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// waitForEdge() — block until a GPIO edge fires or shutdown() is called
//
// poll() multiplexes two file descriptors simultaneously:
//
//   fds[0] = mEventFd (GPIO event fd)
//     POLLIN becomes ready when the kernel detects a voltage edge on the pin.
//     We then read() a gpioevent_data struct which contains:
//       .timestamp — monotonic clock, nanoseconds (from the interrupt handler)
//       .id        — GPIOEVENT_EVENT_RISING_EDGE or GPIOEVENT_EVENT_FALLING_EDGE
//
//   fds[1] = mShutdownPipe[0] (pipe read end)
//     POLLIN becomes ready when shutdown() writes one byte to the write end.
//     This is the self-pipe trick — a portable way to interrupt poll() from
//     another thread without using signals.
//
// timeout = -1: block indefinitely. poll() returns only when at least one fd
// is ready or a signal interrupts it (EINTR).
// ─────────────────────────────────────────────────────────────────────────────
bool GpioHal::waitForEdge(GpioEvent& outEvent) {
    struct pollfd fds[2];
    fds[0].fd      = mEventFd;
    fds[0].events  = POLLIN;
    fds[0].revents = 0;
    fds[1].fd      = mShutdownPipe[0];
    fds[1].events  = POLLIN;
    fds[1].revents = 0;

    int ret = poll(fds, 2, -1 /* block forever */);

    if (ret < 0) {
        if (errno == EINTR) {
            // Interrupted by signal — treat as a soft stop (caller should retry
            // or exit, depending on their loop condition).
            ALOGD("poll() interrupted by signal");
        } else {
            ALOGE("poll() failed: %s", strerror(errno));
        }
        return false;
    }

    // Check shutdown pipe first — if both are ready, honour shutdown.
    if (fds[1].revents & POLLIN) {
        ALOGI("GpioHal::waitForEdge: shutdown signal received, exiting");
        return false;
    }

    // GPIO edge event ready
    if (fds[0].revents & POLLIN) {
        struct gpioevent_data event = {};
        ssize_t n = read(mEventFd, &event, sizeof(event));

        if (n < 0) {
            ALOGE("read(event_fd) failed: %s", strerror(errno));
            return false;
        }
        if (n != static_cast<ssize_t>(sizeof(event))) {
            ALOGE("read(event_fd) short read: got %zd, expected %zu", n, sizeof(event));
            return false;
        }

        outEvent.timestampNs = static_cast<int64_t>(event.timestamp);
        outEvent.edge = (event.id == GPIOEVENT_EVENT_RISING_EDGE)
                        ? EdgeType::RISING
                        : EdgeType::FALLING;

        ALOGD("GpioHal: edge=%s  ts=%lld ns",
              (outEvent.edge == EdgeType::RISING) ? "RISING" : "FALLING",
              static_cast<long long>(outEvent.timestampNs));
        return true;
    }

    // poll() returned > 0 but neither fd matched — shouldn't happen
    ALOGW("poll() returned but no expected fd is ready (revents: gpio=0x%x pipe=0x%x)",
          fds[0].revents, fds[1].revents);
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// shutdown() — wake waitForEdge() from any thread
//
// Writes one byte to the write end of the self-pipe. poll() in waitForEdge()
// detects POLLIN on mShutdownPipe[0] and returns false, causing the EventThread
// to exit its loop cleanly.
//
// Thread safety: write() of ≤ PIPE_BUF bytes is atomic on Linux — safe to call
// from any thread while waitForEdge() blocks on another thread.
// ─────────────────────────────────────────────────────────────────────────────
void GpioHal::shutdown() {
    if (mShutdownPipe[1] >= 0) {
        uint8_t byte = 1;
        ssize_t written = write(mShutdownPipe[1], &byte, sizeof(byte));
        if (written < 0) {
            ALOGE("shutdown() write to pipe failed: %s", strerror(errno));
        } else {
            ALOGI("GpioHal::shutdown() called — EventThread will exit");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// closeFds() — close all owned file descriptors, reset to sentinel -1
// ─────────────────────────────────────────────────────────────────────────────
void GpioHal::closeFds() {
    if (mEventFd >= 0) {
        close(mEventFd);
        mEventFd = -1;
    }
    if (mShutdownPipe[0] >= 0) {
        close(mShutdownPipe[0]);
        mShutdownPipe[0] = -1;
    }
    if (mShutdownPipe[1] >= 0) {
        close(mShutdownPipe[1]);
        mShutdownPipe[1] = -1;
    }
}

}  // namespace myoem::pirdetector
