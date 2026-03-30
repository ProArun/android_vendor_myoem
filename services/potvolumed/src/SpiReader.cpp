// LOG_TAG must be #defined FIRST — before any #include — because log/log.h
// uses it as a string literal in every ALOGI/ALOGE macro expansion.
// If you put it after the includes, you get "LOG_TAG redefined" warnings.
#define LOG_TAG "potvolumed"

// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0

#include "SpiReader.h"

#include <fcntl.h>                  // open(), O_RDWR
#include <unistd.h>                 // close()
#include <sys/ioctl.h>              // ioctl()
#include <linux/spi/spidev.h>       // SPI_IOC_* constants and spi_ioc_transfer struct
#include <cerrno>                   // errno
#include <cstring>                  // strerror()
#include <log/log.h>                // ALOGI, ALOGE, ALOGD

// SPI bus configuration constants for MCP3008
//   Speed: 1 MHz — well within MCP3008's 3.6 MHz max at 3.3V
//   Mode:  SPI_MODE_0 — clock idle LOW, data sampled on rising edge (CPOL=0, CPHA=0)
//          The MCP3008 datasheet specifies Mode 0,0 or Mode 1,1. We use Mode 0.
//   Bits:  8 bits per word — standard byte-oriented transfer
static constexpr uint32_t kSpiSpeedHz    = 1'000'000;  // 1 MHz
static constexpr uint8_t  kSpiMode       = SPI_MODE_0;
static constexpr uint8_t  kSpiBitsPerWord = 8;

SpiReader::SpiReader(const std::string& device, int channel)
    : mDevice(device), mChannel(channel) {}

SpiReader::~SpiReader() {
    close();
}

bool SpiReader::open() {
    // O_RDWR: we need both read (to receive MISO data) and write (to send MOSI data)
    mFd = ::open(mDevice.c_str(), O_RDWR);
    if (mFd < 0) {
        ALOGE("open(%s) failed: %s — is spidev enabled? check /boot/config.txt",
              mDevice.c_str(), strerror(errno));
        return false;
    }

    // SPI_IOC_WR_MODE: set the SPI mode (clock polarity + phase).
    // Must be set before any transfers or the kernel uses an undefined default.
    if (ioctl(mFd, SPI_IOC_WR_MODE, &kSpiMode) < 0) {
        ALOGE("SPI_IOC_WR_MODE failed: %s", strerror(errno));
        ::close(mFd); mFd = -1;
        return false;
    }

    // SPI_IOC_WR_BITS_PER_WORD: each word in the transfer buffer is 8 bits.
    // This must match the MCP3008 protocol which works byte-by-byte.
    if (ioctl(mFd, SPI_IOC_WR_BITS_PER_WORD, &kSpiBitsPerWord) < 0) {
        ALOGE("SPI_IOC_WR_BITS_PER_WORD failed: %s", strerror(errno));
        ::close(mFd); mFd = -1;
        return false;
    }

    // SPI_IOC_WR_MAX_SPEED_HZ: sets the maximum clock frequency.
    // The kernel SPI driver clocks at this speed or slower (it may pick the
    // nearest lower frequency the hardware supports).
    if (ioctl(mFd, SPI_IOC_WR_MAX_SPEED_HZ, &kSpiSpeedHz) < 0) {
        ALOGE("SPI_IOC_WR_MAX_SPEED_HZ failed: %s", strerror(errno));
        ::close(mFd); mFd = -1;
        return false;
    }

    ALOGI("SPI device %s opened: mode=%d bits=%d speed=%uHz",
          mDevice.c_str(), kSpiMode, kSpiBitsPerWord, kSpiSpeedHz);
    return true;
}

void SpiReader::close() {
    if (mFd >= 0) {
        ::close(mFd);
        mFd = -1;
    }
}

int SpiReader::read() {
    if (mFd < 0) {
        ALOGE("read() called but SPI device is not open");
        return -1;
    }

    // Build the 3-byte TX (transmit) buffer for MCP3008.
    //
    // Byte 0: 0x01
    //   The MCP3008 ignores all incoming bits until it sees a '1' (start bit).
    //   Sending 0x01 as the first byte puts the start bit in the LSB of the
    //   first byte; by the time the clock cycles through byte 1, the MCP3008
    //   is ready to receive the configuration.
    //
    // Byte 1: 0x80 | (channel << 4)
    //   Bit layout sent to MCP3008: [SGL/DIFF | D2 | D1 | D0 | X | X | X | X]
    //   SGL=1 means single-ended (read channel vs GND, not differential).
    //   D2:D0 select which of the 8 channels to sample.
    //   The 4 LSBs are don't-care (X) — they just clock out the result.
    //   CH0: 1 000 xxxx = 0x80
    //   CH1: 1 001 xxxx = 0x90
    //   CH2: 1 010 xxxx = 0xA0  ... etc.
    //
    // Byte 2: 0x00
    //   Pure padding. The master keeps clocking so the MCP3008 can shift out
    //   the remaining 8 bits of the ADC result on MISO. TX value doesn't matter.
    uint8_t tx[3] = {
        0x01,
        static_cast<uint8_t>(0x80 | ((mChannel & 0x07) << 4)),
        0x00
    };
    uint8_t rx[3] = {0x00, 0x00, 0x00};

    // spi_ioc_transfer describes a single SPI transfer segment.
    // We zero-initialize with {} so unset fields (cs_change, delay_usecs, etc.)
    // default to 0, which means: no chip-select toggle mid-transfer, no delay.
    struct spi_ioc_transfer tr{};
    tr.tx_buf        = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf        = reinterpret_cast<unsigned long>(rx);
    tr.len           = sizeof(tx);          // 3 bytes
    tr.speed_hz      = kSpiSpeedHz;
    tr.bits_per_word = kSpiBitsPerWord;
    tr.delay_usecs   = 0;

    // SPI_IOC_MESSAGE(1): perform 1 transfer segment.
    // The kernel asserts CS, clocks all 3 bytes, then de-asserts CS.
    if (ioctl(mFd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        ALOGE("SPI_IOC_MESSAGE ioctl failed: %s", strerror(errno));
        return -1;
    }

    // Decode the MCP3008 response:
    //   rx[0]: received while we were sending 0x01 — MCP3008 output is undefined
    //          here (it hasn't seen the start bit yet). Discard.
    //   rx[1]: received while we sent the channel byte.
    //          MCP3008 starts responding. Bits [1:0] are ADC result bits [9:8].
    //          Bits [7:2] are 0 (null bit + leading zeros from MCP3008).
    //   rx[2]: received while we sent 0x00.
    //          MCP3008 shifts out ADC result bits [7:0].
    //
    //   value = bits[9:8] concatenated with bits[7:0]
    //         = ((rx[1] & 0x03) << 8) | rx[2]
    //   Range: 0 (knob fully counterclockwise / 0V) to 1023 (fully clockwise / 3.3V)
    int value = ((rx[1] & 0x03) << 8) | rx[2];

    ALOGV("SPI raw: tx=[%02X %02X %02X] rx=[%02X %02X %02X] value=%d",
          tx[0], tx[1], tx[2], rx[0], rx[1], rx[2], value);

    return value;
}
