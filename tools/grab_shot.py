"""Capture real-device 480x480 RGB565 screenshots via serial `shot` command.

Usage:
  python tools/grab_shot.py --out docs/evidence/shot_battle.png
  python tools/grab_shot.py --cmd boss --delay 0.85 --out docs/evidence/shot_boss_cutin.png

Auto-detects CH340 (1A86:7523) port and keeps DTR/RTS low to avoid resetting the MCU.
"""
import argparse
import time
from pathlib import Path
from PIL import Image
import serial
from serial.tools import list_ports


def detect_port(explicit=None):
    if explicit:
        return explicit
    for p in list_ports.comports():
        hwid = (p.hwid or '').upper()
        if '1A86:7523' in hwid or 'CH340' in (p.description or '').upper():
            return p.device
    names = [p.device for p in list_ports.comports()]
    for fallback in ('COM6', 'COM5'):
        if fallback in names:
            return fallback
    raise RuntimeError(f'No CH340 / COM6 / COM5 serial port found (available: {names})')


def open_serial(port):
    s = serial.Serial(port=None, baudrate=115200, timeout=0.5)
    s.dtr = False
    s.rts = False
    s.port = port
    s.open()
    return s


def capture_shot(port, out_path, cmd=None, delay=0.5, pre_cmds=()):
    s = open_serial(port)
    try:
        time.sleep(0.1)
        s.reset_input_buffer()
        for pc in pre_cmds:
            s.write((pc.strip() + '\n').encode())
            s.flush()
            time.sleep(0.08)
        if cmd:
            s.write((cmd.strip() + '\n').encode())
            s.flush()
        if delay > 0:
            time.sleep(delay)
        s.reset_input_buffer()
        s.write(b'shot\n')
        s.flush()

        # Wait for SHOT 480 480\n
        w, h = 480, 480
        deadline = time.monotonic() + 8.0
        header_found = False
        while time.monotonic() < deadline:
            line = s.readline().decode('utf-8', errors='replace').strip()
            if line.startswith('SHOT '):
                parts = line.split()
                if len(parts) >= 3:
                    w, h = int(parts[1]), int(parts[2])
                header_found = True
                break
        if not header_found:
            raise RuntimeError('Timed out waiting for SHOT header')

        raw = bytearray()
        total_px = w * h
        decoded = bytearray(total_px * 3)
        px_idx = 0
        deadline = time.monotonic() + 60.0
        while px_idx < total_px and time.monotonic() < deadline:
            chunk = s.read(4096)
            if not chunk:
                continue
            raw.extend(chunk)
            # Process 4-byte RLE packets: [run_lo, run_hi, col_lo, col_hi]
            # Wait: stop if we hit "\nRLEEND" prematurely or just parse up to total_px
            pos = 0
            limit = len(raw) - (len(raw) % 4)
            while pos < limit and px_idx < total_px:
                run = raw[pos] | (raw[pos + 1] << 8)
                c = raw[pos + 2] | (raw[pos + 3] << 8)
                pos += 4
                r = ((c >> 11) & 0x1F) * 255 // 31
                g = ((c >> 5) & 0x3F) * 255 // 63
                b = (c & 0x1F) * 255 // 31
                end_px = min(total_px, px_idx + run)
                for p_i in range(px_idx, end_px):
                    off = p_i * 3
                    decoded[off] = r
                    decoded[off + 1] = g
                    decoded[off + 2] = b
                px_idx = end_px
            if pos > 0:
                del raw[:pos]

        if px_idx < total_px:
            raise RuntimeError(f'Incomplete frame: got {px_idx}/{total_px} pixels')

        # Drain trailing RLEEND
        t_end = time.monotonic() + 0.5
        while time.monotonic() < t_end:
            if s.in_waiting:
                s.read(s.in_waiting)
            else:
                time.sleep(0.02)

        img = Image.frombytes('RGB', (w, h), bytes(decoded))
        out = Path(out_path)
        out.parent.mkdir(parents=True, exist_ok=True)
        img.save(out)
        print(f'Saved {out} ({w}x{h}) from {port}')
    finally:
        s.close()


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', default=None)
    ap.add_argument('--pre', action='append', default=[])
    ap.add_argument('--cmd', default=None)
    ap.add_argument('--delay', type=float, default=0.4)
    ap.add_argument('--out', required=True)
    args = ap.parse_args()
    capture_shot(detect_port(args.port), args.out, cmd=args.cmd, delay=args.delay, pre_cmds=args.pre)
