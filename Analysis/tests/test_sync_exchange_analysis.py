"""Capture validation, lossless outputs and integration with the real C core."""
from copy import deepcopy
import contextlib
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import sync_exchange_analysis as analysis

UID = (0x001A002D, 0x30365105, 0x36363631)
UID_TEXT = [f'{v:08X}' for v in UID]
SESSION = 'ABCDEF0100000001'
WINDOW = 800000000
FIELDS = ('t1_pc_sw_ns', 't2_board_hw_ns', 't3_board_hw_ns', 't4_pc_sw_ns')
SOURCES = dict(zip(FIELDS, ('PC_SW', 'BOARD_HW', 'BOARD_HW', 'PC_SW')))
COUNTERS = ('send_errors', 'socket_errors', 'invalid', 'wrong_source', 'wrong_key',
            'wrong_session', 'wrong_uid', 'wrong_sequence', 'wrong_tick', 'late',
            'duplicate_ack', 'duplicate_report', 'invalid_timestamps', 'timeouts',
            'missing_ack', 'missing_report', 'unanswered')

def fixture(exchanges=None):
    exchanges = exchanges or [(1000000000, 1000150000, 1000350000, 1000400000),
                              (2000000000, 2000050000, 2000250000, 2000400000)]
    identity = {'session': SESSION, 'uid': UID_TEXT}
    rows = [{'event': 'ready', 'count': len(exchanges), 'timeout_s': 0.8,
             'interval_s': 1, 'bind': ['192.168.10.100', 6007],
             'target': ['192.168.10.1', 5007], 'hardware_verified': False, **identity}]
    for i, (t1, t2, t3, t4) in enumerate(exchanges, 1):
        key = {**identity, 'sequence': i, 'client_tick_ms': (t1 // 1000000) & 0xffffffff}
        rows += [
            {'event': 'request', **key, 't1_pc_sw_ns': str(t1), 'timestamp_source': 'PC_SW'},
            {'event': 'ack', **key, 't4_pc_sw_ns': str(t4), 'timestamp_source': 'PC_SW'},
            {'event': 'report', **key, 't2_board_hw_ns': str(t2), 't3_board_hw_ns': str(t3), 'timestamp_source': 'BOARD_HW'},
            {'event': 'four_stamp_sample', **key, **dict(zip(FIELDS, map(str, (t1,t2,t3,t4)))),
             'rtt_pc_sw_ns': str(t4-t1), 'timestamp_sources': SOURCES, 'hardware_verified': False},
        ]
    rows.append({'event': 'summary', **identity, 'requested': len(exchanges), 'sent': len(exchanges),
                 'samples': len(exchanges), **dict.fromkeys(COUNTERS, 0),
                 'status': 'PASS', 'stop_reason': 'count', 'hardware_verified': False})
    return rows

class CaptureFixture:
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='sync observation ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.input = self.root / 'time.jsonl'
        self.evidence = self.root / 'evidence.json'
        self.rows = fixture()
        self.save()

    def save(self):
        self.input.write_text(''.join(json.dumps(r)+'\n' for r in self.rows), encoding='utf-8')
        count = next(row['count'] for row in self.rows if row.get('event') == 'ready')
        summary = next(row for row in self.rows if row.get('event') == 'summary')
        proof = {'status': 'PASS', 'hardware_verified': True,
                 'debug_sha256': '1'*64,
                 'boot': '$BOOT,FW=SBCG-M2.2-IO,HCLK_HZ=200000000,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631',
                 'four_stamp_count': count, 'new_ack_count': count,
                 'new_report_count': count,
                 'summary': {'time': summary},
                 'raw_files': {str(self.input): hashlib.sha256(self.input.read_bytes()).hexdigest()}}
        self.evidence.write_text(json.dumps(proof), encoding='utf-8')

    def load(self):
        return analysis.validate_capture(self.input, self.evidence, UID, WINDOW)

    def bad(self, mutate):
        mutate(self.rows)
        self.save()
        with self.assertRaises(analysis.AnalysisError):
            self.load()

class CaptureTests(CaptureFixture, unittest.TestCase):
    def test_valid_capture_keeps_complete_keys_and_string_times(self):
        result = self.load()
        self.assertIsNotNone(result)
        self.assertEqual(len(result.samples), 2)
        self.assertEqual(result.samples[0]['session'], SESSION)
        self.assertEqual(result.samples[0]['timestamp_sources'], SOURCES)
        self.assertEqual(result.metadata['max_exchange_ns'], str(WINDOW))
        self.assertEqual(result.metadata['input_firmware_sha256'], '1'*64)

    def test_report_may_precede_ack(self):
        self.rows[2], self.rows[3] = self.rows[3], self.rows[2]
        self.save()
        self.assertEqual(len(self.load().samples), 2)

    def test_large_integer_strings_are_lossless(self):
        base = 2**54
        self.rows = fixture([(base, base+1, base+2, base+3)])
        self.save()
        self.assertEqual(self.load().samples[0]['t1_pc_sw_ns'], str(base))

    def test_missing_or_duplicate_each_exchange_event_rejected(self):
        original = deepcopy(self.rows)
        for index in (1,2,3,4):
            for duplicate in (False,True):
                with self.subTest(index=index, duplicate=duplicate):
                    self.rows = deepcopy(original)
                    if duplicate: self.rows.insert(index, deepcopy(self.rows[index]))
                    else: self.rows.pop(index)
                    self.save()
                    with self.assertRaises(analysis.AnalysisError): self.load()

    def test_wrong_identity_sequence_tick_and_types_rejected(self):
        for field, value in [('session','1234567800000001'),('uid',['00000001']*3),
                             ('sequence',2),('sequence',True),('client_tick_ms',0)]:
            with self.subTest(field=field):
                self.rows = fixture()
                self.bad(lambda r: r[2].update({field:value}))

    def test_wrong_sources_and_software_claimed_hardware_rejected(self):
        for index, field, value in [(1,'timestamp_source','BOARD_HW'),
                                   (2,'timestamp_source','BOARD_HW'),(3,'timestamp_source','PC_SW'),
                                   (4,'timestamp_sources', dict.fromkeys(FIELDS,'BOARD_HW')),
                                   (4,'hardware_verified',True)]:
            with self.subTest(index=index, field=field):
                self.rows=fixture(); self.bad(lambda r:r[index].update({field:value}))

    def test_float_negative_oversize_and_nondecimal_times_rejected(self):
        for value in (1.5, 1000000000, '-1', '+1', '1e9', '4294967296000000000', True):
            with self.subTest(value=value):
                self.rows=fixture(); self.bad(lambda r:r[4].update(t2_board_hw_ns=value))

    def test_pair_values_tick_rtt_and_event_order_rejected(self):
        mutations=[lambda r:r[4].update(t1_pc_sw_ns='1000000001'),
                   lambda r:r[4].update(rtt_pc_sw_ns='1'),
                   lambda r:r[4].update(client_tick_ms=1001),
                   lambda r:r.insert(1,r.pop(4)), lambda r:r.insert(0,r.pop())]
        for mutation in mutations:
            self.rows=fixture()
            with self.subTest(mutation=mutation): self.bad(mutation)

    def test_within_domain_and_cross_sample_reverse_times_rejected(self):
        for exchanges in [[(100,100,101,99)], [(100,101,100,102)],
                          [(100,200,201,102),(110,190,191,112)],
                          [(100,200,201,102),(101,210,211,103)]]:
            with self.subTest(exchanges=exchanges):
                self.rows=fixture(exchanges);self.save()
                with self.assertRaises(analysis.AnalysisError):self.load()

    def test_next_board_rx_cannot_precede_previous_ack_tx(self):
        self.rows=fixture([(1000,2000,2100,1200),(1300,2050,2150,1500)])
        self.save()
        with self.assertRaises(analysis.AnalysisError):self.load()

    def test_sample_cannot_inject_estimate_or_lock_fields(self):
        for field in ('offset_x2_ns','mean_path_delay_x2_ns','status','index','LOCK'):
            self.rows=fixture()
            self.rows[4][field]='999'
            self.save()
            with self.subTest(field=field),self.assertRaises(analysis.AnalysisError):self.load()

    def test_capture_window_parameter_and_expiry_rejected(self):
        for window in (0, -1, True, 4294967296000000000, WINDOW-1):
            with self.subTest(window=window), self.assertRaises(analysis.AnalysisError):
                analysis.validate_capture(self.input,self.evidence,UID,window)
        self.rows=fixture([(1000000000,1000000000,1000000001,1800000001)])
        self.save()
        with self.assertRaises(analysis.AnalysisError):self.load()

    def test_any_anomaly_or_false_success_count_rejected(self):
        for field in (*COUNTERS,'samples','sent','requested','status','stop_reason','hardware_verified'):
            self.rows=fixture()
            value=1 if field in COUNTERS else (True if field=='hardware_verified' else 'REVIEW' if field=='status' else 'interrupted' if field=='stop_reason' else 1)
            with self.subTest(field=field): self.bad(lambda r:r[-1].update({field:value}))
        self.rows=fixture();self.bad(lambda r:r.insert(-1,{'event':'invalid'}))

    def test_missing_or_duplicate_ready_summary_and_empty_window_rejected(self):
        for rows in [[],fixture()[1:],fixture()[:-1], [fixture()[0],*fixture()],
                     [*fixture(),fixture()[-1]]]:
            self.rows=rows
            # Build evidence from a valid fixture, then replace only input/hash.
            valid=fixture();self.rows=valid;self.save()
            self.input.write_text(''.join(json.dumps(r)+'\n' for r in rows),encoding='utf-8')
            proof=json.loads(self.evidence.read_text());proof['raw_files'][str(self.input)]=hashlib.sha256(self.input.read_bytes()).hexdigest();self.evidence.write_text(json.dumps(proof))
            with self.subTest(rows=len(rows)),self.assertRaises(analysis.AnalysisError):self.load()

    def test_input_hash_missing_proof_uid_boot_and_counts_rejected(self):
        for mutate in [lambda p:p.update(status='REVIEW'),lambda p:p.update(hardware_verified=False),
                       lambda p:p.update(boot=p['boot'].replace('001A002D','001A002E')),
                       lambda p:p.update(debug_sha256='bad'),lambda p:p.update(four_stamp_count=1),
                       lambda p:p.update(raw_files={})]:
            self.rows=fixture();self.save();proof=json.loads(self.evidence.read_text());mutate(proof);self.evidence.write_text(json.dumps(proof))
            with self.subTest(mutate=mutate),self.assertRaises(analysis.AnalysisError):self.load()
        self.save();self.input.write_text(self.input.read_text()+'\n')
        with self.assertRaises(analysis.AnalysisError):self.load()

    def test_duplicate_json_keys_and_malformed_json_rejected(self):
        for text in ['{"event":"ready","event":"summary"}\n','{bad}\n','[]\n']:
            self.input.write_text(text);proof=json.loads(self.evidence.read_text());proof['raw_files'][str(self.input)]=hashlib.sha256(self.input.read_bytes()).hexdigest();self.evidence.write_text(json.dumps(proof))
            with self.subTest(text=text),self.assertRaises(analysis.AnalysisError):self.load()

    def test_invalid_capture_never_invokes_native_build(self):
        self.input.write_text('corrupted')
        with patch.object(analysis,'build_cli') as build:
            with self.assertRaises(analysis.AnalysisError):
                analysis.analyze_capture(self.input,self.evidence,UID,WINDOW,self.root/'out')
            build.assert_not_called()

class NativeBridgeTests(CaptureFixture, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.native_temp=tempfile.TemporaryDirectory(prefix='native sync bridge ')
        cls.addClassCleanup(cls.native_temp.cleanup)
        cls.executable,cls.build_metadata=analysis.build_cli(Path(cls.native_temp.name))

    def test_real_c_preserves_sign_half_ns_and_failure_without_old_result(self):
        rows=[dict(zip(FIELDS,map(str,values))) for values in [
            (1000000000,1000150000,1000350000,1000400000),
            (1000000000,1000050000,1000250000,1000400000),
            (1000,1001,1002,1002),(1000,2000,3000,1100)]]
        results=analysis.run_estimator(self.executable,rows,WINDOW)
        self.assertEqual([r['offset_x2_ns'] for r in results[:3]],['100000','-100000','1'])
        self.assertEqual(results[2]['mean_path_delay_x2_ns'],'1')
        self.assertEqual(results[3],{'index':4,'status':'SYNC_ESTIMATOR_NEGATIVE_DELAY'})

    def test_native_protocol_mismatch_and_bad_results_fail_closed(self):
        good={'index':1,'status':'SYNC_ESTIMATOR_OK','offset_x2_ns':'0','mean_path_delay_x2_ns':'2'}
        invalid=[[],[good,good],[{**good,'index':2}],[{**good,'status':'UNKNOWN'}],
                 [{**good,'offset_x2_ns':1.0}],[{**good,'offset_x2_ns':str(2**63)}],
                 [{**good,'mean_path_delay_x2_ns':'-1'}],
                 [{'index':1,'status':'SYNC_ESTIMATOR_NEGATIVE_DELAY','offset_x2_ns':'9'}]]
        sample=[dict(zip(FIELDS,map(str,(1000,1001,1002,1004))))]
        for rows in invalid:
            with self.subTest(rows=rows),patch.object(analysis.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'\n'.join(json.dumps(r) for r in rows),'')):
                with self.assertRaises(analysis.AnalysisError):analysis.run_estimator(self.executable,sample,WINDOW)
        for rc,out,err in [(2,'',''),(0,json.dumps(good),'unexpected diagnostic'),(0,'not json','')]:
            with patch.object(analysis.subprocess,'run',return_value=subprocess.CompletedProcess([],rc,out,err)),self.assertRaises(analysis.AnalysisError):analysis.run_estimator(self.executable,sample,WINDOW)

    def test_end_to_end_outputs_lossless_results_trace_and_sources(self):
        before=(self.input.read_bytes(),self.evidence.read_bytes())
        output=self.root/'out'
        result=analysis.analyze_capture(self.input,self.evidence,UID,WINDOW,output)
        self.assertEqual(result['status'],'PASS');self.assertFalse(result['hardware_verified'])
        self.assertEqual(result['valid'],2);self.assertEqual(result['rejected'],0)
        self.assertEqual(result['offset_x2_ns']['median_middle'],['-100000','100000'])
        saved=[json.loads(line) for line in (output/'observations.jsonl').read_text().splitlines()]
        self.assertEqual([r['status'] for r in saved],['SYNC_ESTIMATOR_OK']*2)
        self.assertEqual(saved[0]['timestamp_sources'],SOURCES)
        self.assertEqual((self.input.read_bytes(),self.evidence.read_bytes()),before)
        self.assertTrue((output/'native-input.txt').is_file());self.assertTrue((output/'native-output.jsonl').is_file())
        with self.assertRaises(analysis.AnalysisError):analysis.analyze_capture(self.input,self.evidence,UID,WINDOW,output)

    def test_semantic_reject_is_review_and_has_no_estimate(self):
        self.rows=fixture([(1000000000,1000001000,1000100000,1000001000)])
        self.save();out=self.root/'rejected';result=analysis.analyze_capture(self.input,self.evidence,UID,WINDOW,out)
        self.assertEqual(result['status'],'REVIEW');self.assertEqual(result['rejected'],1)
        row=json.loads((out/'observations.jsonl').read_text())
        self.assertEqual(row['status'],'SYNC_ESTIMATOR_NEGATIVE_DELAY');self.assertNotIn('offset_x2_ns',row)

    def test_cli_review_exit_and_artifact_on_invalid_capture(self):
        self.input.write_text('corrupted');output=self.root/'review'
        with contextlib.redirect_stdout(io.StringIO()),contextlib.redirect_stderr(io.StringIO()):
            rc=analysis.main(['--input',str(self.input),'--evidence',str(self.evidence),'--uid',':'.join(UID_TEXT),'--output-dir',str(output)])
        self.assertNotEqual(rc,0)
        self.assertEqual(json.loads((output/'report.json').read_text())['status'],'REVIEW')

    def test_cli_relative_output_is_relative_to_calling_directory(self):
        relative=self.root.name+'-out'
        result=subprocess.run([sys.executable,'-B',str(Path(analysis.__file__).resolve()),
                               '--input',str(self.input),'--evidence',str(self.evidence),
                               '--uid',':'.join(UID_TEXT),'--output-dir',relative],
                              cwd=self.root,capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertEqual(json.loads((self.root/relative/'report.json').read_text())['status'],'PASS')

    def test_cli_blocked_parent_still_emits_review(self):
        blocked=self.root/'blocked';blocked.write_text('preserve')
        stdout=io.StringIO()
        with contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(io.StringIO()):
            rc=analysis.main(['--input',str(self.input),'--evidence',str(self.evidence),
                              '--uid',':'.join(UID_TEXT),'--output-dir',str(blocked/'out')])
        self.assertNotEqual(rc,0);self.assertEqual(json.loads(stdout.getvalue())['status'],'REVIEW')
        self.assertEqual(blocked.read_text(),'preserve')

    def test_cli_unwritable_output_still_emits_review(self):
        stdout=io.StringIO()
        with (patch.object(Path,'mkdir',side_effect=PermissionError('read only output')),
              contextlib.redirect_stdout(stdout),contextlib.redirect_stderr(io.StringIO())):
            rc=analysis.main(['--input',str(self.input),'--evidence',str(self.evidence),
                              '--uid',':'.join(UID_TEXT),'--output-dir',str(self.root/'out')])
        self.assertNotEqual(rc,0);self.assertEqual(json.loads(stdout.getvalue())['status'],'REVIEW')

if __name__=='__main__':
    unittest.main()
