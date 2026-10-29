"""Acceptance analysis rejects invalid data instead of manufacturing a pass."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC=importlib.util.spec_from_file_location('candidate_trial',Path(__file__).resolve().parents[1]/'sound_candidate_trial.py')
MOD=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(MOD)

class CandidateTrialTests(unittest.TestCase):
    def fixture(self):
        boot='$BOOT,FW=SBCG-M4.1-CAND-R1,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631'
        run={'status':'CAPTURED_PENDING_ANALYSIS','duration_s':90,'port_closed':True,'boot_line':boot,
             'tones':[{'number':i+1,'host_start_s':60+3*i} for i in range(5)]}
        rows=[{'elapsed_s':0,'line':boot}]
        for i in range(1,90):
            for line in [f'$HEARTBEAT,SEQ={i},TICK_MS={i*1000},TX_ERRORS=0',
                         f'$AUDIO,STATE=RUN,FRAMES={i*8192},VALID=1,DROP=0,ERR=0,HAL=00000000,DMA=00000000',
                         '$AUDIO_POS,SESSION=1,INDEX_VALID=1,VALID=1,CONTIG=1,GAP=0,FAULTS=00000000',
                         f'$SOUND_STATE,VALID_BLOCKS={i},CANDIDATES={sum(60.4+3*j<=i for j in range(5))},REJECTED=0,RESETS=0,DROPPED=0,ERRORS=0,ARMED=1,PENDING=0']:
                rows.append({'elapsed_s':i,'line':line})
        for i in range(5):rows.append({'elapsed_s':60.4+3*i,'line':f'$SOUND_CAND,SEQ={i+1},SESSION=1,FIRST={1000+i*100},CONFIRMED={1015+i*100},E_AC=100000,PEAK=1000,CLIP=0,LOCAL=1,SYNC=0,CH=0'})
        rows.sort(key=lambda r:r['elapsed_s'])
        return run,rows

    def result(self,run,rows):
        with tempfile.TemporaryDirectory() as folder:
            prefix=Path(folder)/'test'
            Path(str(prefix)+'-run.json').write_text(json.dumps(run),encoding='utf-8')
            Path(str(prefix)+'-serial.jsonl').write_text('\n'.join(json.dumps(r) for r in rows),encoding='utf-8')
            return MOD.analyze(prefix)

    def test_complete_scope_pass_keeps_precision_false(self):
        run,rows=self.fixture();r=self.result(run,rows)
        self.assertEqual(r['status'],'PASS_SINGLE_BOARD_CANDIDATE_SCOPE');self.assertFalse(r['hardware_precision_verified'])

    def test_stalled_audio_cannot_pass(self):
        run,rows=self.fixture();next(r for r in rows if r['line'].startswith('$AUDIO,'))['line']='$AUDIO,STATE=STALLED,FRAMES=8192,VALID=0,DROP=0,ERR=0,HAL=0,DMA=0'
        self.assertEqual(self.result(run,rows)['status'],'FAIL_OR_INCOMPLETE')

    def test_extra_candidate_in_recovery_is_failure(self):
        run,rows=self.fixture();row=next(r for r in rows if r['line'].startswith('$SOUND_CAND,'));rows.append({'elapsed_s':80,'line':row['line']})
        r=self.result(run,rows);self.assertFalse(r['checks']['one_candidate_per_stimulus']);self.assertFalse(r['checks']['recovery_candidates_zero'])

    def test_background_candidate_is_failure(self):
        run,rows=self.fixture();next(r for r in rows if r['line'].startswith('$SOUND_CAND,'))['elapsed_s']=20
        self.assertFalse(self.result(run,rows)['checks']['background_candidates_zero'])

    def test_second_boot_cannot_pass(self):
        run,rows=self.fixture();rows.append({'elapsed_s':50,'line':run['boot_line']})
        self.assertFalse(self.result(run,rows)['checks']['one_expected_boot'])

    def test_heartbeat_gap_cannot_pass(self):
        run,rows=self.fixture();rows=[r for r in rows if not r['line'].startswith('$HEARTBEAT,SEQ=20,')]
        self.assertFalse(self.result(run,rows)['checks']['heartbeat_contiguous'])

    def test_hex_dma_error_is_rejected_without_parser_crash(self):
        run,rows=self.fixture();row=next(r for r in rows if r['line'].startswith('$AUDIO,'));row['line']=row['line'].replace('DMA=00000000','DMA=0000000A')
        self.assertFalse(self.result(run,rows)['checks']['dma_no_errors_or_drops'])

    def test_missing_stimulus_is_not_assumed_success(self):
        run,rows=self.fixture();run['tones'].pop()
        self.assertFalse(self.result(run,rows)['checks']['five_pc_stimuli'])

    def test_early_only_summary_cannot_prove_recovery(self):
        run,rows=self.fixture();rows=[r for r in rows if not r['line'].startswith('$SOUND_STATE,') or r['elapsed_s']==1]
        self.assertEqual(self.result(run,rows)['status'],'FAIL_OR_INCOMPLETE')

    def test_replayed_candidates_cannot_pass(self):
        run,rows=self.fixture();events=[r for r in rows if r['line'].startswith('$SOUND_CAND,')]
        for row in events[1:]:row['line']=events[0]['line']
        self.assertEqual(self.result(run,rows)['status'],'FAIL_OR_INCOMPLETE')

    def test_cross_session_candidates_cannot_pass(self):
        run,rows=self.fixture();events=[r for r in rows if r['line'].startswith('$SOUND_CAND,')]
        events[1]['line']=events[1]['line'].replace('SESSION=1','SESSION=2')
        self.assertEqual(self.result(run,rows)['status'],'FAIL_OR_INCOMPLETE')

    def test_wrong_uid_is_failure(self):
        run,rows=self.fixture();rows[0]['line']=rows[0]['line'].replace('001A002D','00380050')
        self.assertFalse(self.result(run,rows)['checks']['one_expected_boot'])

if __name__=='__main__':unittest.main()
