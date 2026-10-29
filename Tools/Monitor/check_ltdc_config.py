"""Read-only preflight of this project's generated 480x272 RGB565 LTDC configuration.

Eight unused low color GPIO are intentionally deferred by the user. This checker
never edits CubeMX inputs, generated code, pin modes or clocks.
"""
from pathlib import Path
import argparse
import re

INIT = {'HorizontalSync':'40','VerticalSync':'9','AccumulatedHBP':'53','AccumulatedVBP':'11',
        'AccumulatedActiveW':'533','AccumulatedActiveH':'283','TotalWidth':'565','TotalHeigh':'285',
        'HSPolarity':'LTDC_HSPOLARITY_AL','VSPolarity':'LTDC_VSPOLARITY_AL',
        'DEPolarity':'LTDC_DEPOLARITY_AL','PCPolarity':'LTDC_PCPOLARITY_IPC'}
LAYER = {'WindowX0':'0','WindowX1':'480','WindowY0':'0','WindowY1':'272',
         'PixelFormat':'LTDC_PIXEL_FORMAT_RGB565','Alpha':'255','Alpha0':'0',
         'BlendingFactor1':'LTDC_BLENDING_FACTOR1_CA','BlendingFactor2':'LTDC_BLENDING_FACTOR2_CA',
         'FBStartAdress':'0xC0000000','ImageWidth':'480','ImageHeight':'272'}
PINS = {'GPIOJ':{2,3,4,5,6,9,10,11,15},'GPIOK':{0,1,2,4,5,6,7},'GPIOI':{9,10,14},'GPIOG':{12}}


def function(source, name):
    """Extract a generated function body by balanced braces; absent/ambiguous fails."""
    matches=list(re.finditer(r'(?m)^(?:static\s+)?(?:void|int)\s+'+re.escape(name)+r'\s*\([^)]*\)\s*\{',source))
    if len(matches)!=1:
        raise ValueError(f'expected one {name} definition')
    start=matches[0].end(); depth=1
    for i in range(start,len(source)):
        depth += (source[i]=='{')-(source[i]=='}')
        if not depth:
            return source[start:i]
    raise ValueError(f'unclosed {name} definition')


def assignments(source):
    return dict(re.findall(r'([A-Za-z_]\w*(?:\.\w+)*)\s*=(?!=)\s*([^;]+);',source))


def check(main, msp, header, interrupts, mode='legacy'):
    """Return precise violations without modifying any source."""
    errors=[]
    try:
        entry=function(main,'main'); init=function(main,'MX_LTDC_Init'); gpio=function(main,'MX_GPIO_Init')
        ltdc_msp=function(msp,'HAL_LTDC_MspInit')
    except ValueError as error:
        return [str(error)]
    if re.search(r'\bMX_LTDC_Init\s*\(',entry):
        errors.append('main must not automatically call MX_LTDC_Init before SDRAM READY')
    if re.search(r'\bstatic\s+void\s+MX_LTDC_Init',main) or not re.search(r'\bvoid\s+MX_LTDC_Init\s*\(void\)\s*;',header):
        errors.append('MX_LTDC_Init must be global and declared in main.h')
    values=assignments(init)
    for prefix, expected in [('hltdc.Init.',INIT),('pLayerCfg.',LAYER)]:
        for key,value in expected.items():
            if values.get(prefix+key,'').strip()!=value:
                errors.append(f'{prefix}{key} must be {value}')
    if len(re.findall(r'HAL_LTDC_ConfigLayer\s*\(',init))!=1 or not re.search(r'HAL_LTDC_ConfigLayer\s*\(&hltdc,\s*&pLayerCfg,\s*0\)',init):
        errors.append('exactly one layer (HAL layer 0) is required')
    clock=assignments(ltdc_msp)
    for key,value in {'PeriphClockSelection':'RCC_PERIPHCLK_LTDC','PLLSAI.PLLSAIN':'192',
                      'PLLSAI.PLLSAIR':'5','PLLSAIDivR':'RCC_PLLSAIDIVR_4'}.items():
        if clock.get('PeriphClkInitStruct.'+key,'').strip()!=value:
            errors.append(f'LTDC clock {key} must be {value}')
    configured={}; state={}
    token=re.compile(r'GPIO_InitStruct\.(\w+)\s*=\s*([^;]+);|HAL_GPIO_Init\((GPIO\w+),\s*&GPIO_InitStruct\)')
    for match in token.finditer(ltdc_msp):
        field,value,port=match.groups()
        if field:
            state[field]=value.strip()
        else:
            for pin in map(int,re.findall(r'GPIO_PIN_(\d+)',state.get('Pin',''))):
                configured[(port,pin)]=dict(state)
    if set(configured)!={(port,pin) for port,pins in PINS.items() for pin in pins}:
        errors.append('LTDC pins must match the board fixed RGB565 mapping (20 signals)')
    for (port,pin),settings in configured.items():
        expected={'Mode':'GPIO_MODE_AF_PP','Pull':'GPIO_NOPULL','Speed':'GPIO_SPEED_FREQ_VERY_HIGH',
                  'Alternate':'GPIO_AF9_LTDC' if (port,pin)==('GPIOG',12) else 'GPIO_AF14_LTDC'}
        for key,value in expected.items():
            if settings.get(key)!=value:
                errors.append(f'{port}{pin} {key} must be {value}')
    for label,port,pin in [('LCD_DISP','GPIOI',12),('LCD_BL_CTRL','GPIOK',3)]:
        if not re.search(r'#define\s+'+label+r'_Pin\s+GPIO_PIN_'+str(pin)+r'\b',header) or not re.search(r'#define\s+'+label+r'_GPIO_Port\s+'+port+r'\b',header):
            errors.append(f'{label} mapping must be {port} pin {pin}')
        if not re.search(r'HAL_GPIO_WritePin\('+label+r'_GPIO_Port,\s*'+label+r'_Pin,\s*GPIO_PIN_RESET\)',gpio):
            errors.append(f'{label} must start LOW')
        block=re.search(r'GPIO_InitStruct\.Pin\s*=\s*'+label+r'_Pin;(?P<config>.*?)HAL_GPIO_Init\('
                        +label+r'_GPIO_Port,\s*&GPIO_InitStruct\)',gpio,re.S)
        settings=assignments(block.group('config')) if block else {}
        for key,value in {'Mode':'GPIO_MODE_OUTPUT_PP','Pull':'GPIO_NOPULL','Speed':'GPIO_SPEED_FREQ_LOW'}.items():
            if settings.get('GPIO_InitStruct.'+key,'').strip()!=value:
                errors.append(f'{label} GPIO {key} must be {value} with an explicit initialization call')
    if mode == 'legacy':
        if re.search(r'HAL_NVIC_EnableIRQ\(\s*LTDC',msp) or re.search(r'void\s+LTDC\w*_IRQHandler',interrupts):
            errors.append('LTDC NVIC must remain disabled for polling mode')
    elif mode == 'touchgfx':
        for pattern, message in [
            (r'HAL_NVIC_SetPriority\(\s*LTDC_IRQn,\s*9,\s*0\)', 'LTDC priority must be 9/0'),
            (r'HAL_NVIC_EnableIRQ\(\s*LTDC_IRQn\)', 'LTDC interrupt must be enabled')]:
            if not re.search(pattern, ltdc_msp):
                errors.append(message)
        try:
            handler = function(interrupts, 'LTDC_IRQHandler')
            if not re.search(r'HAL_LTDC_IRQHandler\(\s*&hltdc\s*\)', handler):
                errors.append('LTDC_IRQHandler must dispatch HAL_LTDC_IRQHandler(&hltdc)')
        except ValueError as error:
            errors.append(str(error))
    else:
        errors.append(f'unsupported LTDC preflight mode: {mode}')
    return errors


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[2])
    parser.add_argument('--mode', choices=('legacy', 'touchgfx'), default='legacy')
    args=parser.parse_args()
    root=args.root
    paths=['Core/Src/main.c','Core/Src/stm32f7xx_hal_msp.c','Core/Inc/main.h','Core/Src/stm32f7xx_it.c']
    errors=check(*( (root/path).read_text(encoding='utf-8') for path in paths), mode=args.mode)
    for error in errors:
        print('FAIL:',error)
    if not errors:
        print(f'PASS LTDC: 480x272 RGB565, 9.6MHz, fixed 20 pins, controls LOW, deferred global init, {args.mode}')
        print('NOTE: 8 low color GPIO intentionally deferred; physical panel acceptance still required')
    return bool(errors)


if __name__=='__main__':
    raise SystemExit(main())
