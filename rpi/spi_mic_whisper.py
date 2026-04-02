import spidev
import time
import wave
import struct
import tempfile
import array

# --- SPI Setup ---
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 2000000  # 2 MHz
spi.mode = 0

SAMPLE_RATE = 16000
RECORD_SECONDS = 5
CHUNK_SIZE = 4096

print(f"Recording {RECORD_SECONDS}s of audio from USB mic...")

# --- Step 1: Record audio from USB mic ---
import sounddevice as sd

# Use array instead of numpy
audio = array.array('h')  # 'h' = signed 16-bit int

def callback(indata, frames, time_info, status):
    # indata is a buffer of bytes
    audio.frombytes(indata)

stream = sd.RawInputStream(
    samplerate=SAMPLE_RATE,
    channels=1,
    dtype='int16',
    callback=callback
)

with stream:
    time.sleep(RECORD_SECONDS)

print(f"Recorded {len(audio)} samples")

# Convert to bytes
audio_bytes = audio.tobytes()
audio_list = list(audio_bytes)

# --- Step 2: Send audio over SPI to STM32, get echo back ---
print(f"Sending {len(audio_bytes)} bytes over SPI to STM32...")

chunks = [audio_list[i:i+CHUNK_SIZE] for i in range(0, len(audio_list), CHUNK_SIZE)]
received_bytes = []

start = time.perf_counter()

for chunk in chunks:
    resp = spi.xfer2(chunk)
    received_bytes.extend(resp)

elapsed = time.perf_counter() - start
print(f"SPI round-trip: {elapsed * 1000:.1f} ms for {len(audio_bytes)} bytes")

spi.close()

# --- Step 3: Reconstruct audio from echoed bytes ---
# Remove first byte (preloaded 0xAA) and shift by 1
received_bytes = received_bytes[1:] + [0]
received_audio = bytes(received_bytes)

# --- Step 4: Save echoed audio to WAV (verify it survived the round trip) ---
wav_path = tempfile.mktemp(suffix='.wav')
with wave.open(wav_path, 'w') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)  # 16-bit
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(received_audio)
print(f"Saved echoed audio to {wav_path}")

# --- Step 5: Transcribe with Whisper ---
print("Transcribing with Whisper...")
import whisper

model = whisper.load_model("base")  # or "tiny" for faster, "small" for better
result = model.transcribe(wav_path)

print(f"\n{'='*50}")
print(f"TRANSCRIPTION: {result['text']}")
print(f"{'='*50}")
print(f"\nLatency breakdown:")
print(f"  SPI round-trip: {elapsed * 1000:.1f} ms")
print(f"  Audio duration: {RECORD_SECONDS * 1000} ms")
