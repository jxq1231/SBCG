"""Slot-aligned TOA repeatability analysis for repeated standard-audio playbacks.

Maps every valid SOUND_CAND TOA_US to a chirp slot using only the board clock:
chirps of the 20 s standard file sit exactly 2 s apart, so differences between
consecutive detections are integer multiples of 2e6 us. Each run's slot-0 TOA
is used as the common-mode anchor (removes host playback-start latency and the
board play-start phase), then the normalized slot offsets are compared across
runs. Verdict passes when every slot's max abs deviation and sigma stay within
+/-100 us, matching the task criterion for repeated arrivals at a fixed
position. Host line timestamps are not used for alignment because playback is
asynchronous and serial reads may be batched.
"""
import argparse
import json
from pathlib import Path

SLOT_SPACING_US = 2_000_000
N_SLOTS = 10
TOL_US = 800_000  # < half the spacing; keeps only unambiguous matches
LIMIT_US = 100.0


def load_candidates(directory):
    """Return (host_s, toa_us) records and the playback start host second."""
    play_start = None
    summary_path = Path(directory) / "trial_summary.json"
    if not summary_path.exists():
        alternate = Path(directory).parent / "PCM" / Path(directory).name / "trial_summary.json"
        if alternate.exists():
            summary_path = alternate
    if summary_path.exists():
        play = json.loads(summary_path.read_text(encoding="utf-8")).get("play_span")
        if play:
            play_start = play[0]
    candidates = []
    for candidate in (Path(directory) / "run.jsonl",
                      Path(directory).parent / "PCM" / Path(directory).name / "run.jsonl"):
        if candidate.exists():
            serial_path = candidate
            break
    else:
        return candidates, play_start
    with serial_path.open(encoding="utf-8") as handle:
        for line in handle:
            record = json.loads(line)
            text = record.get("line", "")
            if text.startswith("$SOUND_CAND,"):
                fields = dict(pair.split("=", 1) for pair in text.split(",")[1:] if "=" in pair)
                if fields.get("TOA_US") != "NA":
                    candidates.append((record["t"], int(fields["TOA_US"])))
    return candidates, play_start


def assign(toas):
    """Return {slot_index: [toa]} using integer-multiple board spacing."""
    result = {}
    base = toas[0]
    for toa in toas:
        distance = toa - base
        k = round(distance / SLOT_SPACING_US)
        if k < 0 or k >= N_SLOTS:
            continue
        if abs(distance - k * SLOT_SPACING_US) > TOL_US:
            continue
        result.setdefault(k, []).append(toa)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trial-dirs", nargs="+", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()

    per_run = []
    for directory in args.trial_dirs:
        loaded, play_start = load_candidates(directory)
        # Discard pre-play transients (e.g. the volume-control click); the first
        # chirp arrives no earlier than ~play+0.9 s in this setup.
        toas = sorted(toa for host_s, toa in loaded
                      if play_start is None or host_s >= play_start + 0.5)
        if len(toas) < 2:
            print(f"{directory}: skipped ({len(toas)} toa)", flush=True)
            continue
        slots = assign(toas)
        anchor = slots.get(0)
        if not anchor:
            print(f"{directory}: skipped (no slot-0 anchor)", flush=True)
            continue
        base = anchor[0]
        normalized = {k: [value - base for value in values]
                      for k, values in slots.items()}
        per_run.append({"dir": directory, "normalized": normalized,
                        "raw": toas, "slots": {k: len(v) for k, v in slots.items()}})

    per_slot = {}
    for run in per_run:
        for slot, values in run["normalized"].items():
            per_slot.setdefault(slot, []).extend(values)
    report = {"runs": len(per_run), "slots": {}}
    worst_max, worst_sigma = 0.0, 0.0
    for slot in sorted(per_slot):
        values = per_slot[slot]
        if len(values) < 2:
            continue
        mean = sum(values) / len(values)
        deviations = [v - mean for v in values]
        sigma = (sum(d * d for d in deviations) / len(deviations)) ** 0.5
        worst = max(abs(d) for d in deviations)
        worst_max = max(worst_max, worst)
        worst_sigma = max(worst_sigma, sigma)
        report["slots"][slot] = {"n": len(values), "max_dev_us": round(worst, 2),
                                 "sigma_us": round(sigma, 2)}
        print(f"slot {slot}: n={len(values)} max_dev_us={worst:.2f} sigma_us={sigma:.2f}",
              flush=True)
    report["worst_max_dev_us"] = round(worst_max, 2)
    report["worst_sigma_us"] = round(worst_sigma, 2)
    report["verdict"] = ("PASS" if per_run and 0 < worst_max <= LIMIT_US and
                         worst_sigma <= LIMIT_US else "FAIL_OR_INSUFFICIENT")
    args.out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(report, ensure_ascii=False), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())