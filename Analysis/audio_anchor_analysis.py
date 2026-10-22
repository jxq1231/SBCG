"""Check local PTP/DMA cursor intervals; never calibrates acoustic arrival time."""
import argparse
from collections import Counter
from fractions import Fraction
import hashlib
import json
from pathlib import Path
from audio_position_analysis import number, require, parse_records, UINT32_MAX, UINT64_MAX

FIRMWARE = "SBCG-M3.2-ANCHOR"
GOOD_FIELDS = {"SEQ", "STATUS", "VALID", "SESSION", "F_LO", "F_HI", "SEC", "NSEC",
               "WIDTH", "SYNC", "OK", "REJECTS"}
BAD_FIELDS = {"SEQ", "STATUS", "VALID", "PTP_ERR", "SYNC", "OK", "REJECTS"}
TRANSIENT = {"CURSOR", "BOUNDARY", "WIDE"}
NOMINAL_HZ = Fraction(47991071, 1000)


def rate_bounds(first, last):
    elapsed_ns = last[0]-first[0]
    # Integer NDTR frame groups have a full frame bin, even at WIDTH=0.
    return (Fraction((last[1]-first[2]-1)*1000000000, elapsed_ns),
            Fraction((last[2]+1-first[1])*1000000000, elapsed_ns))


def check_anchor_rows(rows, minimum_valid=60):
    require(minimum_valid >= 2 and len(rows) >= minimum_valid, "short anchor window")
    good = bad = 0
    previous = first = None
    session = None
    widths, reasons = Counter(), Counter()
    interval_lower, interval_upper = [], []
    try:
        for i, row in enumerate(rows, 1):
            require(number(row, "SEQ") == (i & UINT32_MAX) and row["SYNC"] == "0",
                    "missing/reordered anchor or unexpected synchronization claim")
            if row["STATUS"] != "OK":
                require(set(row) == BAD_FIELDS and row["VALID"] == "0", "invalid anchor leaked fields")
                require(row["STATUS"] in TRANSIENT and number(row, "PTP_ERR") == 0,
                        "clock/session/order failure is not a normal window")
                bad += 1; reasons[row["STATUS"]] += 1
            else:
                require(set(row) == GOOD_FIELDS and row["VALID"] == "1", "invalid success fields")
                current_session = number(row, "SESSION")
                lo, hi = number(row, "F_LO", UINT64_MAX), number(row, "F_HI", UINT64_MAX)
                seconds, nanoseconds = number(row, "SEC"), number(row, "NSEC")
                width = number(row, "WIDTH")
                require(current_session != 0 and (session is None or session == current_session),
                        "audio session changed or zero")
                require(nanoseconds < 1000000000 and lo <= hi and hi-lo == width <= 4,
                        "invalid PTP/frame bounds")
                time_ns = seconds*1000000000+nanoseconds
                if previous is not None:
                    require(time_ns > previous[0] and lo > previous[2], "non-monotonic anchor")
                    local_lower, local_upper = rate_bounds(previous, (time_ns, lo, hi))
                    require(NOMINAL_HZ*99/100 <= local_lower <= local_upper <= NOMINAL_HZ*101/100,
                            "local rate outside derived +/-1% sanity range")
                    interval_lower.append(local_lower); interval_upper.append(local_upper)
                previous = time_ns, lo, hi
                if first is None: first = previous
                session = current_session
                widths[width] += 1; good += 1
            require(number(row, "OK") == min(good, UINT32_MAX) and
                    number(row, "REJECTS") == min(bad, UINT32_MAX), "anchor counters disagree")
    except (KeyError, TypeError) as error:
        raise ValueError(f"missing/invalid anchor field: {error}") from error
    require(good >= minimum_valid and good*10 >= len(rows)*9, "insufficient valid anchors")
    elapsed_ns = previous[0]-first[0]
    # A complete-frame integer cursor still has a full frame bin. The continuous
    # DMA phase lies in [lo, hi+1); zero-width observations are not exact times.
    lower, upper = rate_bounds(first, previous)
    require(NOMINAL_HZ*99/100 <= lower <= upper <= NOMINAL_HZ*101/100,
            "relative frame rate outside derived +/-1% sanity range")
    midpoint = (lower+upper)/2
    return {"attempts": len(rows), "valid": good, "rejected": bad, "session": session,
            "max_width_frames": max(widths), "width_histogram": dict(widths),
            "rejection_reasons": dict(reasons), "ptp_span_ns": str(elapsed_ns),
            "rate_hz_lower": f"{float(lower):.9f}", "rate_hz_upper": f"{float(upper):.9f}",
            "rate_hz_midpoint": f"{float(midpoint):.9f}",
            "interval_rate_hz_min": f"{float(min(interval_lower)):.9f}",
            "interval_rate_hz_max": f"{float(max(interval_upper)):.9f}",
            "relative_ppm_midpoint": f"{float((midpoint/NOMINAL_HZ-1)*1000000):.6f}",
            "hardware_verified": False,
            "scope": "DMA cursor interval relative to unsynchronized local PTP; includes frame quantization, excludes codec/FIFO/acoustic delay calibration"}


def parse_anchor_records(records):
    # Also enforce the preserved AUDIO -> AUDIO_POS order when present.
    parse_records(records, expected_firmware=FIRMWARE)
    rows, boot_seen = [], False
    for record in records:
        line = record.get("line", "").rstrip("\r\n")
        if line.startswith("$BOOT,"): boot_seen = True
        if not line.startswith("$AUDIO_ANCHOR,"): continue
        require(boot_seen, "anchor before expected BOOT")
        row = {}
        for field in line.split(",")[1:]:
            key, separator, value = field.partition("=")
            require(separator and key and value and key not in row, "malformed/duplicate anchor field")
            row[key] = value
        rows.append(row)
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True, type=Path, help="JSONL window beginning at this firmware BOOT")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--minimum-valid", type=int, default=60)
    args = parser.parse_args()
    data = args.serial.read_bytes()
    records = [json.loads(line) for line in data.decode("utf-8").splitlines()]
    result = check_anchor_rows(parse_anchor_records(records), args.minimum_valid)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(dict(status="PASS_LOCAL_PTP_DMA_ANCHOR_WINDOW", source=str(args.serial.resolve()),
                       source_sha256=hashlib.sha256(data).hexdigest(), **result), stream, indent=2)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__": main()
