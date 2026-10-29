"""Synthetic board-log checks, real native estimator; never board access."""
import copy
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import audio_time_map_board_analysis as board
from audio_time_map_analysis import Anchor,reference,build_native

def line(prefix,row): return dict(line=prefix+","+",".join(f"{k}={v}" for k,v in row.items()))
def fixture():
    records=[dict(line="$BOOT,FW=SBCG-M3.2-MAP,UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631")]
    anchors=[]
    for i in range(1,91):
        f=i*47991071//1000
        a=dict(SEQ=str(i),STATUS="OK",VALID="1",SESSION="1",F_LO=str(f),F_HI=str(f+1),SEC=str(i),NSEC="0",WIDTH="1",SYNC="0",OK=str(i),REJECTS="0")
        anchors.append(a);blocks=i*6
        records.append(line("$AUDIO",dict(SEQ=i,STATE="RUN",FRAMES=blocks*8192,H=blocks//2,F=blocks//2,OK=blocks,DROP=0,ERR=0,HAL="00000000",DMA="00000000",VALID=1)))
        records.append(line("$AUDIO_POS",dict(SEQ=i,SESSION=1,OBS_FRAMES=blocks*8192,FIRST=(blocks-1)*8192,COUNT=8192,GAP=0,INDEX_VALID=1,CONTIG=1,FAULTS="00000000",VALID=1)))
        records.append(line("$AUDIO_ANCHOR",a))
        if i%3==0:
            l,m,r=anchors[-3:]
            def anchor(a):return Anchor(True,1,int(a['F_LO']),int(a['F_HI']),int(a['SEC']),0)
            target=int(m['F_LO'])+1
            status,valid,estimate,lo,hi=reference(anchor(l),anchor(r),1,target)
            records.append(line("$AUDIO_MAP",dict(SEQ=i//3,STATUS="OK",CORE=status,VALID=valid,SESSION=1,L=i-2,M=i-1,R=i,FRAME=target,EST_NS=estimate,LO_NS=lo,HI_NS=hi,SYNC=0)))
            observed=int(m['SEC'])*10**9
            records.append(line("$AUDIO_MAP_CHECK",dict(SEQ=i//3,OBS_NS=observed,RES_NS=observed-estimate,SYNC=0)))
    return records

class BoardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory()
        cls.executable=Path(build_native(Path(cls.directory.name))['executable_path'])
    @classmethod
    def tearDownClass(cls): cls.directory.cleanup()
    def test_normal_exact_native_and_fraction(self):
        result=board.check_window(fixture(),self.executable)
        self.assertEqual(result['map_groups'],30)
        self.assertEqual(result['native_matches'],30)
        self.assertEqual(result['positions']['pairs'],90)
        self.assertFalse(result['hardware_verified'])
    def test_bad_numbers_session_frame_context_pair_and_residual_fail(self):
        for old,new in (('FRAME=','FRAME=-'),('STATUS=OK,CORE=0,VALID=1','STATUS=CORE,CORE=6,VALID=0'),
                        ('SESSION=1,L=1','SESSION=2,L=1'),('L=1,M=2,R=3','L=2,M=2,R=3'),
                        ('RES_NS=','RES_NS=9'),('SYNC=0','SYNC=1'),('CORE=0','CORE=1')):
            rows=fixture();idx=next(i for i,r in enumerate(rows) if r['line'].startswith('$AUDIO_MAP_CHECK' if old=='RES_NS=' else '$AUDIO_MAP,'))
            rows[idx]['line']=rows[idx]['line'].replace(old,new,1)
            with self.subTest(old=old),self.assertRaises(ValueError):board.check_window(rows,self.executable)
    def test_missing_duplicate_reordered_checks_and_old_boot_fail(self):
        original=fixture();idx=next(i for i,r in enumerate(original) if r['line'].startswith('$AUDIO_MAP_CHECK'))
        for mode in range(5):
            rows=copy.deepcopy(original)
            if mode==0:del rows[idx]
            elif mode==1:rows.insert(idx,rows[idx])
            elif mode==2:rows[idx-1],rows[idx]=rows[idx],rows[idx-1]
            elif mode==3:rows[0]['line']=rows[0]['line'].replace('SBCG-M3.2-MAP','SBCG-M3.2-ANCHOR')
            else:rows=rows[:30]
            with self.subTest(mode=mode),self.assertRaises(ValueError):board.check_window(rows,self.executable)
    def test_forged_time_and_missing_anchor_audio_error_fail(self):
        for marker,old,new in (('$AUDIO_MAP,','EST_NS=','EST_NS=1'),('$AUDIO,','DROP=0','DROP=1'),('$BOOT,','001A002D','00000000')):
            rows=fixture();idx=next(i for i,r in enumerate(rows) if r['line'].startswith(marker))
            rows[idx]['line']=rows[idx]['line'].replace(old,new,1)
            with self.subTest(marker=marker),self.assertRaises(ValueError):board.check_window(rows,self.executable)
    def test_check_cannot_cross_next_source_anchor(self):
        rows=fixture()
        check_index=next(i for i,r in enumerate(rows) if r['line'].startswith('$AUDIO_MAP_CHECK,'))
        held=rows.pop(check_index)
        next_anchor=next(i for i in range(check_index,len(rows)) if rows[i]['line'].startswith('$AUDIO_ANCHOR,'))
        rows.insert(next_anchor+1,held)
        with self.assertRaises(ValueError):board.check_window(rows,self.executable)

if __name__=='__main__':unittest.main()
