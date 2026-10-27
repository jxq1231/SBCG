#!/usr/bin/env python3
"""按固定10个WAV槽对账A/B串口与O1测距诊断；只读证据，不连接硬件。

输入manifest必须给出每个槽在采集时间轴上的绝对时刻，避免把首个检出事件
误当slot0。当前串口JSONL支持elapsed_s或t，但同一文件不能混用或倒退。
输出ledger.csv固定10行、summary.json和integrity.json；输出目录必须新建。
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path
from typing import Any

SCHEMA = 1
DEFAULT_SLOT_COUNT = 10
DEFAULT_TOLERANCE_S = 1.0


def as_dict(line: str) -> dict[str, str]:
    """把逗号分隔的$TAG字段转为只读字典；重复键保留最后一次。"""
    result: dict[str, str] = {}
    for item in line.split(","):
        if "=" in item:
            key, value = item.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def parse_manifest(path: Path) -> tuple[list[float], dict[str, Any], list[str]]:
    """读取槽时刻；只接受可证明的绝对采集时间轴，不猜首个事件。"""
    problems: list[str] = []
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [], {}, [f"manifest unreadable: {exc}"]
    if not isinstance(manifest, dict):
        return [], {}, ["manifest root must be object"]

    raw_slots: Any = None
    if isinstance(manifest.get("slot_times_s"), list):
        raw_slots = manifest["slot_times_s"]
    elif isinstance(manifest.get("slots"), list):
        raw_slots = []
        for item in manifest["slots"]:
            if isinstance(item, dict):
                raw_slots.append(item.get("onset_s", item.get("time_s")))
            else:
                raw_slots.append(item)
    elif isinstance(manifest.get("expected_slots_s"), list):
        raw_slots = manifest["expected_slots_s"]
    elif isinstance(manifest.get("playback_event_onsets_s"), list):
        start = manifest.get("capture_play_start_s")
        if start is None:
            problems.append(
                "playback_event_onsets_s needs capture_play_start_s on the capture clock")
        else:
            raw_slots = [float(start) + float(x)
                         for x in manifest["playback_event_onsets_s"]]

    if raw_slots is None:
        problems.append("manifest needs slot_times_s or absolute slot onsets")
        return [], manifest, problems
    if len(raw_slots) != DEFAULT_SLOT_COUNT:
        problems.append(
            f"expected {DEFAULT_SLOT_COUNT} slots, manifest has {len(raw_slots)}")
    times: list[float] = []
    for index, value in enumerate(raw_slots):
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            problems.append(f"slot {index} is not numeric")
            continue
        number = float(value)
        if not math.isfinite(number) or number < 0:
            problems.append(f"slot {index} is not a finite non-negative time")
            continue
        times.append(number)
    if len(times) > 1:
        for index in range(1, len(times)):
            if times[index] <= times[index - 1]:
                problems.append("slot times must be strictly increasing")
                break
    return times, manifest, problems


def load_serial(path: Path, board: str) -> tuple[list[dict[str, Any]], list[str]]:
    """逐行读取JSONL，保留原行号/时钟键；坏行不会静默丢弃。"""
    records: list[dict[str, Any]] = []
    problems: list[str] = []
    clock_key: str | None = None
    last_time: float | None = None
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        return [], [f"{board}: serial unreadable: {exc}"]

    for line_number, text in enumerate(lines, 1):
        if not text.strip():
            continue
        try:
            item = json.loads(text)
        except json.JSONDecodeError:
            problems.append(f"{board}:{line_number}: invalid JSON")
            continue
        if not isinstance(item, dict):
            problems.append(f"{board}:{line_number}: JSON root is not object")
            continue
        present = [key for key in ("elapsed_s", "t") if key in item]
        if not present:
            problems.append(f"{board}:{line_number}: missing elapsed_s/t")
            continue
        if len(present) > 1:
            problems.append(f"{board}:{line_number}: elapsed_s/t mixed")
            continue
        if clock_key is None:
            clock_key = present[0]
        elif clock_key != present[0]:
            problems.append(
                f"{board}:{line_number}: mixed clock keys {clock_key}/{present[0]}")
            continue
        value = item[present[0]]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            problems.append(f"{board}:{line_number}: clock is not numeric")
            continue
        timestamp = float(value)
        if not math.isfinite(timestamp) or timestamp < 0:
            problems.append(f"{board}:{line_number}: clock is not finite non-negative")
            continue
        if last_time is not None and timestamp < last_time:
            problems.append(f"{board}:{line_number}: time regression")
            continue
        last_time = timestamp
        line = item.get("line")
        if not isinstance(line, str) or not line.startswith("$"):
            continue
        tag = line.split(",", 1)[0]
        fields = as_dict(line)
        records.append({
            "board": board,
            "line_number": line_number,
            "time_s": timestamp,
            "clock_key": present[0],
            "tag": tag,
            "fields": fields,
            "line": line,
        })
    return records, problems


def nearest_slot(timestamp: float, slot_times: list[float],
                 tolerance_s: float) -> int | None:
    """返回落在容差内的最近槽；背景事件不占用槽。"""
    best: tuple[float, int] | None = None
    for index, slot_time in enumerate(slot_times):
        delta = abs(timestamp - slot_time)
        if delta <= tolerance_s and (best is None or delta < best[0]):
            best = (delta, index)
    return None if best is None else best[1]


def collect(records: list[dict[str, Any]], slot_times: list[float],
            tolerance_s: float) -> tuple[dict[int, dict[str, Any]], int]:
    """按固定槽收集pair/decision/result/UI；返回槽账与背景记录数。"""
    rows: dict[int, dict[str, Any]] = {
        index: {
            "a_pairs": [], "b_pairs": [], "decisions": [],
            "results": [], "ui_ranges": [],
        }
        for index in range(len(slot_times))
    }
    background = 0
    for record in records:
        index = nearest_slot(record["time_s"], slot_times, tolerance_s)
        if index is None:
            background += 1
            continue
        tag = record["tag"]
        if tag == "$SOUND_PAIR":
            if record["board"] == "A":
                rows[index]["a_pairs"].append(record)
            else:
                rows[index]["b_pairs"].append(record)
        elif tag == "$RANGE_DECISION":
            rows[index]["decisions"].append(record)
        elif tag == "$RANGE_RESULT":
            rows[index]["results"].append(record)
        elif tag == "$UI_RANGE":
            rows[index]["ui_ranges"].append(record)
    return rows, background


def pair_row(records: list[dict[str, Any]]) -> dict[str, str] | None:
    """取槽内最早OK pair；失败/歧义记录不冒充有效距离。"""
    for record in sorted(records, key=lambda item: item["time_s"]):
        fields = record["fields"]
        if fields.get("STATUS") == "OK":
            return fields
    return None


def first_fields(records: list[dict[str, Any]]) -> dict[str, str] | None:
    return sorted(records, key=lambda item: item["time_s"])[0]["fields"] if records else None


def build_ledger(rows: dict[int, dict[str, Any]], slot_times: list[float],
                 tolerance_s: float) -> tuple[list[dict[str, Any]], dict[str, int]]:
    """生成固定10行CSV记录，重复事件只计数，不删证据。"""
    ledger: list[dict[str, Any]] = []
    counts = {
        "background_records": 0,
        "duplicate_pairs": 0,
        "slots_with_pair": 0,
        "slots_with_decision": 0,
        "slots_with_result": 0,
        "slots_with_ui": 0,
    }
    for index, expected_s in enumerate(slot_times):
        item = rows[index]
        a_pair = pair_row(item["a_pairs"])
        b_pair = pair_row(item["b_pairs"])
        a_count = len(item["a_pairs"])
        b_count = len(item["b_pairs"])
        counts["duplicate_pairs"] += max(0, a_count - 1) + max(0, b_count - 1)
        if a_pair is not None or b_pair is not None:
            counts["slots_with_pair"] += 1
        if item["decisions"]:
            counts["slots_with_decision"] += 1
        if item["results"]:
            counts["slots_with_result"] += 1
        if item["ui_ranges"]:
            counts["slots_with_ui"] += 1

        decision = first_fields(item["decisions"])
        result = first_fields(item["results"])
        ui = first_fields(item["ui_ranges"])
        distance = (a_pair or b_pair or {}).get("DIST_MM")
        ledger.append({
            "slot": index,
            "expected_s": f"{expected_s:.6f}",
            "tolerance_s": f"{tolerance_s:.3f}",
            "a_pair_count": a_count,
            "b_pair_count": b_count,
            "a_mm": (a_pair or {}).get("DIST_MM", ""),
            "b_mm": (b_pair or {}).get("DIST_MM", ""),
            "direction": (a_pair or b_pair or {}).get("DIR", ""),
            "raw_delta_ns": (a_pair or b_pair or {}).get("DELTA_NS", ""),
            "decision": (decision or {}).get("DEC", ""),
            "decision_sequence": (decision or {}).get("SEQ", ""),
            "result_state": (result or {}).get("STATE", ""),
            "result_id": (result or {}).get("ID", ""),
            "result_mm": (result or {}).get("MM", ""),
            "ui_state": (ui or {}).get("STATE", ""),
            "ui_id": (ui or {}).get("ID", ""),
            "ui_mm": (ui or {}).get("MM", ""),
            "has_pair": 1 if distance not in (None, "") else 0,
            "has_decision": 1 if decision else 0,
            "has_result": 1 if result else 0,
            "has_ui": 1 if ui else 0,
        })
    return ledger, counts


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--a", required=True, type=Path)
    parser.add_argument("--b", required=True, type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--tolerance-s", type=float, default=DEFAULT_TOLERANCE_S)
    args = parser.parse_args(argv)
    if args.tolerance_s <= 0 or not math.isfinite(args.tolerance_s):
        print("tolerance-s must be positive finite", file=sys.stderr)
        return 2
    if args.out.exists():
        print(f"output already exists: {args.out}", file=sys.stderr)
        return 2

    slot_times, manifest, problems = parse_manifest(args.manifest)
    a_records, a_problems = load_serial(args.a, "A")
    b_records, b_problems = load_serial(args.b, "B")
    problems.extend(a_problems)
    problems.extend(b_problems)
    if not slot_times:
        problems.append("no usable slot times")
    rows, background = collect(a_records + b_records, slot_times, args.tolerance_s)
    ledger, counts = build_ledger(rows, slot_times, args.tolerance_s)
    counts["background_records"] = background
    missing = [str(item["slot"]) for item in ledger if not item["has_pair"]]
    summary = {
        "schema": SCHEMA,
        "source_manifest": str(args.manifest),
        "a_serial": str(args.a),
        "b_serial": str(args.b),
        "expected_slots": len(slot_times),
        "slot_times_s": slot_times,
        "tolerance_s": args.tolerance_s,
        "counts": counts,
        "missing_pair_slots": missing,
        "clock_keys": {
            "A": sorted({item["clock_key"] for item in a_records}),
            "B": sorted({item["clock_key"] for item in b_records}),
        },
        "manifest_keys": sorted(manifest.keys()) if isinstance(manifest, dict) else [],
    }
    integrity = {
        "schema": SCHEMA,
        "status": "OK" if not problems else "EVIDENCE_INVALID",
        "problem_count": len(problems),
        "problems": problems,
        "scope": "software evidence only; no hardware/accuracy pass is implied",
    }

    args.out.mkdir(parents=True, exist_ok=False)
    with (args.out / "ledger.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(ledger[0].keys()))
        writer.writeheader()
        writer.writerows(ledger)
    (args.out / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (args.out / "integrity.json").write_text(
        json.dumps(integrity, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "status": integrity["status"],
        "ledger": str(args.out / "ledger.csv"),
        "expected_slots": len(slot_times),
        "missing_pair_slots": missing,
        "problems": len(problems),
    }, ensure_ascii=False))
    return 0 if not problems else 1


if __name__ == "__main__":
    raise SystemExit(main())
