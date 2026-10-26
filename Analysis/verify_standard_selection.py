"""Verify the selected standard-sound contract against the frozen WAV assets.

Read-only: no audio playback, hardware, build or file rewriting.
"""
import hashlib
import json
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
AUDIO = ROOT / "Data" / "Audio"
SELECTION = AUDIO / "standard_selection.json"
MANIFEST = AUDIO / "candidate_manifest.json"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    selection = json.loads(SELECTION.read_text(encoding="utf-8"))
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    playback = AUDIO / selection["playback_file"]
    template = AUDIO / selection["template_file"]
    assert playback.is_file() and template.is_file(), "selected WAV missing"
    assert digest(playback) == selection["playback_file_sha256"], "playback hash mismatch"
    assert digest(template) == selection["template_file_sha256"], "template hash mismatch"
    assert manifest["files"][selection["playback_file"]]["sha256"] == selection["playback_file_sha256"]
    assert manifest["files"][selection["template_file"]]["sha256"] == selection["template_file_sha256"]
    with wave.open(str(playback), "rb") as source:
        assert source.getnchannels() == 1
        assert source.getsampwidth() == 2
        assert source.getframerate() == selection["format"]["sample_rate_hz"]
        assert source.getnframes() == int(selection["format"]["duration_s"] * source.getframerate())
    with wave.open(str(template), "rb") as source:
        assert source.getnchannels() == 1
        assert source.getsampwidth() == 2
        assert source.getframerate() == selection["format"]["sample_rate_hz"]
        assert source.getnframes() == 2400
    assert selection["selection_basis"].startswith("Instructor allowed")
    assert selection["recognition_evidence"]["status"] == "not_passed"
    compiled = (ROOT / "App/Detector/chirp_template.c").read_text(encoding="utf-8")
    assert selection["template_file_sha256"] in compiled, "compiled template source hash mismatch"
    print(json.dumps({
        "status": "PASS_STANDARD_SOUND_SELECTION_CONTRACT",
        "playback_sha256": digest(playback),
        "template_sha256": digest(template),
        "events_s": selection["playback_event_onsets_s"],
        "recognition": selection["recognition_evidence"]["status"],
        "scope": selection["scope"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
