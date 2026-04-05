#!/usr/bin/env python3
"""Receive WAV files from STM32 over SPI.

The STM32 acts as a UART-to-SPI bridge. This script runs on the RPi
as SPI master, continuously polling for incoming data.

Protocol:
    Idle:  STM32 responds 0xFF
    Sync:  0xAA 0x55 0xAA 0x55
    Size:  4 bytes big-endian
    Data:  <size> bytes of raw WAV file

Usage:
    python3 receive_wav.py              # saves to current directory
    python3 receive_wav.py /path/to/dir # saves to specified directory
"""

import spidev
import struct
import sys
import os
import time
from datetime import datetime

OUT_DIR = sys.argv[1] if len(sys.argv) > 1 else os.getcwd()
os.makedirs(OUT_DIR, exist_ok=True)

SPI_SPEED = 1000000  # 1 MHz
SPI_MODE = 0         # CPOL=0, CPHA=0

spi = spidev.SpiDev()
spi.open(0, 0)       # bus 0, device 0
spi.max_speed_hz = SPI_SPEED
spi.mode = SPI_MODE

SYNC_PATTERN = [0xAA, 0x55, 0xAA, 0x55]

print(f"SPI receiver ready. Polling at {SPI_SPEED/1e6:.0f} MHz...")
print(f"WAV files will be saved to: {OUT_DIR}")
print("Waiting for data from STM32...\n")

recording_num = 0

def spi_read_byte():
    """Clock one byte from SPI slave."""
    return spi.xfer2([0x00])[0]

def wait_for_sync():
    """Poll until we see the 0xAA 0x55 0xAA 0x55 sync pattern."""
    match_idx = 0
    while True:
        b = spi_read_byte()
        if b == SYNC_PATTERN[match_idx]:
            match_idx += 1
            if match_idx == len(SYNC_PATTERN):
                return True
        elif b == SYNC_PATTERN[0]:
            match_idx = 1
        else:
            match_idx = 0
        # Small sleep during idle to reduce CPU usage
        if match_idx == 0 and b == 0xFF:
            time.sleep(0.001)

try:
    while True:
        # Wait for sync marker
        wait_for_sync()
        print("Sync detected!")

        # Read 4-byte file size (big-endian)
        size_bytes = spi.xfer2([0x00] * 4)
        file_size = struct.unpack('>I', bytes(size_bytes))[0]
        print(f"  File size: {file_size} bytes ({file_size/1024:.1f} KB)")

        if file_size == 0 or file_size > 10 * 1024 * 1024:  # sanity check: max 10MB
            print(f"  ERROR: invalid file size {file_size}, skipping")
            continue

        # Read file data in chunks
        data = bytearray()
        chunk_size = 4096
        remaining = file_size

        while remaining > 0:
            n = min(chunk_size, remaining)
            chunk = spi.xfer2([0x00] * n)
            data.extend(chunk)
            remaining -= n
            pct = (file_size - remaining) * 100 // file_size
            print(f"\r  Receiving: {file_size - remaining}/{file_size} ({pct}%)", end='', flush=True)

        print()

        # Save WAV file
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        recording_num += 1
        filename = f"recv_{timestamp}_{recording_num:04d}.wav"
        filepath = os.path.join(OUT_DIR, filename)

        with open(filepath, 'wb') as f:
            f.write(data)

        print(f"  Saved: {filepath}")
        print(f"  Play:  aplay {filepath}")
        print("Waiting for next transfer...\n")

except KeyboardInterrupt:
    print("\nStopped.")
finally:
    spi.close()
