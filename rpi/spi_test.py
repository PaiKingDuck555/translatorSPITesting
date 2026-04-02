import spidev
import time

spi = spidev.SpiDev()
spi.open(0, 0)          # bus 0, device 0 (CE0)
spi.max_speed_hz = 1000000  # 1 MHz
spi.mode = 0             # CPOL=0, CPHA=0 — matches STM32

print("Sending bytes to STM32 over SPI...\n")

for i in range(10):
    send_val = i * 10
    resp = spi.xfer2([send_val])
    print(f"Sent: {send_val:3d} (0x{send_val:02X})  |  Received: {resp[0]:3d} (0x{resp[0]:02X})")
    time.sleep(0.1)

spi.close()
print("\nDone!")
