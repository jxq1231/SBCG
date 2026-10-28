"""Real A/B processes: command barriers, packet faults, ACK and stream recovery.

Only HAL/audio/PTP and LwIP I/O are replaced. JSON done records delimit each
command; the reader thread only writes to Queue, and the main thread owns the
packet history. No fixed sleeps are used as a completion condition.
"""
from pathlib import Path
import json, queue, shutil, subprocess, sys, threading

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Logs/App/UDPIntegration'
EXE=OUT/'test_app_udp_integration.exe'

class Board:
    """One real application process with one completed command at a time."""
    def __init__(self,role,start_ms,common_ns):
        self.role=role
        self.messages=queue.Queue()
        self.packets=[]
        self.history=[]
        self.proc=subprocess.Popen([str(EXE),role,str(start_ms),str(common_ns)],
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
            text=True,encoding='utf-8')
        self.reader=threading.Thread(target=self._read,daemon=True)
        self.reader.start()
        self._done()

    def _read(self):
        for line in self.proc.stdout:
            if line.startswith('{'):
                self.messages.put(json.loads(line))
            else:
                self.messages.put({'type':'error','line':line.rstrip()})
        self.messages.put({'type':'eof'})

    def _done(self):
        snapshot=None
        while True:
            item=self.messages.get(timeout=10)
            kind=item['type']
            if kind=='done':
                return snapshot
            if kind=='packet':
                self.packets.append(item)
                self.history.append(item)
            elif kind=='snapshot':
                snapshot=item
            elif kind in ('error','eof'):
                raise AssertionError(f'{self.role}: child failed: {item}')

    def cmd(self,text):
        self.proc.stdin.write(text+'\n')
        self.proc.stdin.flush()
        return self._done()

    def stop(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.reader.join(timeout=5)
        self.proc.stdout.close()
        assert self.proc.returncode==0, (self.role,self.proc.returncode)

def drain(a,b,drop_acks=False):
    """Forward completed-command packets; failed sends never reach the peer."""
    for source,destination in ((a,b),(b,a)):
        pending,source.packets=source.packets,[]
        for packet in pending:
            if not packet['ok']:
                continue
            if drop_acks and packet['hex'].startswith('52414b32'):
                continue
            destination.cmd('RX '+packet['hex'])

def snap(a,b,label):
    sa=a.cmd('SNAP');sb=b.cmd('SNAP')
    assert sa is not None and sb is not None
    print(f'[{label}] A state={sa["state"]} id={sa["id"]} rev={sa["rev"]} '
          f'has={sa["has"]} mm={sa["mm"]} | B state={sb["state"]} '
          f'id={sb["id"]} rev={sb["rev"]} has={sb["has"]} mm={sb["mm"]}')
    return sa,sb

def check(condition,message):
    assert condition,message
    print('PASS',message)

def status_packets(board,after_ms=0):
    return [p for p in board.history
            if p['tick_ms']>=after_ms and p['hex'].startswith('52535432')]

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


    a=Board('A',0,0);b=Board('B',0,0)
    delta_ns=900_000
    def step(at,sequence=0,drop_acks=False):
        a.cmd(f'STEP {at} {at*1_000_000} {sequence} {at*1_000_000}')
        b.cmd(f'STEP {at} {at*1_000_000} {sequence} {at*1_000_000+delta_ns}')
        drain(a,b,drop_acks)
    try:
        for t in range(0,7001,50):
            step(t,{2000:1,4000:2,6000:3}.get(t,0))
        sa,sb=snap(a,b,'three-chirps')
        for field in ('state','id','rev','epoch','mm','direction','distance_text','quality_text'):
            check(sa[field]==sb[field],f'A/B same {field}')
        check(sa['has']==sb['has']==1 and sa['id']==3 and abs(sa['mm']-309)<2,
              'three independent sounds produce measurement 3 on both boards')
        saved_wire=status_packets(a)[-1]['hex']
        start=len(status_packets(a))
        for t in range(7050,8051,50):step(t)
        sa,sb=snap(a,b,'ACK-confirmed')
        check(len(status_packets(a))==start,'RAK2 stops RST2 attempts across 1000ms')

        # All RAK2 for a new voice are lost; retransmissions must actually occur.
        for t in range(8100,9551,50):step(t,4 if t==8100 else 0,True)
        attempts=status_packets(a,8100)
        check(len(attempts)>=3,'lost RAK2 causes repeated RST2 attempts')
        check(len({p['hex'] for p in attempts})==1,
              'ACK-loss retransmissions keep identical revision/source/deadlines')
        check(all(q['tick_ms']-p['tick_ms']>=500 for p,q in zip(attempts,attempts[1:])),
              'ACK-loss retry interval is at least 500ms')
        for t in range(9600,10001,50):step(t)
        sa,sb=snap(a,b,'ACK-restored')
        check(sa['has']==sb['has']==1 and sa['id']==sb['id']==4,'ACK recovery retains source ID')

        # A fails its sends: do not feed those datagrams to B.
        a.cmd('SEND 0')
        for t in range(10050,11251,50):step(t,5 if t==10050 else 0)
        failed=status_packets(a,10050)
        check(len(failed)>=3 and all(not p['ok'] for p in failed),'send failures are observed')
        check(all(q['tick_ms']-p['tick_ms']>=500 for p,q in zip(failed,failed[1:])),
              'failed sends preserve 500ms retry cadence')
        sa,sb=snap(a,b,'failed-sends')
        check(sa['id']==5 and sb['id']==4,'failed RST2 packets never arrive at B')
        a.cmd('SEND 1')
        for t in range(11300,12001,50):step(t)
        sa,sb=snap(a,b,'send-recovered')
        check(sa['id']==sb['id']==5 and sa['has']==sb['has']==1,'successful retry updates B')

        # A single cumulative gap must revoke once, then permit new good blocks.
        a.cmd('PROCESS 0');a.cmd('DROP 1');step(12050)
        sa,sb=snap(a,b,'gap-observed')
        check(sa['has']==0,'new gap immediately revokes A old value')
        before=sum(p['hex'].startswith('52534332') for p in a.history)
        for t in range(12100,12551,50):step(t)
        check(sum(p['hex'].startswith('52534332') for p in a.history)==before,
              'fresh callbacks alone cannot restart RSC2 before a successful block')
        a.cmd('PROCESS 1')
        for t in range(12600,13501,50):step(t)
        sa,sb=snap(a,b,'gap-no-new-sound')
        check(sa['has']==sb['has']==0,'requalification alone cannot revive old distance')
        for t in range(13550,18551,50):step(t,{14000:6,16000:7,18000:8}.get(t,0))
        sa,sb=snap(a,b,'gap-recovered')
        check(sa['has']==sb['has']==1 and sa['id']==sb['id'] and sa['id']>5,
              'good blocks and new sounds recover without reset')
        check(sa['audio_dropped']==1 and sa['audio_valid']==1,
              'recovery preserves cumulative DROP=1 while audio is healthy')
        a.cmd('DROP 2');step(18600)
        sa,sb=snap(a,b,'second-gap')
        check(sa['has']==0,'a second gap revokes again')
        for t in range(18650,25551,50):step(t,{21000:9,23000:10,25000:11}.get(t,0))
        sa,sb=snap(a,b,'second-recovery')
        check(sa['has']==sb['has']==1 and sa['audio_dropped']==2,'second gap also recovers')

        b.cmd('DROP 1');step(25600)
        sa,sb=snap(a,b,'B-gap-observed')
        check(sb['has']==0,'B gap immediately withdraws its live value')
        for t in range(25650,32551,50):step(t,{28000:12,30000:13,32000:14}.get(t,0))
        sa,sb=snap(a,b,'B-gap-recovered')
        check(sa['has']==sb['has']==1 and sa['id']==sb['id'] and sb['audio_dropped']==1,
              'B also recovers through handshake and new independent sounds')

        for t in range(32600,38601,50):step(t)
        sa,sb=snap(a,b,'silence-expired')
        check(sa['has']==sb['has']==0 and sa['state']==sb['state']==5,
              'both source deadlines expire to STALE')
        check(sa['distance_text']==sb['distance_text']=='-- M','no-value UI never shows zero metres')
        for t in range(38650,44551,50):step(t,{40000:15,42000:16,44000:17}.get(t,0))
        sa,sb=snap(a,b,'live-before-domain-loss')
        check(sa['has']==sb['has']==1,'domain-loss test begins with a live value')
        a.cmd('SYNC 0');b.cmd('SYNC 0');step(44600)
        sa,sb=snap(a,b,'domain-lost')
        check(sa['has']==sb['has']==0 and sa['state']==sb['state']==6,
              'domain loss withdraws a live value on A/B')
        b.cmd('RX '+saved_wire);step(44650)
        sa,sb=snap(a,b,'replayed-while-invalid')
        check(sa['has']==sb['has']==0,'old RST2 cannot resurrect a value during domain loss')
        a.cmd('SYNC 1');b.cmd('SYNC 1')
        for t in range(44700,47001,50):step(t)
        b.cmd('RX '+saved_wire);step(47050)
        sa,sb=snap(a,b,'replayed-after-requalification')
        check(sa['has']==sb['has']==0,'old authority packet rejected after domain recovery')
    finally:
        a.stop();b.stop()
    print('RESULT PASS')

if __name__=='__main__':
    main()
