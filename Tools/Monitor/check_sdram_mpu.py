"""Read-only preflight: verify CubeMX MPU permits FMC registers AND SDRAM data."""
import argparse,re
from pathlib import Path

def regions(source):
    """Expand inherited MPU_InitStruct assignments at each HAL region write."""
    start=re.search(r'void\s+MPU_Config\s*\(void\)\s*\{',source)
    if not start:raise ValueError('MPU_Config definition missing')
    state={};result=[]
    pattern=r'MPU_InitStruct\.(\w+)\s*=\s*([^;]+);|HAL_MPU_ConfigRegion\(&MPU_InitStruct\);'
    for token in re.finditer(pattern,source[start.end():]):
        if token.group(1):state[token.group(1)]=token.group(2).strip()
        else:result.append(dict(state))
    return result

def effective(layout,address):
    """Highest numbered enabled matching region wins; disabled subregions do not match."""
    matches=[]
    for r in layout:
        if r.get('Enable')!='MPU_REGION_ENABLE':continue
        size=re.fullmatch(r'MPU_REGION_SIZE_(\d+)(B|KB|MB|GB)',r['Size'])
        if not size:raise ValueError('unsupported region size')
        count=int(size[1])*{'B':1,'KB':1024,'MB':1024**2,'GB':1024**3}[size[2]]
        base=int(r['BaseAddress'],0)
        if not base<=address<base+count:continue
        if count>=256 and int(r.get('SubRegionDisable','0'),0)&(1<<((address-base)//(count//8))):continue
        matches.append((int(r['Number'].removeprefix('MPU_REGION_NUMBER')),r))
    return max(matches,key=lambda pair:pair[0])[1] if matches else None

def check(source):
    """Require the approved 4KB Device aperture and 8MB noncache Normal SDRAM window."""
    layout=regions(source);checks=[]
    for address,tex,buffer,share in [(0xA0000000,'MPU_TEX_LEVEL0','MPU_ACCESS_BUFFERABLE','MPU_ACCESS_SHAREABLE'),
             (0xA0000140,'MPU_TEX_LEVEL0','MPU_ACCESS_BUFFERABLE','MPU_ACCESS_SHAREABLE'),
             (0xA0000FFF,'MPU_TEX_LEVEL0','MPU_ACCESS_BUFFERABLE','MPU_ACCESS_SHAREABLE'),
             (0xC0000000,'MPU_TEX_LEVEL1','MPU_ACCESS_NOT_BUFFERABLE','MPU_ACCESS_NOT_SHAREABLE'),
             (0xC07FFFFF,'MPU_TEX_LEVEL1','MPU_ACCESS_NOT_BUFFERABLE','MPU_ACCESS_NOT_SHAREABLE')]:
        actual=effective(layout,address)
        expected={'AccessPermission':'MPU_REGION_FULL_ACCESS','DisableExec':'MPU_INSTRUCTION_ACCESS_DISABLE',
                  'IsCacheable':'MPU_ACCESS_NOT_CACHEABLE','TypeExtField':tex,'IsBufferable':buffer,'IsShareable':share}
        checks.append((bool(actual) and all(actual.get(k)==v for k,v in expected.items()),
                       f'0x{address:08X} full-access XN '+('Device' if tex=='MPU_TEX_LEVEL0' else 'Normal noncache')))
    for address in [0xA0001000,0xC0800000]:
        actual=effective(layout,address)
        checks.append((bool(actual) and actual.get('AccessPermission')=='MPU_REGION_NO_ACCESS',
                       f'0x{address:08X} remains protected outside approved windows'))
    return checks

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--main',type=Path,default=Path(__file__).resolve().parents[2]/'Core/Src/main.c')
    args=parser.parse_args()
    checks=check(args.main.read_text(encoding='utf-8'))
    for ok,message in checks:print(('PASS: ' if ok else 'FAIL: ')+message)
    return 0 if all(ok for ok,_ in checks) else 1
if __name__=='__main__':raise SystemExit(main())
