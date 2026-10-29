"""Reproduce the real startup fault and protect the proposed MPU correction."""
from pathlib import Path
import sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import check_sdram_mpu as m
BASE='''void MPU_Config(void) {
MPU_InitStruct.Enable = MPU_REGION_ENABLE;
MPU_InitStruct.Number = MPU_REGION_NUMBER0;
MPU_InitStruct.BaseAddress = 0x0;
MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
MPU_InitStruct.SubRegionDisable = 0x87;
MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
HAL_MPU_ConfigRegion(&MPU_InitStruct);
MPU_InitStruct.Number = MPU_REGION_NUMBER1;
MPU_InitStruct.BaseAddress = 0xC0000000;
MPU_InitStruct.Size = MPU_REGION_SIZE_8MB;
MPU_InitStruct.SubRegionDisable = 0;
MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
HAL_MPU_ConfigRegion(&MPU_InitStruct);
'''
CORRECTION='''MPU_InitStruct.Number = MPU_REGION_NUMBER2;
MPU_InitStruct.BaseAddress = 0xA0000000;
MPU_InitStruct.Size = MPU_REGION_SIZE_4KB;
MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;
HAL_MPU_ConfigRegion(&MPU_InitStruct);
}'''
class SdramMpuTests(unittest.TestCase):
 def test_actual_failure_before_app(self):
  layout=m.regions(BASE+'}')
  self.assertEqual(m.effective(layout,0xA0000140)['AccessPermission'],'MPU_REGION_NO_ACCESS')
  self.assertFalse(all(ok for ok,_ in m.check(BASE+'}')))
 def test_device_region_correction(self):self.assertTrue(all(ok for ok,_ in m.check(BASE+CORRECTION)))
 def test_wrong_region_base_fails(self):self.assertFalse(all(ok for ok,_ in m.check(BASE+CORRECTION.replace('0xA0000000','0xA0001000'))))
 def test_broad_window_rejected(self):self.assertFalse(all(ok for ok,_ in m.check(BASE+CORRECTION.replace('MPU_REGION_SIZE_4KB','MPU_REGION_SIZE_8MB'))))
 def test_cacheable_device_rejected(self):self.assertFalse(all(ok for ok,_ in m.check(BASE+CORRECTION.replace('HAL_MPU_ConfigRegion(&MPU_InitStruct);','MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;\nHAL_MPU_ConfigRegion(&MPU_InitStruct);'))))
