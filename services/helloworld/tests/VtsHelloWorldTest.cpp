// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

// LOG_TAG before any #include — AOSP rule.
#define LOG_TAG "VtsHelloWorldTest"

#include <gtest/gtest.h>
#include <log/log.h>

#include <cstdio>
#include <string>
#include <unistd.h>

// Run a shell command and return its stdout.
static std::string shellOutput(const char* cmd) {
    FILE* pipe = popen(cmd, "r");
    if (!pipe) return {};
    char buf[256];
    std::string result;
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    return result;
}

// ── Test 1: process is alive ──────────────────────────────────────────────────
//
// pidof returns the PID (non-empty) when the process is running, empty if not.
// This confirms the RC file was processed and init launched the daemon.
//
TEST(HelloWorldDaemonTest, ProcessIsRunning) {
    std::string pid = shellOutput("pidof helloworldd");

    // Strip trailing newline for a clean failure message.
    if (!pid.empty() && pid.back() == '\n') pid.pop_back();

    EXPECT_FALSE(pid.empty()) << "helloworldd is not running — check RC file and SELinux denials";
    ALOGD("helloworldd PID: %s", pid.c_str());
}

// ── Test 2: "Hello World" appears in logcat ───────────────────────────────────
//
// The daemon logs once at startup and then every 30 seconds.
// We poll logcat for up to 35 seconds so the test passes even if it starts
// just after a cycle boundary.
//
// Why 35s and not 30s? Gives a one-second buffer for scheduling jitter.
//
TEST(HelloWorldDaemonTest, LogsHelloWorldMessage) {
    constexpr int kMaxWaitSeconds    = 35;
    constexpr int kPollIntervalSeconds = 2;

    for (int waited = 0; waited <= kMaxWaitSeconds; waited += kPollIntervalSeconds) {
        // -d  : dump and exit (don't follow)
        // -s HelloWorldDaemon:I : only lines from our LOG_TAG at Info level
        std::string output = shellOutput("logcat -d -s HelloWorldDaemon:I 2>/dev/null");

        if (output.find("Hello World") != std::string::npos) {
            ALOGD("Found 'Hello World' in logcat after %d seconds", waited);
            SUCCEED();
            return;
        }

        if (waited < kMaxWaitSeconds) {
            sleep(kPollIntervalSeconds);
        }
    }

    FAIL() << "'Hello World' not found in logcat after " << kMaxWaitSeconds
           << " seconds. Run: adb logcat -s HelloWorldDaemon:* to debug.";
}
