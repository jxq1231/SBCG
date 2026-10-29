"""Replay captured software/hardware observations through the existing C core.

Only files and a host executable are used. A successful replay does not prove
synchronization or perform a new hardware test.
"""
import argparse
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
MAX_NS = 4294967295999999999
DEFAULT_WINDOW_NS = 800000000
FIELDS = ('t1_pc_sw_ns', 't2_board_hw_ns', 't3_board_hw_ns', 't4_pc_sw_ns')
SOURCES = dict(zip(FIELDS, ('PC_SW', 'BOARD_HW', 'BOARD_HW', 'PC_SW')))
SAMPLE_FIELDS = {'event', 'sequence', 'client_tick_ms', 'session', 'uid',
                 'rtt_pc_sw_ns', 'timestamp_sources', 'hardware_verified', *FIELDS}
COUNTERS = ('send_errors', 'socket_errors', 'invalid', 'wrong_source', 'wrong_key',
            'wrong_session', 'wrong_uid', 'wrong_sequence', 'wrong_tick', 'late',
            'duplicate_ack', 'duplicate_report', 'invalid_timestamps', 'timeouts',
            'missing_ack', 'missing_report', 'unanswered')
STATUSES = {'SYNC_ESTIMATOR_' + name for name in
            ('OK', 'BAD_ARGUMENT', 'OUT_OF_RANGE', 'BAD_ORDER', 'WINDOW_EXCEEDED', 'NEGATIVE_DELAY')}

class AnalysisError(ValueError):
    """The whole observation window needs review; no fallback estimate exists."""

@dataclass(frozen=True)
class ValidatedCapture:
    samples: tuple
    metadata: dict

def require(condition, message):
    if not condition:
        raise AnalysisError(message)

def sha256(data):
    return hashlib.sha256(data).hexdigest()

def object_fields(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'duplicate JSON field: ' + key)
        result[key] = value
    return result

def bad_constant(value):
    raise AnalysisError('nonfinite JSON: '+value)

def json_value(text):
    try:
        return json.loads(text, object_pairs_hook=object_fields, parse_constant=bad_constant)
    except (ValueError, TypeError) as error:
        raise AnalysisError('invalid JSON: ' + str(error)) from error

def read_bytes(path):
    try:
        require(path.stat().st_size <= 16*1024*1024, 'input exceeds 16 MiB')
        data = path.read_bytes()
        require(len(data) <= 16*1024*1024, 'input grew beyond limit')
        return data
    except OSError as error:
        raise AnalysisError(str(error)) from error

def uid_value(value):
    require(isinstance(value, (list, tuple)) and len(value) == 3, 'UID needs three words')
    require(all(isinstance(v, str) and re.fullmatch(r'[0-9A-Fa-f]{8}', v) for v in value), 'invalid UID words')
    words = tuple(int(v, 16) for v in value)
    require(any(words), 'zero UID')
    return words

def parse_uid(text):
    try:
        return uid_value(text.split(':'))
    except AnalysisError as error:
        raise argparse.ArgumentTypeError(str(error)) from error

def session_value(value):
    require(isinstance(value, str) and re.fullmatch(r'[0-9A-Fa-f]{16}', value) is not None, 'invalid session')
    require(int(value, 16) != 0, 'zero session')
    return value.upper()

def integer(value, minimum, maximum, name):
    require(type(value) is int and minimum <= value <= maximum, 'invalid ' + name)
    return value

def ns_value(value):
    require(isinstance(value, str) and re.fullmatch(r'[0-9]{1,20}', value) is not None, 'timestamp must be an unsigned decimal string')
    number = int(value)
    require(number <= MAX_NS, 'timestamp out of C domain')
    return number

def validate_capture(input_path, evidence_path, uid, max_exchange_ns):
    """Fail the entire window before native execution on capture anomalies."""
    try:
        input_path, evidence_path = Path(input_path).resolve(), Path(evidence_path).resolve()
        integer(max_exchange_ns, 1, MAX_NS, 'exchange window')
        require(isinstance(uid, (tuple, list)) and len(uid) == 3 and
                all(type(v) is int and 0 <= v <= 0xffffffff for v in uid) and any(uid), 'invalid target UID')
        uid = tuple(uid)
        raw, proof_raw = read_bytes(input_path), read_bytes(evidence_path)
        proof = json_value(proof_raw.decode('utf-8'))
        require(isinstance(proof, dict) and proof.get('status') == 'PASS' and proof.get('hardware_verified') is True, 'missing successful joint hardware evidence')
        digest = sha256(raw)
        hashes = proof.get('raw_files')
        require(isinstance(hashes, dict), 'missing evidence hashes')
        matching = []
        for name, value in hashes.items():
            candidate = Path(name.replace('\\', '/'))
            if not candidate.is_absolute():
                candidate = ROOT / candidate
            if candidate.resolve() == input_path:
                matching.append(value)
        require(matching == [digest], 'capture hash not uniquely bound to evidence')
        firmware_hash = proof.get('debug_sha256')
        require(isinstance(firmware_hash, str) and re.fullmatch(r'[0-9a-fA-F]{64}', firmware_hash) is not None, 'invalid firmware hash in evidence')
        boot_line = proof.get('boot')
        require(isinstance(boot_line, str) and boot_line.startswith('$BOOT,'), 'missing BOOT evidence')
        boot = dict(item.split('=', 1) for item in boot_line.split(',')[1:])
        require(bool(boot.get('FW')) and boot.get('HCLK_HZ') == '200000000', 'incompatible BOOT evidence')
        require(uid_value([boot.get('UID_W'+str(i)) for i in range(3)]) == uid, 'BOOT UID mismatch')
        rows = [json_value(line) for line in raw.decode('utf-8').splitlines()]
        require(len(rows) >= 6 and all(isinstance(r, dict) for r in rows), 'empty/incomplete capture')
        require(rows[0].get('event') == 'ready' and rows[-1].get('event') == 'summary', 'ready/summary must delimit the window')
        ready, summary = rows[0], rows[-1]
        count = integer(ready.get('count'), 1, 3600, 'request count')
        require(len(rows) == 2+count*4, 'missing or extra capture events')
        session = session_value(ready.get('session'))
        require(uid_value(ready.get('uid')) == uid and ready.get('hardware_verified') is False, 'ready identity/source mismatch')
        timeout = Decimal(str(ready.get('timeout_s')))
        require(timeout.is_finite() and 0 < timeout <= 60, 'invalid capture timeout')
        timeout_ns = timeout * 1000000000
        require(timeout_ns == timeout_ns.to_integral_value() and int(timeout_ns) == max_exchange_ns, 'capture timeout and explicit window differ')
        require(summary.get('status') == 'PASS' and summary.get('stop_reason') == 'count' and summary.get('hardware_verified') is False, 'capture window is not a complete software PASS')
        require(session_value(summary.get('session')) == session and uid_value(summary.get('uid')) == uid, 'summary identity mismatch')
        for name in ('requested', 'sent', 'samples'):
            require(type(summary.get(name)) is int and summary[name] == count, 'summary count mismatch: '+name)
        for name in COUNTERS:
            require(type(summary.get(name)) is int and summary[name] == 0, 'capture anomaly: '+name)
        require(all(type(proof.get(n)) is int and proof[n] == count for n in ('four_stamp_count', 'new_ack_count', 'new_report_count')), 'joint evidence count mismatch')
        proof_summary = proof.get('summary', {}).get('time', {})
        require(isinstance(proof_summary, dict) and all(proof_summary.get(k) == summary.get(k) for k in ('session', 'uid', 'status', 'requested', 'sent', 'samples', 'stop_reason', 'hardware_verified', *COUNTERS)), 'joint evidence summary mismatch')
        groups = {name:{} for name in ('request', 'ack', 'report', 'four_stamp_sample')}
        sequences = {name:[] for name in groups}
        for position, row in enumerate(rows[1:-1], 1):
            event = row.get('event')
            require(event in groups, 'unexpected capture event')
            require(session_value(row.get('session')) == session and uid_value(row.get('uid')) == uid, 'mixed identity')
            sequence = integer(row.get('sequence'), 1, count, 'sequence')
            tick = integer(row.get('client_tick_ms'), 0, 0xffffffff, 'client tick')
            key = (sequence, tick)
            require(key not in groups[event], 'duplicate '+event)
            groups[event][key] = (row, position)
            sequences[event].append(sequence)
        for event in groups:
            require(sequences[event] == list(range(1, count+1)), 'missing/duplicate/out of order '+event)
            require(groups[event].keys() == groups['request'].keys(), 'full-key mismatch')
        samples = []
        previous_rx = previous_tx = previous_t4 = None
        previous_sample_position = 0
        for key, (sample, sample_position) in groups['four_stamp_sample'].items():
            require(set(sample) == SAMPLE_FIELDS, 'unexpected sample fields')
            request, request_position = groups['request'][key]
            ack, ack_position = groups['ack'][key]
            report, report_position = groups['report'][key]
            require(previous_sample_position < request_position < min(ack_position, report_position) and max(ack_position, report_position) < sample_position, 'capture event order violates one pending request')
            require(request.get('timestamp_source') == ack.get('timestamp_source') == 'PC_SW' and report.get('timestamp_source') == 'BOARD_HW', 'incorrect observation source')
            require(sample.get('timestamp_sources') == SOURCES and sample.get('hardware_verified') is False, 'sample source mismatch')
            t1, t2, t3, t4 = [ns_value(sample.get(field)) for field in FIELDS]
            require(sample[FIELDS[0]] == request.get(FIELDS[0]) and sample[FIELDS[3]] == ack.get(FIELDS[3]) and sample[FIELDS[1]] == report.get(FIELDS[1]) and sample[FIELDS[2]] == report.get(FIELDS[2]), 'sample and request/ACK/REPORT timestamps differ')
            require(((t1//1000000) & 0xffffffff) == key[1], 'client tick differs from t1')
            require(t4 >= t1 and t3 >= t2, 'within-domain reversed time')
            require(t4-t1 <= max_exchange_ns and t3-t2 <= max_exchange_ns, 'capture exceeds exchange window')
            require(ns_value(sample.get('rtt_pc_sw_ns')) == t4-t1, 'software RTT mismatch')
            if previous_rx is not None:
                # The next request starts after receiving the previous ACK;
                # its board RX must also follow that ACK's board TX.
                require(t2 > previous_rx and t2 > previous_tx and t3 > previous_tx and t1 > previous_t4, 'noncontinuous clock/sample order')
            previous_rx, previous_tx, previous_t4 = t2, t3, t4
            previous_sample_position = sample_position
            samples.append(dict(sample))
        return ValidatedCapture(tuple(samples), {
            'input_path':str(input_path), 'input_sha256':digest,
            'evidence_path':str(evidence_path), 'evidence_sha256':sha256(proof_raw),
            'input_firmware_sha256':firmware_hash.lower(), 'input_boot':boot_line,
            'input_hardware_evidence':'PASS: referenced prior joint acceptance',
            'max_exchange_ns':str(max_exchange_ns), 'expected_count':count,
            'session':session, 'uid':[f'{v:08X}' for v in uid],
            'domains':{'A':'PC_SW monotonic reference', 'B':'BOARD_HW local MAC reference'},
            'timestamp_sources':SOURCES,
        })
    except AnalysisError:
        raise
    except (OSError, UnicodeError, ValueError, TypeError, KeyError, AttributeError, InvalidOperation) as error:
        raise AnalysisError('capture validation failed: '+str(error)) from error

def build_cli(output_dir):
    """Compile only the host adapter plus the unchanged real C estimator."""
    output_dir = Path(output_dir).resolve()
    compiler = shutil.which('gcc')
    require(compiler is not None, 'host GCC is required')
    executable = output_dir / ('sync_estimator_cli.exe' if os.name == 'nt' else 'sync_estimator_cli')
    require(not executable.exists(), 'native output already exists')
    paths = [ROOT/'Analysis/sync_estimator_cli.c', ROOT/'App/Sync/sync_estimator.c', ROOT/'App/Sync/sync_estimator.h']
    hashes = {str(p.relative_to(ROOT)):sha256(read_bytes(p)) for p in paths}
    command = [compiler, '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic', '-I', str(ROOT/'App'), str(paths[0]), str(paths[1]), '-o', str(executable)]
    try:
        result = subprocess.run(command, cwd=ROOT, text=True, encoding='utf-8', errors='replace', capture_output=True, timeout=30, check=False)
    except (OSError, subprocess.SubprocessError) as error:
        raise AnalysisError('native compilation failed: '+str(error)) from error
    (output_dir/'build.log').write_text(result.stdout+result.stderr, encoding='utf-8')
    require(result.returncode == 0 and executable.is_file(), 'native compilation failed; see build.log')
    require(all(sha256(read_bytes(ROOT/p)) == h for p,h in hashes.items()), 'native source changed during compilation')
    metadata = {'command':command, 'returncode':result.returncode, 'source_sha256':hashes, 'executable_path':str(executable), 'executable_sha256':sha256(read_bytes(executable))}
    (output_dir/'build.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
    return executable, metadata

def run_estimator(executable, samples, max_exchange_ns, trace_dir=None):
    integer(max_exchange_ns, 1, MAX_NS, 'exchange window')
    require(1 <= len(samples) <= 3600, 'invalid native batch size')
    inputs = []
    for index, sample in enumerate(samples, 1):
        times = [ns_value(sample.get(field)) for field in FIELDS]
        inputs.append(' '.join(map(str, [index, *times, max_exchange_ns]))+'\n')
    payload = ''.join(inputs)
    try:
        result = subprocess.run([str(executable)], input=payload, text=True, encoding='utf-8', errors='strict', capture_output=True, timeout=10, check=False)
    except (OSError, UnicodeError, subprocess.SubprocessError) as error:
        raise AnalysisError('native execution failed: '+str(error)) from error
    if trace_dir is not None:
        for name, text in [('native-input.txt',payload), ('native-output.jsonl',result.stdout), ('native-stderr.txt',result.stderr)]:
            with (Path(trace_dir)/name).open('x', encoding='utf-8') as stream:
                stream.write(text)
    require(result.returncode == 0 and not result.stderr, 'native process failed or emitted diagnostics')
    rows = [json_value(line) for line in result.stdout.splitlines()]
    require(len(rows) == len(samples), 'native result count mismatch')
    for index, row in enumerate(rows, 1):
        require(isinstance(row, dict) and type(row.get('index')) is int and row['index'] == index, 'native index mismatch')
        status = row.get('status')
        require(isinstance(status, str) and status in STATUSES, 'unknown native status')
        expected = {'index', 'status'}
        if status == 'SYNC_ESTIMATOR_OK':
            expected.update(('offset_x2_ns', 'mean_path_delay_x2_ns'))
            for field in ('offset_x2_ns', 'mean_path_delay_x2_ns'):
                value = row.get(field)
                require(isinstance(value, str) and re.fullmatch(r'-?[0-9]{1,20}', value) is not None and -(2**63) <= int(value) < 2**63, 'invalid native integer result')
            require(int(row['mean_path_delay_x2_ns']) >= 0, 'native OK returned negative delay')
        require(set(row) == expected, 'native result schema mismatch or stale failure output')
    return rows

def distribution(values):
    ordered = sorted(values)
    middle = len(ordered)//2
    median = ordered[middle:middle+1] if len(ordered)%2 else ordered[middle-1:middle+1]
    return {'min':str(ordered[0]), 'median_middle':[str(v) for v in median], 'max':str(ordered[-1])}

def save_report(output_dir, report):
    with (Path(output_dir)/'report.json').open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)

def analyze_capture(input_path, evidence_path, uid, max_exchange_ns, output_dir):
    capture = validate_capture(input_path, evidence_path, uid, max_exchange_ns)
    output_dir = Path(output_dir).resolve()
    try:
        output_dir.mkdir(parents=True, exist_ok=False)
    except OSError as error:
        raise AnalysisError('output directory must be new: '+str(error)) from error
    try:
        executable, build = build_cli(output_dir)
        results = run_estimator(executable, capture.samples, max_exchange_ns, output_dir)
        rows = []
        for sample, native in zip(capture.samples, results):
            row = dict(sample)
            row.update(native)
            row.update(event='estimate', hardware_verified=False)
            rows.append(row)
        valid = [row for row in rows if row['status'] == 'SYNC_ESTIMATOR_OK']
        report = {'event':'summary', 'status':'PASS' if len(valid) == len(rows) else 'REVIEW',
                  'hardware_verified':False, 'algorithm_replay_verified':len(valid) == len(rows),
                  'valid':len(valid), 'rejected':len(rows)-len(valid), **capture.metadata,
                  'build':build, 'units':'output numerators equal twice nanoseconds; no accuracy claim'}
        for field in ('offset_x2_ns', 'mean_path_delay_x2_ns'):
            report[field] = distribution([int(row[field]) for row in valid]) if valid else None
        with (output_dir/'observations.jsonl').open('x', encoding='utf-8') as stream:
            for row in rows:
                stream.write(json.dumps(row)+'\n')
        save_report(output_dir, report)
        return report
    except (AnalysisError, OSError) as error:
        save_report(output_dir, {'event':'summary', 'status':'REVIEW', 'hardware_verified':False, 'reason':str(error), **capture.metadata})
        raise AnalysisError(str(error)) from error

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--uid', type=parse_uid, required=True)
    parser.add_argument('--max-exchange-ns', type=int, default=DEFAULT_WINDOW_NS)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args(argv)
    output = args.output_dir or ROOT/'Logs/M2_2_Sync_Observation'/(
        datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S')+'-'+uuid.uuid4().hex[:8])
    existed_before = output.exists()
    try:
        report = analyze_capture(args.input, args.evidence, args.uid, args.max_exchange_ns, output)
    except (AnalysisError, OSError) as error:
        report = {'event':'summary', 'status':'REVIEW', 'hardware_verified':False, 'reason':str(error)}
        # Artifact failure must not suppress the structured rejection itself.
        try:
            if not existed_before and not output.exists():
                output.mkdir(parents=True, exist_ok=False)
                save_report(output, report)
        except OSError as artifact_error:
            report['artifact_error'] = str(artifact_error)
        print(json.dumps(report))
        print('Replay failed: '+str(error), file=sys.stderr)
        return 1
    print(json.dumps(report))
    return 0 if report['status'] == 'PASS' else 1

if __name__ == '__main__':
    sys.exit(main())
