"""Replay real captured PCM windows through the frozen TOA estimators.

Frozen-algorithm offline replay for the single-board event baseline. For each
SOUND candidate associated to a captured PCM, extracts the exact
[FIRST-64, FIRST-64+256) window (no zero padding, no stitching), runs two
frozen estimator versions (git c79bf25 peak-backtrack and the current working
tree envelope) compiled at O0/O2 and repeated twice, and reports deterministic
results alongside the online serial TOA. Known-onset synthetic windows drive
the same C binaries for bias/coverage diagnostics. Produces a fresh run
directory under --output-root; never overwrites. No firmware change.
"""
import argparse
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Analysis"))

import pcm_event_ledger as ledger_mod  # noqa: E402
import chirp_capture_analysis  # noqa: E402  (reused load_template/norm_peak)

SAMPLE_HZ = 48000
LEFT_MARGIN = 64
WINDOW_FRAMES = 256
WINDOW_BYTES = WINDOW_FRAMES * 2
TRIALS_DEFAULT = ["toa-20261026-04", "toa-20261026-05", "toa-20261026-06",
                  "toaB-20261026-07", "toaB-20261026-08", "toaB-20261026-09",
                  "toaC-20261026-01", "toaC-20261026-02", "toaC-20261026-03"]
PEAK_REF = "c79bf25"

FORMAT_MARKERS = ("bad json", "odd byte", "frames*2", "non-numeric",
                  "bad json shape", "regress", "metadata",
                  "without capture", "no capture domain",
                  "HINT ", "BEST ", "not object")

RECEIVE_GUARD_S = 2.0
SLOT_TOLERANCE_S = 0.95
PLAYBACK_BASIS = "host_serial_receive_approx"


def finite_number(value):
    """Accept actual finite JSON numbers; booleans/strings are not times."""
    try:
        return isinstance(value, (int, float)) and not isinstance(value, bool) \
            and math.isfinite(value)
    except OverflowError:
        return False


def playback_context(trial_input):
    """Validate frozen trial references without I/O or a TOA-based fallback.

    play_span[1] records the asynchronous playback API return. The receive
    analysis window ends at start + WAV duration + receive guard, not there.
    """
    out = {"status": "UNKNOWN", "basis": PLAYBACK_BASIS,
           "source": "trial_summary.json + trial_manifest.json",
           "receive_guard_s": RECEIVE_GUARD_S,
           "slot_tolerance_s": SLOT_TOLERANCE_S, "problems": []}
    try:
        summary = trial_input["summary"]["content"]
        metadata = summary["metadata"]
        manifest = trial_input["trial_manifest"]["content"]
        spans = [summary["play_span"],
                 metadata["playback"]["play_span_api_s"],
                 manifest["playback"]["play_span_api_s"]]
        for span in spans:
            if not isinstance(span, list) or len(span) != 2 or \
                    not all(finite_number(v) and v >= 0 for v in span) or \
                    span[1] < span[0]:
                raise ValueError("invalid playback API span")
        if not all(span == spans[0] for span in spans):
            raise ValueError("conflicting playback API spans")
        wavs = [metadata["wav"], manifest["wav"]]
        for wav in wavs:
            duration = wav["duration_s"]
            onsets = wav["expected_onsets_s"]
            digest = wav["sha256"]
            if not finite_number(duration) or duration <= 0:
                raise ValueError("invalid WAV duration")
            if not isinstance(onsets, list) or not onsets or \
                    not all(finite_number(v) and 0 <= v < duration
                            for v in onsets) or \
                    any(b <= a for a, b in zip(onsets, onsets[1:])):
                raise ValueError("invalid WAV expected onsets")
            if not isinstance(digest, str) or len(digest) != 64 or \
                    any(c not in "0123456789abcdefABCDEF" for c in digest):
                raise ValueError("invalid WAV sha256")
            entry = wav.get("manifest_entry", {})
            for field in ("duration_s", "sha256"):
                if field in entry and entry[field] != wav[field]:
                    raise ValueError(f"conflicting WAV manifest_entry {field}")
        for field in ("duration_s", "expected_onsets_s", "sha256"):
            if wavs[0][field] != wavs[1][field]:
                raise ValueError(f"conflicting WAV {field}")
        start, returned = spans[0]
        end = start + wavs[0]["duration_s"]
        if not finite_number(end + RECEIVE_GUARD_S):
            raise ValueError("nonfinite analysis window")
        out.update({"status": "OK", "api_start_s": start,
                    "api_return_s": returned, "wav_duration_s": wavs[0]["duration_s"],
                    "wav_end_s": end, "analysis_start_s": start,
                    "analysis_end_s": end + RECEIVE_GUARD_S,
                    "analysis_interval": "[start,end)",
                    "expected_onsets_s": list(wavs[0]["expected_onsets_s"]),
                    "wav_sha256": wavs[0]["sha256"]})
    except (KeyError, TypeError, ValueError, OverflowError) as exc:
        out["problems"].append(str(exc))
    return out


def classify_playback_time(host_sec, context):
    """Classify one record's own receive time against immutable WAV slots."""
    row = {"host_sec": host_sec, "time_phase": "unknown",
           "playback_relative_s": None, "expected_slot_id": None,
           "slot_residual_s": None, "slot_status": "unknown",
           "slot_candidates": [], "phase_basis": PLAYBACK_BASIS,
           "phase_source": context.get("source", "unavailable")}
    if context.get("status") != "OK" or not finite_number(host_sec) or host_sec < 0:
        return row
    start = context["api_start_s"]
    relative = host_sec - start
    row["playback_relative_s"] = relative
    row["slot_status"] = "not_playback"
    if host_sec < start:
        row["time_phase"] = "prelude"
    elif host_sec >= context["analysis_end_s"]:
        row["time_phase"] = "postlude"
    else:
        row["time_phase"] = "playback"
        matches = []
        for slot, onset in enumerate(context["expected_onsets_s"]):
            residual = host_sec - (start + onset)
            if abs(residual) <= context["slot_tolerance_s"] or math.isclose(
                    abs(residual), context["slot_tolerance_s"],
                    rel_tol=0, abs_tol=1e-12):
                matches.append((slot, residual))
        row["slot_candidates"] = [slot for slot, _ in matches]
        row["slot_status"] = "ambiguous" if len(matches) > 1 else "extra"
        if len(matches) == 1:
            row.update({"expected_slot_id": matches[0][0],
                        "slot_residual_s": matches[0][1],
                        "slot_status": "matched"})
    return row


def frozen_json_input(path):
    """Freeze metadata bytes/hash even when JSON cannot provide a reference."""
    if not path.exists():
        return None
    raw = path.read_bytes()
    result = {"bytes": len(raw), "sha256": sha256_bytes(raw)}
    try:
        result["content"] = json.loads(raw.decode("utf-8"))
    except (UnicodeError, ValueError) as exc:
        result.update({"content": None, "read_error": str(exc)})
    return result


def format_error(problem: str) -> bool:
    """True when a ledger problem is an input-format defect rather than a
    legitimate coverage gap (e.g. a timer-aligned candidate missing PCM)."""
    if "outside every capture" in problem:
        return False
    return any(marker in problem for marker in FORMAT_MARKERS)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_path(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def run_git(args):
    return subprocess.run(["git"] + args, capture_output=True, text=True,
                          encoding="utf-8", errors="replace",
                          cwd=ROOT, check=True)


def make_run_dir(output_root: Path) -> Path:
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")
    candidate = output_root / stamp
    seq = 0
    while candidate.exists():
        seq += 1
        candidate = output_root / f"{stamp}-{seq}"
    candidate.mkdir(parents=True)
    return candidate


def freeze_snapshots(run_dir: Path, prior_envelope: Optional[Path] = None):
    snapshot_dir = run_dir / "source_snapshots"
    snapshot_dir.mkdir()
    frozen = {}
    sources = [("peak_backtrack", PEAK_REF), ("current_envelope", "worktree")]
    if prior_envelope is not None:
        sources.insert(1, ("original_envelope", prior_envelope))
    for version, ref in sources:
        dest = snapshot_dir / version
        dest.mkdir()
        for name in ("toa_estimator.c", "toa_estimator.h"):
            if ref == "worktree":
                content = (ROOT / "App" / "Detector" / name).read_bytes()
            elif isinstance(ref, Path):
                content = (ref / name).read_bytes()
            else:
                content = run_git([f"show", f"{ref}:App/Detector/{name}"]).stdout.encode()
            (dest / name).write_bytes(content)
            frozen[f"{version}:{name}"] = sha256_bytes(content)
    return snapshot_dir, frozen


def compile_cli(run_dir, snapshot_dir, version, opt):
    build = run_dir / "build"
    build.mkdir(exist_ok=True)
    exe = build / f"toa_replay_{version}_{opt[1:]}.exe"
    cc = shutil.which("gcc")
    cmd = [cc, "-std=c11", opt, "-Wall", "-Wextra", "-Werror", "-pedantic",
           "-I", str(snapshot_dir / version),
           str(ROOT / "Analysis" / "toa_replay_cli.c"),
           str(snapshot_dir / version / "toa_estimator.c"),
           "-o", str(exe), "-lm"]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    (build / f"build_{version}_{opt[1:]}.log").write_text(
        " ".join(cmd) + "\n" + result.stdout + result.stderr, encoding="utf-8")
    return result.returncode, exe


def run_cli(exe, window_paths, chunk=250):
    """Run the adapter over many windows, batching argv, return {label: row}.

    The adapter prints FILE=<path> as the LAST token so paths containing
    spaces survive the whitespace split of the earlier fields.
    """
    results = {}
    for offset in range(0, len(window_paths), chunk):
        part = window_paths[offset:offset + chunk]
        proc = subprocess.run([str(exe)] + [str(p) for p in part],
                              capture_output=True, text=True,
                              encoding="utf-8", errors="replace",
                              timeout=120)
        if proc.returncode != 0:
            raise RuntimeError(f"cli exit {proc.returncode}: {proc.stderr[:200]}")
        for line in proc.stdout.splitlines():
            marker = "FILE="
            index = line.rfind(marker)
            if index < 0:
                raise RuntimeError(f"cli row missing FILE marker: {line!r}")
            label = line[index + len(marker):]
            row = {}
            for item in line[:index].split():
                if "=" in item:
                    key, _, value = item.partition("=")
                    row[key] = value
            results[label] = row
    return results


def extract_window(pcm_path, frames, pcm_first, anchor):
    """Return (bytes, status). Status is OK or one of missing_left/truncated."""
    if pcm_first > anchor:
        return None, "missing_left"
    offset = (anchor - pcm_first) * 2
    if offset + WINDOW_BYTES > frames * 2:
        return None, "truncated_tail"
    with open(pcm_path, "rb") as handle:
        handle.seek(offset)
        data = handle.read(WINDOW_BYTES)
    if len(data) != WINDOW_BYTES:
        return None, "short_read"
    return data, "OK"


def build_ledger_for_trial(trial, log_root):
    records, parse_problems = ledger_mod.parse_trial_records(
        trial, log_root / trial / "run.jsonl")
    ledger_mod.assign_domains(records, trial)
    entries, scan_problems = ledger_mod.scan_pcm_directory(
        log_root / "PCM" / trial, trial)
    ledger = ledger_mod.build_ledger(records, trial, entries, scan_problems)
    # ledger.problems contains scan problems plus internal association/format
    # problems (capture regressions, HINT mismatches, ...). They must reach the
    # summary/TOOL_CHECK gate instead of being silently dropped.
    return ledger, entries, parse_problems + list(ledger.problems)


def first_int(fields, key):
    value = ledger_mod.to_int(fields.get(key))
    return value


def asset_check(assets_dir):
    manifest_path = assets_dir / "candidate_manifest.json"
    if not manifest_path.exists():
        return {"status": "UNKNOWN", "reason": "manifest missing"}
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    report = {"status": "OK", "files": {}}
    for name, spec in manifest.get("files", {}).items():
        path = assets_dir / name
        if not path.exists():
            report["status"] = "MISMATCH"
            continue
        actual = sha256_path(path)
        report["files"][name] = {"expected": spec.get("sha256"),
                                 "actual": actual,
                                 "ok": actual == spec.get("sha256")}
        if actual != spec.get("sha256"):
            report["status"] = "MISMATCH"
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trials", nargs="+", default=TRIALS_DEFAULT)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--root", type=Path, default=ROOT / "Logs" / "Chirp",
                        help="log tree (Logs/Chirp by default)")
    parser.add_argument("--assets-dir", type=Path, default=ROOT / "Data" / "Audio")
    parser.add_argument("--skip-synthetic", action="store_true")
    parser.add_argument("--prior-envelope", type=Path, default=None,
                        help="run dir snapshot to freeze as original_envelope")
    args = parser.parse_args()

    run_dir = make_run_dir(args.output_root)
    status = {"generated": str(run_dir), "hard_failures": []}

    # ---- git + input freezes ----
    git_status = subprocess.run(["git", "status", "--short"], cwd=ROOT,
                                capture_output=True, text=True).stdout
    head = run_git(["rev-parse", "HEAD"]).stdout.strip()
    prior_snapshot = None
    if args.prior_envelope is not None:
        prior_snapshot = args.prior_envelope / "source_snapshots" / "current_envelope"
        if not prior_snapshot.exists():
            status["hard_failures"].append(
                f"prior envelope snapshot missing: {prior_snapshot}")
    snapshot_dir, snapshot_hashes = freeze_snapshots(run_dir, prior_snapshot)
    versions = ["peak_backtrack", "current_envelope"]
    if prior_snapshot is not None:
        versions.insert(1, "original_envelope")
    version_meta = {}
    for version in versions:
        hashes = {name.split(":", 1)[1]: digest
                  for name, digest in snapshot_hashes.items()
                  if name.startswith(version + ":")}
        if version == "peak_backtrack":
            source = f"git:{PEAK_REF}"
            semantic = "peak-backtrack reference"
        elif version == "original_envelope":
            source = f"prior-run:{args.prior_envelope}"
            semantic = "prior envelope snapshot (provenance must be checked)"
        else:
            source = "worktree"
            semantic = "current envelope"
        version_meta[version] = {"source": source, "semantic": semantic,
                                  "head": head if version == "current_envelope" else None,
                                  "snapshot_hashes": hashes}
    gcc = shutil.which("gcc") or "gcc-missing"
    gcc_version = "unknown"
    if gcc != "gcc-missing":
        gcc_version = subprocess.run([gcc, "--version"], capture_output=True,
                                     text=True).stdout.splitlines()[0]

    # ---- input inventory + hashes ----
    inventory = {}
    missing = []
    for trial in args.trials:
        serial_path = args.root / trial / "run.jsonl"
        pcm_dir = args.root / "PCM" / trial
        trial_input = {"serial": None, "summary": frozen_json_input(
            pcm_dir / "trial_summary.json"), "trial_manifest": frozen_json_input(
            pcm_dir / "trial_manifest.json"), "pcm": {}, "pairs": 0}
        if not serial_path.exists():
            missing.append(f"{trial}: run.jsonl missing")
            inventory[trial] = trial_input
            continue
        trial_input["serial"] = {"bytes": serial_path.stat().st_size,
                                 "sha256": sha256_path(serial_path)}
        stems = sorted({p.stem for p in pcm_dir.glob("id*.pcm")} |
                       {j.stem for j in pcm_dir.glob("id*.json")}) \
            if pcm_dir.exists() else []
        for stem in stems:
            pcm_path = pcm_dir / (stem + ".pcm")
            json_path = pcm_dir / (stem + ".json")
            row = {"pcm": None, "json": None}
            if pcm_path.exists():
                row["pcm"] = {"bytes": pcm_path.stat().st_size,
                              "sha256": sha256_path(pcm_path)}
            if json_path.exists():
                row["json"] = {"bytes": json_path.stat().st_size,
                               "sha256": sha256_path(json_path)}
            trial_input["pcm"][stem] = row
            if row["pcm"] and row["json"]:
                trial_input["pairs"] += 1
        inventory[trial] = trial_input
    if missing:
        status["hard_failures"].extend(missing)
    playback_contexts = {trial: playback_context(inputs)
                         for trial, inputs in inventory.items()}

    # ---- ledger + window extraction + replay ----
    assets = asset_check(args.assets_dir)
    if assets.get("status") != "OK" and not args.skip_synthetic:
        status["hard_failures"].append(f"assets: {assets.get('status')}")
    binaries = {}
    build_errors = []
    for version in versions:
        for opt in ("-O0", "-O2"):
            code, exe = compile_cli(run_dir, snapshot_dir, version, opt)
            if code != 0:
                build_errors.append((version, opt, f"build rc {code}"))
                continue
            binaries[(version, opt)] = exe
    if build_errors:
        status["hard_failures"].extend(
            f"build {v} {o}: {r}" for v, o, r in build_errors)

    windows_dir = run_dir / "windows"
    windows_dir.mkdir()
    replay_rows = []
    ledgers = {}
    conservation = {"raw_lines": 0, "events": 0, "sound_total": 0,
                    "sound_replayable_pcm": 0, "windows_ok": 0,
                    "pcm_total": 0, "pcm_unused": 0}
    per_trial = {}
    format_errors = []

    for trial in args.trials:
        serial_path = args.root / trial / "run.jsonl"
        if not serial_path.exists():
            continue
        ledger, entries, problems = build_ledger_for_trial(trial, args.root)
        for entry in entries:
            if entry["has_pcm"]:
                entry["_path"] = args.root / "PCM" / trial / \
                    f"id{entry['id']}.pcm"
        conservation["raw_lines"] += ledger_mod_to_lines(serial_path)
        conservation["events"] += len(ledger.events)
        trial_detail = {"events": len(ledger.events), "problems": problems,
                        "sounds": 0, "unique_windows": 0,
                        "ambiguous": 0, "orphan_sounds": 0}
        pcm_by_id = {}
        for entry in entries:
            if entry["has_pcm"] and entry["has_json"] and entry["valid"]:
                pcm_by_id[entry["id"]] = entry
        for problem in problems:
            if format_error(problem):
                format_errors.append(f"{trial}: {problem}")
        for event in ledger.events:
            if event.kind != "$SOUND_CAND":
                continue
            trial_detail["sounds"] += 1
            conservation["sound_total"] += 1
            first = first_int(event.fields, "FIRST")
            serial_toa = event.fields.get("TOA_US")
            time_fields = classify_playback_time(event.host_sec,
                                                 playback_contexts[trial])
            edges = [e for e in ledger.edges
                     if e.source == event.key and e.kind == "sound-capture"]
            if not edges:
                trial_detail["orphan_sounds"] += 1
                replay_rows.append({
                    "trial": trial, "kind": "SOUND", "sound_key": event.key,
                    "seq": event.fields.get("SEQ"), "first_frame": first,
                    "serial_toa_us": serial_toa, "edges": 0,
                    "offline_window_status": "no_pcm",
                    "online_window_status": "UNKNOWN",
                    "firmware_binding": "UNKNOWN", "replays": [],
                    **time_fields})
                continue
            ambiguous = any(e.status == "AMBIGUOUS" for e in edges)
            if ambiguous:
                trial_detail["ambiguous"] += 1
            prior_rows = len(replay_rows)
            for edge in edges:
                target = edge.target  # capture:<domain>:<id>
                ident = target.rsplit(":", 1)[-1]
                try:
                    ident_int = int(ident)
                except ValueError:
                    continue
                entry = pcm_by_id.get(ident_int)
                if entry is None:
                    continue
                pcm_first = int(entry["json"].get("first_frame", 0))
                frames = int(entry["json"].get("frames", 0))
                anchor = max(first - LEFT_MARGIN, 0) if first is not None else None
                if anchor is None:
                    continue
                data, win_status = extract_window(
                    entry["_path"], frames, pcm_first, anchor)
                row = {"trial": trial, "kind": "WINDOW", "sound_key": event.key,
                       "seq": event.fields.get("SEQ"), "first_frame": first,
                       "serial_toa_us": serial_toa, "edges": len(edges),
                       "pcm_id": ident_int, "edge_status": edge.status,
                       "anchor": anchor,
                       "offline_window_status": win_status,
                       "online_window_status": "UNKNOWN",
                       "firmware_binding": "UNKNOWN",
                       "firmware_note": "no BOOT/image sha captured; "
                                        "on-device version not provable",
                       "replays": [], **time_fields}
                if win_status == "OK":
                    if not ambiguous:
                        conservation["windows_ok"] += 1
                        trial_detail["unique_windows"] += 1
                    label = f"{trial}:{event.key}:pcm{ident_int}"
                    window_path = windows_dir / f"{label.replace(':', '__')}.pcm"
                    window_path.write_bytes(data)
                    (window_path.with_suffix(".json")).write_text(
                        json.dumps({"trial": trial, "sound_key": event.key,
                                    "pcm_id": ident_int, "anchor": anchor,
                                    "first_frame": first}), encoding="utf-8")
                    win_rows = []
                    for (version, opt), exe in binaries.items():
                        runs = []
                        for repeat in (1, 2):
                            out = run_cli(exe, [window_path])
                            fields = out.get(str(window_path), {})
                            valid = fields.get("VALID") == "1"
                            fraction = float(fields.get("FRACTION", "0.0"))
                            reason = fields.get("REASON", "missing")
                            runs.append({"repeat": repeat, "valid": valid,
                                         "fraction_frames": fraction,
                                         "reason": reason})
                        win_rows.append({"version": version, "opt": opt,
                                         "runs": runs})
                    row["replays"] = win_rows
                    replay_rows.append(row)
                else:
                    replay_rows.append(row)
            if len(replay_rows) == prior_rows:
                replay_rows.append({
                    "trial": trial, "kind": "SOUND", "sound_key": event.key,
                    "seq": event.fields.get("SEQ"), "first_frame": first,
                    "serial_toa_us": serial_toa, "edges": len(edges),
                    "offline_window_status": "no_pcm",
                    "online_window_status": "UNKNOWN",
                    "firmware_binding": "UNKNOWN", "replays": [],
                    **time_fields})
        ledgers[trial] = {
            "events": [{"kind": e.kind, "trial": e.trial, "line_no": e.line_no,
                        "host_sec": e.host_sec, "domain": e.domain,
                        "key": e.key, "fields": e.fields,
                        "parse_problem": e.parse_problem,
                        **classify_playback_time(e.host_sec,
                                                 playback_contexts[trial])}
                       for e in ledger.events],
            "edges": [{"source": e.source, "target": e.target,
                       "kind": e.kind, "status": e.status, "detail": e.detail}
                      for e in ledger.edges],
            "domains": {domain: {
                "conflict": bool(dom.get("conflict", False)),
                "n_captures": len(dom.get("captures", [])),
                "n_sounds": len(dom.get("sounds", [])),
                "n_searches": len(dom.get("searches", [])),
                "n_sound_times": len(dom.get("sound_times", []))}
                for domain, dom in ledger.domains.items()},
            "isolation": ledger.isolation,
            "problems": problems,
        }
        trial_detail["windows_ok"] = sum(
            1 for r in replay_rows
            if r["trial"] == trial and r["offline_window_status"] == "OK")
        per_trial[trial] = trial_detail

    conservation["sound_replayable_pcm"] = sum(
        1 for r in replay_rows if r["kind"] == "WINDOW")
    conservation["pcm_total"] = sum(v.get("pairs", 0)
                                    for v in inventory.values())
    trial_used = {}
    for row in replay_rows:
        if row["kind"] == "WINDOW" and row["offline_window_status"] == "OK":
            trial_used.setdefault(row["trial"], set()).add(row["pcm_id"])
    conservation["pcm_unused"] = sum(
        max(0, inventory.get(t, {}).get("pairs", 0) - len(trial_used.get(t, set())))
        for t in args.trials)

    # ---- serialize outputs ----
    (run_dir / "manifest.json").write_text(
        json.dumps({
            "head": head, "git_status": git_status,
            "algorithm_snapshots": snapshot_hashes,
            "gcc": gcc_version, "build_command":
                "gcc -std=c11 {O0,O2} -Wall -Wextra -Werror -pedantic "
                "-I <snap> Analysis/toa_replay_cli.c <snap>/toa_estimator.c -lm",
            "assets": assets, "inventory": inventory,
            "versions": versions, "version_meta": version_meta,
        }, ensure_ascii=False, indent=2), encoding="utf-8")
    (run_dir / "ledger.json").write_text(
        json.dumps({"conservation": conservation, "per_trial": per_trial,
                    "ledgers": ledgers, "hard_failures": status["hard_failures"],
                    "format_errors": format_errors},
                   ensure_ascii=False, indent=2), encoding="utf-8")
    with (run_dir / "records.jsonl").open("w", encoding="utf-8") as handle:
        for trial, ledger in ledgers.items():
            for event in ledger["events"]:
                handle.write(json.dumps(event, ensure_ascii=False) + "\n")
    with (run_dir / "ledger.csv").open("w", encoding="utf-8",
                                       newline="") as handle:
        import csv
        writer = csv.writer(handle)
        writer.writerow(["trial", "source", "target", "kind", "status", "detail"])
        for trial, ledger in ledgers.items():
            for edge in ledger["edges"]:
                writer.writerow([trial, edge["source"], edge["target"],
                                 edge["kind"], edge["status"], edge["detail"]])
    with (run_dir / "replay.jsonl").open("w", encoding="utf-8") as handle:
        for row in replay_rows:
            handle.write(json.dumps(row, ensure_ascii=False) + "\n")

    # ---- summary ----
    diagnostics = aggregate_diagnostics(replay_rows, versions,
                                        playback_contexts, ledgers)
    synthetic_results, corr_ref = [], {}
    if not args.skip_synthetic:
        synthetic_results, corr_ref = run_synthetic(
            run_dir, binaries, args.assets_dir)
    synthetic_stats = None
    if synthetic_results:
        synthetic_stats = summarize_synthetic(synthetic_results, versions)
    determinism_rows = 0
    determinism_bad = 0
    for row in replay_rows:
        for rw in row.get("replays", []):
            run_a, run_b = rw["runs"]
            if run_a["valid"] == run_b["valid"] and \
                    run_a["fraction_frames"] == run_b["fraction_frames"]:
                determinism_rows += 1
            else:
                determinism_bad += 1
    for result in synthetic_results:
        for score in result["scores"].values():
            for run_a, run_b in zip(score["runs"][::2], score["runs"][1::2]):
                if run_a["valid"] == run_b["valid"] and \
                        run_a["fraction"] == run_b["fraction"]:
                    determinism_rows += 1
                else:
                    determinism_bad += 1
    determinism_opt_mismatch = 0
    for row in replay_rows:
        opt_fractions = {}
        for rw in row.get("replays", []):
            key = (rw["version"], rw["opt"])
            run_a = rw["runs"][0]
            opt_fractions[key] = (run_a["valid"], run_a["fraction_frames"])
        for version in versions:
            o0 = opt_fractions.get((version, "-O0"))
            o2 = opt_fractions.get((version, "-O2"))
            if o0 is not None and o2 is not None and o0 != o2:
                determinism_opt_mismatch += 1
    for result in synthetic_results:
        per_version = {}
        for label, score in result["scores"].items():
            per_version[label] = (score["runs"][0]["valid"],
                                  score["runs"][0]["fraction"])
        for version in versions:
            o0 = per_version.get(f"{version}:-O0")
            o2 = per_version.get(f"{version}:-O2")
            if o0 is not None and o2 is not None and o0 != o2:
                determinism_opt_mismatch += 1
    tool_check_ok = (determinism_bad == 0 and determinism_opt_mismatch == 0
                     and not status["hard_failures"] and not format_errors)
    summary = {"run_dir": str(run_dir), "conservation": conservation,
               "versions": versions, "version_meta": version_meta,
               "per_trial": per_trial, "diagnostics": diagnostics,
               "synthetic": {"windows": len(synthetic_results),
                             "corr_reference": corr_ref,
                             "stats": synthetic_stats},
               "format_errors": format_errors,
               "reproducibility": {"repeat_rows": determinism_rows,
                                   "repeat_mismatch": determinism_bad,
                                   "opt_mismatch": determinism_opt_mismatch},
               "hard_failures": status["hard_failures"], "assets": assets,
               "conclusions": {"TOOL_CHECK": "PASS" if tool_check_ok else "FAIL",
                               "DATA_COVERAGE": {
                                   k: detail_summary(d) for k, d in per_trial.items()},
                               "STANDARD_ID": "reported_original_only",
                               "PHYSICAL_TOA_ACCURACY": "NOT_VERIFIED"}}
    (run_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
    if status["hard_failures"] or format_errors:
        print("HARD FAILURES:", status["hard_failures"], flush=True)
        print("FORMAT ERRORS:", format_errors, flush=True)
        return 1
    print("REPLAY_OK", flush=True)
    return 0


def ledger_mod_to_lines(serial_path):
    """Line count of a trial serial file (for conservation reporting)."""
    with serial_path.open(encoding="utf-8") as handle:
        return sum(1 for _ in handle)


def detail_summary(detail):
    return {"events": detail["events"], "sounds": detail["sounds"],
            "unique_windows": detail["unique_windows"],
            "ambiguous": detail["ambiguous"], "orphan_sounds": detail["orphan_sounds"]}


# --------------------------------------------------------------------------
# Task 4: known-onset synthetic windows driven through the same C binaries.
# --------------------------------------------------------------------------
SLOT_SPACING_US = 2_000_000
N_SLOTS = 10
RAMP_STEP = 117  # per-sample rise so peak stays below int16 saturation


def synth_ramp(onset, amp, noise, echo_delay, echo_gain, seed=7):
    rng = __import__("random").Random(seed)
    signal = [0] * WINDOW_FRAMES
    saturations = 0
    floor = 128.0
    for i in range(WINDOW_FRAMES):
        if i >= onset:
            value = amp * RAMP_STEP * (i - onset + 1)
        else:
            value = 0.0
        if echo_delay and i - echo_delay >= onset and i - echo_delay < WINDOW_FRAMES:
            echo = (amp * RAMP_STEP * (i - echo_delay - onset + 1)) * echo_gain
            value += echo
        value += rng.randint(-noise, noise) if noise else 0.0
        clip = 32767 if value > 32767 else (-32768 if value < -32768 else value)
        if clip != value:
            saturations += 1
        signal[i] = int(clip)
    peak = max(abs(v) for v in signal)
    limit = max(int(floor), peak // 8) if peak else 0
    crossing = None
    for i in range(1, WINDOW_FRAMES):
        before, after = abs(signal[i - 1]), abs(signal[i])
        if before < limit <= after:
            crossing = (i - 1) + (limit - before) / (after - before)
            break
    crossing = crossing if crossing is not None else None
    return signal, {"signal_start": onset, "amp_cross": crossing,
                    "limit": limit, "peak": peak,
                    "saturations": saturations}


def synth_chirp(onset, assets_dir, noise, echo_delay, echo_gain, rng):
    """Slice the standard chirp asset around its fade start.

    Returns (signal, meta) where meta.amp_cross is the numeric amplitude
    threshold crossing inside the 256 window (limit = max(128, peak//8)),
    and signal_start is the slice-relative fade start.
    """
    import struct
    wav_path = assets_dir / "standard_chirp_2-6khz_10x_20s.wav"
    if not wav_path.exists():
        return None, {"skipped": "asset missing"}
    signal = [0] * WINDOW_FRAMES
    saturations = 0
    with open(wav_path, "rb") as handle:
        base = 48000  # first chirp fade start in the file
        for i in range(WINDOW_FRAMES):
            frame = base - onset + i
            if frame < 0 or frame >= 960000:
                value = 0.0
            else:
                handle.seek(44 + frame * 2)
                raw = struct.unpack("<h", handle.read(2))[0]
                value = float(raw) / 32768.0 * 24000.0
            if echo_delay and i >= echo_delay:
                echo_frame = base - onset + i - echo_delay
                if 0 <= echo_frame < 960000:
                    handle.seek(44 + echo_frame * 2)
                    echo_raw = struct.unpack("<h", handle.read(2))[0]
                    value += float(echo_raw) / 32768.0 * 24000.0 * echo_gain
            value += rng.randint(-noise, noise) if noise else 0.0
            clip = 32767 if value > 32767 else (-32768 if value < -32768 else value)
            if clip != value:
                saturations += 1
            signal[i] = int(clip)
    peak = max(abs(v) for v in signal)
    limit = max(128, peak // 8) if peak else 0
    crossing = None
    for i in range(1, WINDOW_FRAMES):
        before, after = abs(signal[i - 1]), abs(signal[i])
        if before < limit <= after:
            crossing = (i - 1) + (limit - before) / (after - before)
            break
    return signal, {"signal_start": onset, "amp_cross": crossing,
                    "limit": limit, "peak": peak,
                    "saturations": saturations}


def corr_reference(assets_dir, rng):
    """norm_peak(diagnostic) reference on a long std-chirp slice (reuse)."""
    import struct
    wav_path = assets_dir / "standard_chirp_2-6khz_10x_20s.wav"
    template_path = assets_dir / "chirp_2-6khz_50ms_template.wav"
    if not wav_path.exists() or not template_path.exists():
        return {"status": "SKIPPED", "reason": "assets missing"}
    try:
        template = chirp_capture_analysis.load_template(template_path)
    except (ValueError, OSError):
        return {"status": "SKIPPED", "reason": "template unreadable"}
    samples = []
    with open(wav_path, "rb") as handle:
        for i in range(48000, 48000 + 4096):
            handle.seek(44 + i * 2)
            samples.append(struct.unpack("<h", handle.read(2))[0])
    signal = [float(v) for v in samples]
    import numpy as np
    lag, corr = chirp_capture_analysis.norm_peak(
        np.asarray(signal, dtype=np.float64), template)
    return {"status": "OK", "n": len(samples),
            "corr_lag_frames": lag, "corr": corr}


def run_synthetic(run_dir, binaries, assets_dir, seed=7):
    """Generate known-onset windows and run both frozen versions over them.

    Every binary is invoked twice per batch so synthetic windows participate
    in the repeat/O0-O2 determinism gates.
    """
    rng = __import__("random").Random(seed)
    windows = []
    meta = []
    # ramp grid: phase x amplitude x noise x echo(delay 24/96; gain 0.5/1.5)
    for phase in range(64):
        for amp in (0.25, 0.5, 1.0):
            for noise in (0, 32, 128):
                for echo_delay, echo_gain in ((0, 0.0), (24, 0.5), (24, 1.5),
                                              (96, 0.5), (96, 1.5)):
                    signal, info = synth_ramp(phase, amp, noise,
                                              echo_delay, echo_gain, seed)
                    windows.append(signal)
                    info.update({"signal": "ramp", "phase": phase, "amp": amp,
                                 "noise": noise, "echo_delay": echo_delay,
                                 "echo_gain": echo_gain})
                    meta.append(info)
    # std chirp slice: phase x noise x echo (bounded coverage)
    for phase in (0, 8, 16, 24, 32, 40, 48, 56, 63):
        for noise in (0, 128):
            for echo_delay, echo_gain in ((0, 0.0), (24, 1.5)):
                signal, info = synth_chirp(phase, assets_dir, noise,
                                           echo_delay, echo_gain, rng)
                if signal is not None:
                    windows.append(signal)
                    info.update({"signal": "stdchirp", "phase": phase,
                                 "amp": 1.0, "noise": noise,
                                 "echo_delay": echo_delay,
                                 "echo_gain": echo_gain})
                    meta.append(info)
    # silence and single-spike sanity
    windows.append([0] * WINDOW_FRAMES)
    meta.append({"signal": "silence", "phase": 0, "amp": 1.0, "noise": 0,
                 "echo_delay": 0, "echo_gain": 0.0, "signal_start": None,
                 "amp_cross": None, "limit": None, "peak": 0,
                 "saturations": 0})
    spike = [0] * WINDOW_FRAMES
    spike[128] = 30000
    windows.append(spike)
    meta.append({"signal": "spike", "phase": 0, "amp": 1.0, "noise": 0,
                 "echo_delay": 0, "echo_gain": 0.0, "signal_start": 128,
                 "amp_cross": 128.0, "limit": 3750, "peak": 30000,
                 "saturations": 0})
    synth_dir = run_dir / "synthetic_windows"
    synth_dir.mkdir()
    paths = []
    for index, signal in enumerate(windows):
        path = synth_dir / f"syn{index:04d}.pcm"
        raw = bytearray()
        for value in signal:
            raw += (int(value) & 0xFFFF).to_bytes(2, "little")
        path.write_bytes(bytes(raw))
        paths.append(path)
    results = [{"index": index, **meta[index], "scores": {}}
               for index in range(len(paths))]
    for (version, opt), exe in binaries.items():
        for repeat in (1, 2):
            out = run_cli(exe, paths)
            for index, path in enumerate(paths):
                fields = out.get(str(path), {})
                label = f"{version}:{opt}"
                results[index]["scores"].setdefault(label, {"runs": []})["runs"].append(
                    {"repeat": repeat, "valid": fields.get("VALID") == "1",
                     "fraction": float(fields.get("FRACTION", "0.0")),
                     "reason": fields.get("REASON")})
    (run_dir / "synthetic_known_input.json").write_text(
        json.dumps(results, ensure_ascii=False), encoding="utf-8")
    return results, corr_reference(assets_dir, rng)


# --------------------------------------------------------------------------
# Task 5: aggregate diagnostics (interval residual, slots, version delta).
# --------------------------------------------------------------------------
def nominal_us(anchor, fraction):
    return (anchor + fraction) * 1e6 / SAMPLE_HZ


def slot_diagnostics(values_us):
    """Map nominal TOAs to slots via integer spacing; count collisions/gaps."""
    values = sorted(values_us)
    if len(values) < 2:
        return {"samples": len(values), "slots": {}, "verdict_possible": False}
    base = values[0]
    slots = {}
    collisions = 0
    for value in values:
        distance = value - base
        k = round(distance / SLOT_SPACING_US)
        if k < 0 or k >= N_SLOTS:
            continue
        if abs(distance - k * SLOT_SPACING_US) > SLOT_SPACING_US * 0.25:
            continue
        slots.setdefault(k, []).append(value)
        if len(slots[k]) > 1:
            collisions += 1
    missing = [k for k in range(N_SLOTS) if k not in slots]
    complete = len(slots) == N_SLOTS and not collisions
    return {"samples": len(values), "slots": {str(k): len(v) for k, v in
                                             slots.items()},
            "collisions": collisions, "missing": len(missing),
            "verdict_possible": complete}


def fixed_slot_coverage(sources, context):
    """Count every source; duplicates and rejected links remain inspectable."""
    slots = {}
    extra, ambiguous, unknown, orphan, invalid = [], [], [], [], []
    for source in sources:
        phase = source["time_phase"]
        if phase == "unknown":
            unknown.append(source)
        if source.get("link_status") == "orphan":
            orphan.append(source)
        if phase != "playback":
            continue
        if source["slot_status"] == "extra":
            extra.append(source)
        if source["slot_status"] == "ambiguous" or \
                source.get("link_status") == "ambiguous":
            ambiguous.append(source)
        if source.get("version_valid") is False:
            invalid.append(source)
        if source.get("eligible", True) and source["expected_slot_id"] is not None:
            slots.setdefault(str(source["expected_slot_id"]), []).append(source)
    expected = list(range(len(context.get("expected_onsets_s", []))))
    missing = [slot for slot in expected if str(slot) not in slots]
    duplicates = {slot: values for slot, values in slots.items() if len(values) > 1}
    complete = context.get("status") == "OK" and bool(expected) and not missing
    rejected = any(source["time_phase"] == "playback" and
                   not source.get("eligible", True) for source in sources)
    return {"expected_slots": expected, "slots": slots,
            "covered_slots": sorted(int(slot) for slot in slots),
            "missing_slots": missing, "coverage_complete": complete,
            "unique_complete": complete and not any((duplicates, extra,
                                                       ambiguous, unknown,
                                                       rejected)),
            "duplicates": duplicates, "extra": extra,
            "ambiguous": ambiguous, "unknown": unknown, "orphan": orphan,
            "invalid": invalid, "source_events": sources}


def version_validity_delta(replay_rows, versions):
    """Compare validity without filtering away either side of a transition."""
    per_window = {}
    for row in replay_rows:
        if row["offline_window_status"] != "OK":
            continue
        for rw in row.get("replays", []):
            per_window.setdefault((row["trial"], row["sound_key"]),
                                  {})[rw["version"]] = rw["runs"][0]
    result = {}
    for a, b in zip(versions, versions[1:]):
        item = {"both_valid": 0, "valid_to_invalid": [],
                "invalid_to_valid": [], "both_invalid": 0}
        for key, runs in per_window.items():
            ra, rb = runs.get(a), runs.get(b)
            if not ra or not rb:
                continue
            if ra["valid"] and rb["valid"]:
                item["both_valid"] += 1
            elif ra["valid"]:
                item["valid_to_invalid"].append("|".join(key))
            elif rb["valid"]:
                item["invalid_to_valid"].append("|".join(key))
            else:
                item["both_invalid"] += 1
        result[f"{a}_to_{b}"] = item
    return result


def playback_diagnostics(replay_rows, versions, contexts, ledgers):
    """Separate coarse capture, unique SOUND/PCM and valid-TOA populations.

    Capture receive times never substitute for SOUND receive times. Full
    ledger edges, including ambiguities, determine association eligibility.
    """
    by_trial = {}
    playback_rows = []
    for trial in sorted(set(contexts) | set(ledgers)):
        context = contexts.get(trial, {"status": "UNKNOWN"})
        ledger = ledgers.get(trial, {"events": [], "edges": []})
        events = []
        phase_counts = {}
        for event in ledger["events"]:
            source = {**event, **classify_playback_time(event.get("host_sec"),
                                                       context)}
            events.append(source)
            counts = phase_counts.setdefault(source["kind"], {
                "prelude": 0, "playback": 0, "postlude": 0, "unknown": 0})
            counts[source["time_phase"]] += 1
        captures = [event for event in events if event["kind"] == "$CHIRP_CAPTURE"]
        sounds = [event for event in events if event["kind"] == "$SOUND_CAND"]
        trial_rows = [row for row in replay_rows if row["trial"] == trial]
        linked_sources, by_version = [], {v: [] for v in versions}
        for sound in sounds:
            source = dict(sound)
            edges = [edge for edge in ledger["edges"]
                     if edge["kind"] == "sound-capture" and
                     edge["source"] == sound["key"]]
            pcm_edges = [edge for edge in ledger["edges"]
                         if edge["kind"] == "pcm-capture" and
                         any(edge["target"] == cap["target"] for cap in edges)]
            rows = [row for row in trial_rows if row["sound_key"] == sound["key"]]
            ambiguous = len(edges) > 1 or len(pcm_edges) > 1 or len(rows) > 1 or \
                any(edge["status"] == "AMBIGUOUS" for edge in edges + pcm_edges)
            eligible = not ambiguous and len(edges) == 1 and \
                edges[0]["status"] == "UNIQUE" and len(pcm_edges) == 1 and \
                pcm_edges[0]["status"] == "LINKED" and len(rows) == 1 and \
                rows[0]["kind"] == "WINDOW" and \
                rows[0]["offline_window_status"] == "OK" and \
                rows[0].get("edge_status") == "UNIQUE" and \
                pcm_edges[0]["source"] == f"pcm:{trial}:{rows[0].get('pcm_id')}"
            source.update({"eligible": eligible,
                           "link_status": "linked" if eligible else
                           ("ambiguous" if ambiguous else "orphan"),
                           "sound_capture_edges": edges,
                           "pcm_capture_edges": pcm_edges,
                           "replay_windows": [{k: row.get(k) for k in (
                               "kind", "pcm_id", "edge_status",
                               "offline_window_status")} for row in rows]})
            linked_sources.append(source)
            for version in versions:
                version_source = dict(source)
                run = next((rw["runs"][0] for row in rows
                            for rw in row.get("replays", [])
                            if rw["version"] == version and rw["opt"] == "-O0"),
                           None)
                valid = bool(run and run["valid"])
                version_source.update({"version_valid": valid,
                                       "eligible": eligible and valid})
                by_version[version].append(version_source)
        phase_counts.setdefault("$SOUND_CAND", {
            "prelude": 0, "playback": 0, "postlude": 0, "unknown": 0})
        phase_counts.setdefault("$CHIRP_CAPTURE", {
            "prelude": 0, "playback": 0, "postlude": 0, "unknown": 0})
        scoped_rows = [row for row in trial_rows if classify_playback_time(
            row.get("host_sec"), context)["time_phase"] == "playback"]
        playback_rows.extend(scoped_rows)
        by_trial[trial] = {
            "context": context, "phase_counts": phase_counts,
            "capture_log": {"population": "capture_log_coarse_receive_time",
                            **fixed_slot_coverage(captures, context)},
            "linked_sound_pcm": {"population": "unique_sound_capture_pcm",
                                 **fixed_slot_coverage(linked_sources, context)},
            "by_version": {version: {"population": "valid_toa_unique_sound_pcm",
                                     **fixed_slot_coverage(sources, context)}
                           for version, sources in by_version.items()},
            "version_validity_delta": version_validity_delta(scoped_rows, versions)}
    return {"population": "playback_receive_window", "basis": PLAYBACK_BASIS,
            "by_trial": by_trial,
            "version_validity_delta": version_validity_delta(playback_rows, versions)}


def aggregate_diagnostics(replay_rows, versions=("peak_backtrack",
                                                 "current_envelope"),
                          playback_contexts=None, ledgers=None):
    """Per-version per-trial slot stats plus version cross-reference."""
    by_trial_version = {}
    for row in replay_rows:
        if row["offline_window_status"] != "OK":
            continue
        anchor = row["anchor"]
        for rw in row["replays"]:
            if rw["opt"] != "-O0":
                continue
            run0 = rw["runs"][0]
            if not run0["valid"]:
                continue
            key = (row["trial"], rw["version"])
            by_trial_version.setdefault(key, []).append(
                nominal_us(anchor, run0["fraction_frames"]))
    diagnostics = {"by_trial_version": {}, "population": "legacy_all_events"}
    per_window = {}
    for row in replay_rows:
        if row["offline_window_status"] != "OK":
            continue
        for rw in row["replays"]:
            run0 = rw["runs"][0]
            per_window.setdefault((row["trial"], row["sound_key"]),
                                  {})[rw["version"]] = run0
    pairs = [(a, b) for a, b in zip(versions, versions[1:])]
    deltas = {f"{a}_to_{b}": [] for a, b in pairs}
    for key, runs in per_window.items():
        for a, b in pairs:
            ra, rb = runs.get(a), runs.get(b)
            if ra and rb and ra["valid"] and rb["valid"]:
                deltas[f"{a}_to_{b}"].append(
                    rb["fraction_frames"] - ra["fraction_frames"])
    validity_delta = {}
    for a, b in pairs:
        valid_to_invalid, invalid_to_valid = [], []
        both_valid = both_invalid = 0
        for key, runs in per_window.items():
            ra, rb = runs.get(a), runs.get(b)
            if not ra or not rb:
                continue
            if ra["valid"] and rb["valid"]:
                both_valid += 1
            elif ra["valid"]:
                valid_to_invalid.append("|".join(key))
            elif rb["valid"]:
                invalid_to_valid.append("|".join(key))
            else:
                both_invalid += 1
        validity_delta[f"{a}_to_{b}"] = {
            "both_valid": both_valid,
            "valid_to_invalid": valid_to_invalid,
            "invalid_to_valid": invalid_to_valid,
            "both_invalid": both_invalid}
    diagnostics["version_validity_delta"] = validity_delta
    for key in sorted(by_trial_version):
        trial, version = key
        diagnostics["by_trial_version"].setdefault(trial, {})[version] = \
            slot_diagnostics(by_trial_version[key])
    # Cross-round normalized per-slot deviation per group (A/B/C only; pooled
    # mixing across differently-bound groups is not meaningful). Groups with
    # missing slots or extra candidates cannot support a full-10 verdict.
    import statistics
    groups = {"A": ("toa-20261026-04", "toa-20261026-05", "toa-20261026-06"),
              "B": ("toaB-20261026-07", "toaB-20261026-08", "toaB-20261026-09"),
              "C": ("toaC-20261026-01", "toaC-20261026-02", "toaC-20261026-03")}
    cross = {}
    for group, trials in groups.items():
        group_stats = {"trials": list(trials), "per_version": {}}
        for version in versions:
            trial_slots = {}
            for trial in trials:
                values_us = by_trial_version.get((trial, version), [])
                if not values_us:
                    continue
                base = min(values_us)
                slots = {}
                for value in values_us:
                    distance = value - base
                    k = round(distance / SLOT_SPACING_US)
                    if k < 0 or k >= N_SLOTS:
                        continue
                    if abs(distance - k * SLOT_SPACING_US) > 500_000.0:
                        continue
                    slots.setdefault(k, []).append(value - base)
                trial_slots[trial] = slots
            slots = {}
            complete = True
            for k in range(N_SLOTS):
                values = []
                n_rounds = 0
                for trial, t_slots in trial_slots.items():
                    here = t_slots.get(k)
                    if here is None or len(here) != 1:
                        continue
                    n_rounds += 1
                    values.append(here[0])
                if not values or n_rounds != len(trials):
                    complete = False
                    continue
                mean = statistics.mean(values)
                deviations = [v - mean for v in values]
                slots[str(k)] = {
                    "n": n_rounds,
                    "max_dev_us": round(max(abs(d) for d in deviations), 2),
                    "sigma_us": round((sum(d * d for d in deviations) /
                                       len(deviations)) ** 0.5, 2)}
            missing = [k for k in range(N_SLOTS) if str(k) not in slots]
            group_stats["per_version"][version] = {
                "slots": slots, "missing_slots": missing,
                "full_10_slot_consistency_possible": complete}
        cross[group] = group_stats
    diagnostics["cross_round_by_group"] = cross
    for name, values in deltas.items():
        if values:
            diagnostics["version_delta_frames_" + name] = {
                "n": len(values),
                "mean_frames": round(statistics.mean(values), 4),
                "stdev_frames": round(statistics.pstdev(values), 4),
                "min_frames": round(min(values), 4),
                "max_frames": round(max(values), 4),
                "mean_us": round(statistics.mean(values) * 1e6 / SAMPLE_HZ, 2)}
    diagnostics["playback_window"] = playback_diagnostics(
        replay_rows, versions, playback_contexts or {}, ledgers or {})
    return diagnostics


def summarize_synthetic(results, versions):
    """Per-version per-signal validity, bias and jump over the synthetic grid."""
    import statistics
    out = {"per_version": {}}
    for version in versions:
        per_signal = {}
        for result in results:
            signal = result["signal"]
            entry = per_signal.setdefault(
                signal, {"n": 0, "valid": 0, "bias_cross_us": [],
                         "bias_start_us": [], "jump_us": []})
            entry["n"] += 1
            score = result["scores"].get(f"{version}:-O2")
            if score and score["runs"] and score["runs"][0]["valid"]:
                entry["valid"] += 1
                fraction = score["runs"][0]["fraction"]
                amp_cross = result.get("amp_cross")
                signal_start = result.get("signal_start")
                if amp_cross is not None:
                    entry["bias_cross_us"].append(
                        (fraction - amp_cross) * 1e6 / SAMPLE_HZ)
                if signal_start is not None:
                    entry["bias_start_us"].append(
                        (fraction - signal_start) * 1e6 / SAMPLE_HZ)
            fractions = [run_["fraction"]
                         for label, score2 in result["scores"].items()
                         if label.startswith(version)
                         for run_ in score2["runs"] if run_["valid"]]
            if len(fractions) >= 2:
                entry["jump_us"].append(
                    (max(fractions) - min(fractions)) * 1e6 / SAMPLE_HZ)
        out["per_version"][version] = {}
        for signal, entry in per_signal.items():
            def agg(values):
                if not values:
                    return None
                return {"mean_us": round(statistics.mean(values), 2),
                        "max_abs_us": round(max(abs(v) for v in values), 2)}
            out["per_version"][version][signal] = {
                "n": entry["n"], "valid": entry["valid"],
                "bias_vs_amp_cross": agg(entry["bias_cross_us"]),
                "bias_vs_signal_start": agg(entry["bias_start_us"]),
                "jump_max_us": agg(entry["jump_us"]),
            }
    return out


if __name__ == "__main__":
    raise SystemExit(main())
