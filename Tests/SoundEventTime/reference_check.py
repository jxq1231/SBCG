"""Check real event service output against exact Fraction transfer-progress mapping."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[2];sys.path.insert(0,str(root/'Analysis'))
from audio_time_map_analysis import Anchor,reference

def check(exe):
 cases=[]
 for i in range(64):
  base=(2**53+i*100000) if i%2 else i*100000
  width=i%5;position=base+500+i*701
  cases.append((base,position-1,100000+i,width))
 text=''.join(' '.join(map(str,c))+'\n' for c in cases)
 run=subprocess.run([str(exe)],input=text,text=True,capture_output=True,timeout=20,check=True)
 rows=[s for s in run.stdout.splitlines() if s.startswith('$SOUND_TIME,')]
 assert len(rows)==len(cases),(len(rows),run.stdout)
 for i,((base,first,sec,width),line) in enumerate(zip(cases,rows),1):
  d=dict(p.split('=',1) for p in line.split(',')[1:])
  expected=reference(Anchor(True,1,base,base+width,sec,0),Anchor(True,1,base+48000,base+48000+width,sec+1,0),1,first+1)
  assert expected[0]==0 and d['STATUS']=='OK' and int(d['SEQ'])==i and int(d['FIRST'])==first,line
  assert tuple(int(d[k]) for k in ('EST_NS','LO_NS','HI_NS'))==expected[2:],(d,expected)
 print('PASS 64 real event service/Fraction cases, FIRST+1, phase widths0..4 and frames above2^53')
