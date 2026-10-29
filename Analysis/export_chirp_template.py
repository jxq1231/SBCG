"""从冻结WAV逐点导出只读C模板；不播放、不改WAV或硬件配置。"""
import argparse
import hashlib
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT/'Data/Audio/chirp_2-6khz_50ms_template.wav'
TARGET = ROOT/'App/Detector/chirp_template.c'
SHA256 = 'c37114332a698910e12ac855b47b566df229a3c7ec01414371e32969da3e084d'


def rendered():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != SHA256:
        raise ValueError('Source WAV differs from frozen template; do not silently replace its identity')
    with wave.open(str(SOURCE), 'rb') as source:
        if (source.getnchannels(),source.getsampwidth(),source.getframerate(),source.getnframes(),source.getcomptype()) != (1,2,48000,2400,'NONE'):
            raise ValueError('Unexpected template WAV format')
        samples = struct.unpack('<2400h',source.readframes(2400))
    rows = ['    '+','.join(map(str,samples[i:i+16]))+',' for i in range(0,len(samples),16)]
    return '''/**
 * @file        chirp_template.c
 * @brief       保存冻结50ms扫频WAV的只读PCM16模板及完整文件SHA256。
 * @details     本文件由Analysis/export_chirp_template.py导出，逐样本保留原WAV数值。
 * @note        更换模板须同步版本和测试；不改CubeMX、时钟、DMA或现有板端行为。
 */
#include "chirp_template.h"

const int16_t ChirpTemplate_PCM[CHIRP_TEMPLATE_FRAMES]={
'''+ '\n'.join(rows)+'''
};
const char ChirpTemplate_SourceSha256[65]="'''+SHA256+'";\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true',help='仅检查导出源码与冻结WAV是否一致')
    args = parser.parse_args()
    expected = rendered()
    if args.check:
        if TARGET.read_text(encoding='utf-8') != expected:
            raise ValueError('C template differs from frozen WAV export')
    else:
        TARGET.write_text(expected,encoding='utf-8')
    print(f'PASS 2400 PCM16 samples; source_sha256={SHA256}')


if __name__ == '__main__':
    main()
