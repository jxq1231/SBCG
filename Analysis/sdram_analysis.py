"""U1 SDRAM全范围测试及静默音频共存验收；不代表LTDC或声学精度。"""
import argparse,json
from pathlib import Path
import sound_event_time_analysis as pipeline
FW='SBCG-U1-SDRAM-R1'

def evaluate(records,run,pipeline_result=None):
    """检查一次BOOT后的明确初始化/唯一完整PASS，并复用原管线严格检查。"""
    prior=pipeline_result if pipeline_result is not None else pipeline.evaluate(
        records,run,quiet=True,expected_firmware=FW)
    start=next(i for i,r in enumerate(records) if r.get('line','').startswith('$BOOT,'))
    groups={}
    for row in records[start:]:
        if 'line' not in row:continue
        key,data=pipeline.fields(row['line'])
        groups.setdefault(key,[]).append((row['elapsed_s'],data))
    init=groups.get('$SDRAM_INIT',[]);states=groups.get('$SDRAM',[])
    passes=[(t,d) for t,d in states if d.get('STATUS')=='PASS']
    expected={'RESULT':'OK','BASE':'C0000000','BYTES':'8388608',
              'SDCLK_HZ':'66666666','CAS':'3','REFRESH':'1021'}
    checks={
        'expected_init':len(init)==1 and all(init[0][1].get(k)==v for k,v in expected.items()),
        'full_memory_pass':len(passes)==1 and all(passes[0][1].get(k)==v for k,v in {
            'PHASE':'5','PASSES':'4','CHECKED_BYTES':'33554432','CMD':'5',
            'ERR_OFFSET':'00000000','EXPECTED':'00000000','ACTUAL':'00000000'}.items()),
        'no_sdram_errors':bool(states) and all(d.get('STATUS') in ('TESTING','PASS') for _,d in states),
        'ordered_completion':bool(init and passes) and init[0][0]<=passes[0][0] and
            any(t<=passes[0][0] and d.get('STATUS')=='TESTING' for t,d in states) and
            states[-1][1].get('STATUS')=='PASS',
        'original_pipeline':prior['status']=='PASS_QUIET_PIPELINE_SCOPE',
    }
    return {'status':'PASS_SDRAM_AND_QUIET_PIPELINE' if all(checks.values()) else 'FAIL_OR_INCOMPLETE',
            'checks':checks,'init':init,'memory_pass':passes,'pipeline':prior,
            'display_verified':False,'scope':'single A board / no playback / no network cable / no oscilloscope'}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',type=Path,required=True);args=parser.parse_args()
    prior=pipeline.analyze(args.prefix,1,quiet=True,expected_firmware=FW)
    records=[json.loads(s) for s in Path(str(args.prefix)+'-serial.jsonl').read_text(encoding='utf-8').splitlines()]
    run=json.loads(Path(str(args.prefix)+'-run.json').read_text(encoding='utf-8'))
    result=evaluate(records,run,prior)
    Path(str(args.prefix)+'-sdram-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if result['status']=='PASS_SDRAM_AND_QUIET_PIPELINE' else 1
if __name__=='__main__':raise SystemExit(main())
