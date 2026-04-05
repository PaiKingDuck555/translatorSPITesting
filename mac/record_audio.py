import serial
import serial.tools.list_ports
import wave
import struct
import sys

SAMPLE_RATE = 16000

# Find the STM32 serial port
ports = list(serial.tools.list_ports.comports())
stm_port = None
for p in ports:
    if 'usbmodem' in p.device or 'STLink' in (p.description or '') or 'STM' in (p.description or ''):
        stm_port = p.device
        break

if not stm_port:
    print("Available ports:")
    for p in ports:
        print(f"  {p.device} — {p.description}")
    print("\nSTM32 not auto-detected. Pass port as argument:")
    print(f"  python3 {sys.argv[0]} /dev/cu.usbmodemXXXX")
    if len(sys.argv) > 1:
        stm_port = sys.argv[1]
    else:
        sys.exit(1)

print(f"Connecting to {stm_port}...")
ser = serial.Serial(stm_port, 115200, timeout=30)

print("Listening for STM32... Press blue button to record.\n")

while True:
    line = ser.readline().decode('ascii', errors='ignore').strip()
    if line:
        print(f"STM32: {line}")

    if line.startswith("AUDIO:"):
        num_samples = int(line.split(":")[1])
        print(f"Receiving {num_samples} samples ({num_samples/SAMPLE_RATE:.2f}s)...")

        # Read raw bytes: 2 bytes per sample
        raw = ser.read(num_samples * 2)
        if len(raw) != num_samples * 2:
            print(f"ERROR: expected {num_samples*2} bytes, got {len(raw)}")
            continue

        # Wait for DONE
        done_line = ser.readline().decode('ascii', errors='ignore').strip()
        print(f"STM32: {done_line}")

        # Convert 12-bit unsigned to 16-bit signed
        samples = []
        for i in range(0, len(raw), 2):
            val = raw[i] | (raw[i+1] << 8)  # 12-bit unsigned (0-4095)
            signed_val = val - 2048           # center at 0 (-2048 to +2047)
            signed_val *= 16                  # scale to 16-bit range
            samples.append(signed_val)

        # Save WAV
        wav_path = '/tmp/stm32_recording.wav'
        with wave.open(wav_path, 'w') as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(SAMPLE_RATE)
            for s in samples:
                wf.writeframes(struct.pack('<h', max(-32768, min(32767, s))))

        print(f"\nSaved: {wav_path}")
        print(f"Play: afplay {wav_path}")
        print(f"\nPress button to record again...")
