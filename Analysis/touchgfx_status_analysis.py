"""Verify TouchGFX quiet-runtime evidence; physical display confirmation remains separate."""
import argparse
import hashlib
import json
from pathlib import Path
import ui_status_analysis as legacy

FIRMWARE='SBCG-U3.1-TOUCHGFX'


def select_reset_window(raw,records):
    """Use the established strict raw proof, including an interrupted old UART line."""
    lines,proof=legacy.pipeline.validate_boot_capture(raw,records,FIRMWARE)
    start=proof['discarded_pre_boot_lines']
    selected=[dict(row,line=line) for row,line in zip(records[start:],lines)]
    return selected,proof


def complete_capture(original,records):
    """Retain a normal incomplete line, but never discard evidence of another BOOT."""
    if original.endswith(b'\n'):return original,records,b''
    raw,tail=original.rsplit(b'\n',1)
    if records[-1]!={'incomplete_tail_hex':tail.hex()}:raise ValueError('tail mismatch')
    marker=b'$BOOT'
    if marker in tail or any(tail.endswith(marker[:n]) for n in range(1,len(marker)+1)):
        raise ValueError('incomplete tail may contain an additional BOOT')
    return raw+b'\n',records[:-1],tail


def evaluate(records,prior):
    """Retain all quiet-pipeline gates and independently check 1Hz data progress."""
    result=legacy.evaluate(records,prior)
    rows=[(row.get('elapsed_s',0),legacy.pipeline.fields(row['line'])[1])
          for row in records if row.get('line','').startswith('$UI,')]
    checks=result['checks']
    checks['touchgfx_backend']=bool(rows) and all(d.get('BACKEND')=='TOUCHGFX' for _,d in rows)
    try:
        updates=[int(d['UPDATES']) for _,d in rows]
        last_change=rows[0][0] if rows else 0
        live=bool(updates) and updates[0]>0
        for i in range(1,len(rows)):
            if rows[i][0]-last_change>2.5 or updates[i]<updates[i-1]:live=False
            if updates[i]>updates[i-1]:last_change=rows[i][0]
        checks['live_data_generation']=live and len(set(updates))>=55
    except (KeyError,ValueError):
        checks['live_data_generation']=False
    result['status']='PASS_TOUCHGFX_RUNTIME_PENDING_VISUAL' if all(checks.values()) else 'FAIL_OR_INCOMPLETE'
    result['firmware']=FIRMWARE
    result['scope']='single A, no cable/sound; frame count and data generation are distinct'
    return result


def analyze(prefix):
    """Analyze one immutable capture and preserve its exact reset-boundary proof."""
    run=json.loads(Path(str(prefix)+'-run.json').read_text(encoding='utf-8'))
    records=[json.loads(line) for line in Path(str(prefix)+'-serial.jsonl').read_text(encoding='utf-8').splitlines()]
    original=Path(str(prefix)+'-serial.bin').read_bytes()
    raw,records,tail=complete_capture(original,records)
    records,proof=select_reset_window(raw,records)
    prior=legacy.pipeline.evaluate(records,run,1,quiet=True,expected_firmware=FIRMWARE)
    prior['integrity']={**proof,'original_raw_sha256':hashlib.sha256(original).hexdigest(),
                        'excluded_tail_bytes':len(tail),'excluded_tail_hex':tail.hex()}
    return evaluate(records,prior)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix',type=Path,required=True)
    args=parser.parse_args()
    try:result=analyze(args.prefix)
    except (OSError,ValueError,KeyError,IndexError) as error:
        result={'status':'FAIL_OR_INCOMPLETE','error':str(error),
                'physical_panel_verified':False,'external_sync_precision_verified':False}
    Path(str(args.prefix)+'-touchgfx-analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))
    return result['status']!='PASS_TOUCHGFX_RUNTIME_PENDING_VISUAL'


if __name__=='__main__':raise SystemExit(main())
