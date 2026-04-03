import spidev
import time
import wave
import tempfile

# --- SPI Setup ---
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

SAMPLE_RATE = 16000
IDLE_MARKER = 0xFF
POLL_INTERVAL = 1.0 / SAMPLE_RATE  # 62.5 us between polls

print("Press and hold the blue button on the Nucleo to record.")
print("Release to transcribe with Whisper.\n")

# Load Whisper model once at startup
print("Loading Whisper model (this takes a moment)...")
import whisper
model = whisper.load_model("tiny")
print("Ready!\n")

while True:
    # Poll for button press
    resp = spi.xfer2([0x00])

    if resp[0] != IDLE_MARKER:
        # Button is pressed — start collecting audio
        print("Recording...", end='', flush=True)
        audio_samples = []
        audio_samples.append(resp[0])  # first sample

        while True:
            resp = spi.xfer2([0x00])
            if resp[0] == IDLE_MARKER:
                break
            audio_samples.append(resp[0])
            time.sleep(POLL_INTERVAL)

        duration = len(audio_samples) / SAMPLE_RATE
        print(f" {duration:.1f}s captured ({len(audio_samples)} samples)")

        if duration < 0.3:
            print("Too short, skipping.\n")
            continue

        # Convert 8-bit unsigned samples to 16-bit signed WAV
        audio_bytes = bytearray()
        for sample in audio_samples:
            # 8-bit unsigned (0-254) → 16-bit signed (-32768 to +32767)
            signed_16 = (sample - 128) * 256
            audio_bytes += signed_16.to_bytes(2, byteorder='little', signed=True)

        # Save to WAV
        wav_path = tempfile.mktemp(suffix='.wav')
        with wave.open(wav_path, 'w') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(bytes(audio_bytes))

        # Transcribe
        print("Transcribing...")
        result = model.transcribe(wav_path)
        print(f"\n>>> {result['text'].strip()}\n")
        print("Press button to record again...")

    time.sleep(0.01)  # 10ms idle poll rate
