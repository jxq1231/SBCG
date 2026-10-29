"""Receive SBCG chirp-candidate PCM chunks and reassemble per (session,id)."""
import argparse
import json
import socket
import struct
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HEADER = struct.Struct("!4sBBBBIIIIIIQQQ")
MAGIC = b"PCM1"
CHUNK_FRAMES = 512


def decode_packet(data):
    if len(data) < HEADER.size:
        raise ValueError(f"short datagram {len(data)} < {HEADER.size}")
    magic, version, kind, flags, _reserved, seq, total, present, _pad, \
        capture_id, session, first_frame, hint_frame, raw_first_frame = HEADER.unpack(data[:HEADER.size])
    if (magic, version, kind) != (MAGIC, 1, 1):
        raise ValueError("unsupported magic/version/kind")
    if present > CHUNK_FRAMES or total < 1 or total > 17 or seq >= total:
        raise ValueError(f"invalid chunk framing seq={seq} total={total} present={present}")
    payload_bytes = len(data) - HEADER.size
    if payload_bytes != present * 2:
        raise ValueError(f"payload {payload_bytes} != present*2={present * 2}")
    payload = list(struct.unpack(f"<{present}h", data[HEADER.size:]))
    return {"flags": flags, "seq": seq, "total": total, "id": capture_id,
            "session": session, "first_frame": first_frame, "hint_frame": hint_frame,
            "raw_first_frame": raw_first_frame, "payload": payload}


class CaptureAssembler:
    """Reassembles one capture per (session,id); tracks incompletes, not an ACK protocol."""
    def __init__(self):
        self._parts = {}
        self.received = self.completed = self.duplicates = 0

    def feed(self, meta, payload):
        key = (meta["session"], meta["id"])
        entry = self._parts.get(key)
        if entry is None:
            entry = {"meta": meta, "base": None, "slots": {}, "expected": meta["total"], "first": None}
            self._parts[key] = entry
        elif entry["expected"] != meta["total"]:
            raise ValueError("capture total changed mid-stream")
        if meta["seq"] == 0:
            entry["base"] = meta
        if meta["seq"] in entry["slots"]:
            self.duplicates += 1
            return None
        self.received += 1
        entry["slots"][meta["seq"]] = payload
        if len(entry["slots"]) != entry["expected"]:
            return None
        order = [entry["slots"][i] for i in range(entry["expected"])]
        frames = [len(p) for p in order]
        data = b"".join(struct.pack(f"<{n}h", *p) for n, p in zip(frames, order))
        base = entry["base"] if entry["base"] is not None else entry["meta"]
        hint = base["hint_frame"] if "hint_frame" in base else entry["meta"]["hint_frame"]
        raw = base["raw_first_frame"] if "raw_first_frame" in base else entry["meta"]["raw_first_frame"]
        del self._parts[key]
        self.completed += 1
        total_frames = sum(frames)
        if total_frames > 8544:
            raise ValueError(f"capture exceeds bounded span: {total_frames}")
        return {"session": key[0], "id": key[1], "frames": total_frames,
                "first_frame": base["first_frame"],
                "hint_frame": hint, "raw_first_frame": raw,
                "flags": entry["meta"]["flags"], "data": data}


def write_capture(out_dir, capture, tag, received_at):
    out_dir.mkdir(parents=True, exist_ok=True)
    pcm = out_dir / f"id{capture['id']}.pcm"
    pcm.write_bytes(capture["data"])
    meta = {k: capture[k] for k in ("session", "id", "frames", "first_frame",
                                    "hint_frame", "raw_first_frame", "flags")}
    meta.update({"tag": tag, "received_iso": received_at,
                 "left_clamped": bool(capture["flags"] & 1),
                 "right_clamped": bool(capture["flags"] & 2),
                 "tail_topup": bool(capture["flags"] & 4)})
    (out_dir / f"id{capture['id']}.json").write_text(
        json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")
    return pcm, meta


def run(listen, port, out_dir, tag, duration_s):
    out_dir = Path(out_dir)
    first_rx = None
    assembler = CaptureAssembler()
    started = time.time()
    socket.setdefaulttimeout(1.0)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind((listen, port))
        sock.settimeout(1.0)
        while duration_s <= 0 or time.time() - started < duration_s:
            try:
                data, _addr = sock.recvfrom(1600)
            except socket.timeout:
                continue
            if first_rx is None:
                first_rx = time.time()
            try:
                meta = decode_packet(data)
                capture = assembler.feed(meta, meta.pop("payload"))
            except ValueError as error:
                print(f"skip malformed packet: {error}", flush=True)
                continue
            if capture is not None:
                pcm, meta_out = write_capture(out_dir, capture, tag,
                                              datetime.now(timezone.utc).isoformat())
                print(f"capture id={capture['id']} session={capture['session']} "
                      f"frames={capture['frames']} -> {pcm.name}", flush=True)
    summary = {"status": "COMPLETED", "received": assembler.received,
               "completed": assembler.completed, "duplicates": assembler.duplicates,
               "first_rx_iso": datetime.fromtimestamp(first_rx, timezone.utc).isoformat()
               if first_rx else None, "duration_s": time.time() - started}
    (out_dir / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2),
                                          encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False), flush=True)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=6008)
    parser.add_argument("--out", required=True, type=Path,
                        help="output dir for Logs/Chirp/PCM/<trial>")
    parser.add_argument("--tag", default="", help="trial conditions, e.g. dist=1m;vol=75;dir=0")
    parser.add_argument("--duration", type=float, default=0, help="seconds to listen; 0 = until Ctrl+C")
    args = parser.parse_args()
    return 0 if run(args.bind, args.port, args.out, args.tag, args.duration) else 1


if __name__ == "__main__":
    sys.exit(main())