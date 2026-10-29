"""Verify LCD runtime diagnostics with original quiet pipeline; requires separate visual acceptance."""
from pathlib import Path
import argparse
import json
import math
import sound_event_time_analysis as pipeline

FIRMWARE='SBCG-U2-STATUS-R2'


def queue_timeline(records):
    """Compare each periodic counter to its own UART prefix, then require all events resolved.

    A later candidate/result must not be compared to an earlier periodic counter.
    This does not discard any event or excuse a dropped/error record.
    """
    events=set(); results=set(); ok=reject=states=0
    try:
        for record in records:
            key,data=pipeline.fields(record.get('line',''))
            if key=='$SOUND_CAND':
                seq=int(data['SEQ'])
                if seq in events:
                    return False
                events.add(seq)
            elif key=='$SOUND_TIME':
                seq=int(data['SEQ'])
                if seq not in events or seq in results:
                    return False
                results.add(seq)
                if data['STATUS']=='OK': ok+=1
                else: reject+=1
            elif key=='$SOUND_TIME_STATE':
                states+=1
                expected={'QUEUED':len(events),'OK':ok,'REJECT':reject,
                          'PENDING':len(events)-len(results),'DROPPED':0}
                if any(int(data[name])!=value for name,value in expected.items()):
                    return False
        return states>0 and events==results
    except (KeyError,ValueError):
        return False


def evaluate(records, prior):
    groups={}
    for record in records:
        if 'line' in record:
            key,values=pipeline.fields(record['line']);groups.setdefault(key,[]).append(values)
    ui=groups.get('$UI',[])
    running=[item for item in ui if item.get('STATE')=='RUNNING']
    memory=groups.get('$SDRAM',[])
    # Keep the original analyzer result intact; only its last-snapshot equality
    # may be replaced by a stricter comparison of every snapshot at its own time.
    old_checks=prior.get('checks',{})
    queue_only_failure=bool(old_checks) and not old_checks.get('queue_drained_no_losses',True) and all(
        value for key,value in old_checks.items() if key!='queue_drained_no_losses')
    temporal_queue_ok=queue_timeline(records) if queue_only_failure else False
    try:
        frames=[int(item['FRAMES']) for item in running]
        shift=prior.get('selected_boot_shift_s',0)
        duration=prior.get('duration_after_selected_boot_s',0)
        times=[record.get('elapsed_s',float('nan'))-shift for record in records
               if record.get('line','').startswith('$UI,') and pipeline.fields(record['line'])[1].get('STATE')=='RUNNING']
        progressing=bool(frames)
        last_change=times[0] if times else 0
        for i in range(1,len(frames)):
            if times[i]-last_change>2.5:
                progressing=False
            if frames[i]>frames[i-1]:
                last_change=times[i]
            elif frames[i]<frames[i-1] or times[i]-last_change>2.5:
                progressing=False
        checks={
            'original_quiet_pipeline':prior.get('status')=='PASS_QUIET_PIPELINE_SCOPE' or (queue_only_failure and temporal_queue_ok),
            'display_memory_ready_without_destructive_test':bool(memory) and all(item.get('STATUS')=='READY' and item.get('CHECKED_BYTES')=='0' for item in memory),
            'sdram_chip_init':bool(groups.get('$SDRAM_INIT')) and all(item.get('RESULT')=='OK' for item in groups['$SDRAM_INIT']),
            'bounded_live_refresh':len(running)>=55 and progressing,
            'complete_ui_time_coverage':bool(times) and duration>=60 and all(math.isfinite(t) and 0<=t<=duration+1 for t in times)
                and times[0]<=2.5 and times[-1]>=duration-3 and all(0<b-a<=2.5 for a,b in zip(times,times[1:])),
            'no_display_errors_or_discards':bool(ui) and all(item.get('STATE') in ('READY','RUNNING') and item.get('ERRORS')=='0' and item.get('DISCARDS')=='0' for item in ui),
            'expected_single_A_no_cable':bool(running) and all(item.get('ROLE')=='A' and item.get('NET')=='DOWN' for item in running),
        }
    except (ValueError,KeyError):
        checks={'well_formed_diagnostics':False}
    return {'status':'PASS_UI_RUNTIME_PENDING_VISUAL' if all(checks.values()) else 'FAIL_OR_INCOMPLETE',
            'checks':checks,'ui_rows':len(ui),'last_ui':ui[-1] if ui else None,
            'physical_panel_verified':False,'external_sync_precision_verified':False,
            'periodic_counter_timeline_replaces_last_snapshot_equality':queue_only_failure and temporal_queue_ok,
            'original_pipeline':prior}


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--prefix',type=Path,required=True)
    args=parser.parse_args()
    prior=pipeline.analyze(args.prefix,1,quiet=True,expected_firmware=FIRMWARE)
    records=[json.loads(line) for line in Path(str(args.prefix)+'-serial.jsonl').read_text(encoding='utf-8').splitlines()]
    # Use exactly the BOOT-selected window already checked against the binary source.
    start=next(i for i,record in enumerate(records) if record.get('line','').startswith('$BOOT,FW='+FIRMWARE+','))
    result=evaluate(records[start:],prior)
    Path(str(args.prefix)+'-ui-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return result['status']!='PASS_UI_RUNTIME_PENDING_VISUAL'


if __name__=='__main__':
    raise SystemExit(main())
