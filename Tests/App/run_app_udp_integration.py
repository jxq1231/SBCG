"""Two-process A/B app integration over real modules and lwip-boundary stubs.

A and B run as separate host processes with real app_main wiring; UDP packets are
looped back between them. Timeline: domain/session handshake, three chirps,
A->B RST2 + B->A RAK2 confirmation, then source-age expiry on both screens.
"""
from pathlib import Path
import json,re,shutil,subprocess,sys,threading

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Logs/App/UDPIntegration'
EXE=OUT/'test_app_udp_integration.exe'

class Board:
    """One app process; packet/snapshot lines are consumed by a reader thread."""
    def __init__(self,role,start_ms,common_ns):
        self.role=role
        self.packets=[]   # (tick_ms, ok, hex)
        self.snapshots=[] # parsed snapshot dicts
        self.failures=[]
        self.proc=subprocess.Popen([str(EXE),role,str(start_ms),str(common_ns)],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,encoding='utf-8',errors='replace')
        self.reader=threading.Thread(target=self._read,daemon=True)
        self.reader.start()
    def _read(self):
        for line in self.proc.stdout:
            if '"type":"packet"' in line:
                ok=0 if '"ok":0' in line else 1
                tick=int(re.search(r'"tick_ms":(\d+)',line).group(1))
                hexs=re.search(r'"hex":"([0-9a-f]*)"',line).group(1)
                self.packets.append((tick,ok,hexs))
            elif '"type":"snapshot"' in line:
                self.snapshots.append(json.loads(line[line.index('{'):]))
            elif 'FAIL' in line or 'Assertion' in line:
                self.failures.append(line.strip())
    def cmd(self,text):
        self.proc.stdin.write(text+'\n')
        self.proc.stdin.flush()
    def stop(self):
        try: self.proc.stdin.close()
        except OSError: pass
        try: self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired: self.proc.kill()

def drain(a,b):
    """Move each board's produced packets into the peer's RX queue."""
    import time
    # Reader threads parse stdout asynchronously; give them a moment so the
    # packets produced by the previous STEP are visible before forwarding.
    prev_a,prev_b=-1,-1
    for _ in range(20):
        time.sleep(0.01)
        if len(a.packets)==prev_a and len(b.packets)==prev_b: break
        prev_a,prev_b=len(a.packets),len(b.packets)
    for _,_,hexs in a.packets: b.cmd('RX '+hexs)
    for _,_,hexs in b.packets: a.cmd('RX '+hexs)
    a.packets.clear(); b.packets.clear()

def snap(a,b,label):
    a.cmd('SNAP'); b.cmd('SNAP')
    import time
    time.sleep(0.05)
    sa=a.snapshots[-1] if a.snapshots else None
    sb=b.snapshots[-1] if b.snapshots else None
    print(f'[{label}] A state={sa["state"]} id={sa["id"]} rev={sa["rev"]} has={sa["has"]} '
          f'mm={sa["mm"]} text="{sa["distance_text"]}" | B state={sb["state"]} id={sb["id"]} '
          f'rev={sb["rev"]} has={sb["has"]} mm={sb["mm"]} text="{sb["distance_text"]}"')
    return sa,sb

def expect(cond,message,details=''):
    if cond: print('PASS',message)
    else: print('FAIL',message,details)
    return cond

def main():
    gcc=shutil.which('gcc')
    if gcc is None: print('gcc not found',file=sys.stderr); sys.exit(2)
    OUT.mkdir(parents=True,exist_ok=True)
    cmd=[gcc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
         '-DSBCG_HOST_TEST=1','-DSBCG_USE_LWIP=1','-DSBCG_USE_AUDIO=1','-DSTM32F746xx',
         '-DSBCG_USE_AUDIO_ANCHOR=1','-DSBCG_USE_AUDIO_MAP=1',
         '-DSBCG_USE_SOUND_TIME=1','-DSBCG_USE_BOARD_SYNC=1','-DSBCG_USE_BOARD_IDENTITY=1',
         '-I',str(ROOT), '-I',str(ROOT/'Tests/App'), '-I',str(ROOT/'App'), '-I',str(ROOT/'Tests/App/stubs'),
         '-I',str(ROOT/'Middlewares/Third_Party/LwIP/src/include'),
         '-I',str(ROOT/'Middlewares/Third_Party/LwIP/system'), '-I',str(ROOT/'LWIP/Target'),
         '-I',str(ROOT/'Core/Inc'), '-I',str(ROOT/'Drivers/STM32F7xx_HAL_Driver/Inc'),
         '-I',str(ROOT/'Drivers/CMSIS/Device/ST/STM32F7xx/Include'), '-I',str(ROOT/'Drivers/CMSIS/Include'),
         str(ROOT/'Tests/App/test_app_udp_integration.c'),str(ROOT/'App/app_main.c'),
         str(ROOT/'App/Measure/measurement_result.c'),str(ROOT/'App/Measure/ranging_tracker.c'),
         str(ROOT/'App/Measure/result_exchange_state.c'),
         str(ROOT/'App/Measure/measurement_gate.c'),str(ROOT/'App/Detector/standard_sequence.c'),
         str(ROOT/'App/Calibration/calibration.c'),str(ROOT/'App/Sync/time_domain.c'),
         str(ROOT/'App/Network/udp_sound_event.c'),str(ROOT/'App/Network/ranging_session.c'),
         str(ROOT/'App/Network/domain_wire.c'),str(ROOT/'App/Network/measurement_wire.c'),
         str(ROOT/'App/Network/measurement_ack.c'),str(ROOT/'App/Network/ranging_status_wire.c'),
         str(ROOT/'App/Network/temperature_wire.c'),str(ROOT/'App/Network/sound_event_wire.c'),
         str(ROOT/'App/Measure/sound_pair.c'),str(ROOT/'App/Temperature/sound_speed.c'),
         str(ROOT/'App/UI/status_snapshot.c'),str(ROOT/'App/UI/status_view.c'),
         str(ROOT/'App/BSP/board_identity.c'),
         '-o',str(EXE)]
    build=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
    if build.returncode: print(build.stdout+build.stderr); sys.exit(build.returncode)

    ok=True
    a=Board('A',0,0); b=Board('B',0,0)
    step_ms=50
    chirps={2000:1,4000:2,6000:3}  # step start time -> sequence number
    chirp_done=set()
    delta_ns=900_000  # ~0.309 m one-way at 343.42 m/s
    last_attempt_stable=None
    t=0
    while t<=7000:
        clock_ns=t*1_000_000
        seq=chirps.get(t,0)
        # Chirp on A at T, on B T+delta; STEP injects the scalar TOA event.
        a.cmd(f'STEP {t} {clock_ns} {seq} {clock_ns}')
        b.cmd(f'STEP {t} {clock_ns} {seq} {clock_ns+delta_ns}')
        drain(a,b)
        if t in chirps: chirp_done.add(t)
        t+=step_ms
    import time; time.sleep(0.1)
    sa,sb=snap(a,b,'after-3-chirps')
    ok&=expect(sa and sb and sa['has']==1 and sb['has']==1,
               'R05/A2b: both boards present a value after three chirps',f'{sa} {sb}')
    ok&=expect(sa and sb and sa['id']==sb['id'] and sa['id']!=0,
               'R02: A/B share one measurement id, monotonic counter',
               f'A id={sa["id"] if sa else None} B id={sb["id"] if sb else None}')
    ok&=expect(sa and sb and sa['mm']==sb['mm'] and abs((sb["mm"] or 0)-309)<15,
               'A2b: B mirrors A distance within temperature conversion',
               f'A mm={sa["mm"] if sa else None} B mm={sb["mm"] if sb else None}')
    ok&=expect(sa and sb and sa['epoch']==sb['epoch'] and sa['epoch']!=0,
               'R04: same authority epoch from RSC2 handshake')
    ok&=expect(sb and sb['rx_bad']==0,
               'R03: no 24-byte dispatch loss on B receive path',
               f'rx_bad={sb["rx_bad"] if sb else None}')

    # RAK2 stops RST2 retries: attempts must not grow without new revisions.
    def attempts(board):
        return board.snapshots[-1]['packet_attempts'] if board.snapshots else -1
    before_a=attempts(a)
    for _ in range(4):
        a.cmd(f'STEP {t} {t*1_000_000} 0 0'); b.cmd(f'STEP {t} {t*1_000_000} 0 0')
        drain(a,b); t+=step_ms
    time.sleep(0.1)
    after_a=attempts(a)
    ok&=expect(after_a==before_a,'R09: ACK stops RST2 retransmission',
               f'before={before_a} after={after_a}')

    # Keep advancing with no new source past the 6.5 s display deadline.
    while t<=13500:
        a.cmd(f'STEP {t} {t*1_000_000} 0 0'); b.cmd(f'STEP {t} {t*1_000_000} 0 0')
        drain(a,b); t+=step_ms
    time.sleep(0.1)
    sa,sb=snap(a,b,'after-6.5s-silence')
    ok&=expect(sa and sb and sa['has']==0 and sb['has']==0,
               'R01: both boards withdraw the value after the display deadline',
               f'A has={sa["has"] if sa else None} B has={sb["has"] if sb else None}')
    ok&=expect(sb and ('STALE' in sb['quality_text'] or 'FAULT' in sb['quality_text']),
               'R01: B UI state text is STALE/FAULT',
               f'text="{sb["quality_text"] if sb else None}"')

    # Domain loss revokes without reviving old packets.
    a.cmd('SYNC 0'); b.cmd('SYNC 0')
    for _ in range(8):
        a.cmd(f'STEP {t} {t*1_000_000} 0 0'); b.cmd(f'STEP {t} {t*1_000_000} 0 0')
        drain(a,b); t+=step_ms
    time.sleep(0.1)
    sa,sb=snap(a,b,'after-domain-loss')
    ok&=expect(sa and sb and sa['has']==0 and sb['has']==0,
               'R01/R05: domain loss revokes presentation on both boards')

    for board in (a,b):
        ok&=expect(not board.failures,f'{board.role}: no device-side assertion',
                   ';'.join(board.failures[:3]))
        board.stop()
    print('RESULT', 'PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)

if __name__=='__main__':
    main()
