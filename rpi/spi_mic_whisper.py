import spidev
import time
import wave
import array
import tempfile
import sounddevice as sd

# --- SPI Setup ---
spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

SAMPLE_RATE = 16000
BTN_PRESSED = 0x01

print("Hold the blue button on the Nucleo to record...")
print("Release to transcribe.\n")

while True:
    # --- Poll button state ---
    resp = spi.xfer2([0x00])

    if resp[0] == BTN_PRESSED:
        print("Recording...", end='', flush=True)

        # Start recording audio
        audio = array.array('h')

        def callback(indata, frames, time_info, status):
            audio.frombytes(indata)

        stream = sd.RawInputStream(
            samplerate=SAMPLE_RATE,
            channels=1,
            dtype='int16',
            callback=callback
        )

        stream.start()

        # Keep recording while button is held
        while True:
            resp = spi.xfer2([0x00])
            if resp[0] != BTN_PRESSED:
                break
            time.sleep(0.01)  # poll every 10ms

        stream.stop()
        stream.close()

        duration = len(audio) / SAMPLE_RATE
        print(f" {duration:.1f}s captured")

        if duration < 0.5:
            print("Too short, skipping.\n")
            continue

        # Save to WAV
        wav_path = tempfile.mktemp(suffix='.wav')
        audio_bytes = audio.tobytes()
        with wave.open(wav_path, 'w') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            wf.writeframes(audio_bytes)

        # Transcribe
        print("Transcribing...")
        import whisper
        model = whisper.load_model("base")
        result = model.transcribe(wav_path)

        print(f"\n>>> {result['text'].strip()}\n")
        print("Hold button to record again...")

    time.sleep(0.05)  # poll button every 50ms when idle
