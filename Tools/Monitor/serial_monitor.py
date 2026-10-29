"""Bounded 115200/8N1 raw serial recorder; capture alone is not acceptance."""
import argparse
import json
import math
from pathlib import Path
import time


def capture_serial(port, duration, prefix, serial_factory=None, clock=time.monotonic):
    """Record raw bytes and their line projection without resetting the target.

    Existing outputs are never overwritten. DTR/RTS stay deasserted. Each complete
    line has elapsed_s; a partial final line is retained as hex, never fabricated.
    Read/open failures close the port and write FAILED metadata for inspection.
    """
    if not math.isfinite(duration) or not 0 < duration <= 3600:
        raise ValueError('duration must be finite, >0 and <=3600 seconds')
    prefix=Path(prefix)
    paths=[Path(str(prefix)+suffix) for suffix in ('-serial.bin','-serial.jsonl','-run.json')]
    for path in paths:
        if path.exists():raise FileExistsError(str(path))
    if serial_factory is None:
        import serial
        serial_factory=serial.Serial
    prefix.parent.mkdir(parents=True,exist_ok=True)
    stream=serial_factory(port=None,baudrate=115200,bytesize=8,parity='N',stopbits=1,timeout=0.2)
    stream.dtr=stream.rts=False
    stream.port=port
    start=clock();pending=b'';count=0;received=0
    result={'status':'FAILED','port':port,'duration_s':duration,'hardware_verified':False}
    try:
        with paths[0].open('xb') as raw,paths[1].open('x',encoding='utf-8') as rows:
            try:
                stream.open()
                while clock()-start < duration:
                    data=stream.read(4096)
                    if not data:continue
                    raw.write(data);raw.flush();received+=len(data);pending+=data
                    while b'\n' in pending:
                        line,pending=pending.split(b'\n',1)
                        rows.write(json.dumps({'elapsed_s':round(clock()-start,6),
                            'line':line.rstrip(b'\r').decode('ascii',errors='replace')})+'\n')
                        rows.flush();count+=1
                result['status']='CAPTURED_PENDING_ANALYSIS'
            finally:
                if pending:
                    rows.write(json.dumps({'incomplete_tail_hex':pending.hex()})+'\n');rows.flush()
    except KeyboardInterrupt:
        result['status']='INTERRUPTED_PENDING_ANALYSIS'
    except Exception as error:
        result['error']=str(error)
    finally:
        stream.close()
        result.update(elapsed_s=clock()-start,lines=count,bytes=received,port_closed=not stream.is_open)
        with paths[2].open('x',encoding='utf-8') as metadata:
            json.dump(result,metadata,indent=2)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',required=True)
    parser.add_argument('--duration',type=float,default=620)
    parser.add_argument('--log-prefix',type=Path,required=True)
    args=parser.parse_args()
    result=capture_serial(args.port,args.duration,args.log_prefix)
    print(json.dumps(result,ensure_ascii=False))
    return 1 if result['status']=='FAILED' else 0


if __name__=='__main__':raise SystemExit(main())
