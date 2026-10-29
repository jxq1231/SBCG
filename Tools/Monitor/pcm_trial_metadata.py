"""Pure metadata helpers for a controlled single-board acoustic trial.

No hardware access: WAV/manifest/Hash and already-captured serial lines are
turned into a machine-readable manifest.  The runner records BOOT/UID, image
hash, channel, measured sample rate, playback reference and pre/post silence;
this module keeps that logic testable without a board.
"""
import hashlib
import json
import re
import wave
from pathlib import Path

FIELD_LINE = re.compile(r"\$([A-Z][A-Z0-9_]*),([^$]*)")


def sha256_path(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def parse_fields(body):
    out = {}
    for item in body.split(","):
        if "=" in item:
            key, _, value = item.partition("=")
            out[key] = value
    return out


def wav_reference(wav_path, manifest_path=None):
    """Return WAV identity plus expected event starts from the audio manifest."""
    wav_path = Path(wav_path)
    with wave.open(str(wav_path), "rb") as handle:
        rate = handle.getframerate()
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        frames = handle.getnframes()
    duration_s = frames / rate if rate else 0.0
    expected = []
    template = None
    manifest_path = Path(manifest_path) if manifest_path else None
    if manifest_path and manifest_path.exists():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            manifest = {}
        starts = manifest.get("playback_event_start_frames")
        if isinstance(starts, list) and starts:
            expected = [round(float(v) / float(manifest.get("sample_rate_hz", rate)), 6)
                        for v in starts]
        files = manifest.get("files", {})
        if wav_path.name in files:
            template = files[wav_path.name]
    return {
        "path": str(wav_path),
        "sha256": sha256_path(wav_path),
        "sample_rate_hz": rate,
        "channels": channels,
        "sample_width_bytes": width,
        "frames": frames,
        "duration_s": round(duration_s, 6),
        "expected_onsets_s": expected,
        "manifest_entry": template,
    }


def parse_serial_metadata(lines):
    """Extract BOOT/UID, CHIRP_INIT channel and measured AUDIO rate from lines."""
    out = {"firmware": None, "uid": None, "channel": None,
           "rate_hz": None, "boot_line": None}
    for raw in lines:
        line = raw.strip()
        for tag, body in FIELD_LINE.findall(line):
            fields = parse_fields(body)
            if tag == "BOOT":
                out["firmware"] = fields.get("FW")
                uid = [fields.get("UID_W0"), fields.get("UID_W1"),
                       fields.get("UID_W2")]
                if all(uid):
                    out["uid"] = ":".join(uid)
                out["boot_line"] = line
            elif tag == "CHIRP_INIT" and out["channel"] is None:
                try:
                    out["channel"] = int(fields.get("CH"))
                except (TypeError, ValueError):
                    pass
            elif tag == "AUDIO" and out["rate_hz"] is None:
                try:
                    out["rate_hz"] = int(fields.get("RATE_HZ"))
                except (TypeError, ValueError):
                    pass
    return out


def count_sound_candidates(lines):
    """Return candidate sequence/timestamps in host-record form for silence checks."""
    out = []
    for row in lines:
        line = row.get("line", "") if isinstance(row, dict) else str(row)
        if any(tag == "SOUND_CAND" for tag, _body in FIELD_LINE.findall(line)):
            out.append(row if isinstance(row, dict) else {"line": line})
    return out


def build_manifest(image_path=None, image_sha256=None, expected_fw=None,
                   wav=None, manifest_path=None, serial_records=None,
                   play_span=None, prelude_s=None, tail_s=None,
                   expected_channel=0):
    """Build and validate the controlled-trial manifest.

    Returns (manifest, errors).  Missing required evidence is an error, not a
    silently omitted field.  Image hash override is useful when the flashed
    binary is not the local ELF.
    """
    errors = []
    image = Path(image_path) if image_path else None
    digest = image_sha256
    if digest is None and image and image.exists():
        digest = sha256_path(image)
    if not digest:
        errors.append("image sha256 missing")
    elif len(str(digest)) != 64:
        errors.append("image sha256 must be 64 hex chars")
    if not image:
        errors.append("image path missing")
    wav_data = wav_reference(wav, manifest_path) if wav else {}
    if not wav_data:
        errors.append("wav missing")
    elif not wav_data.get("expected_onsets_s"):
        errors.append("wav expected onsets missing")
    lines = serial_records or []
    parsed = parse_serial_metadata(
        [row.get("line", "") if isinstance(row, dict) else str(row)
         for row in lines])
    for key in ("firmware", "uid", "channel", "rate_hz"):
        if parsed.get(key) is None:
            errors.append(f"serial metadata missing: {key}")
    if expected_fw and parsed.get("firmware") != expected_fw:
        errors.append(f"firmware mismatch: {parsed.get('firmware')} != {expected_fw}")
    if expected_channel is not None and parsed.get("channel") not in (None, expected_channel):
        errors.append(f"channel mismatch: {parsed.get('channel')} != {expected_channel}")
    candidates = count_sound_candidates(lines)
    span = play_span
    before = after = None
    if span and span[0] is not None and span[1] is not None:
        before = sum(1 for row in candidates
                     if isinstance(row.get("t"), (int, float)) and row["t"] < span[0])
        after = sum(1 for row in candidates
                    if isinstance(row.get("t"), (int, float)) and row["t"] > span[1])
    manifest = {
        "schema": "controlled-chirp-trial/v1",
        "image": {"path": str(image) if image else None,
                  "sha256": digest},
        "expected_firmware": expected_fw,
        "observed": parsed,
        "wav": wav_data,
        "playback": {"play_span_api_s": span,
                     "prelude_s": prelude_s,
                     "tail_s": tail_s,
                     "sound_candidates_before": before,
                     "sound_candidates_after": after},
        "errors": errors,
    }
    return manifest, errors
