// Copyright (C) 2024 MyOEM
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

// SpiReader — wraps the Linux spidev ioctl interface to read from an MCP3008
// Analog-to-Digital Converter.
//
// HOW SPI WORKS (brief):
//   SPI (Serial Peripheral Interface) is a synchronous full-duplex bus.
//   The master (RPi5) drives a clock line (SCLK). For every clock pulse,
//   one bit goes out on MOSI and one bit comes in on MISO simultaneously.
//   CS (Chip Select) must be pulled LOW to select the MCP3008 for the
//   duration of the transaction, then pulled HIGH again.
//
//   Linux exposes the SPI master as a character device: /dev/spidev0.0
//     - "0.0" means: bus 0, chip-select 0
//   We send/receive data using the SPI_IOC_MESSAGE ioctl.
//
// HOW MCP3008 READS WORK:
//   Each reading requires a 3-byte (24 clock cycle) transaction:
//
//   Byte sent [0]:  0x01          — start bit (the MCP3008 waits for this '1')
//   Byte sent [1]:  0x80 | (ch<<4) — SGL=1 (single-ended), channel select
//                   For CH0: 1000_0000b = 0x80
//                   For CH1: 1001_0000b = 0x90
//                   For CH2: 1010_0000b = 0xA0  ... etc.
//   Byte sent [2]:  0x00          — don't care, just clocking out the result
//
//   Byte received [1]: bits [1:0] contain ADC result bits [9:8] (the 2 MSBs)
//   Byte received [2]: bits [7:0] contain ADC result bits [7:0] (the 8 LSBs)
//
//   Final value = ((received[1] & 0x03) << 8) | received[2]
//   Range: 0 (0V) to 1023 (VREF = 3.3V)

class SpiReader {
public:
    // device: path to the spidev node, e.g. "/dev/spidev0.0"
    // channel: MCP3008 analog input channel, 0–7. We use CH0.
    SpiReader(const std::string& device, int channel);
    ~SpiReader();

    // open() configures the SPI bus parameters and opens the device file.
    // Must be called once before read(). Returns true on success.
    bool open();

    void close();

    // read() sends the 3-byte MCP3008 transaction and returns the 10-bit
    // ADC result (0–1023). Returns -1 on ioctl error.
    int read();

private:
    std::string mDevice;
    int         mChannel;
    int         mFd{-1};    // file descriptor; -1 = not open
};
