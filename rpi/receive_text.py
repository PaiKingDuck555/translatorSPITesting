#!/usr/bin/env python3
"""Receive text from STM32 over SPI. Dead simple test."""

import spidev
import time

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

print("SPI receiver ready. Waiting for message...")

try:
    while True:
        # Poll for length byte (non-0xFF means data incoming)
        resp = spi.xfer2([0x00])[0]

        if resp == 0xFF:
            time.sleep(0.01)  # idle, wait a bit
            continue

        # Got length byte
        length = resp
        print(f"Incoming: {length} bytes")

        # Read that many bytes
        data = spi.xfer2([0x00] * length)
        message = bytes(data).decode('ascii', errors='ignore')
        print(f"Message: {message}")

except KeyboardInterrupt:
    print("\nStopped.")
finally:
    spi.close()
