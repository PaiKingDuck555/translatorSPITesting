import spidev
import time

spi = spidev.SpiDev()
spi.open(0, 0)
spi.max_speed_hz = 1000000  # 1 MHz
spi.mode = 0

DURATION = 60  # seconds
total_sent = 0
total_errors = 0
prev_sent = None

print(f"Running SPI echo test for {DURATION} seconds...\n")

start = time.perf_counter()

while time.perf_counter() - start < DURATION:
    send_val = total_sent % 256  # cycle through 0-255
    resp = spi.xfer2([send_val])
    received = resp[0]

    # Response is echo of PREVIOUS byte (SPI is 1 transfer behind)
    if prev_sent is not None and received != prev_sent:
        total_errors += 1

    prev_sent = send_val
    total_sent += 1

    # Print status every 5 seconds
    if total_sent % 50000 == 0:
        elapsed = time.perf_counter() - start
        error_pct = (total_errors / total_sent) * 100
        print(f"  {elapsed:5.1f}s | Sent: {total_sent:>8} | Errors: {total_errors:>6} | Accuracy: {100 - error_pct:.2f}%")

elapsed = time.perf_counter() - start
error_pct = (total_errors / total_sent) * 100 if total_sent > 0 else 0
throughput = (total_sent * 8) / elapsed / 1000

spi.close()

print(f"\n{'='*50}")
print(f"Duration:   {elapsed:.1f} seconds")
print(f"Bytes sent: {total_sent}")
print(f"Errors:     {total_errors}")
print(f"Accuracy:   {100 - error_pct:.4f}%")
print(f"Throughput: {throughput:.0f} kbps")
print(f"{'='*50}")
