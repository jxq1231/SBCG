"""Validate M3.2.1 numbered-block serial evidence; never verifies PTP mapping."""
import argparse
import json
from pathlib import Path

BLOCK_FRAMES = 8192
UINT32_MAX = 2**32 - 1
UINT64_MAX = 2**64 - 1


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(row, field, maximum=UINT32_MAX):
    value = row[field]
    require(isinstance(value, str) and value.isascii() and value.isdecimal(),
            f"invalid unsigned decimal {field}")
    result = int(value)
    require(result <= maximum, f"out of range {field}")
    return result


def check_positions(audio, positions, minimum_rows=60):
    """Strict normal window: no omissions, faults, restarts or known drops.

    CONTIG refers to adjacent *processed* blocks; adjacent 1 Hz log rows
    intentionally skip intermediate processed blocks and need not abut.
    """
    require(minimum_rows >= 2 and len(audio) == len(positions) >= minimum_rows,
            "short or unmatched AUDIO/AUDIO_POS window")
    previous = None
    session = None
    try:
        for a, p in zip(audio, positions):
            seq = number(p, "SEQ")
            current_session = number(p, "SESSION")
            frames = number(p, "OBS_FRAMES", UINT64_MAX)
            first = number(p, "FIRST", UINT64_MAX)
            require(current_session != 0 and (session is None or current_session == session),
                    "capture session changed or is zero")
            session = current_session
            require(number(a, "SEQ") == seq and number(a, "FRAMES", UINT64_MAX) == frames,
                    "AUDIO/AUDIO_POS snapshots disagree")
            require(a["STATE"] == "RUN" and a["VALID"] == "1" and
                    all(a[k] == "0" for k in ("DROP", "ERR")) and
                    all(a[k] == "00000000" for k in ("HAL", "DMA")), "audio error or drop")
            require(all(p[k] == "1" for k in ("INDEX_VALID", "CONTIG", "VALID")) and
                    p["FAULTS"] == "00000000" and number(p, "GAP", UINT64_MAX) == 0,
                    "invalid position or known gap")
            require(number(p, "COUNT") == BLOCK_FRAMES and frames >= BLOCK_FRAMES and
                    frames % BLOCK_FRAMES == first % BLOCK_FRAMES == 0 and
                    frames - first - BLOCK_FRAMES in (0, BLOCK_FRAMES),
                    "invalid/stale frame range")
            blocks = frames // BLOCK_FRAMES
            require(number(a, "H") == ((blocks + 1)//2 & UINT32_MAX) and
                    number(a, "F") == (blocks//2 & UINT32_MAX) and
                    number(a, "OK") == ((first//BLOCK_FRAMES + 1) & UINT32_MAX),
                    "callback/processed accounting disagrees with frame position")
            if previous is not None:
                require(seq == ((previous[0]+1) & UINT32_MAX) and
                        frames > previous[1] and first > previous[2],
                        "missing/reordered/frozen records")
            previous = seq, frames, first
    except (KeyError, TypeError) as error:
        raise ValueError(f"missing/invalid position field: {error}") from error
    return {"pairs": len(positions), "session": session, "first_seq": int(positions[0]["SEQ"]),
            "last_seq": int(positions[-1]["SEQ"]), "last_observed_frames": str(frames),
            "scope": "observed single-board DMA frame numbering; no PTP/sample-time claim",
            "hardware_verified": False}


def parse_records(records):
    audio, positions = [], []
    boot_seen = False
    pending_seq = None
    for record in records:
        require("incomplete_tail_hex" not in record, "incomplete serial tail")
        line = record.get("line", "")
        if line.startswith("$BOOT,"):
            require(not boot_seen and not audio and not positions, "multiple/late BOOT records")
            require("FW=SBCG-M3.2-INDEX" in line.split(","), "unexpected firmware")
            boot_seen = True
        if not line.startswith(("$AUDIO,", "$AUDIO_POS,")):
            continue
        require(boot_seen, "audio data before expected BOOT")
        row = {}
        for field in line.rstrip("\r\n").split(",")[1:]:
            key, separator, value = field.partition("=")
            require(separator and key and value and key not in row, "malformed/duplicate field")
            row[key] = value
        require("SEQ" in row, "missing serial sequence")
        if line.startswith("$AUDIO_POS,"):
            require(pending_seq is not None and row["SEQ"] == pending_seq,
                    "missing/out-of-order AUDIO before AUDIO_POS")
            positions.append(row)
            pending_seq = None
        else:
            require(pending_seq is None, "missing AUDIO_POS before next AUDIO")
            audio.append(row)
            pending_seq = row["SEQ"]
    require(boot_seen, "missing firmware BOOT")
    require(pending_seq is None, "incomplete AUDIO/AUDIO_POS pair")
    return audio, positions


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", type=Path, required=True, help="existing serial JSONL capture")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--minimum-rows", type=int, default=60)
    args = parser.parse_args()
    records = [json.loads(line) for line in args.serial.read_text(encoding="utf-8").splitlines()]
    audio, positions = parse_records(records)
    result = check_positions(audio, positions, args.minimum_rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Do not replace an older acceptance record, even on a failed rerun.
    with args.output.open("x", encoding="utf-8") as stream:
        json.dump(dict(status="PASS_FRAME_POSITION_WINDOW", source=str(args.serial.resolve()),
                       **result), stream, indent=2)
    print(json.dumps(result, ensure_ascii=False))


if __name__ == "__main__":
    main()
