"""Check board-side LOCAL PTP/DMA map observations against the real C core.

Input starts at the unique SBCG-M3.2-MAP BOOT. Software validation only; a
successful run is not hardware acceptance or acoustic timing calibration.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

from audio_anchor_analysis import check_anchor_rows,parse_anchor_records
from audio_position_analysis import parse_records,check_positions,number,require,UINT64_MAX
from audio_time_map_analysis import (Case,build_native,run_native,reference,row_anchor,
                                     unique_object,EXPECTED_UID)

FIRMWARE="SBCG-M3.2-MAP"
MAP_FIELDS={'SEQ','STATUS','CORE','VALID','SESSION','L','M','R','FRAME','EST_NS','LO_NS','HI_NS','SYNC'}
CHECK_FIELDS={'SEQ','OBS_NS','RES_NS','SYNC'}

def fields(line):
    result={}
    for token in line.split(',')[1:]:
        key,separator,value=token.partition('=')
        require(separator and key and value and key not in result,'malformed/duplicate serial field')
        result[key]=value
    return result

def check_window(records,executable):
    """Strict normal window. No resets or omitted groups are silently excluded."""
    try:
        require(all(isinstance(record,dict) and isinstance(record.get('line'),str) for record in records),
                'invalid serial record')
        audio,positions=parse_records(records,expected_firmware=FIRMWARE)
        position_result=check_positions(audio,positions,minimum_rows=90)
        anchors=parse_anchor_records(records,expected_firmware=FIRMWARE)
        anchor_result=check_anchor_rows(anchors,minimum_valid=90)
        require(all(a['STATUS']=='OK' for a in anchors),'invalid anchor in normal map window')
        maps,checks=[],[]
        pending=None;seen_anchor=0;boot=None
        for record in records:
            text=record.get('line','').rstrip('\r\n')
            if text.startswith('$BOOT,'):
                boot=fields(text)
                require(tuple(boot.get(f'UID_W{i}') for i in range(3))==EXPECTED_UID,'unexpected board UID')
            if text.startswith('$AUDIO_ANCHOR,'):
                require(pending is None,'map CHECK crosses the next source anchor')
                seen_anchor+=1
            if text.startswith('$AUDIO_MAP,'):
                require(boot is not None and pending is None,'map before BOOT or missing CHECK')
                row=fields(text)
                require(set(row)==MAP_FIELDS and row['STATUS']=='OK' and row['CORE']=='0' and
                        row['VALID']=='1' and row['SYNC']=='0','invalid map/status/schema in normal window')
                require(seen_anchor==(len(maps)+1)*3,'map precedes its right anchor or misses a fixed triple')
                pending=row;maps.append(row)
            if text.startswith('$AUDIO_MAP_CHECK,'):
                row=fields(text)
                require(pending is not None and set(row)==CHECK_FIELDS and
                        row['SEQ']==pending['SEQ'] and row['SYNC']=='0','missing/reordered/duplicate map check')
                checks.append(row);pending=None
        require(pending is None and len(maps)==len(checks)==len(anchors)//3 and len(maps)>=30,
                'missing/short map/check groups')
        cases=[];residuals=[]
        for index,(mapped,checked) in enumerate(zip(maps,checks)):
            left,middle,right=anchors[index*3:index*3+3]
            identifier=number(mapped,'SEQ')
            require(identifier==index+1,'map output sequence missing/reordered')
            for key,source in (('L',left),('M',middle),('R',right)):
                require(number(mapped,key)==number(source,'SEQ'),'map source triple mismatch')
            session=number(mapped,'SESSION')
            require(session==number(middle,'SESSION')==position_result['session'],'map session mismatch')
            target=number(mapped,'FRAME',UINT64_MAX)
            lo,hi=number(middle,'F_LO',UINT64_MAX),number(middle,'F_HI',UINT64_MAX)
            require(target==lo+(hi-lo+1)//2,'wrong middle frame coordinate')
            case=Case(identifier,row_anchor(left),row_anchor(right),session,target)
            expected=reference(case.left,case.right,session,target)
            actual=(0,1,number(mapped,'EST_NS',UINT64_MAX),number(mapped,'LO_NS',UINT64_MAX),number(mapped,'HI_NS',UINT64_MAX))
            require(actual==expected,'board estimate differs from exact Fraction model')
            observed=number(checked,'OBS_NS',UINT64_MAX)
            require(observed==number(middle,'SEC')*10**9+number(middle,'NSEC'),'wrong held-out observed time')
            value=checked['RES_NS']
            require(re.fullmatch(r'-?[0-9]+',value) is not None,'invalid signed residual')
            residual=int(value)
            require(-(2**63)<=residual<2**63 and residual==observed-actual[2],'wrong residual sign/value')
            require(abs(residual)<=100000,'board residual exceeds predeclared 100us sanity gate')
            residuals.append(residual);cases.append(case)
        native=run_native(executable,cases)
        for result,case in zip(native,cases):
            require(result==(case.identifier,*reference(case.left,case.right,case.target_session,case.frame_position)),
                    'board/Fraction/native C comparison mismatch')
        return dict(status='PASS_M324_BOARD_MAP_SERIAL_WINDOW',hardware_verified=False,
                    map_groups=len(maps),native_matches=len(native),positions=position_result,anchors=anchor_result,
                    max_abs_residual_ns=max(map(abs,residuals)),signed_residual_min_ns=min(residuals),
                    signed_residual_max_ns=max(residuals),
                    scope='board-reported NDTR linear model checked in software; LOCAL/SYNC=0; no acoustic calibration or new hardware acceptance')
    except (KeyError,TypeError) as error:
        raise ValueError(f'missing/invalid board map field: {error}') from error

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True,help='New directory; refuses overwrite')
    args=parser.parse_args(argv)
    try:
        raw=args.serial.read_bytes()
        records=[json.loads(text,object_pairs_hook=unique_object) for text in raw.decode('utf-8').splitlines()]
        args.output_dir.mkdir(parents=True,exist_ok=False)
        build=build_native(args.output_dir)
        result=check_window(records,Path(build['executable_path']))
        result.update(source=str(args.serial.resolve()),source_sha256=hashlib.sha256(raw).hexdigest(),native_build=build)
        with (args.output_dir/'summary.json').open('x',encoding='utf-8') as stream:json.dump(result,stream,indent=2)
        print(json.dumps(result))
        return 0
    except (OSError,ValueError,KeyError,TypeError) as error:
        print('ERROR: '+str(error),file=sys.stderr)
        return 1

if __name__=='__main__':sys.exit(main())
