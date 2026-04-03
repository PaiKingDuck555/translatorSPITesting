import spidev
import time
import wave
import array
import sounddevice as sd

# --- SPI Setup ---
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

SAMPLE_RATE = 16000
RECORD_SECONDS = 5

# --- Step 1: Record audio from USB mic ---
audio = array.array('h')

def callback(indata, frames, time_info, status):
    audio.frombytes(indata)

stream = sd.RawInputStream(
    samplerate=SAMPLE_RATE,
    channels=1,
    dtype='int16',
    callback=callback
)

print(f"Recording {RECORD_SECONDS}s from USB mic...")
with stream:
    time.sleep(RECORD_SECONDS)

audio_bytes = audio.tobytes()
print(f"Recorded {len(audio)} samples ({len(audio_bytes)} bytes)")

# --- Step 2: Send audio over SPI, get echo back ---
print(f"Sending to STM32...")

received_bytes = bytearray()
total_errors = 0
prev_sent = None

start = time.perf_counter()

for i, byte_val in enumerate(audio_bytes):
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

for path, data in [(original_path, audio_bytes), (echo_path, received_shifted)]:
    with wave.open(path, 'w') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(data)

# --- Results ---
print(f"\n{'='*50}")
print(f"SPI Transfer: {elapsed:.1f}s for {RECORD_SECONDS}s of audio")
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
