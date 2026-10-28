#!/usr/bin/env python3
"""按固定10个WAV槽对账A/B串口与O1测距诊断；只读证据，不连接硬件。

输入manifest给出固定10个槽的采集时刻；schema2用clock_alignment显式声明
capture_s=serial_s+offset_s及uncertainty_s。先按源身份/ID/epoch/revision关联，
再归槽；延迟结果和HOLD不能占用新的声音槽。旧日志仅保留原始观测。
可选common_time_reference把SRC_NS映射到采集时钟；否则源打印时刻仅为代理。
输出固定10行ledger.csv、summary.json、integrity.json及全部records.jsonl；
输出目录必须新建。任何统计均不等同于硬件精度或物理屏幕首显验收。
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from pathlib import Path
from typing import Any

SCHEMA = 2
DEFAULT_SLOT_COUNT = 10
DEFAULT_TOLERANCE_S = 1.0
SOURCE_FIELDS = ("DOMAIN", "A_SESS", "A_SEQ", "B_SESS", "B_SEQ")
STATE_FIELDS = ("DOMAIN", "EPOCH", "ID", "REV")
SNAPSHOT_FIELDS = ("STATE", "MM", "DIR", "SRC_NS", "HAS_VALUE", "FRESH", "FORMAL", "PROV")
RANGE_TAGS = {"$RANGE_DECISION", "$RANGE_LINK", "$RANGE_RESULT", "$UI_RANGE"}
LEDGER_FIELDS = (
    "slot", "expected_s", "tolerance_s", "a_pair_count", "b_pair_count", "a_mm", "b_mm",
    "direction", "raw_delta_ns", "decision", "decision_sequence", "result_state",
    "result_id", "result_mm", "ui_state", "ui_id", "ui_mm", "has_pair", "has_decision",
    "has_result", "has_ui", "raw_decision_count", "raw_result_count", "raw_ui_count",
    "new_measurement_count", "provisional_count", "formal_count", "hold_count",
    "source_time_basis", "measurement_keys", "first_ui_log_latency_s",
    "first_ui_log_latency_min_s", "first_ui_log_latency_max_s")


def as_dict(line: str) -> dict[str, str]:
    """把逗号分隔的$TAG字段转为只读字典；重复键保留最后一次。"""
    result: dict[str, str] = {}
    for item in line.split(","):
        if "=" in item:
            key, value = item.split("=", 1)
            result[key.strip()] = value.strip()
    return result


def finite_number(value: Any, non_negative: bool = True) -> float | None:
    """只接受JSON数值，拒绝bool、非有限数和超出浮点范围的整数。"""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    try:
        number = float(value)
    except (ValueError, OverflowError):
        return None
    return number if math.isfinite(number) and (not non_negative or number >= 0) else None


def parse_manifest(path: Path) -> tuple[list[float | None], dict[str, Any], list[str]]:
    """保留固定槽索引；坏时刻用None占位，不压缩或用首个事件推测。"""
    problems: list[str] = []
    empty = [None] * DEFAULT_SLOT_COUNT
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, ValueError) as exc:
        return empty, {}, [f"manifest unreadable: {exc}"]
    if not isinstance(manifest, dict):
        return empty, {}, ["manifest root must be object"]

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
        start = finite_number(manifest.get("capture_play_start_s"))
        if start is None:
            problems.append(
                "playback_event_onsets_s needs finite capture_play_start_s on the capture clock")
            raw_slots = [None] * len(manifest["playback_event_onsets_s"])
        else:
            raw_slots = [None if finite_number(x) is None else start + finite_number(x)
                         for x in manifest["playback_event_onsets_s"]]

    if raw_slots is None:
        problems.append("manifest needs slot_times_s or absolute slot onsets")
        return empty, manifest, problems
    if len(raw_slots) != DEFAULT_SLOT_COUNT:
        problems.append(
            f"expected {DEFAULT_SLOT_COUNT} slots, manifest has {len(raw_slots)}")
    times: list[float | None] = empty.copy()
    previous: float | None = None
    for index, value in enumerate(raw_slots[:DEFAULT_SLOT_COUNT]):
        number = finite_number(value)
        if number is None:
            problems.append(f"slot {index} is not a finite non-negative time")
            continue
        if previous is not None and number <= previous:
            problems.append(f"slot {index}: slot times must be strictly increasing")
            continue
        times[index] = number
        previous = number
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
        timestamp = finite_number(value)
        if timestamp is None:
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


def nearest_slot(timestamp: float, slot_times: list[float | None],
                 tolerance_s: float) -> int | None:
    """返回落在容差内的最近槽；背景事件不占用槽。"""
    best: tuple[float, int] | None = None
    for index, slot_time in enumerate(slot_times):
        if slot_time is None:
            continue
        delta = abs(timestamp - slot_time)
        if delta <= tolerance_s and (best is None or delta < best[0]):
            best = (delta, index)
    return None if best is None else best[1]


def collect(records: list[dict[str, Any]], slot_times: list[float | None],
            tolerance_s: float) -> tuple[dict[int, dict[str, Any]], int]:
    """只按打印时刻保存原始观测；不能把result/UI收据当成新测量。"""
    rows: dict[int, dict[str, Any]] = {
        index: {
            "a_pairs": [], "b_pairs": [], "decisions": [],
            "results": [], "ui_ranges": [], "measurements": [], "holds": [],
            "raw_decisions": [], "raw_results": [], "raw_ui_ranges": [],
        }
        for index in range(len(slot_times))
    }
    background = 0
    for record in records:
        index = nearest_slot(record.get("capture_s", record["time_s"]), slot_times, tolerance_s)
        record["receipt_slot"] = index
        record["slot"] = None
        record["association"] = "background" if index is None else "raw_observation"
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
            rows[index]["raw_decisions"].append(record)
        elif tag == "$RANGE_RESULT":
            rows[index]["raw_results"].append(record)
        elif tag == "$UI_RANGE":
            rows[index]["raw_ui_ranges"].append(record)
    return rows, background


def integer_field(fields: dict[str, str], name: str, bits: int = 32) -> int | None:
    """读取日志中的无符号定宽整数；NA/缺失/越界均不是身份。"""
    value = fields.get(name, "")
    if not value.isascii() or not value.isdigit():
        return None
    try:
        number = int(value)
    except ValueError:
        return None
    return number if number < (1 << bits) else None


def identity(fields: dict[str, str], names: tuple[str, ...]) -> tuple[int, ...] | None:
    values = tuple(integer_field(fields, name) for name in names)
    return None if any(value is None for value in values) else values


def is_identity_record(record: dict[str, Any]) -> bool:
    """schema1及显式NA兼容记录仅作原始证据，不猜可用身份。"""
    fields = record["fields"]
    return fields.get("SCHEMA") == "2" and not any(
        fields.get(name) == "NA" for name in (*STATE_FIELDS, "NEW"))


def configure_clocks(records: list[dict[str, Any]], manifest: dict[str, Any],
                     problems: list[str]) -> tuple[dict[str, dict[str, Any]], dict[str, Any] | None]:
    """显式归一化独立串口时钟；缺失映射不能用于跨板指标。"""
    required = any(is_identity_record(record) and record["tag"] in RANGE_TAGS
                   for record in records)
    alignments = manifest.get("clock_alignment", {})
    valid: dict[str, dict[str, Any]] = {}
    for board in ("A", "B"):
        entry = alignments.get(board) if isinstance(alignments, dict) else None
        if (not isinstance(entry, dict) or entry.get("clock_key") not in ("elapsed_s", "t")
                or finite_number(entry.get("offset_s"), False) is None
                or finite_number(entry.get("uncertainty_s")) is None):
            if required or "clock_alignment" in manifest:
                problems.append(f"{board}: missing/invalid clock_alignment")
            continue
        valid[board] = {"clock_key": entry["clock_key"],
                        "offset_s": finite_number(entry["offset_s"], False),
                        "uncertainty_s": finite_number(entry["uncertainty_s"])}
    for record in records:
        entry = valid.get(record["board"])
        record["clock_aligned"] = bool(entry and entry["clock_key"] == record["clock_key"])
        record["capture_s"] = record["time_s"]
        record["clock_uncertainty_s"] = 0.0
        if record["clock_aligned"]:
            record["capture_s"] += entry["offset_s"]
            record["clock_uncertainty_s"] = entry["uncertainty_s"]
            if not math.isfinite(record["capture_s"]) or record["capture_s"] < 0:
                record["clock_aligned"] = False
                problems.append(f"{record['board']}:{record['line_number']}: invalid aligned clock")
        elif entry:
            problems.append(f"{record['board']}:{record['line_number']}: clock_alignment key mismatch")
    reference = manifest.get("common_time_reference")
    if reference is not None and (
            not isinstance(reference, dict) or isinstance(reference.get("common_ns"), bool)
            or not isinstance(reference.get("common_ns"), int)
            or not 0 <= reference["common_ns"] < (1 << 64)
            or finite_number(reference.get("capture_s")) is None
            or finite_number(reference.get("uncertainty_s")) is None):
        problems.append("invalid common_time_reference")
        reference = None
    if reference is not None:
        reference = {name: reference[name] for name in ("common_ns", "capture_s", "uncertainty_s")}
    return valid, reference


def associate(records: list[dict[str, Any]], rows: dict[int, dict[str, Any]],
              slot_times: list[float | None], manifest: dict[str, Any], tolerance_s: float,
              cross_board_ready: bool, common_reference: dict[str, Any] | None,
              problems: list[str]) -> dict[str, int]:
    """先建立源→新测量→状态→UI关系；NEW=0绝不把拒绝声绑定到旧ID。"""
    counts = {name: 0 for name in (
        "new_measurements", "provisional_measurements", "formal_measurements", "hold_records",
        "ui_matches", "ui_mismatches", "unbound_records", "legacy_range_records")}
    full_pairs: dict[tuple, list[dict[str, Any]]] = {}
    decisions: dict[tuple, list[dict[str, Any]]] = {}
    links: list[dict[str, Any]] = []
    no_new_links: list[dict[str, Any]] = []
    joined_decisions: set[tuple] = set()
    measurements: dict[tuple, dict[str, Any]] = {}
    source_measurements: dict[tuple, tuple] = {}
    invalid_measurements: set[tuple] = set()
    authority_states: dict[tuple, dict[str, Any]] = {}
    invalid_states: set[tuple] = set()
    reference_uncertainty = finite_number(manifest.get("reference_uncertainty_s"))
    reference_uncertainty_known = reference_uncertainty is not None
    if reference_uncertainty is None and "reference_uncertainty_s" in manifest:
        problems.append("invalid reference_uncertainty_s")
    reference_uncertainty = reference_uncertainty or 0.0

    def problem(record: dict[str, Any], message: str) -> None:
        problems.append(f"{record['board']}:{record['line_number']}: {message}")
        record["association"] = "unbound"
        record.setdefault("association_problems", []).append(message)

    def mark(record: dict[str, Any], measurement: dict[str, Any], label: str) -> None:
        record["slot"] = measurement["slot"]
        record["association"] = label

    for record in records:
        fields = record["fields"]
        source = identity(fields, SOURCE_FIELDS)
        if record["tag"] == "$SOUND_PAIR" and source is not None and fields.get("STATUS") == "OK":
            full_pairs.setdefault(source, []).append(record)
        if record["tag"] not in RANGE_TAGS:
            continue
        if not is_identity_record(record):
            record["association"] = "legacy_unbound"
            counts["legacy_range_records"] += 1
            continue
        if record["tag"] == "$RANGE_DECISION":
            key = identity(fields, ("DOMAIN", "SEQ"))
            if record["board"] != "A" or source is None or key is None:
                problem(record, "unbound decision: missing full authority/source/sequence identity")
                continue
            decisions.setdefault((record["board"], *key), []).append(record)
            if "NEW" in fields:
                links.append(record)
        elif record["tag"] == "$RANGE_LINK":
            links.append(record)

    for record in links:
        fields = record["fields"]
        sequence = identity(fields, ("DOMAIN", "SEQ"))
        state = identity(fields, STATE_FIELDS)
        source_ns = integer_field(fields, "SRC_NS", 64)
        decision_records = decisions.get((record["board"], *sequence), []) if sequence else []
        sources = {identity(item["fields"], SOURCE_FIELDS) for item in decision_records}
        if (record["board"] != "A" or state is None or source_ns is None
                or fields.get("NEW") not in ("0", "1")
                or fields.get("FORMAL") not in ("0", "1") or fields.get("PROV") not in ("0", "1")):
            problem(record, "unbound link: invalid authority/result identity or flags")
            continue
        if len(sources) != 1:
            problem(record, "unbound/ambiguous link: expected one full source decision")
            continue
        joined_decisions.update((item["board"], item["line_number"]) for item in decision_records)
        if fields["NEW"] == "0":
            record["association"] = "no_new_measurement"
            no_new_links.append(record)
            for item in decision_records:
                item["association"] = "no_new_decision"
            continue
        if state[2] == 0 or state[3] == 0:
            problem(record, "unbound new measurement: ID/REV must be nonzero")
            continue
        source = next(iter(sources))
        assert source is not None
        key = ("A", *state[:3])
        anchor = min(decision_records, key=lambda item: item["time_s"])
        basis = "decision_receipt_proxy"
        if common_reference is not None:
            timestamp = common_reference["capture_s"] + (source_ns - common_reference["common_ns"]) / 1e9
            uncertainty = common_reference["uncertainty_s"] + reference_uncertainty
            basis = "common_time_reference"
        else:
            pairs = [item for item in full_pairs.get(source, []) if item["board"] == "A"]
            if pairs:
                anchor = min(pairs, key=lambda item: item["time_s"])
                basis = "pair_receipt_proxy"
            if not anchor["clock_aligned"]:
                problem(record, "unbound source: missing clock_alignment")
                continue
            timestamp = anchor["capture_s"]
            uncertainty = anchor["clock_uncertainty_s"] + reference_uncertainty
        candidates = [index for index, onset in enumerate(slot_times) if onset is not None
                      and abs(timestamp - onset) <= tolerance_s + uncertainty]
        if len(candidates) != 1:
            problem(record, "ambiguous source slot" if candidates else "unbound source outside fixed slots")
            continue
        slot = candidates[0]
        measurement = {"key": key, "source": source, "slot": slot, "link": record,
                       "source_ns": source_ns, "source_time_basis": basis,
                       "source_time_s": timestamp, "uncertainty_s": uncertainty,
                       "formal": fields["FORMAL"] == "1", "provisional": fields["PROV"] == "1"}
        prior = measurements.get(key)
        if prior and (prior["source"] != source or prior["slot"] != slot or prior["source_ns"] != source_ns):
            problem(record, "conflicting measurement identity/source binding")
            invalid_measurements.add(key)
            continue
        if source in source_measurements and source_measurements[source] != key:
            problem(record, "conflicting source mapped to multiple measurement IDs")
            invalid_measurements.update((key, source_measurements[source]))
            continue
        if prior:
            prior["formal"] |= measurement["formal"]
            prior["provisional"] |= measurement["provisional"]
            measurement = prior
        else:
            measurements[key] = measurement
            source_measurements[source] = key
        mark(record, measurement, "new_measurement_link")
        for item in decision_records:
            mark(item, measurement, "source_decision")
            if item not in rows[slot]["decisions"]:
                rows[slot]["decisions"].append(item)
        for item in full_pairs.get(source, []):
            mark(item, measurement, "source_pair")

    for key in invalid_measurements:
        measurements.pop(key, None)
    for items in decisions.values():
        for record in items:
            if (record["board"], record["line_number"]) not in joined_decisions:
                problem(record, "unbound decision: RANGE_LINK partner not observed")
    for record in no_new_links:
        state = identity(record["fields"], STATE_FIELDS)
        assert state is not None
        if state[2] == 0:
            record["association"] = "status_only_link"
            continue
        measurement = measurements.get(("A", *state[:3]))
        if measurement is None or measurement["source_ns"] != integer_field(record["fields"], "SRC_NS", 64):
            problem(record, "unbound carried result: original new measurement not observed")
            continue
        mark(record, measurement, "carried_measurement_link")
        measurement["formal"] |= record["fields"]["FORMAL"] == "1"
        measurement["provisional"] |= record["fields"]["PROV"] == "1"
    for measurement in measurements.values():
        rows[measurement["slot"]]["measurements"].append(measurement)
    for slot, item in rows.items():
        if len(item["measurements"]) > 1:
            problems.append(f"slot {slot}: multiple new measurement identities; per-sound outcome is ambiguous")
    for record in records:
        if record["tag"] not in ("$RANGE_RESULT", "$UI_RANGE") or not is_identity_record(record):
            continue
        fields = record["fields"]
        state = identity(fields, STATE_FIELDS)
        if state is None or integer_field(fields, "SRC_NS", 64) is None:
            problem(record, "unbound snapshot: incomplete state/source identity")
            continue
        if (any(fields.get(name) not in ("0", "1") for name in ("HAS_VALUE", "FRESH", "FORMAL", "PROV"))
                or fields.get("FORMAL") == fields.get("PROV") == "1"
                or any(name not in fields for name in SNAPSHOT_FIELDS)):
            problem(record, "unbound snapshot: missing payload/flags")
            continue
        if state[2] == 0 and fields["HAS_VALUE"] == "0":
            record["association"] = "status_only"
            continue
        key = ("A", *state[:3])
        measurement = measurements.get(key)
        if measurement is None or measurement["source_ns"] != integer_field(fields, "SRC_NS", 64):
            problem(record, "unbound snapshot: no matching source-backed new measurement")
            continue
        mark(record, measurement, "measurement_snapshot")
        record["state_key"] = ("A", *state)
        if record["tag"] == "$RANGE_RESULT":
            if record["board"] != "A":
                problem(record, "unbound result: authority must be A")
                continue
            prior = authority_states.get(record["state_key"])
            if prior and any(prior["fields"][name] != fields[name] for name in SNAPSHOT_FIELDS):
                problem(record, "conflicting authoritative state payload")
                invalid_states.add(record["state_key"])
            else:
                authority_states.setdefault(record["state_key"], record)

    matched_ui: set[tuple] = set()
    for record in records:
        state_key = record.get("state_key")
        if state_key is None or record["association"] == "unbound":
            continue
        fields = record["fields"]
        slot = record["slot"]
        if state_key in invalid_states:
            problem(record, "conflicting state cannot support metrics")
            continue
        if record["tag"] == "$RANGE_RESULT":
            if fields["STATE"] in ("HOLD", "REACQUIRE"):
                rows[slot]["holds"].append(record)
                counts["hold_records"] += 1
            elif fields["HAS_VALUE"] == "1" and fields["STATE"] not in ("STALE", "FAULT", "NONE"):
                rows[slot]["results"].append(record)
        elif record["tag"] == "$UI_RANGE":
            authority = authority_states.get(state_key)
            if authority is None:
                problem(record, "unbound UI revision: authoritative state not observed")
                continue
            if not cross_board_ready or not record["clock_aligned"]:
                problem(record, "unbound UI: cross-board clock_alignment unavailable")
                continue
            if any(authority["fields"][name] != fields[name] for name in SNAPSHOT_FIELDS):
                counts["ui_mismatches"] += 1
                problem(record, "UI payload mismatch for authoritative state")
                continue
            if fields["HAS_VALUE"] == "1" and fields["STATE"] not in ("HOLD", "REACQUIRE", "STALE", "FAULT", "NONE"):
                rows[slot]["ui_ranges"].append(record)
                match_key = (record["board"], *state_key)
                if match_key not in matched_ui:
                    matched_ui.add(match_key)
                    counts["ui_matches"] += 1
            record["association"] = "matched_ui_state"
            record["latency_uncertainty_s"] = (
                record["clock_uncertainty_s"] + reference_uncertainty
                if reference_uncertainty_known else None)
    counts["new_measurements"] = len(measurements)
    counts["formal_measurements"] = sum(item["formal"] for item in measurements.values())
    counts["provisional_measurements"] = sum(item["provisional"] for item in measurements.values())
    counts["unbound_records"] = sum(item["association"] == "unbound" for item in records)
    return counts


def pair_row(records: list[dict[str, Any]]) -> dict[str, str] | None:
    """取槽内最早OK pair；失败/歧义记录不冒充有效距离。"""
    for record in sorted(records, key=lambda item: item["time_s"]):
        fields = record["fields"]
        if fields.get("STATUS") == "OK":
            return fields
    return None


def first_fields(records: list[dict[str, Any]]) -> dict[str, str] | None:
    """取归一化采集时钟上的首条；不能比较独立串口的原始elapsed_s。"""
    return min(records, key=lambda item: item.get("capture_s", item["time_s"]))["fields"] if records else None


def build_ledger(rows: dict[int, dict[str, Any]], slot_times: list[float | None],
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
        "slots_with_new_measurement": 0,
        "missing_measurements": 0,
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
        if item["measurements"]:
            counts["slots_with_new_measurement"] += 1
        else:
            counts["missing_measurements"] += 1

        decision = first_fields(item["decisions"])
        result = first_fields(item["results"])
        ui = first_fields(item["ui_ranges"])
        distance = (a_pair or b_pair or {}).get("DIST_MM")
        ledger.append({
            "slot": index,
            "expected_s": "" if expected_s is None else f"{expected_s:.6f}",
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
            "raw_decision_count": len(item["raw_decisions"]),
            "raw_result_count": len(item["raw_results"]),
            "raw_ui_count": len(item["raw_ui_ranges"]),
            "new_measurement_count": len(item["measurements"]),
            "provisional_count": sum(record["provisional"] for record in item["measurements"]),
            "formal_count": sum(record["formal"] for record in item["measurements"]),
            "hold_count": len(item["holds"]),
            "source_time_basis": ";".join(sorted({record["source_time_basis"] for record in item["measurements"]})),
            "measurement_keys": ";".join(":".join(map(str, record["key"])) for record in item["measurements"]),
            "first_ui_log_latency_s": "",
            "first_ui_log_latency_min_s": "",
            "first_ui_log_latency_max_s": "",
        })
        if item["ui_ranges"] and expected_s is not None:
            first_ui = min(item["ui_ranges"], key=lambda record: record["capture_s"])
            latency = first_ui["capture_s"] - expected_s
            uncertainty = first_ui["latency_uncertainty_s"]
            ledger[-1]["first_ui_log_latency_s"] = f"{latency:.6f}"
            if uncertainty is not None:
                ledger[-1]["first_ui_log_latency_min_s"] = f"{latency - uncertainty:.6f}"
                ledger[-1]["first_ui_log_latency_max_s"] = f"{latency + uncertainty:.6f}"
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
    if not any(value is not None for value in slot_times):
        problems.append("no usable slot times")
    records = a_records + b_records
    clock_alignment, common_reference = configure_clocks(records, manifest, problems)
    cross_board_ready = len(clock_alignment) == 2
    rows, background = collect(records, slot_times, args.tolerance_s)
    linked_counts = associate(records, rows, slot_times, manifest, args.tolerance_s,
                              cross_board_ready, common_reference, problems)
    ledger, counts = build_ledger(rows, slot_times, args.tolerance_s)
    counts.update(linked_counts)
    counts["receipt_background_records"] = background
    counts["background_records"] = sum(item["receipt_slot"] is None and item["slot"] is None
                                        for item in records)
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
        "missing_measurement_slots": [str(item["slot"]) for item in ledger if not item["new_measurement_count"]],
        "clock_keys": {
            "A": sorted({item["clock_key"] for item in a_records}),
            "B": sorted({item["clock_key"] for item in b_records}),
        },
        "manifest_keys": sorted(manifest.keys()) if isinstance(manifest, dict) else [],
        "metric_availability": {
            "identity": ("INVALID" if problems else "AVAILABLE") if any(
                is_identity_record(item) and item["tag"] in RANGE_TAGS for item in records)
                else "UNAVAILABLE",
            "cross_board": "AVAILABLE" if cross_board_ready and not problems else "UNAVAILABLE",
        },
        "clock_alignment": clock_alignment,
        "common_time_reference": common_reference,
        "limitations": [
            "Raw pair/decision/result/UI counts use receipt times; they do not prove per-sound publication.",
            "Legacy schema1/NA records cannot support identity-based measurement or UI metrics.",
            "Without common_time_reference, source slots use pair/decision receipt proxies.",
            "first_ui_log_latency_s measures log arrival relative to WAV onset, not physical first display.",
            "Latency uncertainty bounds require an explicit reference_uncertainty_s; missing is not zero.",
            "UI matches compare an exact authoritative state; they do not prove simultaneous panel agreement.",
        ],
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
        writer = csv.DictWriter(handle, fieldnames=LEDGER_FIELDS)
        writer.writeheader()
        writer.writerows(ledger)
    (args.out / "summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (args.out / "integrity.json").write_text(
        json.dumps(integrity, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    with (args.out / "records.jsonl").open("w", encoding="utf-8") as handle:
        for record in records:
            handle.write(json.dumps(record, ensure_ascii=False) + "\n")
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
