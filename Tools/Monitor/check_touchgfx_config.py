"""Read-only U3.1 preflight of the generated TouchGFX 4.26.1 No OS profile.

Generated initialization must remain deferred to the SDRAM-ready application
path. This does not validate physical display operation or change CubeMX files.
"""
import argparse
import importlib.util
from pathlib import Path
import re

_spec = importlib.util.spec_from_file_location('sbcg_ltdc_preflight', Path(__file__).with_name('check_ltdc_config.py'))
ltdc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ltdc)

INPUT_FILES = (
    'SBCG.ioc', 'Core/Src/main.c', 'Core/Src/stm32f7xx_hal_msp.c',
    'Core/Inc/main.h', 'Core/Src/stm32f7xx_it.c', 'Core/Inc/stm32f7xx_hal_conf.h',
    'TouchGFX/App/app_touchgfx.c', 'TouchGFX/App/app_touchgfx.h',
    'TouchGFX/target/generated/TouchGFXConfiguration.cpp',
    'TouchGFX/target/generated/TouchGFXGeneratedHAL.cpp',
    'TouchGFX/target/generated/OSWrappers.cpp',
    'TouchGFX/target/generated/STM32DMA.cpp',
)


def uncomment(source):
    """Ignore comments so disabled/example code cannot satisfy a requirement."""
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)


def check(sources, allow_generated_process_call=False):
    """Return violations from a mapping of repository paths to source text."""
    missing = [path for path in INPUT_FILES if path not in sources]
    if missing:
        return [f'missing required generated input: {path}' for path in missing]
    data = {path: uncomment(value) if path.endswith(('.c', '.h', '.cpp')) else value
            for path, value in sources.items()}
    main = data['Core/Src/main.c']
    msp = data['Core/Src/stm32f7xx_hal_msp.c']
    errors = ltdc.check(main, msp, data['Core/Inc/main.h'], data['Core/Src/stm32f7xx_it.c'], mode='touchgfx')

    def require(source, pattern, message):
        if not re.search(pattern, source, re.S):
            errors.append(message)

    try:
        entry = ltdc.function(main, 'main')
        for name in ('MX_TouchGFX_Init', 'MX_TouchGFX_Process'):
            if re.search(r'\b' + name + r'\s*\(', entry):
                if name != 'MX_TouchGFX_Process' or not allow_generated_process_call:
                    errors.append(f'main must not automatically call {name}; application owns SDRAM-ready scheduling')
        if len(re.findall(r'\bMX_TouchGFX_Process\s*\(', entry)) > 1:
            errors.append('at most one CubeMX-generated process call is permitted')
        require(entry, r'\bMX_CRC_Init\s*\(\s*\)', 'CRC must initialize before App_Init')
        if entry.find('MX_CRC_Init') > entry.find('App_Init'):
            errors.append('CRC initialization must precede App_Init')
        crc = ltdc.function(main, 'MX_CRC_Init')
        require(crc, r'hcrc\.Instance\s*=\s*CRC\s*;', 'CRC instance must be enabled')
        require(crc, r'HAL_CRC_Init\s*\(\s*&hcrc\s*\)', 'CRC HAL initialization is required')
    except ValueError as error:
        errors.append(str(error))
    if allow_generated_process_call:
        wrapper = data.get('App/UI/touchgfx_service.c', '')
        fragment = data.get('cmake/touchgfx.cmake', '')
        try:
            body = ltdc.function(wrapper, '__wrap_MX_TouchGFX_Process').strip()
            if body not in ('', 'return;'):
                errors.append('generated process wrapper must be a no-op')
        except ValueError as error:
            errors.append(str(error))
        require(wrapper, r'\b__real_MX_TouchGFX_Process\s*\(', 'foreground service must invoke the real generated process entry')
        require(fragment, r'target_link_options\([^)]*LINKER:--wrap=MX_TouchGFX_Process', 'user build must always wrap the generated process entry')
    require(msp, r'__HAL_RCC_CRC_CLK_ENABLE\s*\(\s*\)', 'CRC clock must be enabled by generated MSP')
    require(data['Core/Inc/stm32f7xx_hal_conf.h'], r'#define\s+HAL_CRC_MODULE_ENABLED\b', 'HAL CRC module must be enabled')
    for irq, priority in (('SAI2', 4), ('DMA2_Stream7', 4), ('USART1', 5)):
        require(main + msp, rf'HAL_NVIC_SetPriority\(\s*{irq}_IRQn,\s*{priority},\s*0\)', f'{irq} priority must remain {priority}/0')
    if re.search(r'\bSCB_Enable[ID]Cache\s*\(', main):
        errors.append('U3.1 assumes CPU I/D caches remain disabled')

    ioc = dict(line.split('=', 1) for line in data['SBCG.ioc'].splitlines() if '=' in line)
    prefix = 'STMicroelectronics.X-CUBE-TOUCHGFX.4.26.1.'
    for key, value in {'tgfx_address1': '0xC0000000', 'tgfx_address2': '0xC0040000',
                       'tgfx_buffering_strategy': 'Double', 'tgfx_location': 'By Address',
                       'tgfx_display_interface': 'disp_ltdc', 'tgfx_vsync': 'vsync_ltdc'}.items():
        if ioc.get(prefix + key) != value:
            errors.append(f'CubeMX TouchGFX {key} must be {value}')
    for key, value in ioc.items():
        if ('DMA2D' in key and value.lower() not in ('false', 'no', 'disabled')) or value == 'DMA2D':
            errors.append('DMA2D must remain disabled for software rendering')
        if key.startswith('FREERTOS.') or value == 'FREERTOS':
            errors.append('U3.1 requires No OS')

    config = data['TouchGFX/target/generated/TouchGFXConfiguration.cpp']
    require(config, r'static\s+LCD16bpp\s+display\s*;', 'TouchGFX framebuffer format must be RGB565')
    require(config, r'TouchGFXHAL\s+hal\s*\(\s*dma,\s*display,\s*tc,\s*480,\s*272\s*\)', 'TouchGFX resolution must be 480x272')
    require(config, r'if\s*\(\s*OSWrappers::isVSyncAvailable\s*\(\s*\)\s*\)', 'TouchGFX No OS task entry must return without VSYNC')
    require(config, r'hal\.backPorchExited\s*\(\s*\)', 'TouchGFX foreground must consume LTDC VSYNC')
    hal = data['TouchGFX/target/generated/TouchGFXGeneratedHAL.cpp']
    require(hal, r'setFrameBufferStartAddresses\(\(void\*\)0xC0000000,\s*\(void\*\)0xC0040000,\s*\(void\*\)0\)', 'TouchGFX must use the two approved SDRAM framebuffer addresses')
    require(hal, r'NVIC_SetPriority\(\s*LTDC_IRQn,\s*9\)', 'TouchGFX LTDC priority must remain 9')
    require(hal, r'NVIC_EnableIRQ\(\s*LTDC_IRQn\)', 'TouchGFX must enable LTDC IRQ')
    require(hal, r'void\s+HAL_LTDC_LineEventCallback\s*\(', 'generated LTDC line callback is required')
    require(hal, r'OSWrappers::signalVSync\s*\(\s*\)', 'LTDC callback must publish VSYNC to foreground')
    require(hal, r'HAL_LTDC_ProgramLineEvent\s*\(', 'LTDC line events must be rearmed')
    wrappers = data['TouchGFX/target/generated/OSWrappers.cpp']
    require(wrappers, r'static\s+volatile\s+uint32_t\s+vsync_sem', 'No OS VSYNC handoff must use generated volatile state')
    require(wrappers, r'vsync_sem\s*=\s*1\s*;', 'No OS VSYNC signal must set foreground flag')
    if re.search(r'\b(?:osSemaphore\w*|xSemaphore\w*|vTask\w*|osThread\w*)\s*\(', wrappers):
        errors.append('OSWrappers must remain the generated No OS implementation')
    dma = data['TouchGFX/target/generated/STM32DMA.cpp']
    require(dma, r'return\s+static_cast<touchgfx::BlitOperations>\(0\)\s*;', 'STM32DMA must advertise zero hardware blit capabilities')
    if re.search(r'\b(?:HAL_DMA2D_\w+|__HAL_RCC_DMA2D_CLK_ENABLE)\s*\(', main + msp + dma):
        errors.append('DMA2D initialization/usage is forbidden in U3.1')
    app = data['TouchGFX/App/app_touchgfx.c']
    app_header = data['TouchGFX/App/app_touchgfx.h']
    for name, forward in (('MX_TouchGFX_Init', 'touchgfx_init'), ('MX_TouchGFX_Process', 'touchgfx_taskEntry')):
        require(app_header, rf'void\s+{name}\s*\(void\)\s*;', f'{name} must be declared globally')
        try:
            body = ltdc.function(app, name)
            require(body, rf'\b{forward}\s*\(', f'{name} must forward to generated engine entry')
        except ValueError as error:
            errors.append(str(error))
        if re.search(r'\bstatic\s+void\s+' + name, app):
            errors.append(f'{name} must be globally callable by the application')
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--allow-generated-process-call', action='store_true',
                        help='require the user-owned no-op linker wrapper for a generated main-loop call')
    args = parser.parse_args()
    try:
        paths = INPUT_FILES + (('App/UI/touchgfx_service.c', 'cmake/touchgfx.cmake')
                               if args.allow_generated_process_call else ())
        errors = check({path: (args.root / path).read_text(encoding='utf-8') for path in paths},
                       allow_generated_process_call=args.allow_generated_process_call)
    except OSError as error:
        errors = [str(error)]
    for error in errors:
        print('FAIL:', error)
    if not errors:
        print('PASS TouchGFX U3.1: No OS, LTDC VSYNC IRQ 9/0, RGB565 double SDRAM buffers, software drawing, CRC, deferred application entry')
    return bool(errors)


if __name__ == '__main__':
    raise SystemExit(main())
