#!/usr/bin/env python3
"""Receives polling-mode audio from STM32 and saves timestamped WAV files to out/."""

import serial
import serial.tools.list_ports
import wave
import struct
import sys
import os
from datetime import datetime

SAMPLE_RATE = 16000
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'out')

# Find the STM32 serial port
ports = list(serial.tools.list_ports.comports())
stm_port = None
for p in ports:
    if 'usbmodem' in p.device or 'STLink' in (p.description or '') or 'STM' in (p.description or ''):
        stm_port = p.device
        break

if not stm_port:
    print("Available ports:")
    for p in ports:
        print(f"  {p.device} — {p.description}")
    print(f"\nSTM32 not auto-detected. Pass port as argument:")
    print(f"  python3 {sys.argv[0]} /dev/cu.usbmodemXXXX")
    if len(sys.argv) > 1:
        stm_port = sys.argv[1]
    else:
        sys.exit(1)

os.makedirs(OUT_DIR, exist_ok=True)

print(f"Connecting to {stm_port}...")
ser = serial.Serial(stm_port, 460800, timeout=30)

print(f"Listening for STM32 polling recordings...")
print(f"WAV files will be saved to: {OUT_DIR}\n")

recording_num = 0

while True:
    line = ser.readline().decode('ascii', errors='ignore').strip()
    if not line:
        continue
    print(f"STM32: {line}")

    if line.startswith("AUDIO:"):
        num_samples = int(line.split(":")[1])
        print(f"  Receiving {num_samples} samples ({num_samples/SAMPLE_RATE:.2f}s)...")

        raw = ser.read(num_samples * 2)
        if len(raw) != num_samples * 2:
            print(f"  ERROR: expected {num_samples*2} bytes, got {len(raw)}")
            continue

        done_line = ser.readline().decode('ascii', errors='ignore').strip()
        print(f"  STM32: {done_line}")

        # Convert 12-bit unsigned to 16-bit signed
        samples = struct.unpack(f'<{num_samples}H', raw)
        pcm16 = []
        for val in samples:
            signed_val = (val - 2048) * 16
            pcm16.append(max(-32768, min(32767, signed_val)))

        # Save with datetime filename
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        recording_num += 1
        filename = f"rec_{timestamp}_{recording_num:04d}.wav"
        filepath = os.path.join(OUT_DIR, filename)

        with wave.open(filepath, 'w') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(struct.pack(f'<{len(pcm16)}h', *pcm16))

        print(f"  Saved: {filepath}")
        print(f"  Play:  afplay {filepath}\n")
