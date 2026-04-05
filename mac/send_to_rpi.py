#!/usr/bin/env python3
"""Send a WAV file from Mac to RPi via STM32 UART-to-SPI bridge.

Usage:
    python3 send_to_rpi.py out/rec_20260405_143022_0001.wav
    python3 send_to_rpi.py                # sends the latest file in out/
"""

import serial
import serial.tools.list_ports
import sys
import os
import glob

OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'out')

# --- Find WAV file to send ---
if len(sys.argv) > 1 and not sys.argv[1].startswith('/dev/'):
    wav_path = sys.argv[1]
else:
    # Pick the latest WAV in out/
    wavs = sorted(glob.glob(os.path.join(OUT_DIR, '*.wav')))
    if not wavs:
        print(f"No WAV files found in {OUT_DIR}")
        sys.exit(1)
    wav_path = wavs[-1]

if not os.path.isfile(wav_path):
    print(f"File not found: {wav_path}")
    sys.exit(1)

file_size = os.path.getsize(wav_path)
print(f"File: {wav_path}")
print(f"Size: {file_size} bytes ({file_size/1024:.1f} KB)")

# --- Find STM32 serial port ---
ports = list(serial.tools.list_ports.comports())
stm_port = None
for p in ports:
    if 'usbmodem' in p.device or 'STLink' in (p.description or '') or 'STM' in (p.description or ''):
        stm_port = p.device
        break

if not stm_port:
    print("\nAvailable ports:")
    for p in ports:
        print(f"  {p.device} — {p.description}")
    # Check if port was passed as last argument
    if len(sys.argv) > 1 and sys.argv[-1].startswith('/dev/'):
        stm_port = sys.argv[-1]
    else:
        print(f"\nSTM32 not auto-detected. Pass port as last argument:")
        print(f"  python3 {sys.argv[0]} file.wav /dev/cu.usbmodemXXXX")
        sys.exit(1)

print(f"\nConnecting to {stm_port}...")
ser = serial.Serial(stm_port, 460800, timeout=10)

# Drain any pending output
ser.reset_input_buffer()

# --- Send SEND:<size> command ---
cmd = f"SEND:{file_size}\r\n"
print(f"Sending: {cmd.strip()}")
ser.write(cmd.encode('ascii'))

# Wait for ACK
resp = ser.readline().decode('ascii', errors='ignore').strip()
print(f"STM32: {resp}")

if resp != "ACK":
    print(f"ERROR: Expected ACK, got '{resp}'")
    ser.close()
    sys.exit(1)

# --- Send WAV file data ---
print(f"Sending {file_size} bytes...")

with open(wav_path, 'rb') as f:
    sent = 0
    chunk_size = 1024
    while sent < file_size:
        chunk = f.read(min(chunk_size, file_size - sent))
        ser.write(chunk)
        sent += len(chunk)
        pct = sent * 100 // file_size
        print(f"\r  Progress: {sent}/{file_size} ({pct}%)", end='', flush=True)

print()

# Wait for OK
resp = ser.readline().decode('ascii', errors='ignore').strip()
print(f"STM32: {resp}")

if resp == "OK":
    print("Transfer complete!")
else:
    print(f"WARNING: Expected OK, got '{resp}'")

ser.close()
