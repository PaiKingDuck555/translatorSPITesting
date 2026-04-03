import spidev
import time
import wave

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000
spi.mode = 0

SAMPLE_RATE = 16000
RECORD_SECONDS = 4

total_samples = SAMPLE_RATE * RECORD_SECONDS  # 64000 samples

print(f"Recording {RECORD_SECONDS}s of audio from STM32 ADC mic...")
print("Press and hold the blue button NOW!")
time.sleep(1)
print("GO!")

audio_samples = []
start = time.perf_counter()

for i in range(total_samples):
    resp = spi.xfer2([0x00])
    audio_samples.append(resp[0])

elapsed = time.perf_counter() - start
spi.close()

# Count how many were idle vs audio
idle_count = sum(1 for s in audio_samples if s == 0xFF)
audio_count = len(audio_samples) - idle_count

print(f"\nDone in {elapsed:.2f}s")
print(f"Total samples: {len(audio_samples)}")
print(f"Audio samples: {audio_count} ({audio_count/SAMPLE_RATE:.1f}s)")
print(f"Idle samples:  {idle_count}")
print(f"Actual sample rate: {len(audio_samples)/elapsed:.0f} Hz")

# Show first 50 values
print(f"\nFirst 50 samples:")
for i in range(min(50, len(audio_samples))):
    marker = " <-- IDLE" if audio_samples[i] == 0xFF else ""
    print(f"  {i:3d}: {audio_samples[i]:3d} (0x{audio_samples[i]:02X}){marker}")

# Save to WAV (replace idle markers with silence=128)
audio_bytes = bytearray()
for sample in audio_samples:
    if sample == 0xFF:
        val = 0  # silence
    else:
        val = (sample - 128) * 256
    audio_bytes += val.to_bytes(2, byteorder='little', signed=True)

wav_path = '/tmp/stm32_mic_test.wav'
with wave.open(wav_path, 'w') as wf:
    wf.setnchannels(1)
    wf.setsampwidth(2)
    wf.setframerate(SAMPLE_RATE)
    wf.writeframes(bytes(audio_bytes))

print(f"\nSaved: {wav_path}")
print(f"Copy to Mac and play:")
print(f"  scp dammi@100.83.217.63:{wav_path} /tmp/")
print(f"  afplay /tmp/stm32_mic_test.wav")
