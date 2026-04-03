import spidev
import time
import wave
import array

# --- SPI Setup ---
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000  # 1 MHz
spi.mode = 0

SAMPLE_RATE = 16000
RECORD_SECONDS = 60
CHUNK_SIZE = 4096

# --- Step 1: Record audio from USB mic ---
import sounddevice as sd

audio = array.array('h')  # 'h' = signed 16-bit int

def callback(indata, frames, time_info, status):
    audio.frombytes(indata)

stream = sd.RawInputStream(
    samplerate=SAMPLE_RATE,
    channels=1,
    dtype='int16',
    callback=callback
)

print(f"Recording {RECORD_SECONDS}s of audio from USB mic...")
with stream:
    time.sleep(RECORD_SECONDS)

print(f"Recorded {len(audio)} samples ({len(audio) * 2} bytes)")

audio_bytes = audio.tobytes()

# --- Step 2: Send audio byte-by-byte over SPI, get echo back ---
print(f"Sending {len(audio_bytes)} bytes over SPI to STM32 (one byte at a time)...")

received_bytes = bytearray()
total_errors = 0
prev_sent = None

start = time.perf_counter()

for i, byte in enumerate(audio_bytes):
    resp = spi.xfer2([byte])
    received_bytes.append(resp[0])

    # Check echo accuracy (response is previous byte)
    if prev_sent is not None and resp[0] != prev_sent:
        total_errors += 1

    prev_sent = byte

    # Status every 50000 bytes
    if (i + 1) % 50000 == 0:
        elapsed = time.perf_counter() - start
        error_pct = (total_errors / (i + 1)) * 100
        print(f"  {elapsed:5.1f}s | Sent: {i+1:>8}/{len(audio_bytes)} | Errors: {total_errors:>6} | Accuracy: {100 - error_pct:.2f}%")

elapsed_spi = time.perf_counter() - start
error_pct = (total_errors / len(audio_bytes)) * 100
throughput = (len(audio_bytes) * 8) / elapsed_spi / 1000

spi.close()

print(f"\n{'='*50}")
print(f"SPI Transfer Complete")
print(f"  Duration:   {elapsed_spi:.1f}s")
print(f"  Bytes:      {len(audio_bytes)}")
print(f"  Errors:     {total_errors}")
print(f"  Accuracy:   {100 - error_pct:.4f}%")
print(f"  Throughput: {throughput:.0f} kbps")
print(f"{'='*50}")

# --- Step 3: Reconstruct audio from echoed bytes ---
# Shift by 1 (first response was preloaded 0xAA)
received_shifted = bytes(received_bytes[1:]) + b'\x00'

# --- Step 4: Save both original and echoed audio to WAV ---
original_path = '/tmp/original_audio.wav'
echo_path = '/tmp/echo_audio.wav'

for path, data in [(original_path, audio_bytes), (echo_path, received_shifted)]:
    with wave.open(path, 'w') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        wf.writeframes(data)

print(f"\nSaved original: {original_path}")
print(f"Saved echo:     {echo_path}")

# --- Step 5: Transcribe both with Whisper ---
print("\nLoading Whisper model...")
import whisper

model = whisper.load_model("base")

print("Transcribing original audio...")
result_original = model.transcribe(original_path)

print("Transcribing echoed audio...")
result_echo = model.transcribe(echo_path)

print(f"\n{'='*50}")
print(f"ORIGINAL:  {result_original['text']}")
print(f"{'='*50}")
print(f"ECHO:      {result_echo['text']}")
print(f"{'='*50}")
print(f"\nMatch: {'YES' if result_original['text'] == result_echo['text'] else 'NO'}")
print(f"SPI round-trip: {elapsed_spi:.1f}s for {RECORD_SECONDS}s of audio")
print(f"Real-time factor: {elapsed_spi / RECORD_SECONDS:.1f}x")
