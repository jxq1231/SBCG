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


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_path(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def run_git(args):
    return subprocess.run(["git"] + args, capture_output=True, text=True,
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
    """Run the adapter over many windows, batching argv, return {label: row}."""
    results = {}
    for offset in range(0, len(window_paths), chunk):
        part = window_paths[offset:offset + chunk]
        proc = subprocess.run([str(exe)] + [str(p) for p in part],
                              capture_output=True, text=True, timeout=120)
        if proc.returncode != 0:
            raise RuntimeError(f"cli exit {proc.returncode}: {proc.stderr[:200]}")
        for line in proc.stdout.splitlines():
            fields = dict(item.split("=", 1) for item in line.split()
                          if "=" in item)
            label = fields.get("FILE")
            if label is None:
                raise RuntimeError(f"cli row missing FILE: {line!r}")
            results[label] = fields
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
    return ledger, entries, parse_problems + scan_problems


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
        trial_input = {"serial": None, "summary": None, "pcm": {}, "pairs": 0}
        if not serial_path.exists():
            missing.append(f"{trial}: run.jsonl missing")
            inventory[trial] = trial_input
            continue
        trial_input["serial"] = {"bytes": serial_path.stat().st_size,
                                 "sha256": sha256_path(serial_path)}
        summary_path = args.root / trial / "trial_summary.json"
        if summary_path.exists():
            trial_input["summary"] = {"bytes": summary_path.stat().st_size,
                                      "sha256": sha256_path(summary_path)}
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

    # ---- ledger + window extraction + replay ----
    assets = asset_check(args.assets_dir)
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
    conservation = {"raw_lines": 0, "events": 0, "sound_total": 0,
                    "sound_replayable_pcm": 0, "windows_ok": 0}
    per_trial = {}

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
            if entry["has_pcm"] and entry["has_json"]:
                pcm_by_id[entry["id"]] = entry
        for event in ledger.events:
            if event.kind != "$SOUND_CAND":
                continue
            trial_detail["sounds"] += 1
            conservation["sound_total"] += 1
            first = first_int(event.fields, "FIRST")
            serial_toa = event.fields.get("TOA_US")
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
                    "firmware_binding": "UNKNOWN", "replays": []})
                continue
            if any(e.status == "AMBIGUOUS" for e in edges):
                trial_detail["ambiguous"] += 1
            anchors = {}
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
                       "replays": []}
                if win_status == "OK":
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
        trial_detail["windows_ok"] = sum(
            1 for r in replay_rows
            if r["trial"] == trial and r["offline_window_status"] == "OK")
        per_trial[trial] = trial_detail

    conservation["sound_replayable_pcm"] = sum(
        1 for r in replay_rows if r["kind"] == "WINDOW")

    # ---- serialize outputs ----
    (run_dir / "manifest.json").write_text(
        json.dumps({
            "head": head, "git_status": git_status,
            "algorithm_snapshots": snapshot_hashes,
            "gcc": gcc_version, "build_command":
                "gcc -std=c11 {O0,O2} -Wall -Wextra -Werror -pedantic "
                "-I <snap> Analysis/toa_replay_cli.c <snap>/toa_estimator.c -lm",
            "assets": assets, "inventory": inventory,
        }, ensure_ascii=False, indent=2), encoding="utf-8")
    (run_dir / "ledger.json").write_text(
        json.dumps({"conservation": conservation, "per_trial": per_trial,
                    "hard_failures": status["hard_failures"]},
                   ensure_ascii=False, indent=2), encoding="utf-8")
    with (run_dir / "replay.jsonl").open("w", encoding="utf-8") as handle:
        for row in replay_rows:
            handle.write(json.dumps(row, ensure_ascii=False) + "\n")

    # ---- summary ----
    diagnostics = aggregate_diagnostics(replay_rows, versions)
    synthetic_results = [] if args.skip_synthetic else run_synthetic(
        run_dir, binaries, args.assets_dir)
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
    tool_check_ok = (determinism_bad == 0 and determinism_opt_mismatch == 0
                     and not status["hard_failures"])
    summary = {"run_dir": str(run_dir), "conservation": conservation,
               "per_trial": per_trial, "diagnostics": diagnostics,
               "synthetic": {"windows": len(synthetic_results)},
               "reproducibility": {"repeat_rows": determinism_rows,
                                   "repeat_mismatch": determinism_bad,
                                   "opt_mismatch": determinism_opt_mismatch},
               "hard_failures": status["hard_failures"],
               "conclusions": {"TOOL_CHECK": "PASS" if tool_check_ok else "FAIL",
                               "DATA_COVERAGE": {
                                   k: detail_summary(d) for k, d in per_trial.items()},
                               "STANDARD_ID": "reported_original_only",
                               "PHYSICAL_TOA_ACCURACY": "NOT_VERIFIED"}}
    (run_dir / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")

    print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
    if status["hard_failures"]:
        print("HARD FAILURES:", status["hard_failures"], flush=True)
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
    """Slice the standard chirp asset around its fade start."""
    import struct
    wav_path = assets_dir / "standard_chirp_2-6khz_10x_20s.wav"
    if not wav_path.exists():
        return None, {"skipped": "asset missing"}
    signal = [0] * WINDOW_FRAMES
    saturations = 0
    with open(wav_path, "rb") as handle:
        handle.seek(44)  # standard PCM WAV header
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
    return signal, {"signal_start": onset, "amp_cross": None,
                    "limit": None, "peak": max(abs(v) for v in signal),
                    "saturations": saturations}


def run_synthetic(run_dir, binaries, assets_dir, seed=7):
    """Generate known-onset windows and run both frozen versions over them."""
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
    # std chirp slice: phase sweep, no noise/echo
    for phase in (0, 8, 16, 24, 32, 40, 48, 56, 63):
        signal, info = synth_chirp(phase, assets_dir, 0, 0, 0.0, rng)
        if signal is not None:
            windows.append(signal)
            info.update({"signal": "stdchirp", "phase": phase, "amp": 1.0,
                         "noise": 0, "echo_delay": 0, "echo_gain": 0.0})
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
                 "echo_delay": 0, "echo_gain": 0.0, "signal_start": None,
                 "amp_cross": None, "limit": 3750, "peak": 30000,
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
        out = run_cli(exe, paths)
        for index, path in enumerate(paths):
            fields = out.get(str(path), {})
            results[index]["scores"][f"{version}:{opt}"] = {
                "valid": fields.get("VALID") == "1",
                "fraction": float(fields.get("FRACTION", "0.0")),
                "reason": fields.get("REASON")}
    (run_dir / "synthetic_known_input.json").write_text(
        json.dumps(results, ensure_ascii=False), encoding="utf-8")
    return results


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


def aggregate_diagnostics(replay_rows, versions=("peak_backtrack",
                                                 "current_envelope")):
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
    diagnostics = {"by_trial_version": {}}
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
            per_slot = {}
            for trial in trials:
                values_us = by_trial_version.get((trial, version), [])
                if not values_us:
                    continue
                base = min(values_us)
                for value in values_us:
                    distance = value - base
                    k = round(distance / SLOT_SPACING_US)
                    if k < 0 or k >= N_SLOTS:
                        continue
                    if abs(distance - k * SLOT_SPACING_US) > 500_000.0:
                        continue
                    per_slot.setdefault(k, []).append(value - base)
            slots = {}
            complete = True
            for k in sorted(per_slot):
                values = per_slot[k]
                mean = statistics.mean(values)
                deviations = [v - mean for v in values]
                slots[str(k)] = {
                    "n": len(values),
                    "max_dev_us": round(max(abs(d) for d in deviations), 2),
                    "sigma_us": round((sum(d * d for d in deviations) /
                                       len(deviations)) ** 0.5, 2)}
            missing = [k for k in range(N_SLOTS) if k not in per_slot]
            if missing or any(len(v) != len(trials)
                              for v in per_slot.values()):
                complete = False
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
    return diagnostics


if __name__ == "__main__":
    raise SystemExit(main())