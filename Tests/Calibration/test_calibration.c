#include "Calibration/calibration.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#undef assert
#define assert(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void)
{
    CalibrationSnapshot c;
    Calibration_Init();Calibration_Get(&c);
    assert(!c.calibrated && c.offset_mm==0 && c.version==0U && c.reference_mm==500);
    assert(Calibration_Apply(480)==480);
    assert(Calibration_SetFromMeasurement(485));
    Calibration_Get(&c);assert(c.calibrated && c.offset_mm==15 && c.version==1U);
    assert(Calibration_Apply(485)==500 && Calibration_Apply(600)==615);
    assert(Calibration_SetFromMeasurement(470));
    Calibration_Get(&c);assert(c.offset_mm==30 && c.version==2U);
    assert(!Calibration_SetFromMeasurement(-1));
    assert(!Calibration_SetFromMeasurement(10001));
    assert(!Calibration_SetFromMeasurement(1600)); /* -1100mm beyond bound */
    Calibration_Get(&c);assert(c.offset_mm==30 && c.version==2U);
    Calibration_Reset();Calibration_Get(&c);
    assert(!c.calibrated && c.offset_mm==0 && c.version==0U);
    puts("PASS calibration: 0.5m offset/version/apply/bounds/reset");
    return 0;
}
