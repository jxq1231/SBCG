/**
 * @file        calibration.c
 * @brief       0.5m单点标定实现。
 * @details     单点偏移，版本递增；不持久化、不访问硬件。
 * @note        仅前台串行；失败不改已有标定。
 */
#include "calibration.h"
#include <stddef.h>
#include <string.h>

static CalibrationSnapshot calibration;

void Calibration_Init(void)
{
    memset(&calibration,0,sizeof calibration);
    calibration.reference_mm=CALIBRATION_REFERENCE_MM;
}

bool Calibration_SetFromMeasurement(int32_t raw_mm)
{
    if(raw_mm<0 || raw_mm>10000)return false;
    const int32_t offset=CALIBRATION_REFERENCE_MM-raw_mm;
    if(offset>CALIBRATION_MAX_ABS_OFFSET_MM || offset< -CALIBRATION_MAX_ABS_OFFSET_MM)
        return false;
    calibration.calibrated=true;
    calibration.offset_mm=offset;
    calibration.reference_mm=CALIBRATION_REFERENCE_MM;
    ++calibration.version;
    if(calibration.version==0U)calibration.version=1U;
    return true;
}

void Calibration_Reset(void)
{
    memset(&calibration,0,sizeof calibration);
    calibration.reference_mm=CALIBRATION_REFERENCE_MM;
}

int32_t Calibration_Apply(int32_t raw_mm)
{
    return raw_mm+calibration.offset_mm;
}

void Calibration_Get(CalibrationSnapshot *out)
{
    if(out!=NULL)*out=calibration;
}
