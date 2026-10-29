"""Single-command autonomous trial: serial + UDP PCM capture + WAV playback.

Binds UDP 6008, opens the board serial console, plays the stimulus WAV after a
short quiet baseline, then keeps listening for a tail window. PCM captures are
reassembled per (session,id) into Logs/Chirp/PCM/<trial>/; serial lines go to
a log with host-second offsets. Host results never imply hardware PASS.
"""
import argparse
import json
import socket
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import udp_chirp_capture as capture  # noqa: E402
import pcm_trial_metadata as metadata  # noqa: E402

SND_FILENAME_NODEFAULT_ASYNC = 0x20003  # SND_FILENAME | SND_NODEFAULT | SND_ASYNC


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
    parser.add_argument("--volume", type=int, default=None, metavar="0-100",
                        help="set Windows wave-out master volume for this playback (restored after)")
    parser.add_argument("--controlled", action="store_true",
                        help="require BOOT/UID/image/channel/rate/WAV-reference metadata")
    parser.add_argument("--image", type=Path,
                        default=ROOT / "build" / "Debug" / "SBCG.elf",
                        help="local image used for the recorded SHA256")
    parser.add_argument("--image-sha256", default=None,
                        help="override flashed-image SHA256 (64 hex chars)")
    parser.add_argument("--expected-fw", default="SBCG-M4.2-PCMCAP")
    parser.add_argument("--reference-manifest", type=Path,
                        default=ROOT / "Data" / "Audio" / "candidate_manifest.json")
    parser.add_argument("--reset", action="store_true",
                        help="controlled reset before capture (openocd, no flash)")
    parser.add_argument("--openocd-config", type=Path,
                        default=ROOT / ".vscode" / "openocd-stm32f746.cfg")
    parser.add_argument("--stlink-serial", default="066EFF3832394D3457253156")
    args = parser.parse_args()

    import serial
    import winsound

    wave_volume_restore = None
    if args.volume is not None:
        import ctypes
        try:
            previous = ctypes.c_ulong(0)
            if ctypes.windll.winmm.waveOutGetVolume(0, ctypes.byref(previous)) == 0:
                wave_volume_restore = previous.value
            level = max(0, min(100, args.volume)) * 65535 // 100
            packed = level | (level << 16)
            ctypes.windll.winmm.waveOutSetVolume(0, packed)
            print(f"wave volume set to {args.volume}% (was 0x{previous.value:08x})", flush=True)
        except Exception as error:
            print(f"volume control unavailable: {error}", flush=True)

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
    reset_result = None
    if args.reset:
        reset_cmd = ["openocd", "-f", str(args.openocd_config),
                     "-c", f"adapter serial {args.stlink_serial}",
                     "-c", "init; reset run; exit"]
        try:
            reset = subprocess.run(reset_cmd, capture_output=True, text=True,
                                   timeout=30)
            reset_result = {"command": reset_cmd, "returncode": reset.returncode,
                            "stdout": reset.stdout[-2000:],
                            "stderr": reset.stderr[-2000:]}
            if reset.returncode != 0:
                failures.append(f"reset: rc {reset.returncode}")
        except Exception as error:
            reset_result = {"command": reset_cmd, "error": str(error)}
            failures.append(f"reset:{error}")
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
                # Async playback keeps the serial/UDP loops draining during the whole file.
                winsound.PlaySound(str(args.wav.resolve()), SND_FILENAME_NODEFAULT_ASYNC)
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
    if args.volume is not None and wave_volume_restore is not None:
        import ctypes
        try:
            ctypes.windll.winmm.waveOutSetVolume(0, wave_volume_restore)
            print(f"wave volume restored to 0x{wave_volume_restore:08x}", flush=True)
        except Exception as error:
            print(f"volume restore failed: {error}", flush=True)
    serial_text = "".join(
        json.dumps(r, ensure_ascii=False) + "\n" for r in serial_records)
    (Path(str(args.serial) + ".jsonl")).write_text(serial_text, encoding="utf-8")
    # The offline ledger/replay tool consumes Logs/Chirp/<trial>/run.jsonl;
    # write the canonical copy for every controlled trial automatically.
    canonical = ROOT / "Logs" / "Chirp" / args.trial
    canonical.mkdir(parents=True, exist_ok=True)
    (canonical / "run.jsonl").write_text(serial_text, encoding="utf-8")
    manifest, metadata_errors = metadata.build_manifest(
        image_path=args.image, image_sha256=args.image_sha256,
        expected_fw=args.expected_fw, wav=args.wav,
        manifest_path=args.reference_manifest, serial_records=serial_records,
        play_span=play_span, prelude_s=args.prelude, tail_s=args.tail,
        expected_channel=0)
    if args.controlled:
        failures.extend(f"metadata:{error}" for error in metadata_errors)
        if assembly.completed < 1:
            failures.append("capture: no PCM completed")
    summary = {"status": "COMPLETED", "trial": args.trial, "tag": args.tag,
               "received": assembly.received, "completed": assembly.completed,
               "duplicates": assembly.duplicates, "play_span": play_span,
               "serial_lines": len(serial_records), "failures": failures,
               "controlled": args.controlled, "reset": reset_result,
               "metadata": manifest}
    (out / "trial_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    (out / "trial_manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=True), flush=True)
    if failures:
        print("TRIAL_FAILURES", failures, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
