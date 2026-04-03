import spidev
import time
import wave

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

SAMPLE_RATE = 16000
IDLE_MARKER = 0xFF
RECORD_SECONDS = 4
POLL_INTERVAL = 1.0 / (SAMPLE_RATE * 2)  # poll faster than sample rate

print(f"Will record {RECORD_SECONDS}s of audio from STM32 ADC mic.")
print("Press and hold the blue button NOW!")
time.sleep(1)
print("GO! Polling...\n")

audio_samples = []
idle_streak = 0
started = False
start = time.perf_counter()
timeout = RECORD_SECONDS + 5  # give extra time for button press

while time.perf_counter() - start < timeout:
    resp = spi.xfer2([0x00])
    val = resp[0]

    if val == IDLE_MARKER:
        if started:
            idle_streak += 1
            if idle_streak > 1000:
                print("Button released.")
                break
    else:
        if not started:
            started = True
            print("Recording started!")
        idle_streak = 0
        audio_samples.append(val)

    # Print progress every second
    if started and len(audio_samples) % SAMPLE_RATE == 0:
        print(f"  {len(audio_samples) / SAMPLE_RATE:.0f}s...")

elapsed = time.perf_counter() - start
spi.close()

if not audio_samples:
    print("No audio captured. Did you hold the button?")
    exit()

duration = len(audio_samples) / SAMPLE_RATE
print(f"\nCaptured {len(audio_samples)} samples ({duration:.2f}s)")
print(f"Elapsed: {elapsed:.2f}s")
print(f"Effective sample rate: {len(audio_samples) / elapsed:.0f} Hz")

# Show sample statistics
vals = audio_samples
min_v = min(vals)
max_v = max(vals)
avg_v = sum(vals) / len(vals)
print(f"Sample range: {min_v} - {max_v} (avg {avg_v:.0f})")
print(f"Expected silence center: ~128")

# Show first 30 samples
print(f"\nFirst 30 samples:")
for i in range(min(30, len(audio_samples))):
    bar = '#' * (audio_samples[i] // 4)
    print(f"  {i:3d}: {audio_samples[i]:3d} |{bar}")

# Save to WAV
audio_bytes = bytearray()
for sample in audio_samples:
    signed_16 = (sample - 128) * 256
    audio_bytes += signed_16.to_bytes(2, byteorder='little', signed=True)

wav_path = '/tmp/stm32_mic_test.wav'
with wave.open(wav_path, 'w') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(bytes(audio_bytes))

print(f"\nSaved: {wav_path}")
print(f"\nCopy to Mac and play:")
print(f"  scp dammi@100.83.217.63:{wav_path} /tmp/")
print(f"  afplay /tmp/stm32_mic_test.wav")
