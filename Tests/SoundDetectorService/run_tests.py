"""Run real detector service tests against synthetic PCM, without board access.

O0/O2分别运行真实TOA估算器的原服务/批量回归，以及只替换估算器的完整窗边界测试。
边界替身记录是否调用和完整256点内容，并注入数值边界验证发布前的uint64转换和长日志。
所有证据写入新的带时间目录，不依赖未初始化栈值，
--phase只标记本次RED/GREEN证据，不据源文件是否存在推断测试结果。
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=('RED', 'GREEN'), default='GREEN')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = root / 'Logs/Detector/TOA_Boundary_Fix' / f'{args.phase}_{time.time_ns()}'
    out.mkdir(parents=True)
    print(f'Evidence: {out}', flush=True)
    compiler = shutil.which('gcc')
    if compiler is None:
        print('gcc not found', file=sys.stderr)
        return 2
    source = root / 'App/Detector/sound_detector_service.c'
    classifier = root / 'App/Detector/event_classifier.c'
    detector = root / 'App/Detector/sound_detector.c'
    estimator = root / 'App/Detector/toa_estimator.c'
    scenarios = ('partial-tail', 'complete-boundary', 'cross-block-left',
                 'rejected-recovery', 'missing-recovery', 'large-frame-overflow',
                 'long-decimal', 'invalid-numbers')
    result = 0
    for option in ('-O0', '-O2'):
        for label, filename, real_toa, instrument in (
                ('toa-boundaries', 'test_toa_boundaries.c', False, False),
                ('service', 'test_service.c', True, False),
                ('batch', 'test_batch_service.c', True, True)):
            exe = out / f'{label}_{option[1:]}.exe'
            command = [compiler, '-std=c11', option, '-Wall', '-Wextra', '-Werror', '-pedantic']
            if instrument:
                command.append('-finstrument-functions')
            command += ['-I', str(root / 'App'),
                        str(root / 'Tests/SoundDetectorService' / filename),
                        str(source), str(detector), str(classifier)]
            if real_toa:
                command.append(str(estimator))
            command += ['-o', str(exe)]
            compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
            build_log = ('COMMAND: ' + subprocess.list2cmdline(command) + '\n' +
                         compiled.stdout + compiled.stderr + f'\nexit={compiled.returncode}\n')
            (out / f'{label}_{option[1:]}_build.log').write_text(build_log, encoding='utf-8')
            if compiled.returncode:
                print(build_log, flush=True)
                result = result or compiled.returncode
                continue
            for scenario in scenarios if label == 'toa-boundaries' else ('all',):
                run_command = [str(exe)]
                if label == 'toa-boundaries':
                    run_command.append(scenario)
                ran = subprocess.run(run_command, capture_output=True, text=True, timeout=30)
                log = ('COMMAND: ' + subprocess.list2cmdline(run_command) + '\n' +
                       ran.stdout + ran.stderr + f'\nexit={ran.returncode}\n')
                (out / f'{label}_{option[1:]}_{scenario}.log').write_text(log, encoding='utf-8')
                print(f'{label} {option} {scenario}: exit={ran.returncode}\n' +
                      ran.stdout + ran.stderr, flush=True)
                result = result or ran.returncode
            if label == 'toa-boundaries':
                handoff = out / f'toa-handoff_{option[1:]}.exe'
                handoff_command = [compiler,'-std=c11',option,'-Wall','-Wextra','-Werror','-pedantic',
                    '-DSBCG_USE_SOUND_TIME=1','-I',str(root/'App'),
                    str(root/'Tests/SoundDetectorService/test_toa_handoff.c'),
                    str(source),str(detector),str(classifier),'-o',str(handoff)]
                built_handoff = subprocess.run(handoff_command,capture_output=True,text=True,timeout=30)
                (out / f'toa-handoff_{option[1:]}_build.log').write_text(
                    ('COMMAND: ' + subprocess.list2cmdline(handoff_command) + '\n' +
                     built_handoff.stdout + built_handoff.stderr +
                     f'\nexit={built_handoff.returncode}\n'), encoding='utf-8')
                if built_handoff.returncode:
                    print(built_handoff.stdout + built_handoff.stderr, flush=True)
                    result = result or built_handoff.returncode
                    continue
                ran_handoff = subprocess.run([str(handoff)],capture_output=True,text=True,timeout=30)
                (out / f'toa-handoff_{option[1:]}.log').write_text(
                    ('COMMAND: ' + subprocess.list2cmdline([str(handoff)]) + '\n' +
                     ran_handoff.stdout + ran_handoff.stderr +
                     f'\nexit={ran_handoff.returncode}\n'), encoding='utf-8')
                print(ran_handoff.stdout + ran_handoff.stderr, flush=True)
                result = result or ran_handoff.returncode
    return result


if __name__ == '__main__':
    sys.exit(main())
