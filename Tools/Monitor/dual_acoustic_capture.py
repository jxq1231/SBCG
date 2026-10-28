"""Receive two boards' candidate PCM and serial journals without mixing identities.

No reset, flash, playback or serial writes. Source IPs identify the configured
boards; UID provenance comes from the linked flash record, not the PCM1 packet.
boot0 means attached to a running board without observing BOOT in this window.
Candidate slices cannot prove continuous PCM coverage or absence of missed sounds.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import socket
import time

if __package__:
    from .udp_chirp_capture import decode_packet, HEADER
else:
    from udp_chirp_capture import decode_packet, HEADER

BOARDS={'192.168.10.1':'A','192.168.10.2':'B'}


class CaptureStore:
    """One boundary check per datagram; retain conflicts and incomplete keys."""
    def __init__(self,out):
        self.out=Path(out)
        self.boot={'A':0,'B':0}
        self.parts={}
        self.duplicates=0
        self.completed=Counter(A=0,B=0)

    def note_boot(self,role):
        self.boot[role]+=1

    def accept(self,data,address,elapsed_s):
        role=BOARDS.get(address[0])
        if role is None or address[1]!=5008:raise ValueError('unexpected source endpoint')
        meta=decode_packet(data)
        header=HEADER.unpack(data[:HEADER.size])
        payload=data[HEADER.size:]
        seq,total=meta['seq'],meta['total']
        frames=len(payload)//2
        first=meta['first_frame']-512*seq
        if (header[4]!=0 or header[8]!=0 or meta['flags'] & ~7 or first<0 or
            meta['session']==0 or meta['id']==0 or frames==0 or
            (seq<total-1 and frames!=512) or
            (seq==total-1 and (total-1)*512+frames>8544)):
            raise ValueError('invalid PCM1 metadata or chunk length')
        key=(role,self.boot[role],meta['session'],meta['id'])
        signature=(total,meta['flags'],first,meta['hint_frame'],meta['raw_first_frame'])
        entry=self.parts.setdefault(key,{'signature':signature,'chunks':{},'conflict':False,
                                        'complete':False,'first_rx_s':elapsed_s})
        if entry['conflict']:raise ValueError('capture previously conflicted')
        old=entry['chunks'].get(seq)
        if signature!=entry['signature'] or (old is not None and old!=payload):
            entry['conflict']=True
            raise ValueError('conflicting capture metadata or duplicate payload')
        if old is not None:
            self.duplicates+=1
            return None
        entry['chunks'][seq]=payload
        if len(entry['chunks'])!=total:return None
        pcm=b''.join(entry['chunks'][i] for i in range(total))
        result={'board':role,'boot_context':key[1],'session':key[2],'id':key[3],
                'source':address,'first_frame':first,'hint_frame':meta['hint_frame'],
                'raw_first_frame':meta['raw_first_frame'],'flags':meta['flags'],
                'frames':len(pcm)//2,'sha256':hashlib.sha256(pcm).hexdigest(),
                'first_rx_s':entry['first_rx_s'],'complete_rx_s':elapsed_s,
                'sample_format':'mono PCM16 little-endian; nominal 48000Hz'}
        folder=self.out/role/f'boot{key[1]}'/f'session{key[2]}'
        folder.mkdir(parents=True,exist_ok=True)
        with (folder/f'id{key[3]}.pcm').open('xb') as handle:handle.write(pcm)
        with (folder/f'id{key[3]}.json').open('x',encoding='utf-8') as handle:json.dump(result,handle,indent=2)
        entry['complete']=True
        self.completed[role]+=1
        return result

    def summary(self):
        return {'completed':dict(self.completed),'duplicates':self.duplicates,
                'incomplete':[{'key':key,'conflict':e['conflict'],
                    'missing_chunks':sorted(set(range(e['signature'][0]))-e['chunks'].keys())}
                    for key,e in self.parts.items() if not e['complete'] or e['conflict']]}


def run(args):
    """Bounded passive capture; raw UDP is journaled before decoding."""
    import serial
    args.out.mkdir(parents=True,exist_ok=False)
    store=CaptureStore(args.out)
    ports={};files=[];pending={'A':b'','B':b''};errors=[];packets=0
    started=time.monotonic()
    manifest={'started_utc':datetime.now(timezone.utc).isoformat(),'mode':'candidate-pcm-diagnostic',
              'bind':args.bind,'udp_port':args.udp_port,'source_map':BOARDS,
              'flash_record':str(args.flash_record) if args.flash_record else None,
              'boot0':'existing runtime; BOOT not observed at attachment',
              'duration_limit_s':args.duration,'scope':__doc__,'status':'FAILED'}
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    try:
        sock.bind((args.bind,args.udp_port));sock.settimeout(0.02)
        journals={}
        for role,port in [('A',args.a_port),('B',args.b_port)]:
            stream=serial.Serial(port=None,baudrate=115200,timeout=0)
            stream.dtr=stream.rts=False;stream.port=port;stream.open();ports[role]=stream
            raw=(args.out/f'{role}-serial.bin').open('xb')
            lines=(args.out/f'{role}-serial.jsonl').open('x',encoding='utf-8')
            files.extend((raw,lines));journals[role]=(raw,lines)
        udp=(args.out/'udp-journal.jsonl').open('x',encoding='utf-8');files.append(udp)
        started=time.monotonic();manifest['ready_utc']=datetime.now(timezone.utc).isoformat()
        print('READY: both serial ports and UDP socket open',flush=True)
        while time.monotonic()-started<args.duration and not (args.stop_file and args.stop_file.exists()):
            for role,stream in ports.items():
                data=stream.read(min(stream.in_waiting,65536))
                if not data:continue
                raw,lines=journals[role];raw.write(data);raw.flush();pending[role]+=data
                while b'\n' in pending[role]:
                    line,pending[role]=pending[role].split(b'\n',1)
                    text=line.rstrip(b'\r').decode('ascii',errors='replace')
                    if '$BOOT,' in text:store.note_boot(role)
                    lines.write(json.dumps({'elapsed_s':round(time.monotonic()-started,6),'line':text})+'\n');lines.flush()
            try:data,address=sock.recvfrom(65535)
            except socket.timeout:continue
            elapsed=time.monotonic()-started;packets+=1
            udp.write(json.dumps({'elapsed_s':elapsed,'source':address,'raw_hex':data.hex()})+'\n');udp.flush()
            try:
                result=store.accept(data,address,elapsed)
                if result:print('PCM_COMPLETE',json.dumps(result),flush=True)
            except ValueError as error:
                errors.append({'elapsed_s':elapsed,'source':address,'error':str(error)})
        manifest['status']='CAPTURED_PENDING_ANALYSIS'
    except KeyboardInterrupt:
        manifest['status']='INTERRUPTED_PENDING_ANALYSIS'
    except (OSError,serial.SerialException) as error:
        manifest['error']=str(error)
    finally:
        for role,tail in pending.items():
            if tail and role in journals:
                journals[role][1].write(json.dumps({'incomplete_tail_hex':tail.hex()})+'\n')
        for stream in ports.values():stream.close()
        sock.close()
        for handle in files:handle.close()
        manifest.update(elapsed_s=time.monotonic()-started,packets=packets,errors=errors,
                        ports_closed={role:not stream.is_open for role,stream in ports.items()},**store.summary())
        (args.out/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    print(json.dumps(manifest),flush=True)
    return 1 if manifest['status']=='FAILED' else 0


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--a-port',default='COM3');parser.add_argument('--b-port',default='COM5')
    parser.add_argument('--bind',default='192.168.10.100');parser.add_argument('--udp-port',type=int,default=6008)
    parser.add_argument('--out',required=True,type=Path);parser.add_argument('--duration',type=float,default=300)
    parser.add_argument('--stop-file',type=Path);parser.add_argument('--flash-record',type=Path)
    args=parser.parse_args()
    if not 0<args.duration<=600:parser.error('duration must be finite and between 0 and 600 seconds')
    if args.a_port==args.b_port:parser.error('A and B must use distinct serial ports')
    return run(args)

if __name__=='__main__':raise SystemExit(main())
