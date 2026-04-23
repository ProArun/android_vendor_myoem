// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

// LOG_TAG must be defined before any #include so log macros pick it up.
#define LOG_TAG "HelloWorldDaemon"

#include <log/log.h>
#include <unistd.h>

static constexpr unsigned int kPrintIntervalSeconds = 30;

int main() {
    ALOGI("HelloWorld daemon started");

    while (true) {
        ALOGI("Hello World");
        sleep(kPrintIntervalSeconds);
    }

    return 0;
}
