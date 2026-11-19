"""Capture runtime diagnostics without toggling reset pins."""
import argparse
import time
from pathlib import Path
import serial
from serial.tools import list_ports


def detect_port(explicit=None):
    if explicit:
        return explicit
    for pt in list_ports.comports():
        hwid = (pt.hwid or '').upper()
        if '1A86:7523' in hwid or 'CH340' in (pt.description or '').upper():
            return pt.device
    names = [pt.device for pt in list_ports.comports()]
    for fallback in ('COM6', 'COM5'):
        if fallback in names:
            return fallback
    raise RuntimeError(f'No CH340 / COM6 / COM5 port found (available: {names})')


p = argparse.ArgumentParser()
p.add_argument('--port', default=None)
p.add_argument('--seconds', type=float, default=12)
p.add_argument('--out', default='docs/evidence/pipeline-runtime.txt')
p.add_argument('--command')
a = p.parse_args()
port = detect_port(a.port)
s = serial.Serial(port=None, baudrate=115200, timeout=0.5)
s.dtr = False
s.rts = False
s.port = port
s.open()
if a.command:
    s.write((a.command + '\n').encode())
lines = []
end = time.monotonic() + a.seconds
while time.monotonic() < end:
    line = s.readline().decode('utf-8', errors='replace').strip()
    if line:
        lines.append(line)
s.close()
Path(a.out).write_text('\n'.join(lines) + '\n', encoding='utf-8')
print('\n'.join(lines[-20:]))
