import spidev
import time
import os

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000  # 1 MHz — adjust to test speed limits
spi.mode = 0

# Simulate audio: 16000 Hz sample rate, 16-bit (2 bytes per sample), 1 second
SAMPLE_RATE = 16000
BYTES_PER_SAMPLE = 2
DURATION = 1  # seconds
total_bytes = SAMPLE_RATE * BYTES_PER_SAMPLE * DURATION  # 32000 bytes

# Generate fake audio data (random bytes to simulate real audio)
test_data = list(os.urandom(total_bytes))

# --- Test 1: Round-trip latency for a single byte ---
print("=== Single Byte Latency ===")
times = []
for i in range(100):
    start = time.perf_counter_ns()
    resp = spi.xfer2([0x55])
    end = time.perf_counter_ns()
    times.append(end - start)

avg_ns = sum(times) / len(times)
print(f"Average: {avg_ns / 1000:.1f} us")
print(f"Min:     {min(times) / 1000:.1f} us")
print(f"Max:     {max(times) / 1000:.1f} us")

# --- Test 2: Bulk transfer (1 second of audio) ---
print(f"\n=== Bulk Transfer: {total_bytes} bytes ({DURATION}s of {SAMPLE_RATE} Hz audio) ===")

# Send in small chunks with a gap so STM32 can keep up
CHUNK_SIZE = 1  # 1 byte at a time — safest, we'll increase once it works
chunks = [test_data[i:i+CHUNK_SIZE] for i in range(0, len(test_data), CHUNK_SIZE)]

errors = 0
total_received = []

start = time.perf_counter()

for chunk in chunks:
    resp = spi.xfer2(chunk)
    total_received.extend(resp)

elapsed = time.perf_counter() - start

# Check echo accuracy (response is 1 transfer behind, so compare offset by 1)
# First byte back is the preloaded 0xAA, then each response echoes the PREVIOUS byte
for i in range(1, len(total_received)):
    if total_received[i] != test_data[i - 1]:
        errors += 1

throughput_kbps = (total_bytes * 8) / elapsed / 1000

print(f"Time:       {elapsed * 1000:.1f} ms")
print(f"Throughput: {throughput_kbps:.0f} kbps")
print(f"Echo errors: {errors}/{total_bytes} bytes")
print(f"Accuracy:   {(1 - errors/total_bytes) * 100:.2f}%")

# --- Test 3: Try different SPI speeds ---
print("\n=== Speed Test ===")
test_chunk = list(os.urandom(1024))

for speed in [500000, 1000000, 2000000, 4000000, 8000000]:
    spi.max_speed_hz = speed
    start = time.perf_counter()
    for _ in range(10):
        spi.xfer2(test_chunk)
    elapsed = time.perf_counter() - start
    actual_kbps = (1024 * 10 * 8) / elapsed / 1000
    print(f"  {speed/1000000:.1f} MHz → {actual_kbps:.0f} kbps actual throughput")

# --- Test 4: Read STM32 internal cycle counts ---
print("\n=== STM32 Internal Timing ===")
spi.max_speed_hz = 1000000

# Send a few normal bytes first to generate timing data
for i in range(10):
    spi.xfer2([i])

# Request timing from STM32
# 0xFF = report max cycles, 0xFE = report min cycles
resp_max = spi.xfer2([0xFF])
time.sleep(0.001)
max_cycles = spi.xfer2([0x00])[0]  # actual response comes on next transfer

resp_min = spi.xfer2([0xFE])
time.sleep(0.001)
min_cycles = spi.xfer2([0x00])[0]

cpu_freq = 16_000_000  # 16 MHz HSI default
print(f"STM32 CPU frequency: {cpu_freq/1000000:.0f} MHz")
print(f"Max processing cycles: {max_cycles}")
print(f"Min processing cycles: {min_cycles}")
print(f"Max processing time: {max_cycles / cpu_freq * 1_000_000:.2f} us")
print(f"Min processing time: {min_cycles / cpu_freq * 1_000_000:.2f} us")

print(f"\n=== Can the STM32 Keep Up? ===")
for speed in [500000, 1000000, 2000000, 4000000, 8000000]:
    byte_time_us = 8 / speed * 1_000_000
    process_us = max_cycles / cpu_freq * 1_000_000
    margin = byte_time_us - process_us
    status = "OK" if margin > 0 else "OVERRUN"
    print(f"  {speed/1000000:.1f} MHz: byte every {byte_time_us:.2f} us, "
          f"STM32 needs {process_us:.2f} us, "
          f"margin {margin:.2f} us → {status}")

spi.close()
print("\nDone!")
