"""Scope and rejection checks for local model/quiet acceptance."""
import importlib.util
from pathlib import Path
import unittest
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
spec=importlib.util.spec_from_file_location('event_time_analysis',Path(__file__).resolve().parents[1]/'sound_event_time_analysis.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)

class EventAnalysisTests(unittest.TestCase):
 def fixture(self,event=False):
  boot='$BOOT,FW=SBCG-M4.1-TIME-R1,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631'
  run={'status':'CAPTURED_PENDING_ANALYSIS','port_closed':True,'duration_s':68}
  records=[{'elapsed_s':0,'line':boot}]
  for i in range(1,68):
   fields=[f'$HEARTBEAT,SEQ={i},TX_ERRORS=0',f'$AUDIO,STATE=RUN,VALID=1,FRAMES={i*48000},DROP=0,ERR=0,HAL=00000000,DMA=00000000',f'$AUDIO_POS,GAP=0,FAULTS=00000000,INDEX_VALID=1,CONTIG=1,VALID=1',f'$SOUND_TIME_STATE,QUEUED={int(event and i>=2)},OK={int(event and i>=3)},REJECT=0,DROPPED=0,PENDING={int(event and i==2)}',f'$AUDIO_ANCHOR,SEQ={i},VALID=1,SESSION=1,F_LO={i*48000},F_HI={i*48000},SEC={i},NSEC=0']
   if i%3==0:fields+=['$AUDIO_MAP,STATUS=OK','$AUDIO_MAP_CHECK,RES_NS=0']
   records += [{'elapsed_s':i,'line':line} for line in fields]
  if event:
   records += [{'elapsed_s':1.5,'line':'$SOUND_CAND,SEQ=1,SESSION=1,FIRST=71999'}, {'elapsed_s':2.1,'line':'$SOUND_TIME,SEQ=1,SESSION=1,FIRST=71999,STATUS=OK,L=1,R=2,EST_NS=1499989583,LO_NS=1499979166,HI_NS=1500000000,LOCAL=1,SYNC=0'}]
  records.sort(key=lambda r:r['elapsed_s']);return run,records
 def test_no_stimulus_quiet_scope_is_explicit(self):
  run,r=self.fixture();result=m.evaluate(r,run,quiet=True);self.assertEqual(result['status'],'PASS_QUIET_PIPELINE_SCOPE');self.assertFalse(result['hardware_precision_verified']);self.assertFalse(result['acoustic_recognition_verified'])
 def test_exact_observed_model(self):
  run,r=self.fixture(True);self.assertEqual(m.evaluate(r,run,quiet=True)['exact_models'],1);self.assertEqual(m.evaluate(r,run,quiet=True)['status'],'PASS_QUIET_PIPELINE_SCOPE')
 def test_no_stimuli_does_not_pass_active_scope(self):
  run,r=self.fixture();self.assertEqual(m.evaluate(r,run)['status'],'FAIL_OR_INCOMPLETE')
 def test_one_audio_drop_fails(self):
  run,r=self.fixture();next(x for x in r if x['line'].startswith('$AUDIO,'))['line']=next(x for x in r if x['line'].startswith('$AUDIO,'))['line'].replace('DROP=0','DROP=1');self.assertFalse(m.evaluate(r,run,quiet=True)['checks']['audio_valid_no_drops'])
 def test_stale_state_coverage_fails(self):
  run,r=self.fixture();r=[x for x in r if not x['line'].startswith('$SOUND_TIME_STATE,') or x['elapsed_s']==1];self.assertFalse(m.evaluate(r,run,quiet=True)['checks']['periodic_coverage'])
 def test_extra_boot_requires_explicit_boundary(self):
  run,r=self.fixture();r.append({'elapsed_s':40,'line':r[0]['line']})
  with self.assertRaises(ValueError):m.evaluate(r,run,quiet=True)
 def test_late_boot_cannot_be_excluded(self):
  run,r=self.fixture();r.append({'elapsed_s':40,'line':r[0]['line']})
  with self.assertRaises(ValueError):m.evaluate(r,run,2,quiet=True)
 def test_wrong_model_is_failure(self):
  run,r=self.fixture(True);row=next(x for x in r if x['line'].startswith('$SOUND_TIME,'));row['line']=row['line'].replace('1499989583','1499989584');self.assertFalse(m.evaluate(r,run,quiet=True)['checks']['model_exact_and_no_invalid_fields'])
 def test_future_anchor_cannot_support_earlier_result(self):
  run,r=self.fixture(True);row=next(x for x in r if x['line'].startswith('$SOUND_TIME,'));row['elapsed_s']=1.9;self.assertIn('future/nonadjacent anchor source',m.evaluate(r,run,quiet=True)['errors'])
 def test_failure_cannot_leak_time(self):
  run,r=self.fixture(True);row=next(x for x in r if x['line'].startswith('$SOUND_TIME,'));row['line']=row['line'].replace('STATUS=OK','STATUS=CLOCK');self.assertIn('failed result contains model fields',m.evaluate(r,run,quiet=True)['errors'])
 def test_duplicate_candidate_is_failure(self):
  run,r=self.fixture(True);r.append(next(x for x in r if x['line'].startswith('$SOUND_CAND,')));self.assertIn('duplicate candidate',m.evaluate(r,run,quiet=True)['errors'])
