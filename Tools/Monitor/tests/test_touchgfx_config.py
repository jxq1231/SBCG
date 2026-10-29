"""Mutation fixtures for the approved U3.1 CubeMX profile; no input files are edited."""
import importlib.util
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[3]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'Tools/Monitor' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TouchGFXPreflightTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ltdc = load('check_ltdc_config')
        cls.inputs = [(ROOT / path).read_text(encoding='utf-8') for path in (
            'Core/Src/main.c', 'Core/Src/stm32f7xx_hal_msp.c',
            'Core/Inc/main.h', 'Core/Src/stm32f7xx_it.c')]

    def test_ltdc_accepts_touchgfx_irq_profile(self):
        self.assertEqual([], self.ltdc.check(*self.inputs, mode='touchgfx'))

    def test_legacy_profile_still_rejects_ltdc_interrupts(self):
        self.assertTrue(self.ltdc.check(*self.inputs))

    def test_default_legacy_profile_accepts_original_polling_snapshot(self):
        data = list(self.inputs)
        data[1] = re.sub(r'HAL_NVIC_(?:SetPriority|EnableIRQ)\(LTDC_IRQn[^;]*;', '', data[1])
        data[3] = re.sub(r'void LTDC_IRQHandler\(void\)\s*\{[^}]*\}', '', data[3])
        self.assertEqual([], self.ltdc.check(*data))

    def test_touchgfx_configuration_mutations(self):
        checker = load('check_touchgfx_config')
        data = {path: (ROOT / path).read_text(encoding='utf-8') for path in checker.INPUT_FILES}
        # The pending CubeMX "Do Not Generate Function Call" checkbox is the
        # only fixture adjustment: do not mask any other generated mismatch.
        data['Core/Src/main.c'] = data['Core/Src/main.c'].replace('  MX_TouchGFX_Process();', '')
        self.assertEqual([], checker.check(data))
        mutations = [
            ('Core/Src/main.c', '  App_Init();', '  MX_TouchGFX_Init();\n  App_Init();'),
            ('Core/Src/main.c', '  App_Process();', '  MX_TouchGFX_Process();\n  App_Process();'),
            ('Core/Src/main.c', '  MX_CRC_Init();', ''),
            ('Core/Src/main.c', '  HAL_Init();', '  HAL_Init();\n  SCB_EnableDCache();'),
            ('Core/Src/stm32f7xx_hal_msp.c', 'HAL_NVIC_SetPriority(LTDC_IRQn, 9, 0)', 'HAL_NVIC_SetPriority(LTDC_IRQn, 4, 0)'),
            ('Core/Src/stm32f7xx_it.c', 'HAL_LTDC_IRQHandler(&hltdc);', ''),
            ('Core/Inc/stm32f7xx_hal_conf.h', '#define HAL_CRC_MODULE_ENABLED', '/* CRC disabled */'),
            ('SBCG.ioc', 'tgfx_buffering_strategy=Double', 'tgfx_buffering_strategy=Single'),
            ('SBCG.ioc', 'tgfx_address2=0xC0040000', 'tgfx_address2=0xC0000000'),
            ('TouchGFX/target/generated/TouchGFXConfiguration.cpp', 'tc, 480, 272', 'tc, 800, 480'),
            ('TouchGFX/target/generated/TouchGFXConfiguration.cpp', 'static LCD16bpp display;', 'static LCD24bpp display;'),
            ('TouchGFX/target/generated/TouchGFXConfiguration.cpp', 'if (OSWrappers::isVSyncAvailable())', 'while (1)'),
            ('TouchGFX/target/generated/TouchGFXGeneratedHAL.cpp', '(void*)0xC0040000', '(void*)0xC0000000'),
            ('TouchGFX/target/generated/TouchGFXGeneratedHAL.cpp', 'NVIC_SetPriority(LTDC_IRQn, 9)', 'NVIC_SetPriority(LTDC_IRQn, 3)'),
            ('TouchGFX/target/generated/TouchGFXGeneratedHAL.cpp', 'OSWrappers::signalVSync();', ''),
            ('TouchGFX/target/generated/STM32DMA.cpp', 'static_cast<touchgfx::BlitOperations>(0)', 'static_cast<touchgfx::BlitOperations>(1)'),
            ('TouchGFX/target/generated/OSWrappers.cpp', 'vsync_sem = 1;', 'osSemaphoreRelease(vsync_sem);'),
        ]
        for path, before, after in mutations:
            with self.subTest(path=path, before=before):
                self.assertIn(before, data[path])
                altered = dict(data)
                altered[path] = altered[path].replace(before, after)
                self.assertTrue(checker.check(altered), before)

    def test_generated_process_call_requires_verified_noop_wrapper(self):
        checker = load('check_touchgfx_config')
        data = {path: (ROOT / path).read_text(encoding='utf-8') for path in checker.INPUT_FILES}
        data['Core/Src/main.c'] = data['Core/Src/main.c'].replace('  MX_TouchGFX_Process();', '')
        data['Core/Src/main.c'] = data['Core/Src/main.c'].replace('  App_Process();', '  MX_TouchGFX_Process();\n  App_Process();')
        self.assertTrue(checker.check(data))
        self.assertTrue(checker.check(data, allow_generated_process_call=True))
        data['App/UI/touchgfx_service.c'] = 'void __wrap_MX_TouchGFX_Process(void) {}\nvoid service(void) { __real_MX_TouchGFX_Process(); }'
        data['cmake/touchgfx.cmake'] = 'target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--wrap=MX_TouchGFX_Process")'
        self.assertEqual([], checker.check(data, allow_generated_process_call=True))
        data['App/UI/touchgfx_service.c'] = data['App/UI/touchgfx_service.c'].replace('void) {}', 'void) { __real_MX_TouchGFX_Process(); }')
        self.assertTrue(checker.check(data, allow_generated_process_call=True))


if __name__ == '__main__':
    unittest.main()
