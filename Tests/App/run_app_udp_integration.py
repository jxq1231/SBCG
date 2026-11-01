"""Real A/B processes: packet faults, stream recovery and calibration requests.

Only HAL/audio/PTP and LwIP I/O are replaced. JSON done records delimit each
command; the reader thread only writes to Queue, and the main thread owns the
packet history. No fixed sleeps are used as a completion condition.
Native JSON records and submitted commands are retained for both boards.
"""
from pathlib import Path
import json, queue, shutil, subprocess, sys, threading

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'Logs/AdvancedUI/AppUDPIntegration'
EXE=OUT/'test_app_udp_integration.exe'
CHECKS=0

class Board:
    """One real application process with one completed command at a time."""
    def __init__(self,role,start_ms,common_ns):
        self.role=role
        self.messages=queue.Queue()
        self.packets=[]
        self.history=[]
        self.records=[]
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
            self.records.append(item)
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
        self.records.append({'type':'command','text':text})
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
        (OUT/f'native_{self.role}.jsonl').write_text(
            ''.join(json.dumps(record)+'\n' for record in self.records),encoding='utf-8')
        assert self.proc.returncode==0, (self.role,self.proc.returncode)

def drain(a,b,drop_acks=False,drop_clap_acks=False):
    """Forward completed-command packets; failed sends never reach the peer."""
    for source,destination in ((a,b),(b,a)):
        pending,source.packets=source.packets,[]
        for packet in pending:
            if not packet['ok']:
                continue
            if drop_acks and packet['hex'].startswith('52414b32'):
                continue
            if drop_clap_acks and packet['hex'].startswith('434c4131'):
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
    global CHECKS
    assert condition,message
    CHECKS+=1
    print('PASS',message)

def status_packets(board,after_ms=0):
    return [p for p in board.history
            if p['tick_ms']>=after_ms and p['hex'].startswith('52535432')]

def group_status_packets(packets):
    """Keep every RST2 attempt, grouped by domain/authority epoch/revision/ID."""
    groups={}
    for packet in packets:
        wire=bytes.fromhex(packet['hex'])
        key=tuple(int.from_bytes(wire[offset:offset+4],'big')
                  for offset in (12,16,20,24))
        groups.setdefault(key,[]).append(packet)
    return groups

def main():
    gcc=shutil.which('gcc')
    if gcc is None: print('gcc not found',file=sys.stderr); sys.exit(2)
    OUT.mkdir(parents=True,exist_ok=True)
    cmd=[gcc,'-std=c11','-Wall','-Wextra','-Werror','-pedantic',
         '-DSBCG_HOST_TEST=1','-DSBCG_USE_LWIP=1','-DSBCG_USE_AUDIO=1','-DSTM32F746xx',
         '-DSBCG_USE_AUDIO_ANCHOR=1','-DSBCG_USE_AUDIO_MAP=1',
         '-DSBCG_USE_SOUND_TIME=1','-DSBCG_USE_BOARD_SYNC=1','-DSBCG_USE_BOARD_IDENTITY=1',
         '-DSBCG_USE_CALIBRATION_CONTROL=1',
         '-DSBCG_USE_ADVANCED_UI=1',
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
         str(ROOT/'App/Calibration/calibration_control.c'),
         str(ROOT/'App/Network/udp_sound_event.c'),str(ROOT/'App/Network/ranging_session.c'),
         str(ROOT/'App/Network/domain_wire.c'),str(ROOT/'App/Network/measurement_wire.c'),
         str(ROOT/'App/Network/measurement_ack.c'),str(ROOT/'App/Network/ranging_status_wire.c'),
         str(ROOT/'App/Network/temperature_wire.c'),str(ROOT/'App/Network/sound_event_wire.c'),
         str(ROOT/'App/Measure/sound_pair.c'),str(ROOT/'App/Measure/waveform_tdoa.c'),
         str(ROOT/'App/Temperature/sound_speed.c'),
         str(ROOT/'App/Measure/clap_ranging.c'),str(ROOT/'App/Network/clap_wire.c'),
         str(ROOT/'App/UI/waveform_service.c'),str(ROOT/'App/Audio/audio_time_map.c'),
         str(ROOT/'App/UI/status_snapshot.c'),str(ROOT/'App/UI/status_view.c'),
         str(ROOT/'App/BSP/board_identity.c'),
         '-Wl,--wrap=WaveformService_ObserveAnchor',
         '-o',str(EXE)]
    build=subprocess.run(cmd,capture_output=True,text=True,timeout=120)
    if build.returncode: print(build.stdout+build.stderr); sys.exit(build.returncode)


    a=Board('A',0,0);b=Board('B',0,0)
    delta_ns=900_000
    def step(at,sequence=0,drop_acks=False,drop_clap_acks=False):
        a.cmd(f'STEP {at} {at*1_000_000} {sequence} {at*1_000_000}')
        b.cmd(f'STEP {at} {at*1_000_000} {sequence} {at*1_000_000+delta_ns}')
        drain(a,b,drop_acks,drop_clap_acks)
    try:
        for t in range(0,7001,50):
            step(t,{2000:1,4000:2,6000:3}.get(t,0))
        sa,sb=snap(a,b,'three-chirps')
        check(sa['advanced']==sb['advanced']==1,'real A/B App builds enable advanced dispatch')
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
        # A pending older result can change state before the new sound is published.
        # Each authority key owns its retry cadence; a new revision may send immediately.
        attempts_by_key=group_status_packets(attempts)
        check(any(len(group)>=2 for group in attempts_by_key.values()),
              'lost RAK2 retransmits at least one exact authority key')
        for key,group in attempts_by_key.items():
            check(len({p['hex'] for p in group})==1,
                  f'ACK-loss key {key} keeps identical packet/source/deadlines')
            check(all(q['tick_ms']-p['tick_ms']>=500 for p,q in zip(group,group[1:])),
                  f'ACK-loss key {key} retry interval is at least 500ms')
        for t in range(9600,10001,50):step(t)
        sa,sb=snap(a,b,'ACK-restored')
        check(sa['has']==sb['has']==1 and sa['id']==sb['id']==4,'ACK recovery retains source ID')

        # A fails its sends: do not feed those datagrams to B.
        a.cmd('SEND 0')
        for t in range(10050,11301,50):step(t,5 if t==10050 else 0)
        failed=status_packets(a,10050)
        domain_failed=[p for p in a.history if p['tick_ms']>=10050 and
                       p['hex'].startswith('444f4d4e') and not p['ok']]
        check(len(domain_failed)>=2 and
              all(q['tick_ms']-p['tick_ms']>=500 for p,q in zip(domain_failed,domain_failed[1:])),
              'failed DOMAIN requests also preserve 500ms retry cadence')
        check(len(failed)>=3 and all(not p['ok'] for p in failed),'send failures are observed')
        failed_by_key=group_status_packets(failed)
        check(any(len(group)>=2 for group in failed_by_key.values()),
              'failed sends retry at least one exact authority key')
        for key,group in failed_by_key.items():
            check(len({p['hex'] for p in group})==1,
                  f'failed-send key {key} keeps identical packet/source/deadlines')
            check(all(q['tick_ms']-p['tick_ms']>=500 for p,q in zip(group,group[1:])),
                  f'failed-send key {key} preserves 500ms retry cadence')
        sa,sb=snap(a,b,'failed-sends')
        check(sa['id']==5 and sb['id']==4,'failed RST2 packets never arrive at B')
        a.cmd('SEND 1')
        for t in range(11350,12001,50):step(t)
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
        for t in range(38650,44551,50):
            step(t,{40000:15,42000:16,44000:17}.get(t,0))
            # App waits 700ms for waveform capture; allow two 50ms packet handoffs.
            if t in (40800,42800):
                sa,sb=snap(a,b,'new-provisional-after-expiry')
                check(sa['has']==sb['has']==1 and sa['state']==sb['state']==1 and
                      sa['id']==sb['id'] and sa['mm']==sb['mm'],
                      'first two new sounds after expiry reach B as ACQUIRING values')
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
        b.cmd('CAPTURE 0')
        for t in range(47100,53551,50):step(t,{48000:18,50000:19,52000:20}.get(t,0))
        sa,sb=snap(a,b,'events-without-B-waveforms')
        check(sa['has']==sb['has']==0,
              'high-quality scalar events without B waveforms never create A/B distances')
        b.cmd('CAPTURE 1')
        for t in range(53600,58801,50):step(t,{54000:21,56000:22,58000:23}.get(t,0))
        sa,sb=snap(a,b,'waveforms-recovered')
        check(sa['has']==sb['has']==1 and sa['id']==sb['id'] and sa['mm']==sb['mm'],
              'new waveform evidence restores matching A/B results without reset')
        source=sa['source_ns']
        a.cmd('CAPTURE 0');b.cmd('CAPTURE 0')
        for t in range(58850,64601,50):
            step(t,{60000:24,62000:25,64000:26}.get(t,0))
            if t==60800:
                sa,sb=snap(a,b,'rejection-keeps-old-source-age')
                check(sa['source_ns']==sb['source_ns']==source,
                      'rejected pairs cannot renew old source timestamps')
        sa,sb=snap(a,b,'rejected-events-expired')
        check(sa['has']==sb['has']==0,
              'continued rejected sounds cannot keep an old distance alive')

        original_checks=CHECKS
        a.cmd('CLOCK 0');a.cmd('CAL CAPTURE')
        step(64650);step(64700)
        sa,sb=snap(a,b,'calibration-invalid-context')
        check(sa['control_result']==4 and sa['control_completed']==1 and
              sa['cal_saved']==sa['cal_enabled']==sa['cal_version']==0,
              'CAPTURE with invalid actual clock is rejected without creating calibration')
        a.cmd('CLOCK 1');a.cmd('CAPTURE 1');b.cmd('CAPTURE 1')
        for t in range(64750,70801,50):step(t,{66000:27,68000:28,70000:29}.get(t,0))
        sa,sb=snap(a,b,'calibration-ready')
        check(sa['state']==sb['state']==2 and sa['has']==sb['has']==1 and
              sa['fresh']==sb['fresh']==1 and sa['id']==sb['id'] and abs(sa['mm']-309)<2,
              'new waveforms rebuild fresh A/B TRACKING before calibration')
        raw_mm=sa['mm'];saved_offset=500-raw_mm;previous=sa

        a.cmd('CAL CAPTURE');step(70850);step(70900)
        sa,sb=snap(a,b,'calibration-captured-withdrawn')
        check(sa['control_result']==2 and sa['control_completed']==2 and
              sa['cal_saved']==sa['cal_enabled']==1 and sa['cal_offset_mm']==saved_offset,
              'CAPTURE saves 500-minus-raw from the current actual source and enables it')
        check(sa['has']==sb['has']==0 and sa['rev']==sb['rev'] and sa['rev']>previous['rev'] and
              sa['applied_offset_mm']==sb['applied_offset_mm']==saved_offset and
              sa['applied_version']==sb['applied_version']==sa['cal_version'],
              'capture withdraws the old A/B value and publishes the same changed calibration contract')
        for t in range(70950,76801,50):step(t,{72000:30,74000:31,76000:32}.get(t,0))
        sa,sb=snap(a,b,'calibration-new-sounds-500mm')
        check(sa['state']==sb['state']==2 and sa['fresh']==sb['fresh']==1 and
              sa['id']==sb['id'] and sa['id']>previous['id'] and sa['mm']==sb['mm'] and abs(sa['mm']-500)<=1,
              'only new independent sounds restore matching calibrated 500mm A/B values')
        check(sa['applied_offset_mm']==sb['applied_offset_mm']==saved_offset and
              sa['applied_version']==sb['applied_version']==sa['cal_version'] and
              sb['cal_saved']==sb['cal_enabled']==sb['cal_version']==0,
              'B receives A calibration wire metadata while keeping its own saved state empty')
        previous=sa

        a.cmd('CAL DISABLE');step(76850);step(76900)
        sa,sb=snap(a,b,'calibration-disabled-withdrawn')
        check(sa['control_result']==2 and sa['cal_saved']==1 and sa['cal_enabled']==0 and
              sa['cal_offset_mm']==saved_offset and sa['cal_version']>previous['cal_version'],
              'DISABLE preserves the saved coefficient and advances only the application contract')
        check(sa['has']==sb['has']==0 and sa['applied_offset_mm']==sb['applied_offset_mm']==0 and
              sa['applied_version']==sb['applied_version']==sa['cal_version'],
              'disable withdraws both old values and sends zero effective offset to B')
        for t in range(76950,82801,50):step(t,{78000:33,80000:34,82000:35}.get(t,0))
        sa,sb=snap(a,b,'calibration-disabled-new-sounds')
        check(sa['state']==sb['state']==2 and sa['id']==sb['id'] and sa['id']>previous['id'] and
              sa['mm']==sb['mm']==raw_mm,
              'new sounds after disable restore matching uncalibrated A/B distances')
        previous=sa

        a.cmd('CAL ENABLE');step(82850);step(82900)
        sa,sb=snap(a,b,'calibration-enabled-withdrawn')
        check(sa['control_result']==2 and sa['cal_saved']==sa['cal_enabled']==1 and
              sa['cal_offset_mm']==saved_offset and sa['cal_version']>previous['cal_version'],
              'ENABLE restores the original saved coefficient without recalibrating')
        check(sa['has']==sb['has']==0 and sa['applied_offset_mm']==sb['applied_offset_mm']==saved_offset and
              sa['applied_version']==sb['applied_version']==sa['cal_version'],
              'enable withdraws both old values and restores the same effective contract on B')
        for t in range(82950,88801,50):step(t,{84000:36,86000:37,88000:38}.get(t,0))
        sa,sb=snap(a,b,'calibration-enabled-new-sounds')
        check(sa['state']==sb['state']==2 and sa['id']==sb['id'] and sa['id']>previous['id'] and
              sa['mm']==sb['mm'] and abs(sa['mm']-500)<=1,
              'new sounds after re-enable again produce matching calibrated 500mm A/B values')
        previous=sa

        stable_fields=('id','rev','source_ns','mm','cal_saved','cal_enabled','cal_offset_mm','cal_version',
                       'applied_offset_mm','applied_version')
        for index,action in enumerate(('CAPTURE','ENABLE','DISABLE','CLEAR')):
            b.cmd('CAL '+action);step(88850+index*100);step(88900+index*100)
            sa,sb=snap(a,b,'B-calibration-rejected-'+action.lower())
            check(sb['control_result']==3 and sb['control_completed']==index+1 and
                  sb['cal_saved']==sb['cal_enabled']==sb['cal_version']==0,
                  f'B direct {action} request is rejected as A_ONLY with no local mutation')
            check(all(sa[field]==previous[field] for field in stable_fields) and
                  sa['id']==sb['id'] and sa['mm']==sb['mm'] and
                  sa['applied_offset_mm']==sb['applied_offset_mm']==saved_offset and
                  sa['applied_version']==sb['applied_version'],
                  f'B direct {action} cannot change either board authoritative result or contract')

        for t in range(89250,91551,50):step(t)
        sa,sb=snap(a,b,'calibration-stale-source')
        check(sa['state']==sb['state']==3 and sa['has']==sb['has']==1 and sa['fresh']==sb['fresh']==0,
              'calibration stale-source rejection begins with explicit A/B HOLD values')
        previous=sa
        a.cmd('CAL CAPTURE');step(91600);step(91650)
        sa,sb=snap(a,b,'calibration-stale-capture-rejected')
        check(sa['control_result']==4 and sa['control_completed']==previous['control_completed']+1 and
              all(sa[field]==previous[field] for field in stable_fields),
              'stale CAPTURE cannot replace or disable the saved active coefficient')
        check(sa['id']==sb['id'] and sa['mm']==sb['mm'] and
              sa['applied_offset_mm']==sb['applied_offset_mm']==saved_offset,
              'rejected stale calibration leaves the existing A/B source and applied offset unchanged')

        a.cmd('CAL CLEAR');step(91700);step(91750)
        sa,sb=snap(a,b,'calibration-cleared-withdrawn')
        check(sa['control_result']==2 and sa['cal_saved']==sa['cal_enabled']==sa['cal_offset_mm']==0 and
              sa['cal_version']>previous['cal_version'],
              'CLEAR deletes the saved coefficient, disables it and advances the contract version')
        check(sa['has']==sb['has']==0 and sa['applied_offset_mm']==sb['applied_offset_mm']==0 and
              sa['applied_version']==sb['applied_version']==sa['cal_version'],
              'clear withdraws old A/B HOLD values and publishes matching zero-offset metadata')
        for t in range(91800,96801,50):step(t,{92000:39,94000:40,96000:41}.get(t,0))
        sa,sb=snap(a,b,'calibration-cleared-new-sounds')
        check(sa['state']==sb['state']==2 and sa['fresh']==sb['fresh']==1 and
              sa['id']==sb['id'] and sa['id']>previous['id'] and sa['mm']==sb['mm']==raw_mm,
              'new sounds after clear restore matching raw A/B distances with no old coefficient')
        check(sa['control_applied_offset_mm']==sb['control_applied_offset_mm']==0 and
              sa['control_applied_version']==sb['control_applied_version']==sa['applied_version'],
              'both control snapshots report the actual zero-offset wire contract after clear')
        previous=sa
        a.cmd('CAL ENABLE');step(96850);step(96900)
        sa,sb=snap(a,b,'calibration-cleared-cannot-enable')
        check(sa['control_result']==6 and all(sa[field]==previous[field] for field in stable_fields) and
              sa['has']==sb['has']==1 and sa['mm']==sb['mm']==raw_mm,
              'ENABLE after CLEAR is rejected as NO_SAVED without reviving the deleted coefficient')
        baseline_checks=CHECKS

        # CLASS changes only the detector boundary. Actual App creates SOUND_EVENT_CLAP,
        # UDP transports both real events and A's separate CLP1, and B generates CLA1.
        def clap_packets(board,after=0):
            return [p for p in board.history if p['tick_ms']>=after and
                    p['hex'].startswith('434c5031')]
        def sequence_logs(board):
            return [r for r in board.records if r['type']=='log' and
                    b'$STANDARD_SEQUENCE' in bytes.fromhex(r['hex'])]
        a.cmd('CLASS 2');b.cmd('CLASS 2')
        a.cmd('CAPTURE 0');b.cmd('CAPTURE 0')
        standard_before=sa
        sequences_before=(len(sequence_logs(a)),len(sequence_logs(b)))
        for t in range(96950,99401,50):step(t,42 if t==98000 else 0)
        sa,sb=snap(a,b,'one-clap-without-chirp-waveforms')
        check(sa['clap_has']==sb['clap_has']==sa['clap_fresh']==sb['clap_fresh']==1 and
              sa['clap_id']==sb['clap_id']!=0 and sa['clap_mm']==sb['clap_mm']==raw_mm and
              sa['clap_dir']==sb['clap_dir']==1 and sa['clap_quality']==sb['clap_quality']==98,
              'one real CLAP creates matching independent A/B result without CHIRP waveform evidence')
        check(sa['id']==sb['id']==standard_before['id'] and
              sa['source_ns']==sb['source_ns']==standard_before['source_ns'] and
              sa['mm']==sb['mm']==standard_before['mm'],
              'CLAP never replaces RANGE measurement ID, source or distance')
        check((len(sequence_logs(a)),len(sequence_logs(b)))==sequences_before,
              'CLAP-only detector events never push or log STANDARD_SEQUENCE')
        check(len(clap_packets(a))>=1 and all(len(bytes.fromhex(p['hex']))==80 for p in clap_packets(a)) and
              any(p['hex'].startswith('434c4131') and len(bytes.fromhex(p['hex']))==24 for p in b.history),
              'real UDP emits CLP1 80-byte results and CLA1 24-byte acknowledgements')
        confirmed=len(clap_packets(a))
        for t in range(99450,100451,50):step(t)
        check(len(clap_packets(a))==confirmed,'real CLA1 stops CLP1 retransmission for at least 1000ms')

        # Drop only CLA1; RANGE ACK and RSC2 still cross the same real port.
        for t in range(100500,101851,50):step(t,43 if t==100500 else 0,drop_clap_acks=True)
        sa,sb=snap(a,b,'clap-ACK-lost')
        check(sa['clap_id']==sb['clap_id'] and sa['clap_id']>1 and sa['clap_has']==sb['clap_has']==1,
              'CLA1 loss preserves matching source result while A retries')
        current_ack=[p['hex'] for p in b.history if p['hex'].startswith('434c4131')][-1]
        bad_ack=bytearray.fromhex(current_ack);bad_ack[15]^=0x01
        a.cmd('RX '+bad_ack.hex())
        # Both existing short protocols are exactly 24 bytes; they must retain their dispatch.
        for magic in ('52534332','52414b32'):
            candidate=[p['hex'] for p in b.history if p['hex'].startswith(magic)][-1]
            check(len(bytes.fromhex(candidate))==24,f'existing {bytes.fromhex(magic).decode()} stays 24 bytes')
            a.cmd('RX '+candidate)
        for t in range(101900,103101,50):step(t,drop_clap_acks=True)
        attempts=clap_packets(a,100500)
        check(len(attempts)>=4 and len({p['hex'] for p in attempts})==1 and
              all(q['tick_ms']-p['tick_ms']>=500 for p,q in zip(attempts,attempts[1:])),
              'wrong CLA1 ID, RSC2 and RAK2 never stop exact CLP1 retries or bypass 500ms cadence')
        sa,sb=snap(a,b,'clap-real-source-stale')
        check(sa['clap_has']==sb['clap_has']==1 and sa['clap_fresh']==sb['clap_fresh']==0 and
              sa['clap_source_ns']==sb['clap_source_ns']==100500900000,
              'repeated CLP1 receives do not refresh the 2.5s common source age')
        before_bad=a.cmd('SNAP')['rx_bad']
        # Correct ACK uses current exact key; malformed new-protocol short packets cannot
        # accidentally be treated as that ACK or as an RSC2 handshake.
        for payload in (b'CLP1'+bytes(20),b'WVP1'+bytes(20),bytes.fromhex(current_ack)[:-1]):
            a.cmd('RX '+payload.hex())
        a.cmd('RX '+current_ack)
        step(103150)
        sa,sb=snap(a,b,'clap-short-magic-dispatch')
        check(sa['rx_bad']==before_bad+3 and sa['clap_id']==sb['clap_id'],
              '24-byte CLP1/WVP1 and truncated CLA1 are rejected without changing active result')
        confirmed=len(clap_packets(a))
        for t in range(103200,104201,50):step(t)
        check(len(clap_packets(a))==confirmed,'only the exact CLA1 key stops the pending CLP1')
        saved_clap=attempts[-1]['hex']
        for t in range(104250,107101,50):step(t)
        sa,sb=snap(a,b,'clap-real-source-expired')
        check(sa['clap_has']==sb['clap_has']==0,'real App withdraws both CLAP values at 6.5s source age')
        b.cmd('RX '+saved_clap);step(107150)
        sa,sb=snap(a,b,'clap-expired-replay')
        check(sa['clap_has']==sb['clap_has']==0,'expired CLP1 replay cannot restore the independent value')

        # Failed real udp_sendto attempts must be throttled even when nothing is delivered.
        for t in range(107200,108651,50):step(t,44 if t==108000 else 0)
        a.cmd('SEND 0')
        for t in range(108700,110101,50):step(t)
        failed=clap_packets(a,108000)
        check(len(failed)>=3 and all(not p['ok'] for p in failed) and
              all(q['tick_ms']-p['tick_ms']>=500 for p,q in zip(failed,failed[1:])),
              'failed real CLP1 sends preserve 500ms retry backoff')
        sa,sb=snap(a,b,'clap-real-send-failure')
        check(sa['clap_has']==1 and sb['clap_has']==0,'failed CLP1 never reaches B')
        a.cmd('SEND 1')
        for t in range(110150,111001,50):step(t)
        sa,sb=snap(a,b,'clap-send-recovered')
        check(sa['clap_has']==sb['clap_has']==1 and sa['clap_id']==sb['clap_id'] and
              sa['clap_mm']==sb['clap_mm'],
              'real successful CLP1 retry restores the same A/B result without a new clap')
        a.cmd('SYNC 0');b.cmd('SYNC 0');step(111050)
        sa,sb=snap(a,b,'clap-domain-lost')
        check(sa['clap_has']==sb['clap_has']==0,'actual domain loss immediately withdraws A/B CLAP values')
        b.cmd('RX '+saved_clap);step(111100)
        sa,sb=snap(a,b,'clap-replay-during-domain-loss')
        check(sa['clap_has']==sb['clap_has']==0,'CLP1 replay during invalid domain never revives a value')
        # The production App consumes the single anchor slot and fans out that same
        # observation. The wrapper counts and then calls the real waveform service.
        anchors_before=(sa['wave_anchors'],sb['wave_anchors'])
        a.cmd('ANCHOR 1 111 123456789 5330000')
        b.cmd('ANCHOR 1 111 123450000 5300000')
        step(111150)
        sa,sb=snap(a,b,'real-App-anchor-fanout')
        check((sa['wave_anchors'],sb['wave_anchors'])==tuple(n+1 for n in anchors_before),
              'one actual App TakeObservation fans the unchanged anchor to each real waveform service once')
        for t in range(111200,111701,50):step(t)
        sa,sb=snap(a,b,'anchor-single-slot-consumed')
        check((sa['wave_anchors'],sb['wave_anchors'])==tuple(n+1 for n in anchors_before),
              'later App rounds without a new anchor never repeat waveform ObserveAnchor')
    finally:
        a.stop();b.stop()
    print(f'RESULT PASS ({CHECKS} checks: {baseline_checks-1} preserved baseline + '
          f'{CHECKS-baseline_checks+1} advanced; native_A.jsonl/native_B.jsonl retained)')

if __name__=='__main__':
    main()
