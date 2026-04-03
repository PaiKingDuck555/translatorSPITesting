import spidev
import time
import wave
import os
import math

# --- SPI Setup ---
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

SAMPLE_RATE = 16000
DURATION = 5  # seconds
total_samples = SAMPLE_RATE * DURATION

# --- Step 1: Generate fake audio (440 Hz sine wave, 16-bit) ---
print(f"Generating {DURATION}s of fake audio (440 Hz sine wave)...")

audio_bytes = bytearray()
for i in range(total_samples):
    sample = int(16000 * math.sin(2 * math.pi * 440 * i / SAMPLE_RATE))
    audio_bytes += sample.to_bytes(2, byteorder='little', signed=True)

audio_list = list(audio_bytes)
print(f"Generated {len(audio_bytes)} bytes")

# --- Step 2: Send over SPI, get echo back ---
print(f"Sending to STM32...")

received_bytes = bytearray()
total_errors = 0
prev_sent = None

start = time.perf_counter()

for i, byte_val in enumerate(audio_list):
    resp = spi.xfer2([byte_val])
    received_bytes.append(resp[0])

    if prev_sent is not None and resp[0] != prev_sent:
        total_errors += 1
    prev_sent = byte_val

elapsed = time.perf_counter() - start
error_pct = (total_errors / len(audio_bytes)) * 100
throughput = (len(audio_bytes) * 8) / elapsed / 1000

spi.close()

# --- Step 3: Save original and echoed audio ---
received_shifted = bytes(received_bytes[1:]) + b'\x00'

original_path = '/tmp/original_audio.wav'
echo_path = '/tmp/echo_audio.wav'

for path, data in [(original_path, bytes(audio_bytes)), (echo_path, received_shifted)]:
    with wave.open(path, 'w') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(data)

# --- Results ---
print(f"\n{'='*50}")
print(f"SPI Transfer: {elapsed:.1f}s for {DURATION}s of audio")
print(f"Bytes:        {len(audio_bytes)}")
print(f"Errors:       {total_errors}")
print(f"Accuracy:     {100 - error_pct:.4f}%")
print(f"Throughput:   {throughput:.0f} kbps")
print(f"{'='*50}")
print(f"\nSaved: {original_path}")
print(f"Saved: {echo_path}")
print(f"\nPlay them to compare:")
print(f"  aplay {original_path}")
print(f"  aplay {echo_path}")
