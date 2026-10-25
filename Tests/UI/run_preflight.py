"""Mutate generated inputs in memory; no .ioc or generated file writes."""
from pathlib import Path
import importlib.util
root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('lcd',root/'Tools/Monitor/check_ltdc_config.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
inputs=[(root/path).read_text(encoding='utf-8') for path in ('Core/Src/main.c','Core/Src/stm32f7xx_hal_msp.c','Core/Inc/main.h','Core/Src/stm32f7xx_it.c')]
mode='touchgfx' if 'X-CUBE-TOUCHGFX.4.26.1.tgfx_display_interface=' in (root/'SBCG.ioc').read_text(encoding='utf-8') else 'legacy'
assert not module.check(*inputs,mode=mode)
mutations=[(0,'pLayerCfg.ImageWidth = 480','pLayerCfg.ImageWidth = 960'),
 (0,'pLayerCfg.Alpha = 255','pLayerCfg.Alpha = 0'),
 (0,'pLayerCfg.FBStartAdress = 0xC0000000','pLayerCfg.FBStartAdress = 0x20000000'),
 (0,'  App_Init();','  MX_LTDC_Init();\n  App_Init();'),
 (0,'void MX_LTDC_Init(void)','static void MX_LTDC_Init(void)'),
 (1,'PeriphClkInitStruct.PLLSAI.PLLSAIR = 5','PeriphClkInitStruct.PLLSAI.PLLSAIR = 4'),
 (1,'GPIO_AF9_LTDC','GPIO_AF14_LTDC'),
 (1,'GPIO_SPEED_FREQ_VERY_HIGH','GPIO_SPEED_FREQ_LOW'),
 (2,'LCD_DISP_Pin GPIO_PIN_12','LCD_DISP_Pin GPIO_PIN_11'),
 (0,'GPIO_InitStruct.Pin = LCD_BL_CTRL_Pin;\n  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;',
    'GPIO_InitStruct.Pin = LCD_BL_CTRL_Pin;\n  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;'),
 (0,'HAL_GPIO_Init(LCD_DISP_GPIO_Port, &GPIO_InitStruct);','/* missing display GPIO init */')]
for slot,before,after in mutations:
    assert before in inputs[slot],before
    altered=list(inputs);altered[slot]=altered[slot].replace(before,after)
    assert module.check(*altered,mode=mode),(slot,before)
print(f'PASS LTDC {mode} preflight baseline and {len(mutations)} unsafe regeneration mutations')
