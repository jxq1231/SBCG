"""Offline broadband/band-limited correlation of captured chirp PCM snapshots.

Separates "acoustic distortion/multipath" from "threshold/template/alignment":
- exhaustive range-normalized correlation over the full stored span (broadband),
- same peak after 2-6 kHz band limiting of the capture (in-band variant),
- negative (silence) windows inside the same capture for a per-threshold
  false-alarm curve, then per-condition distribution aggregation across trials.

Offline evidence only: this never changes the 0.80 firmware threshold.
"""
import argparse
import json
import math
import sys
import wave
from pathlib import Path

import numpy as np

BAND_HZ = (2000, 6000)
NOMINAL_HZ = 48000


def norm_peak(signal, template):
    """Exhaustive normalized-correlation peak over all valid lags.

    Range-normalized mean-free correlation, identical math to the firmware's
    per-window scoring, evaluated at every integer lag. Returns (lag, corr).
    lag is the window start frame in `signal` coordinates.
    """
    t = template.astype(np.float64)
    nt = t.size
    st = t.sum()
    tt = float((t * t).sum())
    vt = nt * tt - st * st
    if vt <= 0:
        return None, 0.0
    x = signal.astype(np.float64)
    best_lag, best_corr = None, 0.0
    for lag in range(0, x.size - nt + 1):
        w = x[lag:lag + nt]
        sx = float(w.sum())
        xx = float((w * w).sum())
        numerator = nt * float((w * t).sum()) - sx * st
        denom = (nt * xx - sx * sx) * vt
        corr = 0.0 if denom <= 0 else abs(numerator) / math.sqrt(denom)
        if corr > best_corr:
            best_corr, best_lag = corr, lag
            if corr >= 0.999999:
                break
    return best_lag, best_corr


def band_limit(signal, fs, lower=2000, upper=6000):
    """FFT band mask; pure-numpy alternative to scipy. Keeps bin content within [lower,upper]."""
    n = signal.size
    spectrum = np.fft.rfft(signal.astype(np.float64))
    freqs = np.fft.rfftfreq(n, d=1.0 / fs)
    mask = (freqs >= lower) & (freqs <= upper)
    spectrum[mask == False] = 0.0
    return np.fft.irfft(spectrum, n)


def negatives_curve(signal, template, guard_hint, guard_raw):
    """False-alarm estimate from unambiguous silence windows of the same capture.

    Negative lags are windows outside [first+hint, first+raw+len(template)+guard]:
    - windows fully before the candidate arrival (guard_hint frames falling edge)
    - windows fully after the raw-best window end (guard_raw frames falling edge).
    Returns sorted list of (corr) and a per-threshold FA rate grid at 0.70..0.85.
    """
    wins = []
    stride = 64
    for lag in range(0, signal.size - template.size + 1, stride):
        before = lag + template.size <= guard_hint
        after = lag >= guard_raw
        if before or after:
            _, corr = norm_peak(signal[lag:lag + template.size], template)
            wins.append((lag, corr))
    thresholds = [0.70 + 0.01 * i for i in range(16)]
    fa = {}
    for thr in thresholds:
        over = sum(1 for _lag, c in wins if c >= thr)
        fa[round(thr, 2)] = over / len(wins) if wins else None
    corrs = [c for _l, c in wins]
    return {"windows": len(wins), "corrs": corrs, "fa_curve": fa}


def analyze_capture(pcm_path, meta_path, template):
    meta = json.loads(Path(meta_path).read_text(encoding="utf-8"))
    signal = np.frombuffer(Path(pcm_path).read_bytes(), dtype=np.int16).astype(np.float64)
    span = meta["frames"]
    if signal.size != span:
        raise ValueError(f"{pcm_path.name}: pcm {signal.size} != meta frames {span}")
    hint = meta["hint_frame"] - meta["first_frame"]
    raw = meta["raw_first_frame"] - meta["first_frame"]
    lag0, corr0 = norm_peak(signal, template)
    banded = band_limit(signal, NOMINAL_HZ, *BAND_HZ)
    lag1, corr1 = norm_peak(banded, template)
    result = {
        "id": meta["id"], "session": meta["session"], "frames": span,
        "first_frame": meta["first_frame"], "hint_frame": meta["hint_frame"],
        "raw_first_frame": meta["raw_first_frame"],
        "clamped": meta.get("left_clamped", False) or meta.get("right_clamped", False),
        "tail_topup": meta.get("tail_topup", False),
        "online_hint_lag": hint, "online_raw_lag": raw,
        "offline_broadband": {"lag": lag0, "corr": None if lag0 is None else round(corr0, 6)},
        "offline_bandlimited_2_6k": {"lag": lag1, "corr": None if lag1 is None else round(corr1, 6)},
        "lag_shift_frames": None if (lag0 is None or raw is None) else int(lag0 - raw),
        "negatives": negatives_curve(signal, template, hint, raw + template.size),
    }
    return result


def load_template(path):
    with wave.open(str(path), "rb") as wav:
        if wav.getframerate() != NOMINAL_HZ or wav.getsampwidth() != 2 or wav.getnchannels() != 1:
            raise ValueError(f"template must be 48k mono int16: {path}")
        raw = wav.readframes(wav.getnframes())
    template = np.frombuffer(raw, dtype=np.int16).astype(np.float64)
    if template.size != 2400:
        raise ValueError(f"template frames {template.size} != 2400")
    return template


def find_pairs(directory):
    files = {}
    for pcm in sorted(Path(directory).glob("id*.pcm")):
        meta = pcm.with_suffix(".json")
        if meta.exists():
            files[int(pcm.stem[2:])] = (pcm, meta)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--captures", required=True, type=Path,
                        help="dir from udp_chirp_capture.py (id*.pcm + id*.json)")
    parser.add_argument("--template", required=True, type=Path,
                        help="48 kHz mono int16 chirp template wav")
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    template = load_template(args.template)
    records = []
    for key, pair in find_pairs(args.captures).items():
        records.append(analyze_capture(pair[0], pair[1], template))
    records.sort(key=lambda r: (r["session"], r["id"]))
    args.out.write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding="utf-8")
    for r in records:
        broad = r["offline_broadband"]["corr"]
        band = r["offline_bandlimited_2_6k"]["corr"]
        print(f"id={r['id']} s={r['session']} online_raw={r['online_raw_lag']} "
              f"broad_corr={broad} band_corr={band} shift={r['lag_shift_frames']} "
              f"neg_wins={r['negatives']['windows']}", flush=True)
    print(f"WROTE {args.out}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())