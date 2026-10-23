"""Compare real C to a window-wise exact Fraction specification, not hardware."""
from fractions import Fraction
import json
import os
from pathlib import Path
import random
import shutil
import subprocess


def reference(samples, config, first):
    width, on, off, required, quiet_required, holdoff = config
    armed = False
    quiet = 0
    previous_event = None
    run = []
    events = []
    for start in range(0, len(samples) - width + 1, width):
        window = samples[start:start + width]
        variance = Fraction(sum(x*x for x in window), width) - Fraction(sum(window), width)**2
        last = first + start + width - 1
        if not armed:
            quiet = quiet + 1 if variance <= off**2 else 0
            if quiet >= quiet_required and (previous_event is None or last - previous_event >= holdoff):
                armed = True
                quiet = 0
            run = []
        elif variance < on**2:
            run = []
        else:
            run.append((first + start, variance, max(abs(x) for x in window), any(x in (-32768,32767) for x in window)))
            if len(run) == required:
                events.append((7, run[0][0], last, max(int(w[1]) for w in run), max(w[2] for w in run), int(any(w[3] for w in run))))
                armed = False
                previous_event = last
                quiet = 0
                run = []
    return events


def main():
    root = Path(__file__).resolve().parents[2]
    output = root / 'Logs/Detector/M4_1_1'
    executable = output / ('detector_reference.exe' if os.name == 'nt' else 'detector_reference')
    command = [shutil.which('gcc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
               '-I', str(root / 'App'), str(root / 'Tests/SoundDetector/reference_bridge.c'),
               str(root / 'App/Detector/sound_detector.c'), '-o', str(executable)]
    built = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if built.returncode:
        raise RuntimeError('Native reference build failed:\n' + built.stdout + built.stderr)
    rng = random.Random(20261023)
    cases = []
    # Exhaust phase and nonintegral threshold cases at several window lengths.
    for width in [2,3,4,7,16,64,255,256]:
        for on in [1,8,32767]:
            config = [width,on,0,1,1,0]
            wave = [0]*width + [-32768 if i%2 == 0 else 32767 for i in range(width)]
            cases.append((wave,config,5000000000))
    for _ in range(150):
        width = rng.randint(2,256)
        on = rng.randint(1,16000)
        config = [width,on,rng.randrange(on),rng.randint(1,4),rng.randint(1,4),rng.randint(0,10*width)]
        samples = []
        for index in range(120):
            # Long low/high runs create events and stress hysteresis and holdoff.
            dc = rng.randint(-12000,12000)
            pattern = index%24
            amp = 0 if pattern < 7 else on if pattern < 15 else rng.randint(0,20000)
            samples.extend(max(-32768,min(32767,dc + (-amp if i%2 == 0 else amp))) for i in range(width))
        # Partial last window must not create an extra event.
        samples.extend([32767]*(width//2))
        cases.append((samples,config,rng.randrange(1 << 40)))
    # Fractional variance is neither quiet at off=0 nor high at on=1.
    cases.append(([-1,0,0]+[0,0,0]+[-1,1,1]+[-2,1,1],[3,1,0,1,1,0],0))
    payload = ''.join(' '.join(map(str,[*cfg,first >> 32,first & 0xffffffff,len(samples)])) + '\n' +
                      ' '.join(map(str,samples)) + '\n' for samples,cfg,first in cases)
    process = subprocess.run([str(executable)],input=payload,text=True,capture_output=True,check=True,timeout=90)
    actual_cases = []
    current = []
    for line in process.stdout.splitlines():
        if line == 'END':
            actual_cases.append(current)
            current = []
        elif line.startswith('E '):
            values = list(map(int,line.split()[1:]))
            if len(values) != 8:
                raise AssertionError(('Malformed event',line))
            current.append((values[0],(values[1]<<32)|values[2],(values[3]<<32)|values[4],*values[5:]))
        else:
            raise AssertionError(('Unexpected C output',line))
    if current or len(actual_cases) != len(cases):
        raise AssertionError(('Missing/truncated C output',len(actual_cases),len(cases)))
    event_total = sample_total = 0
    for number,((samples,config,first),actual) in enumerate(zip(cases,actual_cases)):
        expected = reference(samples,config,first)
        if actual != expected:
            raise AssertionError((number,config,first,expected,actual))
        sample_total += len(samples)
        event_total += len(actual)
    summary = {'cases':len(cases),'samples':sample_total,'events':event_total,'status':'PASS','hardware_verified':False}
    (output/'fraction-reference.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
    print('PASS exact Fraction reference:',json.dumps(summary))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
