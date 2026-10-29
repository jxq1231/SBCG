"""Bounded serial trial with a reproducible PC stimulus; never flashes firmware."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import time
import wave


def make_tone(path):
    rate, frames, fade = 48000, 24000, 240
    data = bytearray()
    for i in range(frames):
        envelope = min(1.0, i/fade, (frames-1-i)/fade)
        value = round(32767*0.12*envelope*math.sin(2*math.pi*1000*i/rate))
        data.extend(struct.pack('<h', value))
    with wave.open(str(path), 'wb') as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(data)
    return {'sample_rate_hz':rate,'duration_s':0.5,'frequency_hz':1000,'digital_peak_fraction':0.12,
            'fade_ms':5,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'source':'generated PC stimulus, not course standard audio'}


def check_output_paths(prefix):
    """Reject reused evidence prefixes before creating or modifying any output."""
    for suffix in ('-tone.wav','-serial.bin','-serial.jsonl','-run.json','-analysis.json'):
        path=Path(str(prefix)+suffix)
        if path.exists():raise FileExistsError(f'evidence exists; use a new prefix: {path}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='COM3')
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--tone-only', action='store_true')
    parser.add_argument('--play', action='store_true')
    parser.add_argument('--expected-fw', default='SBCG-M4.1-CAND')
    parser.add_argument('--duration', type=float, default=90)
    args = parser.parse_args()
    check_output_paths(args.prefix)
    args.prefix.parent.mkdir(parents=True,exist_ok=True)
    tone_path = Path(str(args.prefix)+'-tone.wav')
    stimulus = make_tone(tone_path)
    if args.tone_only:
        print(json.dumps(stimulus));return 0
    if args.duration < 80:
        parser.error('trial requires at least 80 seconds for background and five stimuli')
    import serial
    player = None
    if args.play:
        import winsound
        player = winsound
    stream = serial.Serial(port=None,baudrate=115200,bytesize=8,parity='N',stopbits=1,timeout=0.1)
    stream.dtr=stream.rts=False;stream.port=args.port
    run={'status':'FAILED','port':args.port,'stimulus':stimulus,'play_requested':args.play,
         'tones':[],'hardware_precision_verified':False}
    opened=time.monotonic();boot=None;pending=b'';next_progress=0
    try:
        stream.open()
        with Path(str(args.prefix)+'-serial.bin').open('xb') as raw, Path(str(args.prefix)+'-serial.jsonl').open('x',encoding='utf-8') as output:
            print(f'CAPTURE_READY: awaiting {args.expected_fw} BOOT, no flashing in this tool',flush=True)
            while True:
                data=stream.read(4096)
                if data:
                    raw.write(data);raw.flush();pending+=data
                    while b'\n' in pending:
                        line,pending=pending.split(b'\n',1)
                        text=line.rstrip(b'\r').decode('ascii',errors='replace')
                        now=time.monotonic()
                        if text.startswith(f'$BOOT,FW={args.expected_fw},') and boot is None:
                            if 'UID_W0=001A002D,UID_W1=30365105,UID_W2=36363631' not in text:
                                raise RuntimeError('new firmware BOOT UID is not registered A')
                            boot=now;run['boot_line']=text;print('EXPECTED_BOOT: 60s background begins',flush=True)
                        output.write(json.dumps({'elapsed_s':None if boot is None else round(now-boot,6),'line':text})+'\n')
                    output.flush()
                if boot is None:
                    if time.monotonic()-opened>45:raise TimeoutError('expected BOOT not received within 45 seconds')
                    continue
                elapsed=time.monotonic()-boot
                if elapsed>=next_progress:
                    print(f'CAPTURE_PROGRESS {elapsed:.1f}s',flush=True);next_progress+=10
                index=len(run['tones'])
                if player is not None and index<5 and elapsed>=60+3*index:
                    started=time.monotonic()-boot
                    player.PlaySound(str(tone_path),player.SND_FILENAME|player.SND_ASYNC)
                    run['tones'].append({'number':index+1,'host_start_s':round(started,6),'nominal_duration_s':0.5})
                    print(f'PC_TONE {index+1}/5 at {started:.3f}s',flush=True)
                if elapsed>=args.duration:
                    run['status']='CAPTURED_PENDING_ANALYSIS';run['duration_s']=elapsed;break
            if pending:output.write(json.dumps({'incomplete_tail_hex':pending.hex()})+'\n')
    finally:
        stream.close()
        if player is not None:player.PlaySound(None,0)
        run['port_closed']=not stream.is_open
        with Path(str(args.prefix)+'-run.json').open('x',encoding='utf-8') as summary:
            summary.write(json.dumps(run,indent=2))
        print('CAPTURE_CLOSED',run['status'],flush=True)
    return 0


if __name__=='__main__':raise SystemExit(main())
