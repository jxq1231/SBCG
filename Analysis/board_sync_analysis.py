"""离线核算双板MAC硬件四时刻及独立的闭环状态日志。

读取既有串口JSONL，不连接串口、不烧录、不校时。输入记录为
{"line": "...", "elapsed_s": ...}；elapsed_s只描述主机采集时间，不用于时差计算。
当前$PTP_SYNC_RAW与$SYNC_CTRL没有共享样本ID，不能凭相邻行或相近时间逐样本核对。
所有结果均为NOT_EXTERNAL_PRECISION_VERIFIED；INTERNAL_LOCK不代表外部精度。

用法：python -B Analysis/board_sync_analysis.py --serial capture.jsonl --out summary.json
"""
import argparse
from collections import Counter
from decimal import Decimal
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import re
import sys

MAX_TIMESTAMP_NS=4294967295999999999
MAX_WINDOW_NS=100000000  # 与time_sync.c的100ms估计器窗口一致。
UINT32_MAX=2**32-1
INT64_MIN=-(2**63)
INT64_MAX=2**63-1
RAW_REQUIRED={'T1_NS','T2_NS','T3_NS','T4_NS','EPOCH'}
CTRL_REQUIRED={'ROLE','STATE','INTERNAL_LOCK','SYNC','OFFSET_X2_NS','DELAY_X2_NS',
               'DRIFT_PPB','FREQ_PPB','EPOCH','RATE_REV','VALID','REJECT','ERRORS'}
STATS_REQUIRED={'OK','BAD','FOREIGN','CORRECTION','SEQ','EXPIRED','MISSING',
                'TX_ERR','SEND_ERR','BIND_ERR','EPOCH'}
ADJUST_REQUIRED={'KIND','PHASE_NS','FREQ_PPB','STATUS','EPOCH','RATE_REV','SYNC'}
B_STATES={'ACQUIRING','TRACKING','LOCKED_INTERNAL','HOLDOVER','UNSYNCED','FAULT'}


def require(condition,message):
    """拒绝不符合当前日志来源约定的输入。"""
    if not condition:
        raise ValueError(message)


def integer(fields,key,minimum=0,maximum=UINT32_MAX):
    """只读十进制文字整数，避免浮点截断、布尔数值和越界值。"""
    value=fields.get(key)
    require(isinstance(value,str) and re.fullmatch(r'-?(?:0|[1-9][0-9]*)',value) is not None,
            f'{key}: missing or nondecimal integer')
    result=int(value)
    require(minimum<=result<=maximum,f'{key}: outside [{minimum},{maximum}]')
    return result


def fields(text):
    """解析一行机器日志，拒绝字段缺值、重名和非法键。"""
    result={}
    for token in text.split(',')[1:]:
        key,separator,value=token.partition('=')
        require(separator and re.fullmatch(r'[A-Z][A-Z0-9_]*',key) is not None and value and key not in result,
                'malformed or duplicate serial field')
        result[key]=value
    return result


def rational(value):
    """将精确Fraction输出为无精度损失的JSON对象。"""
    value=Fraction(value)
    return {'numerator':str(value.numerator),'denominator':value.denominator}


def fraction_range(values):
    """输出精确纳秒范围；缺数据保持None，不补零。"""
    return {'min':rational(min(values)),'max':rational(max(values))} if values else None


def number_range(values):
    """输出有界整数范围；缺数据保持None。"""
    return {'min':min(values),'max':max(values)} if values else None


def counter_summary(values):
    """汇总累计计数的取值与下降次数，不将多次快照求和为事件数。"""
    return ({'first':values[0],'last':values[-1],'min':min(values),'max':max(values),
             'decreases':sum(b<a for a,b in zip(values,values[1:]))} if values else None)


def pair_from_fields(parsed):
    """按自然A/B方向精确重算四时刻，保持半纳秒分子。"""
    require(RAW_REQUIRED<=set(parsed)<=RAW_REQUIRED|{'SYNC','RATE_REV'},'raw pair schema mismatch')
    t1,t2,t3,t4=(integer(parsed,key,0,MAX_TIMESTAMP_NS) for key in ('T1_NS','T2_NS','T3_NS','T4_NS'))
    epoch=integer(parsed,'EPOCH')
    require(t4>=t1 and t3>=t2,'timestamp order reversed within a clock domain')
    require(t4-t1<=MAX_WINDOW_NS and t3-t2<=MAX_WINDOW_NS,'exchange exceeds coordinator 100ms window')
    forward=t2-t1
    reverse=t4-t3
    delay_x2=forward+reverse
    require(delay_x2>=0,'negative mean path delay')
    offset_x2=forward-reverse
    result={'t1_a_tx_ns':str(t1),'t2_b_rx_ns':str(t2),'t3_b_tx_ns':str(t3),'t4_a_rx_ns':str(t4),
            'epoch':str(epoch),'offset_x2_ns':str(offset_x2),'mean_path_delay_x2_ns':str(delay_x2),
            'offset_ns':rational(Fraction(offset_x2,2)),
            'mean_path_delay_ns':rational(Fraction(delay_x2,2)),
            'orientation':'B_MINUS_A','timestamp_source':'BOARD_REPORTED_MAC_HW'}
    if 'RATE_REV' in parsed:
        result['rate_revision']=str(integer(parsed,'RATE_REV'))
    return result


def control_from_fields(parsed):
    """验证协调器状态快照；内部锁、SYNC和物理精度分别保留语义。"""
    require(set(parsed)==CTRL_REQUIRED,'control schema mismatch')
    role,state=parsed['ROLE'],parsed['STATE']
    require(role in {'A','B'},'unknown role')
    require(state in (B_STATES if role=='B' else {'REFERENCE','FAULT'}),'state inconsistent with role')
    locked=integer(parsed,'INTERNAL_LOCK',0,1)
    require(locked==int(role=='B' and state=='LOCKED_INTERNAL'),'internal lock inconsistent with role/state')
    result={'role':role,'state':state,'internal_lock':bool(locked),'sync':'0',
            'offset_x2_ns':str(integer(parsed,'OFFSET_X2_NS',INT64_MIN,INT64_MAX)),
            'delay_x2_ns':str(integer(parsed,'DELAY_X2_NS',0,4000000)),
            'drift_ppb':integer(parsed,'DRIFT_PPB',-(2**31),2**31-1),
            'frequency_ppb':integer(parsed,'FREQ_PPB',-100000,100000),
            'epoch':str(integer(parsed,'EPOCH')),'rate_revision':str(integer(parsed,'RATE_REV'))}
    result['counters']={key:integer(parsed,key) for key in ('VALID','REJECT','ERRORS')}
    return result


def adjustment_from_fields(parsed):
    """保留实际命令及BSP返回状态，不把非零状态当作已成功校时。"""
    require(set(parsed)==ADJUST_REQUIRED,'adjustment schema mismatch')
    require(parsed['KIND'] in {'STEP','FREQUENCY'},'unknown adjustment kind')
    return {'kind':parsed['KIND'],'phase_ns':str(integer(parsed,'PHASE_NS',INT64_MIN,INT64_MAX)),
            'frequency_ppb':integer(parsed,'FREQ_PPB',-100000,100000),
            'status':integer(parsed,'STATUS',0,10),'epoch':str(integer(parsed,'EPOCH')),
            'rate_revision':str(integer(parsed,'RATE_REV')),'sync':'0'}


def analyze_records(records):
    """逐记录审查现有串口数据；坏记录可见且使整体进入REVIEW。"""
    pairs,controls,adjustments,transport,rejected=[],[],[],[],[]
    ignored=0
    count=0
    elapsed_values=[]
    boot_index=0
    for index,record in enumerate(records,1):
        count=index
        try:
            require(isinstance(record,dict),'capture record is not an object')
            require('_capture_error' not in record,record.get('_capture_error','invalid capture'))
            text=record.get('line')
            require(isinstance(text,str),'capture line is missing or not text')
            text=text.rstrip('\r\n')
            require(text.isascii() and all(32<=ord(char)<=126 for char in text),'garbled or nonprintable serial line')
            elapsed=record.get('elapsed_s')
            if elapsed is not None:
                require(not isinstance(elapsed,bool) and isinstance(elapsed,(int,float,Decimal)),'invalid elapsed_s type')
                require(math.isfinite(elapsed) and elapsed>=0,'elapsed_s is nonfinite or negative')
            tag=text.split(',',1)[0]
            # 所有机器日志的SYNC=1均拒绝，不能被未知tag或内部锁掩盖。
            parsed=fields(text) if text.startswith('$') else {}
            if 'SYNC' in parsed:
                require(parsed['SYNC']=='0','false external synchronization claim: SYNC must remain 0')
            metadata={'record':index,'boot_index':boot_index}
            if elapsed is not None:
                metadata['elapsed_s']=str(elapsed)
                elapsed_values.append(Decimal(str(elapsed)))
            if tag=='$BOOT':
                boot_index+=1
                ignored+=1
            elif tag in {'$PTP_SYNC_RAW','$PTP_PAIR'}:
                pairs.append({**metadata,'tag':tag,**pair_from_fields(parsed)})
            elif tag=='$SYNC_CTRL':
                controls.append({**metadata,**control_from_fields(parsed)})
            elif tag=='$SYNC_ADJUST':
                adjustments.append({**metadata,**adjustment_from_fields(parsed)})
            elif tag=='$PTP_SYNC_STATS':
                require(set(parsed)==STATS_REQUIRED,'transport statistics schema mismatch')
                transport.append({**metadata,**{key:integer(parsed,key) for key in STATS_REQUIRED}})
            else:
                ignored+=1
        except (ValueError,TypeError,OverflowError) as error:
            rejected.append({'record':index,'reason':str(error)})
    faults=sum(c['state']=='FAULT' for c in controls)
    failed_adjustments=sum(a['status']!=0 for a in adjustments)
    any_errors=any(c['counters']['ERRORS']>0 for c in controls)
    status='REVIEW' if rejected or faults or failed_adjustments or any_errors else 'OBSERVATION_ONLY' if pairs else 'NO_PAIR'
    ctrl_counters={key:counter_summary([c['counters'][key] for c in controls]) for key in ('VALID','REJECT','ERRORS')}
    transport_counters={key:counter_summary([s[key] for s in transport]) for key in sorted(STATS_REQUIRED-{'EPOCH'})}
    return {
        'schema':'SBCG_BOARD_SYNC_ANALYSIS_V1','status':status,
        'precision_status':'NOT_EXTERNAL_PRECISION_VERIFIED','external_precision_verified':False,
        'scope':'offline arithmetic and board-reported loop status only; no independent clock, path-asymmetry or PPS precision verification',
        'records':count,'ignored_records':ignored,'pair_count':len(pairs),'rejected':rejected,
        'pairs':pairs,
        'ranges':{
            'offset_ns':fraction_range([Fraction(int(p['offset_x2_ns']),2) for p in pairs]),
            'mean_path_delay_ns':fraction_range([Fraction(int(p['mean_path_delay_x2_ns']),2) for p in pairs]),
            'elapsed_s':{'min':str(min(elapsed_values)),'max':str(max(elapsed_values))} if elapsed_values else None},
        'controls':{'count':len(controls),'roles':dict(Counter(c['role'] for c in controls)),
            'states':dict(Counter(c['state'] for c in controls)),
            'internal_lock_count':sum(c['internal_lock'] for c in controls),
            'fault_count':faults,'holdover_count':sum(c['state']=='HOLDOVER' for c in controls),
            'drift_ppb':number_range([c['drift_ppb'] for c in controls]),
            'frequency_ppb':number_range([c['frequency_ppb'] for c in controls]),
            'epochs':sorted({c['epoch'] for c in controls},key=int),
            'rate_revisions':sorted({c['rate_revision'] for c in controls},key=int),
            'sync_values':sorted({c['sync'] for c in controls}),
            'counters':ctrl_counters,'snapshots':controls},
        'adjustments':{'count':len(adjustments),'failed':failed_adjustments,
            'kinds':dict(Counter(a['kind'] for a in adjustments)),'records':adjustments},
        'transport':{'count':len(transport),'counters':transport_counters,'snapshots':transport},
        'alignment':{'status':'UNAVAILABLE_NO_SHARED_SAMPLE_ID','compared':0,
            'reason':'PTP_SYNC_RAW and periodic SYNC_CTRL have no common sample ID; control may describe an older sample and post-adjustment epoch/rate',
            'drift_recomputation':'NOT_PERFORMED: raw samples do not identify rate revisions; reported DRIFT_PPB is summarized separately'},
        'limitations':[
            'INTERNAL_LOCK is an internal state, never an external accuracy result.',
            'Half-nanosecond fractions express exact integer arithmetic, not half-nanosecond measurement precision.',
            'Cumulative counters are summarized as snapshots and decreases, never summed as independent events.',
            'This file alone does not authenticate hardware timestamps or prove packet pairing/clock continuity.',
            'PTP_PAIR is accepted only as a name alias with the same raw fields; it adds no sample-alignment evidence.']}


def unique_object(items):
    """拒绝JSON同名键，避免后值覆盖前值隐藏原始证据。"""
    result={}
    for key,value in items:
        require(key not in result,'duplicate JSON key')
        result[key]=value
    return result


def reject_constant(value):
    """拒绝JSON扩展NaN/Infinity。"""
    raise ValueError(f'invalid JSON constant {value}')


def analyze_capture(path):
    """读取且哈希绑定原始JSONL；不改写源文件，保留坏行位置。"""
    path=Path(path)
    payload=path.read_bytes()
    records=[]
    for raw_line in payload.splitlines():
        try:
            text=raw_line.decode('utf-8',errors='strict')
            records.append(json.loads(text,object_pairs_hook=unique_object,
                                      parse_float=Decimal,parse_constant=reject_constant))
        except (ValueError,UnicodeError) as error:
            records.append({'_capture_error':f'invalid JSONL record: {error}'})
    result=analyze_records(records)
    result['source']={'path':str(path.resolve()),'sha256':hashlib.sha256(payload).hexdigest(),'bytes':len(payload)}
    return result


def main(argv=None):
    """命令行离线分析；输出新JSON文件，拒绝覆盖已有结果。"""
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial',required=True,type=Path,help='existing serial JSONL capture')
    parser.add_argument('--out',required=True,type=Path,help='new JSON summary file; never overwritten')
    args=parser.parse_args(argv)
    try:
        require(args.serial.resolve()!=args.out.resolve(),'input and output must differ')
        result=analyze_capture(args.serial)
        args.out.parent.mkdir(parents=True,exist_ok=True)
        with args.out.open('x',encoding='utf-8',newline='\n') as stream:
            json.dump(result,stream,ensure_ascii=False,indent=2,allow_nan=False)
            stream.write('\n')
        print(json.dumps({'status':result['status'],'pair_count':result['pair_count'],
            'rejected':len(result['rejected']),'precision_status':result['precision_status'],
            'out':str(args.out.resolve())},ensure_ascii=False))
        return 0 if result['status']=='OBSERVATION_ONLY' else 2
    except (OSError,ValueError) as error:
        print(json.dumps({'status':'REVIEW','precision_status':'NOT_EXTERNAL_PRECISION_VERIFIED',
                          'external_precision_verified':False,'error':str(error)},ensure_ascii=False),file=sys.stderr)
        return 2


if __name__=='__main__':
    raise SystemExit(main())
