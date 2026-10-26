"""Single-command autonomous trial: serial + UDP PCM capture + WAV playback.

Binds UDP 6008, opens the board serial console, plays the stimulus WAV after a
short quiet baseline, then keeps listening for a tail window. PCM captures are
reassembled per (session,id) into Logs/Chirp/PCM/<trial>/; serial lines go to
a log with host-second offsets. Host results never imply hardware PASS.
"""
import argparse
import json
import socket
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import udp_chirp_capture as capture  # noqa: E402

SND_FILENAME_NODEFAULT = 0x20002  # winsound.SND_FILENAME | SND_NODEFAULT


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trial", required=True, help="dir name under Logs/Chirp/PCM")
    parser.add_argument("--tag", default="auto")
    parser.add_argument("--port", default="COM3")
    parser.add_argument("--wav", required=True, type=Path)
    parser.add_argument("--prelude", type=float, default=4.0, help="quiet baseline seconds")
    parser.add_argument("--tail", type=float, default=12.0, help="listen after playback seconds")
    parser.add_argument("--serial", required=True, type=Path,
                        help="serial log path prefix (writes <prefix>.jsonl)")
    args = parser.parse_args()

    import serial
    import winsound

    out = ROOT / "Logs" / "Chirp" / "PCM" / args.trial
    out.mkdir(parents=True, exist_ok=True)
    args.serial.parent.mkdir(parents=True, exist_ok=True)
    assembly = capture.CaptureAssembler()
    serial_records = []
    failures = []

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", 6008))
    sock.settimeout(0.2)
    stream = serial.Serial(port=None, baudrate=115200, bytesize=8, parity="N", stopbits=1,
                           timeout=0.2)
    stream.dtr = stream.rts = False
    stream.port = args.port
    stream.open()

    started = time.monotonic()
    played = False
    end = started + args.prelude + 22.0 + args.tail
    play_span = None
    now = started
    while True:
        now = time.monotonic()
        if now >= end:
            break
        try:
            while stream.in_waiting:
                line = stream.readline().decode("utf-8", "replace").rstrip("\r\n")
                if line:
                    serial_records.append({"t": round(now - started, 3), "line": line})
        except Exception as error:
            failures.append(f"serial:{error}")
        if not played and now - started >= args.prelude:
            played = True
            play_span = [round(now - started, 3), None]
            try:
                winsound.PlaySound(str(args.wav.resolve()), SND_FILENAME_NODEFAULT)
            except Exception as error:
                failures.append(f"play:{error}")
            play_span[1] = round(time.monotonic() - started, 3)
        try:
            data, _addr = sock.recvfrom(1600)
        except socket.timeout:
            continue
        except OSError as error:
            failures.append(f"net:{error}")
            break
        try:
            meta = capture.decode_packet(data)
            done = assembly.feed(meta, meta.pop("payload"))
        except ValueError as error:
            failures.append(f"pkt:{error}")
            continue
        if done is not None:
            pcm, meta_out = capture.write_capture(
                out, done, args.tag, datetime.now(timezone.utc).isoformat())
            print(f"capture id={done['id']} session={done['session']} "
                  f"frames={done['frames']} -> {pcm.name}", flush=True)

    stream.close()
    sock.close()
    (Path(str(args.serial) + ".jsonl")).write_text(
        "".join(json.dumps(r, ensure_ascii=False) + "\n" for r in serial_records),
        encoding="utf-8")
    summary = {"status": "COMPLETED", "trial": args.trial, "tag": args.tag,
               "received": assembly.received, "completed": assembly.completed,
               "duplicates": assembly.duplicates, "play_span": play_span,
               "serial_lines": len(serial_records), "failures": failures}
    (out / "trial_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False), flush=True)
    if failures:
        print("TRIAL_FAILURES", failures, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())