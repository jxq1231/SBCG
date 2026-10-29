"""Verify event/model association; explicit preparation BOOT boundary, no acoustic claims."""
import argparse
import hashlib
import json
from pathlib import Path
from audio_time_map_analysis import Anchor,reference,reference_q16
from serial_capture_integrity import validate_boot_capture
FW='SBCG-M4.1-TIME'

def fields(line):
 parts=line.split(',');return parts[0],dict(p.split('=',1) for p in parts[1:] if '=' in p)

def evaluate(records,run,preparation_boots=1,quiet=False,expected_firmware=None):
 records=[r for r in records if 'line' in r and r.get('elapsed_s') is not None]
 boots=[i for i,r in enumerate(records) if r['line'].startswith('$BOOT,')]
 if len(boots)!=preparation_boots:raise ValueError('BOOT count differs from explicitly recorded preparation')
 start=boots[-1];shift=records[start]['elapsed_s']
 if preparation_boots>1 and (shift>2 or any(records[i]['elapsed_s']>2 for i in boots)):raise ValueError('extra BOOT outside preparation boundary')
 selected=[{'elapsed_s':r['elapsed_s']-shift,'line':r['line']} for r in records[start:]]
 groups={}
 for r in selected:
  k,d=fields(r['line']);groups.setdefault(k,[]).append((r['elapsed_s'],d))
 def rows(k):return groups.get(k,[])
 def zero(k,keys):return bool(rows(k)) and all(all(int(d[x],16 if x in ('HAL','DMA','FAULTS') else 10)==0 for x in keys) for _,d in rows(k))
 duration=run.get('duration_s',0)-shift
 def covers(k):
  times=[t for t,_ in rows(k)]
  return len(times)>=(55 if quiet else 80) and times[0]<=2.5 and times[-1]>=duration-3 and all(0<b-a<=2.5 for a,b in zip(times,times[1:]))
 anchor_sources={int(d['SEQ']):(t,d) for t,d in rows('$AUDIO_ANCHOR') if d.get('VALID')=='1'}
 anchors={seq:d for seq,(_,d) in anchor_sources.items()}
 events={};errors=[]
 for t,d in rows('$SOUND_CAND'):
  seq=int(d['SEQ'])
  if seq in events:errors.append('duplicate candidate')
  events[seq]=(t,d)
 results={};verified=0;rejected=0
 for t,d in rows('$SOUND_TIME'):
  seq=int(d['SEQ'])
  if seq in results or seq not in events:errors.append('duplicate or foreign time result');continue
  results[seq]=(t,d);et,event=events[seq]
  if d.get('SESSION')!=event.get('SESSION') or d.get('FIRST')!=event.get('FIRST') or t<et:errors.append('candidate identity/order mismatch')
  if d.get('LOCAL')!='1' or d.get('SYNC')!='0':errors.append('scope mismatch')
  if d.get('STATUS')!='OK':
   rejected+=1
   if d.get('STATUS') not in ('CLOCK','STREAM','ANCHOR','SESSION','ORDER','GAP','STALE','NO_LEFT','TIMEOUT','CORE'):errors.append('unknown rejection status')
   if any(k in d for k in ('EST_NS','LO_NS','HI_NS','L','R')):errors.append('failed result contains model fields')
   continue
  try:
   left,right=anchors[int(d['L'])],anchors[int(d['R'])]
   if int(d['R'])!=int(d['L'])+1 or any(anchor_sources[int(d[k])][0]>t for k in ('L','R')):errors.append('future/nonadjacent anchor source')
   def a(x):return Anchor(True,int(x['SESSION']),int(x['F_LO']),int(x['F_HI']),int(x['SEC']),int(x['NSEC']))
   expected=reference(a(left),a(right),int(d['SESSION']),int(d['FIRST'])+1)
   actual=tuple(int(d[k]) for k in ('EST_NS','LO_NS','HI_NS'))
   if expected[0]!=0 or actual!=expected[2:]:errors.append('exact Fraction mismatch')
   else:verified+=1
  except (KeyError,ValueError):errors.append('missing/invalid model source')
 # Independent Q16 TOA records are optional for old captures; when present they
 # must identify a real candidate and match the exact rational model.
 toa_results={};toa_verified=0
 for t,d in rows('$SOUND_TOA_TIME'):
  seq=int(d['SEQ'])
  if seq in toa_results or seq not in events:errors.append('duplicate or foreign TOA result');continue
  toa_results[seq]=(t,d)
  et,event=events[seq]
  if d.get('SESSION')!=event.get('SESSION'):errors.append('TOA candidate identity mismatch')
  if d.get('LOCAL')!='1' or d.get('SYNC')!='0':errors.append('TOA scope mismatch')
  if d.get('STATUS')!='OK':
   if d.get('STATUS') not in ('CLOCK','STREAM','ANCHOR','SESSION','ORDER','GAP','STALE','NO_LEFT','TIMEOUT','CORE','TOA_MISSING','TOA_INVALID'):errors.append('unknown TOA status')
   if any(k in d for k in ('EST_NS','LO_NS','HI_NS','L','R','FRAME','Q16')):errors.append('failed TOA contains model fields')
   continue
  try:
   left,right=anchors[int(d['L'])],anchors[int(d['R'])]
   if int(d['R'])!=int(d['L'])+1 or any(anchor_sources[int(d[k])][0]>t for k in ('L','R')):errors.append('future/nonadjacent TOA anchor')
   expected=reference_q16(a(left),a(right),int(d['SESSION']),int(d['FRAME']),int(d['Q16']))
   actual=tuple(int(d[k]) for k in ('EST_NS','LO_NS','HI_NS'))
   if expected[0]!=0 or actual!=expected[2:]:errors.append('TOA exact Fraction mismatch')
   elif event.get('TOA_US')=='NA':errors.append('TOA success with SOUND_CAND TOA_US=NA')
   else:toa_verified+=1
  except (KeyError,ValueError):errors.append('missing/invalid TOA model source')
 responses=[]
 for tone in run.get('tones',[]):
  begin=tone['host_start_s']-shift
  ids=[seq for seq,(t,_) in events.items() if begin<=t<begin+2.5]
  responses.append({'number':tone['number'],'candidate_ids':ids,'all_have_ok_model':bool(ids) and all(seq in results and results[seq][1].get('STATUS')=='OK' for seq in ids)})
 boot=rows('$BOOT')[0][1]
 states=rows('$SOUND_TIME_STATE')
 checks={
  'complete_bounded_capture':run.get('status')=='CAPTURED_PENDING_ANALYSIS' and run.get('port_closed') is True and duration>=(60 if quiet else 80),
  'expected_A_boot':boot.get('FW') in ((expected_firmware,) if expected_firmware is not None else (FW,FW+'-R1')) and (boot.get('UID_W0'),boot.get('UID_W1'),boot.get('UID_W2'))==('001A002D','30365105','36363631'),
  'periodic_coverage':all(covers(k) for k in ('$HEARTBEAT','$AUDIO','$AUDIO_POS','$SOUND_TIME_STATE')),
  'heartbeat_contiguous':all(int(b['SEQ'])==int(a['SEQ'])+1 for (_,a),(_,b) in zip(rows('$HEARTBEAT'),rows('$HEARTBEAT')[1:])),
  'console_no_errors':zero('$HEARTBEAT',['TX_ERRORS']),
  'audio_valid_no_drops':zero('$AUDIO',['DROP','ERR','HAL','DMA']) and all(d.get('STATE')=='RUN' and d.get('VALID')=='1' for _,d in rows('$AUDIO')),
  'positions_contiguous':zero('$AUDIO_POS',['GAP','FAULTS']) and all(d.get('INDEX_VALID')=='1' and d.get('CONTIG')=='1' and d.get('VALID')=='1' for _,d in rows('$AUDIO_POS')),
  'all_candidates_accounted':(quiet or bool(events)) and events.keys()==results.keys(),
  'model_exact_and_no_invalid_fields':verified>=(0 if quiet else 5) and not errors,
  'toa_model_optional_and_exact':(not toa_results) or (toa_verified>0 and not errors),
  'stimulus_scope_satisfied':(quiet and not run.get('tones') and not run.get('play_requested',False)) or (not quiet and len(responses)==5 and all(r['all_have_ok_model'] for r in responses)),
  'queue_drained_no_losses':bool(states) and states[-1][1].get('PENDING')=='0' and zero('$SOUND_TIME_STATE',['DROPPED']) and int(states[-1][1].get('QUEUED','-1'))==len(events) and int(states[-1][1].get('OK','-1'))==verified and int(states[-1][1].get('REJECT','-1'))==rejected,
  'original_map_observer_alive':len(rows('$AUDIO_MAP_CHECK'))>=20 and len(rows('$AUDIO_MAP'))==len(rows('$AUDIO_MAP_CHECK')) and all(d.get('STATUS')=='OK' for _,d in rows('$AUDIO_MAP')),
 }
 return {'status':('PASS_QUIET_PIPELINE_SCOPE' if quiet else 'PASS_LOCAL_EVENT_TRANSFER_MODEL') if all(checks.values()) else 'FAIL_OR_INCOMPLETE','checks':checks,'errors':errors,'firmware':boot,'duration_after_selected_boot_s':duration,'explicit_preparation_boots':preparation_boots,'selected_boot_shift_s':shift,'counts':{k:len(v) for k,v in groups.items()},'quiet_scope':quiet,'exact_models':verified,'rejected_models':rejected,'toa_exact_models':toa_verified,'toa_records':len(toa_results),'responses':responses,'last_time_state':states[-1][1] if states else None,'hardware_precision_verified':False,'acoustic_recognition_verified':False,'coordinate':'FIRST zero-based PCM frame -> FIRST+1 completed-transfer progress; not acoustic arrival time'}

def analyze(prefix,preparation_boots,quiet=False,expected_firmware=None):
 run=json.loads(Path(str(prefix)+'-run.json').read_text(encoding='utf-8'))
 records=[json.loads(s) for s in Path(str(prefix)+'-serial.jsonl').read_text(encoding='utf-8').splitlines()]
 raw=Path(str(prefix)+'-serial.bin').read_bytes();original=raw
 tail=b''
 if not raw.endswith(b'\n'):
  raw,tail=raw.rsplit(b'\n',1);raw+=b'\n'
  if records[-1]!={'incomplete_tail_hex':tail.hex()}:raise ValueError('tail mismatch')
  records=records[:-1]
 parts=raw[:-1].split(b'\n')
 if len(parts)!=len(records) or any(p.rstrip(b'\r').decode('ascii',errors='replace')!=r.get('line') for p,r in zip(parts,records)):raise ValueError('raw/JSON projection mismatch')
 boot_indices=[i for i,r in enumerate(records) if r.get('elapsed_s') is not None and r['line'].startswith('$BOOT,')]
 if len(boot_indices)!=preparation_boots:raise ValueError('unexpected BOOT count')
 start=boot_indices[-1];_,proof=validate_boot_capture(b'\n'.join(parts[start:])+b'\n',records[start:],fields(records[start]['line'])[1]['FW'])
 result=evaluate(records,run,preparation_boots,quiet,expected_firmware)
 result['integrity']={**proof,'original_raw_sha256':hashlib.sha256(original).hexdigest(),'excluded_prefix_rows':start,'excluded_tail_bytes':len(tail),'excluded_tail_hex':tail.hex()}
 return result

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prefix',type=Path,required=True);parser.add_argument('--preparation-boots',type=int,default=1);parser.add_argument('--quiet',action='store_true');args=parser.parse_args()
 result=analyze(args.prefix,args.preparation_boots,args.quiet)
 dest=Path(str(args.prefix)+'-time-analysis.json');dest.write_text(json.dumps(result,indent=2),encoding='utf-8')
 print(json.dumps(result,indent=2));return int(not result['status'].startswith('PASS_'))
if __name__=='__main__':raise SystemExit(main())
