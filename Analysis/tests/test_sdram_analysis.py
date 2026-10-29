"""SDRAM验收不得以初始化成功、部分覆盖或损坏原业务替代全范围通过。"""
from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
try:
 import sdram_analysis as m
except ModuleNotFoundError:
 from types import SimpleNamespace
 m=SimpleNamespace(evaluate=lambda *args:{'status':'UNIMPLEMENTED','checks':dict.fromkeys(
  ['full_memory_pass','no_sdram_errors','expected_init','original_pipeline'],True)})
import test_sound_event_time_analysis as baseline

class SdramAnalysisTests(unittest.TestCase):
 def fixture(self):
  run,rows=baseline.EventAnalysisTests().fixture()
  rows[0]['line']=rows[0]['line'].replace('SBCG-M4.1-TIME-R1','SBCG-U1-SDRAM-R1')
  rows += [{'elapsed_s':.02,'line':'$SDRAM_INIT,RESULT=OK,BASE=C0000000,BYTES=8388608,SDCLK_HZ=66666666,CAS=3,REFRESH=1021'},
           {'elapsed_s':.03,'line':'$SDRAM,STATUS=TESTING,PHASE=0,PASSES=0,CHECKED_BYTES=0,ELAPSED_MS=0,CMD=5,ERR_OFFSET=00000000,EXPECTED=00000000,ACTUAL=00000000'},
           {'elapsed_s':10,'line':'$SDRAM,STATUS=PASS,PHASE=5,PASSES=4,CHECKED_BYTES=33554432,ELAPSED_MS=9900,CMD=5,ERR_OFFSET=00000000,EXPECTED=00000000,ACTUAL=00000000'}]
  rows.sort(key=lambda x:x['elapsed_s']);return rows,run
 def test_pass(self):
  r,run=self.fixture();self.assertEqual(m.evaluate(r,run)['status'],'PASS_SDRAM_AND_QUIET_PIPELINE')
 def test_init_is_not_memory_pass(self):
  r,run=self.fixture();r=[x for x in r if 'STATUS=PASS,' not in x['line']];self.assertEqual(m.evaluate(r,run)['status'],'FAIL_OR_INCOMPLETE')
 def test_partial_test_fails(self):
  r,run=self.fixture();x=next(x for x in r if 'STATUS=PASS,' in x['line']);x['line']=x['line'].replace('33554432','8388608');self.assertFalse(m.evaluate(r,run)['checks']['full_memory_pass'])
 def test_error_before_pass_cannot_be_hidden(self):
  r,run=self.fixture();r.insert(2,{'elapsed_s':.1,'line':'$SDRAM,STATUS=MEMORY_ERROR'});self.assertFalse(m.evaluate(r,run)['checks']['no_sdram_errors'])
 def test_wrong_init_fails(self):
  r,run=self.fixture();x=next(x for x in r if x['line'].startswith('$SDRAM_INIT,'));x['line']=x['line'].replace('REFRESH=1021','REFRESH=1105');self.assertFalse(m.evaluate(r,run)['checks']['expected_init'])
 def test_audio_drop_fails(self):
  r,run=self.fixture();x=next(x for x in r if x['line'].startswith('$AUDIO,'));x['line']=x['line'].replace('DROP=0','DROP=1');self.assertFalse(m.evaluate(r,run)['checks']['original_pipeline'])
 def test_wrong_fw_fails(self):
  r,run=self.fixture();r[0]['line']=r[0]['line'].replace('SBCG-U1-SDRAM-R1','SBCG-M4.1-TIME-R1');self.assertFalse(m.evaluate(r,run)['checks']['original_pipeline'])
