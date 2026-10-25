"""合成、检查声波测距候选音频；不播放音频，不连接或更改开发板。"""
import argparse
import hashlib
import json
import wave
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = ROOT / 'Data' / 'Audio'
RATE_HZ = 48000
EVENT_FRAMES = 2400
FADE_FRAMES = 48
TEMPLATE = 'chirp_2-6khz_50ms_template.wav'
PLAYBACK = 'standard_chirp_2-6khz_10x_20s.wav'


def waveform(start_hz, end_hz):
    """以显式相位公式合成50ms信号，两端各48点余弦渐变，峰值不超过半幅。"""
    t_s = np.arange(EVENT_FRAMES, dtype=np.float64) / RATE_HZ
    slope_hz_s = (end_hz - start_hz) / (EVENT_FRAMES / RATE_HZ)
    phase_rad = 2 * np.pi * (start_hz * t_s + 0.5 * slope_hz_s * t_s**2)
    envelope = np.ones(EVENT_FRAMES)
    ramp = 0.5 - 0.5 * np.cos(np.pi * np.arange(FADE_FRAMES) / (FADE_FRAMES - 1))
    envelope[:FADE_FRAMES] = ramp
    envelope[-FADE_FRAMES:] = ramp[::-1]
    return np.rint(0.5 * 32767 * envelope * np.cos(phase_rad)).astype('<i2')


def expected_files():
    """模板无前导静音；20秒文件在1、3、…、19秒放入完全相同的模板。"""
    template = waveform(2000, 6000)
    playback = np.zeros(20 * RATE_HZ, dtype='<i2')
    for start_s in range(1, 20, 2):
        begin = start_s * RATE_HZ
        playback[begin:begin + EVENT_FRAMES] = template
    return {TEMPLATE: template, PLAYBACK: playback}


def metadata(path):
    """读取落盘WAV的实际格式、大小和完整文件散列。"""
    with wave.open(str(path), 'rb') as source:
        result = dict(channels=source.getnchannels(), sample_width_bytes=source.getsampwidth(),
                      sample_rate_hz=source.getframerate(), frames=source.getnframes(),
                      duration_s=source.getnframes() / source.getframerate(),
                      compression=source.getcomptype())
    result.update(byte_count=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    return result


def ambiguity_measure(samples):
    """相同50ms窗下的理想自相关指标；只衡量周期歧义，不是识别概率。"""
    values = samples.astype(np.float64)
    correlation = np.correlate(values, values, mode='full')[len(values) - 1:]
    return float(np.max(np.abs(correlation[24:])) / correlation[0])


def expected_manifest():
    return {
        'status': 'candidate_assets_only_not_hardware_validated',
        'origin': 'locally_synthesized_from_formula_not_extracted_from_download',
        'sample_rate_hz': RATE_HZ, 'format': 'mono signed PCM16 little-endian WAV',
        'start_frequency_hz': 2000, 'end_frequency_hz_at_50ms_exclusive': 6000,
        'event_frames': EVENT_FRAMES, 'event_duration_ms': 50, 'fade_frames_each_edge': FADE_FRAMES,
        'peak_full_scale_limit': 0.5, 'playback_event_start_frames': [RATE_HZ * s for s in range(1, 20, 2)],
        'template_origin': 'frame zero; first sample is zero due to fade, not an energy-threshold onset',
        'ideal_autocorrelation': {
            'metric': 'max_abs_R_lag_ge_24_samples_div_R_zero; guard=0.5ms; no noise/channel model',
            'chirp_2_6khz': ambiguity_measure(waveform(2000, 6000)),
            'comparison_3khz_tone_same_duration_and_envelope': ambiguity_measure(waveform(3000, 3000)),
            'recognition_probability': None,
        },
        'files': {name: metadata(DESTINATION / name) for name in expected_files()},
    }


def verify():
    """逐样本核对两个合成文件，独立读取格式并验证下载原件散列。"""
    for name, expected in expected_files().items():
        path = DESTINATION / name
        with wave.open(str(path), 'rb') as source:
            if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (1, 2, RATE_HZ, 'NONE'):
                raise ValueError(f'Unexpected WAV format: {name}')
            actual = np.frombuffer(source.readframes(source.getnframes()), dtype='<i2')
        if not np.array_equal(actual, expected):
            raise ValueError(f'Unexpected PCM samples: {name}')
        if np.max(np.abs(actual.astype(np.int32))) > 16384:
            raise ValueError(f'Unexpected clipping or gain: {name}')
    manifest = json.loads((DESTINATION / 'candidate_manifest.json').read_text(encoding='utf-8'))
    if manifest != expected_manifest():
        raise ValueError('Candidate manifest differs from actual files or recipe')
    provenance = json.loads((DESTINATION / 'reference_source.json').read_text(encoding='utf-8'))
    downloaded = DESTINATION / provenance['filename']
    info = metadata(downloaded)
    if info['sha256'] != provenance['sha256'] or info['byte_count'] != provenance['byte_count']:
        raise ValueError('Downloaded original differs from recorded provenance')
    print(json.dumps({'verification': 'PASS', 'generated': manifest['files'],
                      'reference': info, 'ideal_autocorrelation': manifest['ideal_autocorrelation']}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='只检查已有文件，不覆盖文件')
    args = parser.parse_args()
    if not args.check:
        DESTINATION.mkdir(parents=True, exist_ok=True)
        for name, samples in expected_files().items():
            with wave.open(str(DESTINATION / name), 'wb') as target:
                target.setnchannels(1)
                target.setsampwidth(2)
                target.setframerate(RATE_HZ)
                target.writeframes(samples.tobytes())
        (DESTINATION / 'candidate_manifest.json').write_text(
            json.dumps(expected_manifest(), ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    verify()


if __name__ == '__main__':
    main()
