import copy
import unittest
import sys
import re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
try:
    from ui_status_analysis import evaluate
except ImportError:
    def evaluate(*args):
        return {'status':'MISSING_IMPLEMENTATION'}


class UIStatusAnalysisTests(unittest.TestCase):
    def setUp(self):
        self.prior={'status':'PASS_QUIET_PIPELINE_SCOPE','duration_after_selected_boot_s':65.5}
        self.rows=[{'line':'$SDRAM_INIT,RESULT=OK'}, {'line':'$SDRAM,STATUS=READY,CHECKED_BYTES=0'}]
        self.rows += [{'elapsed_s':i,'line':f'$UI,STATE=RUNNING,FRAMES={i},ERRORS=0,DISCARDS=0,ROLE=A,NET=DOWN,TEMP_VALID=0'} for i in range(1,66)]

    def test_accepts_only_scoped_software_observation(self):
        result=evaluate(self.rows,self.prior)
        self.assertEqual(result['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')
        self.assertFalse(result['physical_panel_verified'])

    def test_rejects_display_errors(self):
        rows=copy.deepcopy(self.rows);rows[-1]['line']=rows[-1]['line'].replace('ERRORS=0','ERRORS=1')
        self.assertNotEqual(evaluate(rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_destructive_memory_test(self):
        self.rows[1]['line']='$SDRAM,STATUS=PASS,CHECKED_BYTES=33554432'
        self.assertNotEqual(evaluate(self.rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_failed_original_pipeline(self):
        self.assertNotEqual(evaluate(self.rows,{'status':'FAIL_OR_INCOMPLETE'})['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_reversed_refresh(self):
        self.rows[-1]['line']=self.rows[-1]['line'].replace('FRAMES=65','FRAMES=63')
        self.assertNotEqual(evaluate(self.rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_accepts_normal_poll_repeating_one_frame(self):
        self.rows[-1]['line']=self.rows[-2]['line']
        self.assertEqual(evaluate(self.rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_frame_counter_stalling_while_logs_continue(self):
        for row in self.rows[-4:]:
            row['line']=re.sub(r'FRAMES=\d+','FRAMES=61',row['line'])
        self.assertNotEqual(evaluate(self.rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_frame_progress_arriving_after_deadline(self):
        for i,row in enumerate(self.rows[2:]):
            row['line']=re.sub(r'FRAMES=\d+',f'FRAMES={i//3+1}',row['line'])
        self.assertNotEqual(evaluate(self.rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_ui_stopping_before_pipeline_ends(self):
        prior={**self.prior,'duration_after_selected_boot_s':95}
        self.assertNotEqual(evaluate(self.rows[:57],prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_burst_count_without_time_coverage(self):
        for i,row in enumerate(self.rows[2:]):
            row['elapsed_s']=i*0.01
        self.assertNotEqual(evaluate(self.rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_rejects_mid_capture_ui_gap(self):
        rows=self.rows[:30]+self.rows[36:]
        self.assertNotEqual(evaluate(rows,self.prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')

    def test_periodic_queue_snapshot_precedes_later_completed_event(self):
        prior={**self.prior,'status':'FAIL_OR_INCOMPLETE','checks':{'queue_drained_no_losses':False,'all_other_checks':True}}
        rows=self.rows+[
            {'line':'$SOUND_CAND,SEQ=1'}, {'line':'$SOUND_TIME,SEQ=1,STATUS=OK'},
            {'line':'$SOUND_TIME_STATE,QUEUED=1,OK=1,REJECT=0,PENDING=0,DROPPED=0'},
            {'line':'$SOUND_CAND,SEQ=2'}, {'line':'$SOUND_TIME,SEQ=2,STATUS=OK'}]
        result=evaluate(rows,prior)
        self.assertEqual(result['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')
        self.assertEqual(result['original_pipeline']['status'],'FAIL_OR_INCOMPLETE')
        rows[ -3]['line']='$SOUND_TIME_STATE,QUEUED=99,OK=1,REJECT=0,PENDING=98,DROPPED=0'
        self.assertNotEqual(evaluate(rows,prior)['status'],'PASS_UI_RUNTIME_PENDING_VISUAL')
