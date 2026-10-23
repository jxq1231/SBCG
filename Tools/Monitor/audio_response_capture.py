"""Bounded serial acoustic trial; phase marker is supplied by the operator."""
import argparse
import json
from pathlib import Path
import time
import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--phase-marker', type=Path, required=True)
    args = parser.parse_args()
    prefix = args.log
    prefix.parent.mkdir(parents=True, exist_ok=True)
    if args.phase_marker.exists():
        raise ValueError('phase marker already exists; use a fresh trial')
    start = time.monotonic()
    noise_start = None
    count = 0
    pending = b''
    stream = serial.Serial(port=None, baudrate=115200, bytesize=8, parity='N', stopbits=1, timeout=0.2)
    stream.dtr = stream.rts = False
    stream.port = args.port
    status = 'FAILED'
    try:
        stream.open()
        print('ACOUSTIC_CAPTURE_READY: quiet baseline; awaiting operator sound marker', flush=True)
        with Path(str(prefix)+'-serial.bin').open('xb') as raw, Path(str(prefix)+'-serial.jsonl').open('x', encoding='utf-8') as lines:
            while True:
                elapsed = time.monotonic()-start
                if noise_start is None and args.phase_marker.exists():
                    noise_start = elapsed
                    print('SOUND_WINDOW_STARTED', round(elapsed,3), flush=True)
                data = stream.read(4096)
                if data:
                    raw.write(data);raw.flush();pending += data
                    while b'\n' in pending:
                        line,pending = pending.split(b'\n',1)
                        lines.write(json.dumps({'elapsed_s':round(time.monotonic()-start,6), 'line':line.rstrip(b'\r').decode('ascii', errors='replace')})+'\n')
                        lines.flush();count += 1
                if noise_start is not None and elapsed-noise_start >= 30:
                    status = 'CAPTURED_PENDING_ANALYSIS'
                    break
                if elapsed >= 240:
                    raise TimeoutError('acoustic trial exceeded 240 seconds')
            if pending:
                lines.write(json.dumps({'incomplete_tail_hex':pending.hex()})+'\n')
    finally:
        stream.close()
        Path(str(prefix)+'-run.json').write_text(json.dumps({'status':status, 'lines':count, 'sound_start_s':noise_start, 'elapsed_s':time.monotonic()-start, 'port_closed':not stream.is_open, 'hardware_verified':False},indent=2), encoding='utf-8')
        print('ACOUSTIC_CAPTURE_CLOSED', status, count, flush=True)


if __name__ == '__main__':
    main()
