"""离线板间同步分析的精确算术与诚实证据边界测试。"""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location('board_sync_analysis',ROOT/'Analysis/board_sync_analysis.py')
analysis=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(analysis)


def row(line,elapsed=0):
    return {'line':line,'elapsed_s':elapsed}


def raw(t1=1000,t2=1100,t3=1200,t4=1300,epoch=1,tag='PTP_SYNC_RAW'):
    return row(f'${tag},T1_NS={t1},T2_NS={t2},T3_NS={t3},T4_NS={t4},EPOCH={epoch}')


def control(**changes):
    fields=dict(ROLE='B',STATE='LOCKED_INTERNAL',INTERNAL_LOCK='1',SYNC='0',
                OFFSET_X2_NS='0',DELAY_X2_NS='200',DRIFT_PPB='-15',FREQ_PPB='25',
                EPOCH='1',RATE_REV='2',VALID='8',REJECT='0',ERRORS='0')
    fields.update(changes)
    return row('$SYNC_CTRL,'+','.join(f'{key}={value}' for key,value in fields.items()))


class BoardSyncAnalysisTests(unittest.TestCase):
    def test_positive_negative_offset_and_exact_half_nanosecond(self):
        result=analysis.analyze_records([raw(1000,1200,1300,1300),raw(1000,1000,1100,1300),raw(1000,1001,1002,1002)])
        self.assertEqual(result['status'],'OBSERVATION_ONLY')
        self.assertEqual([p['offset_x2_ns'] for p in result['pairs']],['200','-200','1'])
        self.assertEqual(result['pairs'][2]['offset_ns'],{'numerator':'1','denominator':2})
        self.assertEqual(result['pairs'][2]['mean_path_delay_ns'],{'numerator':'1','denominator':2})
        self.assertEqual(result['ranges']['offset_ns']['min'],{'numerator':'-100','denominator':1})
        self.assertEqual(result['precision_status'],'NOT_EXTERNAL_PRECISION_VERIFIED')
        self.assertFalse(result['external_precision_verified'])

    def test_actual_schema_control_alignment_is_explicitly_unavailable(self):
        result=analysis.analyze_records([raw(),control()])
        self.assertEqual(result['alignment']['status'],'UNAVAILABLE_NO_SHARED_SAMPLE_ID')
        self.assertEqual(result['alignment']['compared'],0)
        self.assertEqual(result['controls']['roles'],{'B':1})
        self.assertEqual(result['controls']['internal_lock_count'],1)
        self.assertEqual(result['controls']['drift_ppb']['min'],-15)
        self.assertEqual(result['controls']['frequency_ppb']['max'],25)
        self.assertEqual(result['controls']['epochs'],['1'])
        self.assertEqual(result['controls']['rate_revisions'],['2'])
        self.assertEqual(result['controls']['sync_values'],['0'])

    def test_pair_alias_and_large_integer_values_are_lossless(self):
        base=2**54
        result=analysis.analyze_records([raw(base,base+1,base+2,base+2,tag='PTP_PAIR')])
        self.assertEqual(result['pairs'][0]['t1_a_tx_ns'],str(base))
        self.assertEqual(result['pairs'][0]['offset_x2_ns'],'1')

    def test_no_pair_is_not_successful_hardware_verification(self):
        result=analysis.analyze_records([control(ROLE='A',STATE='REFERENCE',INTERNAL_LOCK='0')])
        self.assertEqual(result['status'],'NO_PAIR')
        self.assertEqual(result['ranges']['offset_ns'],None)
        self.assertFalse(result['external_precision_verified'])

    def test_fault_holdover_and_failed_adjustment_remain_visible(self):
        result=analysis.analyze_records([raw(),control(STATE='FAULT',INTERNAL_LOCK='0',ERRORS='1'),
            control(STATE='HOLDOVER',INTERNAL_LOCK='0'),
            row('$SYNC_ADJUST,KIND=FREQUENCY,PHASE_NS=0,FREQ_PPB=25,STATUS=3,EPOCH=1,RATE_REV=2,SYNC=0')])
        self.assertEqual(result['status'],'REVIEW')
        self.assertEqual(result['controls']['fault_count'],1)
        self.assertEqual(result['controls']['holdover_count'],1)
        self.assertEqual(result['adjustments']['failed'],1)

    def test_invalid_pair_timestamp_order_window_delay_and_duplicates_rejected(self):
        cases=[raw(-1),raw('1e9'),raw(4294967296000000000),raw(100,100,101,99),
               raw(100,101,100,102),raw(1000,2000,3000,1100),
               raw(0,0,100000001,100000001),row('$PTP_SYNC_RAW,T1_NS=1,T2_NS=2,T3_NS=3,EPOCH=1'),
               row('$PTP_SYNC_RAW,T1_NS=1,T1_NS=2,T2_NS=2,T3_NS=3,T4_NS=4,EPOCH=1')]
        for case in cases:
            with self.subTest(case=case):
                result=analysis.analyze_records([case])
                self.assertEqual(result['status'],'REVIEW')
                self.assertEqual(result['pair_count'],0)
                self.assertEqual(len(result['rejected']),1)

    def test_false_sync_claim_and_inconsistent_lock_state_rejected(self):
        cases=[control(SYNC='1'),control(STATE='FAULT',INTERNAL_LOCK='1'),
               control(ROLE='A',STATE='REFERENCE',INTERNAL_LOCK='1'),control(FREQ_PPB='100001'),
               control(DRIFT_PPB=str(2**31)),control(DELAY_X2_NS='-1'),control(EPOCH=str(2**32)),
               row('$PTP,SYNC=1')]
        for case in cases:
            with self.subTest(case=case):
                result=analysis.analyze_records([case])
                self.assertEqual(result['status'],'REVIEW')
                self.assertFalse(result['external_precision_verified'])

    def test_counter_resets_and_generation_changes_not_summed_as_new_events(self):
        result=analysis.analyze_records([control(VALID='10',REJECT='3'),
            control(VALID='11',REJECT='4',EPOCH='2',RATE_REV='3'),
            control(VALID='1',REJECT='0',STATE='ACQUIRING',INTERNAL_LOCK='0')])
        self.assertEqual(result['controls']['counters']['REJECT']['decreases'],1)
        self.assertEqual(result['controls']['counters']['REJECT']['max'],4)
        self.assertEqual(result['controls']['epochs'],['1','2'])

    def test_bad_records_and_garbled_serial_are_reviewed(self):
        for item in [None,[],{},row('$PTP_SYNC_RAW,乱码'),row('$SYNC_CTRL,STATE=FAULT\ufffd'),
                     row('valid',float('nan')),row('valid',True),row('valid',-1)]:
            with self.subTest(item=item):
                result=analysis.analyze_records([item])
                self.assertEqual(result['status'],'REVIEW')

    def test_cli_jsonl_provenance_preserves_input_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'serial.jsonl';target=Path(folder)/'summary.json'
            source.write_text(json.dumps(raw())+'\n'+json.dumps(control())+'\n',encoding='utf-8')
            original=source.read_bytes()
            result=subprocess.run([sys.executable,'-B',str(ROOT/'Analysis/board_sync_analysis.py'),
                '--serial',str(source),'--out',str(target)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            saved=json.loads(target.read_text(encoding='utf-8'))
            self.assertEqual(saved['pair_count'],1)
            self.assertEqual(len(saved['source']['sha256']),64)
            self.assertEqual(source.read_bytes(),original)
            again=subprocess.run([sys.executable,'-B',str(ROOT/'Analysis/board_sync_analysis.py'),
                '--serial',str(source),'--out',str(target)],capture_output=True,text=True)
            self.assertNotEqual(again.returncode,0)

    def test_bad_json_and_duplicate_keys_are_rejected_without_crash(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'serial.jsonl'
            source.write_text('{bad}\n'+json.dumps(raw())+'\n{"line":"a","line":"b"}\n',encoding='utf-8')
            result=analysis.analyze_capture(source)
            self.assertEqual(result['status'],'REVIEW')
            self.assertEqual(result['pair_count'],1)
            self.assertEqual(len(result['rejected']),2)


if __name__=='__main__':
    unittest.main()
