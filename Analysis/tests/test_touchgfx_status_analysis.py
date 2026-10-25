"""TouchGFX evidence must distinguish data updates from rendering frames."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
try:
    from touchgfx_status_analysis import evaluate
except ImportError:
    def evaluate(*args):return {'status':'MISSING'}


class TouchGFXAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.prior={'status':'PASS_QUIET_PIPELINE_SCOPE','duration_after_selected_boot_s':65.5}
        self.rows=[{'line':'$SDRAM_INIT,RESULT=OK'},{'line':'$SDRAM,STATUS=READY,CHECKED_BYTES=0'}]
        self.rows += [{'elapsed_s':i,'line':f'$UI,STATE=RUNNING,FRAMES={i*59},ERRORS=0,DISCARDS=0,ROLE=A,NET=DOWN,BACKEND=TOUCHGFX,UPDATES={i}'} for i in range(1,66)]

    def test_accepts_scoped_evidence_and_keeps_visual_pending(self):
        result=evaluate(self.rows,self.prior)
        self.assertEqual(result['status'],'PASS_TOUCHGFX_RUNTIME_PENDING_VISUAL')
        self.assertFalse(result['physical_panel_verified'])
        self.assertFalse(result['external_sync_precision_verified'])

    def test_rejects_legacy_backend(self):
        self.rows[-1]['line']=self.rows[-1]['line'].replace('BACKEND=TOUCHGFX','BACKEND=RGB565')
        self.assertEqual(evaluate(self.rows,self.prior)['status'],'FAIL_OR_INCOMPLETE')

    def test_rejects_stale_updates_despite_advancing_frames(self):
        for row in self.rows[2:]:row['line']=row['line'].split(',UPDATES=')[0]+',UPDATES=1'
        self.assertEqual(evaluate(self.rows,self.prior)['status'],'FAIL_OR_INCOMPLETE')

    def test_rejects_reset_update_counter(self):
        self.rows[-1]['line']=self.rows[-1]['line'].replace('UPDATES=65','UPDATES=1')
        self.assertEqual(evaluate(self.rows,self.prior)['status'],'FAIL_OR_INCOMPLETE')

    def test_retains_original_audio_failure(self):
        prior=copy.deepcopy(self.prior);prior['status']='FAIL_OR_INCOMPLETE'
        self.assertEqual(evaluate(self.rows,prior)['status'],'FAIL_OR_INCOMPLETE')

    def test_rejects_missing_update_field(self):
        self.rows[-1]['line']=self.rows[-1]['line'].split(',UPDATES=')[0]
        self.assertEqual(evaluate(self.rows,self.prior)['status'],'FAIL_OR_INCOMPLETE')

    def test_reset_fragment_is_checked_against_raw_and_recorded(self):
        import touchgfx_status_analysis as analysis
        raw=b'old\r\npartial\xff$BOOT,FW=SBCG-U3.1-TOUCHGFX,UID=1\r\n$DATA,A=1\r\n'
        rows=[dict(elapsed_s=i,line=p.rstrip(b'\r').decode('ascii',errors='replace')) for i,p in enumerate(raw[:-1].split(b'\n'))]
        selected,proof=analysis.select_reset_window(raw,rows)
        self.assertEqual(selected[0]['elapsed_s'],1)
        self.assertTrue(selected[0]['line'].startswith('$BOOT,'))
        self.assertEqual(proof['discarded_prefix_hex'],b'partial\xff'.hex())
        self.assertTrue(proof['raw_json_projection_matches'])

    def test_two_firmware_boots_are_never_hidden(self):
        import touchgfx_status_analysis as analysis
        raw=b'$BOOT,FW=SBCG-U3.1-TOUCHGFX,UID=1\r\n'*2
        rows=[dict(elapsed_s=i,line=p.rstrip(b'\r').decode()) for i,p in enumerate(raw[:-1].split(b'\n'))]
        with self.assertRaises(ValueError):analysis.select_reset_window(raw,rows)

    def test_unterminated_boot_is_not_discarded_as_capture_tail(self):
        import touchgfx_status_analysis as analysis
        for tail in (b'$BOOT,FW=SBCG-U3.1-TOUCHGFX',b'old\xff$BOOT',b'$BO'):
            raw=b'$BOOT,FW=SBCG-U3.1-TOUCHGFX,UID=1\n'+tail
            with self.assertRaises(ValueError):
                analysis.complete_capture(raw,[{'line':'$BOOT,FW=SBCG-U3.1-TOUCHGFX,UID=1'}, {'incomplete_tail_hex':tail.hex()}])

    def test_regular_partial_tail_is_preserved_with_exact_hex(self):
        import touchgfx_status_analysis as analysis
        raw,rows,tail=analysis.complete_capture(b'$DATA,A=1\n$HEARTBEAT,SEQ=2',
            [{'line':'$DATA,A=1'},{'incomplete_tail_hex':b'$HEARTBEAT,SEQ=2'.hex()}])
        self.assertEqual(raw,b'$DATA,A=1\n')
        self.assertEqual(tail,b'$HEARTBEAT,SEQ=2')
        self.assertEqual(len(rows),1)
