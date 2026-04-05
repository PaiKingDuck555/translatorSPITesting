#!/usr/bin/env python3
"""Send a text message to STM32 over UART. Dead simple test."""

import serial
import serial.tools.list_ports
import sys

MESSAGE = "Hello my name is Dammi"

# Find STM32
ports = list(serial.tools.list_ports.comports())
stm_port = None
for p in ports:
    if 'usbmodem' in p.device or 'STLink' in (p.description or '') or 'STM' in (p.description or ''):
        stm_port = p.device
        break

if not stm_port:
    if len(sys.argv) > 1:
        stm_port = sys.argv[1]
    else:
        print("Ports:", [p.device for p in ports])
        print("Pass port as argument: python3 send_text.py /dev/cu.usbmodemXXXX")
        sys.exit(1)

ser = serial.Serial(stm_port, 115200, timeout=5)
ser.reset_input_buffer()

# Read the READY message
ready = ser.readline().decode('ascii', errors='ignore').strip()
print(f"STM32: {ready}")

# Send the message
print(f"Sending: {MESSAGE}")
ser.write((MESSAGE + "\r\n").encode('ascii'))

# Read responses
for _ in range(2):
    line = ser.readline().decode('ascii', errors='ignore').strip()
    if line:
        print(f"STM32: {line}")

ser.close()
