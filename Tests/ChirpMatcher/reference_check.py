"""真实C输出与独立离线相关对照；仅覆盖合成/文件样本，不代表物理识别概率。"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import wave

import numpy as np
from scipy.signal import correlate

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'Logs/Detector/M4_2_1'
TEMPLATE_SHA = 'c37114332a698910e12ac855b47b566df229a3c7ec01414371e32969da3e084d'


def wav(name):
    path = ROOT / 'Data/Audio' / name
    with wave.open(str(path), 'rb') as source:
        assert (source.getnchannels(), source.getsampwidth(), source.getframerate()) == (1, 2, 48000)
        return np.frombuffer(source.readframes(source.getnframes()), dtype='<i2').copy()


def reference(samples, template, first, cfg):
    """批量FFT相关+精确整数窗方差，独立求越阈组中的最优窗口。"""
    n = len(template)
    if len(samples) < n:
        return []
    x = samples.astype(np.int64)
    t = template.astype(np.int64)
    sx = np.concatenate(([0], np.cumsum(x)))
    sq = np.concatenate(([0], np.cumsum(x*x)))
    sums, squares = sx[n:]-sx[:-n], sq[n:]-sq[:-n]
    vx = n*squares-sums*sums
    vt = int(n*np.dot(t, t)-sum(t)**2)
    dots = correlate(x.astype(float), t.astype(float), mode='valid', method='fft')
    numerator = n*dots-sums*int(sum(t))
    scores = np.zeros(len(vx))
    valid = (vx > 0) & (vx >= cfg[0]**2*n*n)
    scores[valid] = np.minimum(1, np.abs(numerator[valid])/np.sqrt(vx[valid].astype(float)*vt))
    threshold = float(np.float32(cfg[1]))
    eligible = np.flatnonzero(valid & (scores >= threshold))
    clips = np.concatenate(([0], np.cumsum((x == -32768) | (x == 32767))))
    events = []
    consumed = -1
    next_allowed = 0
    for start in eligible:
        start = int(start)
        if start <= consumed or start < next_allowed:
            continue
        stop = start + cfg[2]
        if stop >= len(scores):
            break  # 缺观察尾部，不提前发布。
        index = start + int(np.argmax(scores[start:stop+1]))
        events.append(dict(first_frame=first+index, last_frame=first+index+n-1,
                           confirmed_frame=first+stop+n-1, correlation=float(scores[index]),
                           rms_pcm=int(np.sqrt(vx[index]/(n*n))),
                           inverted=int(numerator[index] < 0),
                           clipped=int(clips[index+n]-clips[index] != 0)))
        consumed = stop
        next_allowed = index + cfg[3]
    return events


def execute(executable, samples, template, first, cfg):
    header = [len(template), cfg[0], cfg[1], cfg[2], cfg[3], first >> 32, first & 0xffffffff, len(samples)]
    payload = (' '.join(map(str, header))+'\n'+' '.join(map(str, template.tolist()))+'\n'+
               ' '.join(map(str, samples.tolist()))+'\n')
    result = subprocess.run([str(executable)], input=payload, text=True, capture_output=True, timeout=60)
    if result.returncode:
        raise AssertionError((result.returncode, result.stdout, result.stderr))
    rows = result.stdout.splitlines()
    assert rows and rows[-1] == 'END', rows[-3:]
    events = []
    for line in rows[:-1]:
        fields = line.split()
        assert fields[0] == 'E' and len(fields) == 11, line
        p = list(map(int, fields[1:7]))
        events.append(dict(first_frame=(p[0] << 32)|p[1],last_frame=(p[2] << 32)|p[3],
                           confirmed_frame=(p[4] << 32)|p[5],correlation=float(fields[7]),
                           rms_pcm=int(fields[8]),inverted=int(fields[9]),clipped=int(fields[10])))
    return events


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    path = ROOT/'Data/Audio/chirp_2-6khz_50ms_template.wav'
    assert hashlib.sha256(path.read_bytes()).hexdigest() == TEMPLATE_SHA
    template = wav(path.name)
    cfg = (64, 0.80, 48, 24000)
    rng = np.random.default_rng(20261025)
    cases = []

    def case(name, values, expected, first=0, config=cfg, positions=None):
        values = np.rint(values)
        assert np.all(np.isfinite(values)) and values.min() >= -32768 and values.max() <= 32767
        cases.append((name,values.astype('<i2'),expected,first,config,positions))

    def pulse(signal=template, start=600, length=7000, dc=0):
        x = np.full(length, float(dc))
        x[start:start+len(signal)] += signal
        return x

    case('actual_20s_ten_events', wav('standard_chirp_2-6khz_10x_20s.wav'),10,
         positions=list(range(48000,960000,96000)))
    case('unaligned_first',pulse(start=631),1,positions=[631])
    case('half_buffer_crossing',pulse(start=6992,length=13000),1,positions=[6992])
    case('low_gain_dc',pulse(template.astype(float)*0.08,dc=5000),1,positions=[600])
    case('inverted',pulse(-template),1,positions=[600])
    case('uint64_position',pulse(),1,first=5000000000,positions=[5000000600])
    case('adc_clipped',np.clip(pulse(template.astype(float)*3),-32768,32767),1)
    case('noise_plus_chirp',pulse()+rng.normal(0,1200,7000),1,positions=[600])
    rate = 47991.071
    resampled = np.interp(np.arange(2400)*48000/rate,np.arange(2400),template,right=0)
    case('derived_rate_mismatch',pulse(resampled),1)
    case('silence',np.zeros(7000),0)
    case('constant_dc',np.full(7000,32767),0)
    case('white_noise',rng.integers(-4000,4001,20000),0)
    t = np.arange(2400)/48000
    case('single_3khz_tone',pulse(10000*np.cos(2*np.pi*3000*t)),0)
    case('reversed_sweep',pulse(template[::-1]),0)
    impulse = np.zeros(7000);impulse[600] = 32767
    case('single_impulse',impulse,0)
    case('incomplete_template',template[:2399],0)
    case('insufficient_peak_tail',np.concatenate([template,np.zeros(20)]),0)
    case('below_energy_gate',pulse(template.astype(float)*0.001),0)
    repeated = np.zeros(35000)
    for start in (600,5600,30000):
        repeated[start:start+len(template)] = template
    case('refractory_suppression',repeated,2,positions=[600,30000])
    results = []
    for optimization in ('-O0','-O2'):
        exe = OUTPUT/f'chirp_reference_{optimization[1:]}.exe'
        command = [shutil.which('gcc'),'-std=c11',optimization,'-Wall','-Wextra','-Werror','-pedantic',
                   '-I',str(ROOT/'App'),str(ROOT/'Tests/ChirpMatcher/reference_bridge.c'),
                   str(ROOT/'App/Detector/chirp_matcher.c'),str(ROOT/'App/Detector/chirp_template.c'),
                   '-o',str(exe),'-lm']
        built = subprocess.run(command,capture_output=True,text=True,timeout=30)
        assert built.returncode == 0, built.stdout+built.stderr
        for name,samples,count,first,config,positions in cases:
            expected = reference(samples,template,first,config)
            actual = execute(exe,samples,template,first,config)
            assert len(expected) == count,(name,'reference count',expected)
            assert len(actual) == len(expected),(name,actual,expected)
            if positions is not None:
                assert [e['first_frame'] for e in actual] == positions,(name,actual,positions)
            for a,b in zip(actual,expected):
                for key in ('first_frame','last_frame','confirmed_frame','rms_pcm','inverted','clipped'):
                    assert a[key] == b[key],(name,key,a,b)
                assert abs(a['correlation']-b['correlation']) < 2e-6,(name,a,b)
            if name == 'inverted': assert actual[0]['inverted'] == 1
            if name == 'adc_clipped': assert actual[0]['clipped'] == 1
            results.append(dict(name=name,optimization=optimization,events=actual,status='PASS'))
            print(f'PASS {optimization} {name}: {len(actual)} events',flush=True)
    (OUTPUT/'reference_results.json').write_text(json.dumps({
        'scope':'digital_inputs_only_not_hardware_recognition_probability','template_sha256':TEMPLATE_SHA,
        'config':cfg,'cases':results},indent=2)+'\n',encoding='utf-8')
    print(f'PASS {len(results)} real-C/reference scenarios',flush=True)


if __name__ == '__main__':
    main()
