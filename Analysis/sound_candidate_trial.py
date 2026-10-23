"""Analyze bounded PC-stimulus trials; no oscilloscope/standard-audio precision claims."""
import argparse
import json
from pathlib import Path


def fields(line):
    parts=line.split(',')
    return parts[0],dict(p.split('=',1) for p in parts[1:] if '=' in p)


def analyze(prefix):
    run=json.loads(Path(str(prefix)+'-run.json').read_text(encoding='utf-8'))
    records=[json.loads(line) for line in Path(str(prefix)+'-serial.jsonl').read_text(encoding='utf-8').splitlines()]
    records=[r for r in records if r.get('elapsed_s') is not None and 'line' in r]
    groups={}
    for record in records:
        kind,data=fields(record['line'])
        groups.setdefault(kind,[]).append((record['elapsed_s'],data))
    def rows(kind):return groups.get(kind,[])
    def zero(kind,keys):return bool(rows(kind)) and all(all(int(d[k],16 if k in ('HAL','DMA') else 10)==0 for k in keys) for _,d in rows(kind))
    def increasing(kind,key):
        values=[int(d[key]) for _,d in rows(kind)]
        return bool(values) and all(b>a for a,b in zip(values,values[1:]))
    candidates=rows('$SOUND_CAND')
    tones=run['tones']
    first=tones[0]['host_start_s'] if tones else 60.0
    background=[(t,d) for t,d in candidates if t<first]
    responses=[]
    for tone in tones:
        begin=tone['host_start_s']
        found=[(t,d) for t,d in candidates if begin<=t<begin+2.5]
        responses.append({'number':tone['number'],'host_start_s':begin,'candidate_count':len(found),
                          'arrival_host_s':[t for t,_ in found],'frames':[{k:d[k] for k in ['SESSION','FIRST','CONFIRMED','E_AC','PEAK','CLIP']} for _,d in found]})
    after=tones[-1]['host_start_s']+2.5 if tones else first
    def covers_trial(kind):
        stamps=[t for t,_ in rows(kind)]
        return (len(stamps)>=80 and stamps[0]<=2.5 and stamps[-1]>=run.get('duration_s',0)-3
                and all(0<b-a<=2.5 for a,b in zip(stamps,stamps[1:])))
    sessions={d.get('SESSION') for _,d in rows('$AUDIO_POS')}
    unique_candidates=(bool(candidates) and len(sessions)==1 and None not in sessions and '0' not in sessions
        and all(d.get('SESSION') in sessions and int(d['CONFIRMED'])>=int(d['FIRST'])
                and d.get('LOCAL')=='1' and d.get('SYNC')=='0' and d.get('CH')=='0' for _,d in candidates)
        and all(int(b.get('SEQ','0'))==int(a.get('SEQ','0'))+1
                and int(b['FIRST'])>int(a['CONFIRMED']) and int(b['CONFIRMED'])>int(a['CONFIRMED'])
                for (_,a),(_,b) in zip(candidates,candidates[1:])))
    checks={
        'capture_complete':run.get('status')=='CAPTURED_PENDING_ANALYSIS' and run.get('duration_s',0)>=80 and run.get('port_closed') is True,
        'one_expected_boot':len(rows('$BOOT'))==1 and rows('$BOOT')[0][1].get('FW')==fields(run.get('boot_line',''))[1].get('FW') and rows('$BOOT')[0][1].get('FW','').startswith('SBCG-M4.1-CAND') and rows('$BOOT')[0][1].get('UID_W0')=='001A002D' and rows('$BOOT')[0][1].get('UID_W1')=='30365105' and rows('$BOOT')[0][1].get('UID_W2')=='36363631',
        'five_pc_stimuli':len(tones)==5 and first>=60,
        'background_candidates_zero':len(background)==0,
        'one_candidate_per_stimulus':len(responses)==5 and all(r['candidate_count']==1 for r in responses) and len(candidates)==5,
        'recovery_candidates_zero':not any(t>=after for t,_ in candidates),
        'periodic_logs_cover_trial':all(covers_trial(k) for k in ('$HEARTBEAT','$AUDIO','$AUDIO_POS','$SOUND_STATE')),
        'heartbeat_contiguous':len(rows('$HEARTBEAT'))>=80 and all(int(b['SEQ'])==int(a['SEQ'])+1 and int(b['TICK_MS'])>int(a['TICK_MS']) for (_,a),(_,b) in zip(rows('$HEARTBEAT'),rows('$HEARTBEAT')[1:])),
        'console_errors_zero':zero('$HEARTBEAT',['TX_ERRORS']),
        'audio_run_and_valid':len(rows('$AUDIO'))>=80 and all(d.get('STATE')=='RUN' and d.get('VALID')=='1' for _,d in rows('$AUDIO')),
        'dma_no_errors_or_drops':zero('$AUDIO',['DROP','ERR','HAL','DMA']) and increasing('$AUDIO','FRAMES'),
        'audio_positions_valid':bool(rows('$AUDIO_POS')) and all(d.get('INDEX_VALID')=='1' and d.get('VALID')=='1' and d.get('CONTIG')=='1' and int(d['GAP'])==0 and int(d['FAULTS'],16)==0 for _,d in rows('$AUDIO_POS')),
        'detector_no_rejection_drop_or_error':zero('$SOUND_STATE',['REJECTED','RESETS','DROPPED','ERRORS']) and increasing('$SOUND_STATE','VALID_BLOCKS'),
        'detector_rearmed_and_queue_empty':covers_trial('$SOUND_STATE') and int(rows('$SOUND_STATE')[-1][1].get('CANDIDATES','-1'))==len(candidates) and rows('$SOUND_STATE')[-1][1].get('ARMED')=='1' and rows('$SOUND_STATE')[-1][1].get('PENDING')=='0',
        'candidate_frame_order_and_local_scope':unique_candidates,
    }
    result={'status':'PASS_SINGLE_BOARD_CANDIDATE_SCOPE' if all(checks.values()) else 'FAIL_OR_INCOMPLETE',
        'firmware':run.get('boot_line'),'duration_s':run.get('duration_s'),'checks':checks,
        'background_observed_s':first,'background_candidates':len(background),'responses':responses,
        'counts':{kind:len(values) for kind,values in groups.items()},
        'audio_stalled_lines':sum(d.get('STATE')=='STALLED' for _,d in rows('$AUDIO')),
        'last_detector_state':rows('$SOUND_STATE')[-1][1] if rows('$SOUND_STATE') else None,
        'last_pps_state':rows('$PPS')[-1][1] if rows('$PPS') else None,
        'temperature_states':sorted(set(d.get('STATE','') for _,d in rows('$TEMP'))),
        'network_link_states':sorted(set(d.get('LINK','') for _,d in rows('$ETH') if 'LINK' in d)),
        'stimulus':run.get('stimulus'),'hardware_precision_verified':False,
        'excluded':['oscilloscope PPS shape/width','two-board synchronization and network','course standard-audio recognition','acoustic arrival-time accuracy and ranging','temperature sensor recovery'],
        'timing_note':'host arrival windows only associate stimuli; they are not hardware arrival timestamps'}
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prefix',type=Path,required=True);args=parser.parse_args()
    result=analyze(args.prefix);Path(str(args.prefix)+'-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2));return 0 if result['status'].startswith('PASS') else 1
if __name__=='__main__':raise SystemExit(main())
