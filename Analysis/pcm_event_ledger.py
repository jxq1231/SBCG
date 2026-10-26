"""Parse single-board trial logs and PCM into a domain-aware event ledger.

Maintains every raw record (SOUND candidate, SOUND_TIME, chirp SEARCH, chirp
CAPTURE, PCM/JSON pairs and orphans) with provenance and explicit ambiguity,
instead of forcing a one-to-one mapping. Domains are (trial, boot segment,
session); without captured BOOT lines the segment is 'unknown'. Cross-record
links are candidate edges; frame/session regressions block linking rather
than silently merging two boots. This module performs no replay and no
hardware access.
"""
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional


def parse_fields(body: str) -> Dict[str, str]:
    """Split 'K=V,...' tail fields, preserving every key."""
    fields = {}
    for item in body.split(","):
        if "=" in item:
            key, _, value = item.partition("=")
            fields[key] = value
    return fields


def to_int(value: str, default=None) -> Optional[int]:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


@dataclass
class Record:
    kind: str
    trial: str
    line_no: int
    host_sec: float
    raw: str
    fields: Dict[str, str] = field(default_factory=dict)
    domain: str = "trial::unknown::?"

    def parse_int(self, key: str) -> Optional[int]:
        return to_int(self.fields.get(key))

    @property
    def session(self) -> Optional[int]:
        return self.parse_int("SESSION")


def line_tag(raw: str) -> str:
    return raw.split(",")[0]


def parse_trial_records(trial: str, run_path: Path) -> (List[Record], List[str]):
    """Parse a run.jsonl into typed records plus parse problems.

    Returns (records, problems). Every line that carries a known signal tag
    becomes a Record; unknown tags are ignored (they are still accounted for
    in the line count by the caller when needed).
    """
    records: List[Record] = []
    problems: List[str] = []
    recognized = {"$SOUND_CAND", "$SOUND_TIME", "$CHIRP_SEARCH",
                  "$CHIRP_CAPTURE", "BOOT"}
    with run_path.open(encoding="utf-8") as handle:
        for line_no, raw_line in enumerate(handle, start=1):
            raw_line = raw_line.rstrip("\r\n")
            if not raw_line.strip():
                continue
            try:
                host = json.loads(raw_line)
                host_sec = host.get("t")
                text = host.get("line", "")
            except (ValueError, TypeError):
                problems.append(f"{trial}:{line_no}: bad json host record")
                continue
            if not isinstance(text, str) or not text:
                continue
            tag = line_tag(text)
            if tag not in recognized:
                continue
            body = text[len(tag) + 1:]
            records.append(Record(kind=tag, trial=trial, line_no=line_no,
                                  host_sec=host_sec, raw=text,
                                  fields=parse_fields(body)))
    return records, problems


def boot_segments(records: List[Record]) -> List[str]:
    """Ordered segment ids for a trial ('unknown' when no BOOT yet)."""
    segments: List[str] = []
    current = "unknown"
    boots = 0
    for record in records:
        if record.kind == "BOOT":
            boots += 1
            current = f"boot{boots}"
            segments.append("boundary")
        else:
            segments.append(current)
    return segments


def assign_domains(records: List[Record], trial: str) -> List[str]:
    segments = boot_segments(records)
    domains = []
    for record, segment in zip(records, segments):
        if segment == "boundary":
            record.domain = f"{trial}::boundary"
            domains.append(record.domain)
            continue
        session = record.session
        domain = f"{trial}::{segment}::"
        if session is None:
            domain += "?"
        else:
            domain += str(session)
        record.domain = domain
        domains.append(domain)
    return domains


@dataclass
class LedgerEvent:
    kind: str
    trial: str
    line_no: int
    host_sec: float
    raw: str
    domain: str
    key: str
    fields: Dict[str, str]
    parse_problem: Optional[str] = None

    def parse_int(self, key: str) -> Optional[int]:
        return to_int(self.fields.get(key))


@dataclass
class LedgerEdge:
    source: str
    target: str
    kind: str
    status: str
    detail: str = ""


@dataclass
class Ledger:
    trial: str
    events: List[LedgerEvent]
    edges: List[LedgerEdge]
    domains: Dict[str, Dict[str, object]]
    problems: List[str]
    isolation: Dict[str, List[str]]


def event_key(kind: str, fields: Dict[str, str]) -> str:
    session = fields.get("SESSION", "?")
    ident = fields.get("SEQ") or fields.get("ID") or fields.get("FIRST") or "?"
    return f"{kind}:{session}:{ident}"


def _frame_range(fields: Dict[str, str]):
    first = to_int(fields.get("FIRST"))
    frames = to_int(fields.get("FRAMES"))
    if first is None:
        return None
    return (first, first + (frames or 0))


def build_ledger(records: List[Record], trial: str,
                 pcm_entries: List[Dict],
                 scan_problems: Optional[List[str]] = None) -> Ledger:
    """Assemble events, edges and domain conflicts from parsed records."""
    problems = list(scan_problems or ())
    events = [LedgerEvent(kind=r.kind, trial=trial, line_no=r.line_no,
                          host_sec=r.host_sec, raw=r.raw, domain=r.domain,
                          key=event_key(r.kind, r.fields), fields=r.fields)
              for r in records]
    edges: List[LedgerEdge] = []
    domains: Dict[str, Dict[str, object]] = {}
    for event in events:
        dom = domains.setdefault(event.domain, {"captures": [], "sounds": [],
                                                "sound_times": [], "searches": [],
                                                "conflict": False})
        if event.kind == "$CHIRP_CAPTURE":
            dom["captures"].append(event)
        elif event.kind == "$SOUND_CAND":
            dom["sounds"].append(event)
        elif event.kind == "$SOUND_TIME":
            dom["sound_times"].append(event)
        elif event.kind == "$CHIRP_SEARCH":
            dom["searches"].append(event)

    capture_by_id: Dict[int, Dict[int, LedgerEvent]] = {}
    for domain, dom in domains.items():
        by_id: Dict[int, LedgerEvent] = {}
        for capture in dom["captures"]:
            ident = capture.parse_int("ID")
            if ident is None:
                capture.parse_problem = "capture without ID"
                problems.append(f"{capture.domain}: capture missing ID")
                continue
            by_id[ident] = capture
        capture_by_id[domain] = by_id

    def conflict_free(domain: str) -> bool:
        return not domains[domain]["conflict"]

    # Detect id/frame regressions inside a domain.
    for domain, dom in domains.items():
        captures = sorted(dom["captures"],
                          key=lambda e: (to_int(e.fields.get("ID", "0")) or 0))
        ids = [to_int(e.fields.get("ID")) for e in captures]
        firsts = [to_int(e.fields.get("FIRST")) for e in captures]
        if any(i is None for i in ids):
            dom["conflict"] = True
        for a, b in zip(ids, ids[1:]):
            if a is not None and b is not None and b <= a:
                dom["conflict"] = True
                problems.append(f"{domain}: capture ids regress {a}->{b}")
        for a, b in zip(firsts, firsts[1:]):
            if a is not None and b is not None and b < a:
                dom["conflict"] = True
                problems.append(f"{domain}: capture first regresses {a}->{b}")

    # Capture <-> search by id in the same domain, verifying frame overlap.
    for domain, dom in domains.items():
        by_id = capture_by_id[domain]
        for search in dom["searches"]:
            ident = search.parse_int("ID")
            if ident is None:
                search.parse_problem = "search without ID"
                continue
            if ident not in by_id:
                search.parse_problem = f"search ID {ident} has no capture"
                problems.append(f"{domain}: search {ident} without capture")
                continue
            cap = by_id[ident]
            edge = LedgerEdge(source=f"search:{domain}:{ident}",
                              target=f"capture:{domain}:{ident}",
                              kind="search-capture",
                              status="LINKED" if conflict_free(domain)
                              else "CONFLICT_BLOCKED")
            hint = search.parse_int("HINT")
            cap_range = _frame_range(cap.fields)
            if hint is not None and cap_range is not None:
                lo, hi = cap_range
                if not (lo <= hint < hi):
                    edge.status = "metadata_conflict"
                    problems.append(
                        f"{domain}: search {ident} HINT {hint} outside capture "
                        f"[{lo},{hi})")
            edges.append(edge)

    # PCM entries link to captures by id.
    pcm_invalid = {"orphan_pcm": [], "orphan_json": [], "metadata_conflict": []}
    capture_home: Dict[str, str] = {}
    for domain, by_id in capture_by_id.items():
        for ident in by_id:
            capture_home[ident] = domain
    for entry in pcm_entries:
        ident = entry["id"]
        stem = f"{trial}:{ident}"
        if not entry["has_pcm"]:
            pcm_invalid["orphan_json"].append(stem)
            continue
        if not entry["has_json"]:
            pcm_invalid["orphan_pcm"].append(stem)
            continue
        if ident not in capture_home:
            pcm_invalid["orphan_pcm"].append(stem)
            problems.append(f"{trial}: pcm id {ident} has no capture record")
            continue
        domain = capture_home[ident]
        cap = capture_by_id[domain][ident]
        if to_int(entry["json"].get("id")) != ident:
            pcm_invalid["metadata_conflict"].append(stem)
            problems.append(f"{trial}: pcm json id mismatch for {ident}")
        if cap.parse_int("SESSION") is not None and to_int(entry["json"].get("session")) \
                != cap.parse_int("SESSION"):
            pcm_invalid["metadata_conflict"].append(stem)
            problems.append(f"{trial}: pcm json session mismatch for {ident}")
        first_frame = entry["json"].get("first_frame")
        cap_first = cap.parse_int("FIRST")
        if first_frame is not None and cap_first is not None \
                and int(first_frame) != cap_first:
            pcm_invalid["metadata_conflict"].append(stem)
            problems.append(f"{trial}: pcm first frame {first_frame} != "
                            f"capture {cap_first} for {ident}")
        edges.append(LedgerEdge(
            source=f"pcm:{trial}:{ident}", target=f"capture:{domain}:{ident}",
            kind="pcm-capture", status="LINKED"))

    # Sound <-> capture candidate edges by frame containment (channel unproven).
    for domain, dom in domains.items():
        if not conflict_free(domain):
            continue
        for sound in dom["sounds"]:
            first = sound.parse_int("FIRST")
            if first is None:
                continue
            matches = []
            for capture in dom["captures"]:
                cap_range = _frame_range(capture.fields)
                if cap_range is None:
                    continue
                lo, hi = cap_range
                if lo <= first < hi:
                    matches.append(capture)
            if not matches:
                sound.parse_problem = "no containing capture range"
                problems.append(
                    f"{sound.domain}: sound {sound.fields.get('SEQ')} frame "
                    f"{first} outside every capture")
                continue
            for capture in matches:
                lo, hi = _frame_range(capture.fields)
                status = "AMBIGUOUS" if len(matches) > 1 else "UNIQUE"
                edges.append(LedgerEdge(
                    source=sound.key,
                    target=f"capture:{domain}:{capture.fields.get('ID')}",
                    kind="sound-capture", status=status,
                    detail=f"first={first} in [{lo},{hi})"))

    # Sound <-> sound_time via same domain SEQ+FIRST.
    for domain, dom in domains.items():
        stmap = {}
        for st in dom["sound_times"]:
            stmap[(st.fields.get("SEQ"), st.fields.get("FIRST"))] = st
        for sound in dom["sounds"]:
            pair = (sound.fields.get("SEQ"), sound.fields.get("FIRST"))
            if pair in stmap:
                edges.append(LedgerEdge(
                    source=sound.key, target=event_key("$SOUND_TIME", stmap[pair].fields),
                    kind="sound-soundtime", status="LINKED"
                    if conflict_free(domain) else "CONFLICT_BLOCKED"))

    isolation = {
        "orphan_pcm": pcm_invalid["orphan_pcm"],
        "orphan_json": pcm_invalid["orphan_json"],
        "pcm_metadata_conflict": pcm_invalid["metadata_conflict"],
    }
    return Ledger(trial=trial, events=events, edges=edges, domains=domains,
                  problems=problems, isolation=isolation)


def load_pcm_entry(directory: Path, stem: str, trial: str) -> Dict:
    """Describe one id*.pcm / id*.json pairing plus framing validation."""
    pcm_path = directory / (stem + ".pcm")
    json_path = directory / (stem + ".json")
    entry = {"id": int(stem[2:]), "trial": trial, "has_pcm": pcm_path.exists(),
             "has_json": json_path.exists(),
             "domain": f"{trial}::unknown::?", "json": {}, "valid": True,
             "problem": None}
    if json_path.exists():
        try:
            entry["json"] = json.loads(json_path.read_text(encoding="utf-8")) or {}
        except ValueError:
            entry["json"] = {}
            entry["valid"] = False
            entry["problem"] = "bad json"
            return entry
    if not entry["has_pcm"]:
        entry["valid"] = False
        return entry
    size = pcm_path.stat().st_size
    if size % 2 != 0:
        entry["valid"] = False
        entry["problem"] = "odd byte count"
        return entry
    frames = to_int(entry["json"].get("frames")) if entry["json"] else None
    if frames is not None and size != frames * 2:
        entry["valid"] = False
        entry["problem"] = f"bytes {size} != frames*2 ({frames*2})"
    return entry


def scan_pcm_directory(directory: Path, trial: str) -> (List[Dict], List[str]):
    """Enumerate every id*.pcm / id*.json including orphans.

    Structurally invalid entries keep their provenance in `entries` and are
    also surfaced in the returned problems list.
    """
    if not directory.exists():
        return [], [f"{trial}: PCM directory missing"]
    entries = []
    problems = []
    stems = sorted({p.stem for p in directory.glob("id*.pcm")} |
                   {j.stem for j in directory.glob("id*.json")})
    for stem in stems:
        entry = load_pcm_entry(directory, stem, trial)
        entries.append(entry)
        if entry["problem"]:
            problems.append(f"{trial}: {stem} {entry['problem']}")
    return entries, problems


def main() -> int:
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trials", nargs="+", help="trial names (e.g. toaB-20261026-07)")
    parser.add_argument("--root", type=Path, default=Path("Logs/Chirp"))
    args = parser.parse_args()
    for trial in args.trials:
        records, problems = parse_trial_records(trial, args.root / trial / "run.jsonl")
        assign_domains(records, trial)
        entries, scan_problems = scan_pcm_directory(
            args.root / "PCM" / trial, trial)
        ledger = build_ledger(records, trial, entries, scan_problems)
        problems.extend(scan_problems)
        print(f"trial={trial} events={len(ledger.events)} "
              f"edges={len(ledger.edges)} domains={len(ledger.domains)} "
              f"problems={len(problems)}")
        for problem in problems:
            print("  !", problem)
        for domain, dom in ledger.domains.items():
            if dom["conflict"]:
                print("  conflict domain:", domain)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())